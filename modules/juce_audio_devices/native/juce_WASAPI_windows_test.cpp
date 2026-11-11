/*
  ==============================================================================

   This file is part of the JUCE framework.
   Copyright (c) Raw Material Software Limited

   JUCE is an open source framework subject to commercial or open source
   licensing.

   By downloading, installing, or using the JUCE framework, or combining the
   JUCE framework with any other source code, object code, content or any other
   copyrightable work, you agree to the terms of the JUCE End User Licence
   Agreement, and all incorporated terms including the JUCE Privacy Policy and
   the JUCE Website Terms of Service, as applicable, which will bind you. If you
   do not agree to the terms of these agreements, we will not license the JUCE
   framework to you, and you must discontinue the installation or download
   process and cease use of the JUCE framework.

   JUCE End User Licence Agreement: https://juce.com/legal/juce-8-licence/
   JUCE Privacy Policy: https://juce.com/juce-privacy-policy
   JUCE Website Terms of Service: https://juce.com/juce-website-terms-of-service/

   Or:

   You may also use this code under the terms of the AGPLv3:
   https://www.gnu.org/licenses/agpl-3.0.en.html

   THE JUCE FRAMEWORK IS PROVIDED "AS IS" WITHOUT ANY WARRANTY, AND ALL
   WARRANTIES, WHETHER EXPRESSED OR IMPLIED, INCLUDING WARRANTY OF
   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, ARE DISCLAIMED.

  ==============================================================================
*/


namespace juce::WasapiClasses
{

/*  Acceptance tests for P009.1 (WASAPI device timestamps). Builders implement CapturePacketLedger,
    getRenderTimestamp and RenderStreamCorrespondence; they do not edit these tests. Packet QPC positions are in GetBuffer's
    100 ns units, so 1,000,000 is 100,000,000 ns. Every expected value is worked by hand from the
    fake packets: a packet's own time, and its first frame's position counted from the callback's
    first frame. Render expectations are exact fractions, checked with Python's fractions.Fraction
    on 2026-11-11.
*/
class WasapiDeviceTimestampTests final : public UnitTest
{
public:
    WasapiDeviceTimestampTests() : UnitTest ("WASAPI device timestamps", UnitTestCategories::audio) {}

    void runTest() override
    {
        beginTest ("Capture: a packet taken whole gives its own time at offset 0");
        {
            auto ledger = makeLedger();
            ledger.addPacket (480, ok, 1'000'000);
            expectTimestamp (ledger.takeFrames (480, 480), Ts { 100'000'000, 0, 0 });
        }

        beginTest ("Capture: a packet split over two callbacks keeps its time, and the second sees it before its buffer");
        {
            auto ledger = makeLedger();
            ledger.addPacket (960, ok, 1'000'000);
            expectTimestamp (ledger.takeFrames (960, 480), Ts { 100'000'000, 0, 0 });
            expectTimestamp (ledger.takeFrames (480, 480), Ts { 100'000'000, -480, 0 });
        }

        beginTest ("Capture: a callback made of several packets carries the one holding its first sample");
        {
            // A (300 frames) and B (300) fill the first callback with 120 of B left over; B's first
            // frame was at index 300 of the first callback, so it is 180 before the second one
            auto ledger = makeLedger();
            ledger.addPacket (300, ok, 1'000'000);
            ledger.addPacket (300, ok, 1'062'500);
            expectTimestamp (ledger.takeFrames (600, 480), Ts { 100'000'000, 0, 0 });

            ledger.addPacket (360, ok, 1'125'000);
            expectTimestamp (ledger.takeFrames (480, 480), Ts { 106'250'000, -180, 0 });
        }

        beginTest ("Capture: a real sample after leading padding has a positive offset, and padding alone has nothing");
        {
            auto ledger = makeLedger();
            expectAbsent (ledger.takeFrames (0, 480));

            // 300 queued, 480 wanted: 180 frames of padding, then the packet
            ledger.addPacket (300, ok, 1'000'000);
            expectTimestamp (ledger.takeFrames (300, 480), Ts { 100'000'000, 180, 0 });

            ledger.addPacket (480, ok, 1'200'000);
            expectTimestamp (ledger.takeFrames (480, 480), Ts { 120'000'000, 0, 0 });
        }

        beginTest ("Capture: padding in front of a packet already partly taken");
        {
            // 32 frames of padding, then frames 480 to 959 of the packet: its first frame is at 32 - 480
            auto ledger = makeLedger();
            ledger.addPacket (960, ok, 1'000'000);
            expectTimestamp (ledger.takeFrames (960, 480), Ts { 100'000'000, 0, 0 });
            expectTimestamp (ledger.takeFrames (480, 512), Ts { 100'000'000, -448, 0 });
        }

        beginTest ("Capture: each observation carries its own packet's native flags, and its time even with an error");
        {
            auto ledger = makeLedger();
            ledger.addPacket (480, discontinuity, 1'000'000);
            ledger.addPacket (480, timestampError, 1'100'000);
            ledger.addPacket (480, silent, 1'200'000);
            ledger.addPacket (480, discontinuity | timestampError, 1'300'000);

            expectTimestamp (ledger.takeFrames (1920, 480), Ts { 100'000'000, 0, Ts::nativeDiscontinuity });
            expectTimestamp (ledger.takeFrames (1440, 480), Ts { 110'000'000, 0, Ts::nativeTimestampError });
            expectTimestamp (ledger.takeFrames (960, 480),  Ts { 120'000'000, 0, 0 });
            expectTimestamp (ledger.takeFrames (480, 480),  Ts { 130'000'000, 0, Ts::nativeDiscontinuity | Ts::nativeTimestampError });
        }

        beginTest ("Capture: the packet holding the first real sample is chosen, whatever follows it in the buffer");
        {
            auto ledger = makeLedger();
            ledger.addPacket (240, ok, 1'000'000);
            ledger.addPacket (240, discontinuity, 1'050'000);
            expectTimestamp (ledger.takeFrames (480, 480), Ts { 100'000'000, 0, 0 });

            // 240 of C are taken; the next callback starts with C's frame 240, followed by D's error
            ledger.addPacket (480, ok, 2'000'000);
            expectTimestamp (ledger.takeFrames (480, 240), Ts { 200'000'000, 0, 0 });
            ledger.addPacket (480, timestampError, 2'100'000);
            expectTimestamp (ledger.takeFrames (720, 480), Ts { 200'000'000, -240, 0 });
        }

        beginTest ("Capture: a queued frame with no recorded packet gives nothing, rather than a projected time");
        {
            // 960 frames are queued but only the newest 480 were recorded
            auto ledger = makeLedger();
            ledger.addPacket (480, ok, 1'000'000);
            expectAbsent (ledger.takeFrames (960, 480));
            expectTimestamp (ledger.takeFrames (480, 480), Ts { 100'000'000, 0, 0 });
        }

        beginTest ("Capture: a start forgets every packet");
        {
            auto ledger = makeLedger();
            ledger.addPacket (480, ok, 1'000'000);
            ledger.reset (4096);
            expectAbsent (ledger.takeFrames (480, 480));

            ledger.addPacket (480, ok, 2'000'000);
            expectTimestamp (ledger.takeFrames (480, 480), Ts { 200'000'000, 0, 0 });
        }

        beginTest ("Capture: QPC positions become ns exactly, from zero to large values");
        {
            auto ledger = makeLedger();
            ledger.addPacket (480, ok, 0);
            expectTimestamp (ledger.takeFrames (480, 480), Ts { 0, 0, 0 });

            // 9e15 units of 100 ns is 28.5 years
            ledger.addPacket (480, ok, 9'000'000'000'000'000);
            expectTimestamp (ledger.takeFrames (480, 480), Ts { 900'000'000'000'000'000, 0, 0 });
        }

        beginTest ("Capture: a long run of 441-frame packets taken as 512-frame callbacks stays associated");
        {
            // The queue is refilled before each callback, so callback c starts at stream frame 512 c,
            // which lies in packet j = 512 c / 441, whose first frame is at 441 j
            auto ledger = makeLedger (1024);
            int64_t numQueued = 0, numAdded = 0;
            int numWrong = 0;
            String firstWrong;

            for (int64_t c = 0; c < 400; ++c)
            {
                while (numQueued < 512)
                {
                    ledger.addPacket (441, ok, (UINT64) (2'000'000 + 100'000 * numAdded++));
                    numQueued += 441;
                }

                const auto j = 512 * c / 441;
                const Ts expected { (2'000'000 + 100'000 * j) * 100, 441 * j - 512 * c, 0 };
                const auto actual = ledger.takeFrames ((int) numQueued, 512);
                numQueued -= 512;

                if (! isSame (actual, expected) && numWrong++ == 0)
                    firstWrong = "callback " + String (c) + ": got " + describe (actual) + ", expected " + describe (expected);
            }

            expectEquals (numWrong, 0, firstWrong);
        }

        beginTest ("Render: the clock position is converted to frames with the clock's frequency");
        {
            // Position 48000 frames, read at 200 ms, counted in frames, in bytes of 8-byte frames, and at 10 MHz;
            // the callback starts at frame 48480
            expectTimestamp (getRenderTimestamp (S_OK, 48'000,     2'000'000, 48'000,     48000, 48'480), Ts { 200'000'000, -480, 0 });
            expectTimestamp (getRenderTimestamp (S_OK, 384'000,    2'000'000, 384'000,    48000, 48'480), Ts { 200'000'000, -480, 0 });
            expectTimestamp (getRenderTimestamp (S_OK, 10'000'000, 2'000'000, 10'000'000, 48000, 48'480), Ts { 200'000'000, -480, 0 });
        }

        beginTest ("Render: a position between frames is aligned once, to the nearest frame");
        {
            // 384002 bytes = 48000.25 frames: 48000, a quarter period earlier (5208.33 ns)
            expectTimestamp (getRenderTimestamp (S_OK, 384'002, 2'000'000, 384'000, 48000, 48'480), Ts { 199'994'792, -480, 0 });
            // 48000.75: 48001, a quarter period later
            expectTimestamp (getRenderTimestamp (S_OK, 384'006, 2'000'000, 384'000, 48000, 48'480), Ts { 200'005'208, -479, 0 });
            // 48000.5 rounds up: 48001, half a period later (10416.67 ns)
            expectTimestamp (getRenderTimestamp (S_OK, 384'004, 2'000'000, 384'000, 48000, 48'480), Ts { 200'010'417, -479, 0 });
        }

        beginTest ("Render: S_FALSE delivers the reading with reduced accuracy");
        {
            expectTimestamp (getRenderTimestamp (S_FALSE, 48'000, 2'000'000, 48'000, 48000, 48'480), Ts { 200'000'000, -480, Ts::nativeReducedAccuracy });
        }

        beginTest ("Render: a failed reading, a zero frequency or an unknown callback position gives nothing");
        {
            expectTimestamp (getRenderTimestamp (S_OK, 48'000, 2'000'000, 48'000, 48000, 48'480), Ts { 200'000'000, -480, 0 });
            expectAbsent (getRenderTimestamp (E_FAIL, 48'000, 2'000'000, 48'000, 48000, 48'480));
            expectAbsent (getRenderTimestamp (S_OK, 48'000, 2'000'000, 0, 48000, 48'480));
            expectAbsent (getRenderTimestamp (S_OK, 48'000, 2'000'000, 48'000, 48000, std::nullopt));
        }

        beginTest ("Render: the callback's position changes only the offset, of either sign");
        {
            expectTimestamp (getRenderTimestamp (S_OK, 384'002, 2'000'000, 384'000, 48000, 96'000), Ts { 199'994'792, -48'000, 0 });
            expectTimestamp (getRenderTimestamp (S_OK, 384'002, 2'000'000, 384'000, 48000, 47'520), Ts { 199'994'792, 480, 0 });
        }

        beginTest ("Render: large positions do not overflow");
        {
            // 120 days of a 10 MHz clock at 192 kHz, plus one tick: 1,990,656,000,000.0192 frames,
            // which rounds to 1,990,656,000,000, exactly 100 ns earlier
            expectTimestamp (getRenderTimestamp (S_OK, 103'680'000'000'001, 90'000'000'000, 10'000'000, 192000, 1'990'656'000'480),
                             Ts { 8'999'999'999'900, -480, 0 });
        }

        // RenderStreamCorrespondence: frame counts as in the Focusrite run (hw-windows/RESULTS.md):
        // a 1056-frame endpoint buffer filled with silence at start, then 480-frame writes.
        beginTest ("Render bookkeeping: the next callback starts at the count of frames released");
        {
            RenderStreamCorrespondence stream;
            stream.reset();
            stream.paddingBeforeWrite (0);    // start(): the endpoint buffer is still empty, nothing released yet
            stream.released (1056);
            expect (stream.callbackStart() == std::optional<int64_t> (1056));
            stream.paddingBeforeWrite (576);
            stream.released (480);
            stream.paddingBeforeWrite (96);
            stream.released (480);
            expect (stream.callbackStart() == std::optional<int64_t> (2016));

            // through getRenderTimestamp: position 1536 frames at 200 ms, callback at 2016
            expectTimestamp (getRenderTimestamp (S_OK, 1536, 2'000'000, 48'000, 48000, stream.callbackStart()), Ts { 200'000'000, -480, 0 });
        }

        beginTest ("Render bookkeeping: padding found empty before a write is an underrun, and the role stays absent");
        {
            RenderStreamCorrespondence stream;
            stream.reset();
            stream.released (1056);
            stream.paddingBeforeWrite (576);
            stream.released (480);
            stream.paddingBeforeWrite (0);    // the engine ran dry
            expect (! stream.callbackStart().has_value(), "an underrun must make the callback position unknown");
            expectAbsent (getRenderTimestamp (S_OK, 1536, 2'000'000, 48'000, 48000, stream.callbackStart()));

            // latched: later writes with a full buffer do not bring it back
            for (int i = 0; i < 100; ++i)
            {
                stream.released (480);
                stream.paddingBeforeWrite (576);
            }

            expect (! stream.callbackStart().has_value(), "the role must stay absent until the stream restarts");
        }

        // F1/F3 (SPEC "Rulings on the prosecution review"): the start-up dry. On this host every run
        // finds the engine empty at the first application write, after start()'s silence
        // (REVIEW.md F1: hw-windows/data-r2/dbgw_cb.csv, the role absent in all 399 callbacks once
        // that write is latched). The class has no share mode: exclusive mode is fed the same way.
        beginTest ("Render bookkeeping: the first application write finding the engine dry after start()'s silence is an underrun");
        {
            RenderStreamCorrespondence stream;
            stream.reset();
            stream.paddingBeforeWrite (0);    // start(): nothing released yet, not an underrun
            stream.released (1056);
            expect (stream.callbackStart() == std::optional<int64_t> (1056));

            stream.paddingBeforeWrite (0);    // the first application write: the silence has already played out
            expect (! stream.callbackStart().has_value(), "a start-up dry must make the callback position unknown");
            expectAbsent (getRenderTimestamp (S_OK, 1056, 2'000'000, 48'000, 48000, stream.callbackStart()));

            for (int i = 0; i < 100; ++i)
            {
                stream.released (480);
                stream.paddingBeforeWrite (576);
            }

            expect (! stream.callbackStart().has_value(), "the role must stay absent until the stream restarts");
        }

        beginTest ("Render bookkeeping: a restart re-establishes the correspondence from zero");
        {
            RenderStreamCorrespondence stream;
            stream.reset();
            stream.released (1056);
            stream.paddingBeforeWrite (0);
            stream.released (480);
            expect (! stream.callbackStart().has_value());

            stream.reset();
            stream.released (1056);
            expect (stream.callbackStart() == std::optional<int64_t> (1056));
        }

        beginTest ("Render bookkeeping: any other detected loss also latches until reset");
        {
            RenderStreamCorrespondence stream;
            stream.reset();
            stream.released (1056);
            stream.lose();
            stream.paddingBeforeWrite (576);
            stream.released (480);
            expect (! stream.callbackStart().has_value());

            stream.reset();
            expect (stream.callbackStart() == std::optional<int64_t> (0));
        }

        beginTest ("Render bookkeeping: large counts");
        {
            // 2^40 frames (265 days at 48 kHz) in 2^20-frame writes, then one more frame
            RenderStreamCorrespondence stream;
            stream.reset();
            for (int i = 0; i < (1 << 20); ++i)
                stream.released (1 << 20);
            stream.paddingBeforeWrite (1);
            stream.released (1);
            expect (stream.callbackStart() == std::optional<int64_t> (1'099'511'627'777));
        }
    }

private:
    using Ts = AudioIODeviceTimestamp;

    static constexpr DWORD ok = 0,
                           discontinuity = AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY,
                           silent = AUDCLNT_BUFFERFLAGS_SILENT,
                           timestampError = AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR;

    static CapturePacketLedger makeLedger (int queueCapacity = 4096)
    {
        CapturePacketLedger ledger;
        ledger.reset (queueCapacity);
        return ledger;
    }

    static String describe (const std::optional<Ts>& t)
    {
        return t.has_value() ? "{ " + String (t->timeNs) + " ns, offset " + String (t->sampleOffset) + ", flags " + String (t->nativeFlags) + " }"
                             : String ("absent");
    }

    static bool isSame (const std::optional<Ts>& a, const Ts& b)
    {
        return a.has_value() && a->timeNs == b.timeNs && a->sampleOffset == b.sampleOffset && a->nativeFlags == b.nativeFlags;
    }

    void expectTimestamp (const std::optional<Ts>& actual, const Ts& expected)
    {
        expect (isSame (actual, expected), "got " + describe (actual) + ", expected " + describe (expected));
    }

    void expectAbsent (const std::optional<Ts>& actual)
    {
        expect (! actual.has_value(), "got " + describe (actual) + ", expected absent");
    }
};

static WasapiDeviceTimestampTests wasapiDeviceTimestampTests;

} // namespace juce::WasapiClasses
