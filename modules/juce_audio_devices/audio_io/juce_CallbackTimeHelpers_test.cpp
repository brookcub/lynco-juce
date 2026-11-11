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


namespace juce::CallbackTimeHelpers
{

/*  Acceptance tests for the timestamp helpers. Builders implement the helpers; they do not edit
    these tests. Every expected value is a hand-worked exact fraction, checked with Python's
    fractions.Fraction on 2026-11-11 (the rails author's oracle run); none is computed by the code
    under test.
*/

static String describe (const AudioIODeviceTimestamp& t)
{
    return "{ " + String (t.timeNs) + " ns, offset " + String (t.sampleOffset) + ", flags " + String (t.nativeFlags) + " }";
}

static String describe (const std::optional<AudioIODeviceTimestamp>& t)
{
    return t.has_value() ? describe (*t) : String ("absent");
}

static bool isSameTimestamp (const AudioIODeviceTimestamp& a, const AudioIODeviceTimestamp& b)
{
    return a.timeNs == b.timeNs && a.sampleOffset == b.sampleOffset && a.nativeFlags == b.nativeFlags;
}

//==============================================================================
class CallbackTimestampAlignmentTests final : public UnitTest
{
public:
    CallbackTimestampAlignmentTests() : UnitTest ("Callback timestamp alignment", UnitTestCategories::audio) {}

    void runTest() override
    {
        // A native position p becomes x = p * numerator / denominator application samples. The
        // ratios used: 147/160 (48 kHz native, 44.1 kHz application), 160/147 (44.1 native, 48
        // application), 1/2 (96 native, 48 application), 2/1 (48 native, 96 application), and
        // 48000/384000 (a WASAPI clock counting bytes of 8-byte frames at 48 kHz).

        beginTest ("An integral position keeps its time, and only changes its units");
        {
            expectAligned (alignToApplicationSample (5'000'000'000, 48'000, 1, 1, 48000),       5'000'000'000, 48'000);
            expectAligned (alignToApplicationSample (1'000'000'000, 7, 2, 1, 96000),             1'000'000'000, 14);
            expectAligned (alignToApplicationSample (200'000'000, 384'000, 48000, 384000, 48000), 200'000'000, 48'000);
        }

        beginTest ("A position between samples moves to the nearest one, and its time by the same fraction");
        {
            // 123 * 147/160 = 113.00625: n = 113, and the time moves back 1/160 of a 44.1 kHz period, 141.72 ns
            expectAligned (alignToApplicationSample (1'000'000'000, 123, 147, 160, 44100), 999'999'858, 113);

            // 147/160 = 0.91875: n = 1, forward 13/160 of a period, 1842.40 ns
            expectAligned (alignToApplicationSample (1'000'000'000, 1, 147, 160, 44100), 1'000'001'842, 1);

            // 160/147 = 1.0884: n = 1, back 13/147 of a 48 kHz period, 1842.40 ns; 320/147 = 2.1769: back 3684.81 ns
            expectAligned (alignToApplicationSample (1'000'000'000, 1, 160, 147, 48000), 999'998'158, 1);
            expectAligned (alignToApplicationSample (1'000'000'000, 2, 160, 147, 48000), 999'996'315, 2);

            // 384002 bytes = 48000.25 frames: back a quarter period, 5208.33 ns; 384006 = 48000.75: forward 5208.33 ns
            expectAligned (alignToApplicationSample (200'000'000, 384'002, 48000, 384000, 48000), 199'994'792, 48'000);
            expectAligned (alignToApplicationSample (200'000'000, 384'006, 48000, 384000, 48000), 200'005'208, 48'001);
        }

        beginTest ("A half-sample residual rounds up, for positive and negative positions");
        {
            // 80 * 147/160 = 73.5 -> 74, forward half a 44.1 kHz period, 11337.87 ns
            expectAligned (alignToApplicationSample (1'000'000'000, 80, 147, 160, 44100), 1'000'011'338, 74);

            // 1.5 -> 2 and 2.5 -> 3, forward half a 48 kHz period, 10416.67 ns
            expectAligned (alignToApplicationSample (1'000'000'000, 3, 1, 2, 48000), 1'000'010'417, 2);
            expectAligned (alignToApplicationSample (1'000'000'000, 5, 1, 2, 48000), 1'000'010'417, 3);
            expectAligned (alignToApplicationSample (200'000'000, 384'004, 48000, 384000, 48000), 200'010'417, 48'001);

            // -0.5 -> 0, -1.5 -> -1 and -73.5 -> -73: up, not away from zero
            expectAligned (alignToApplicationSample (1'000'000'000, -1, 1, 2, 48000), 1'000'010'417, 0);
            expectAligned (alignToApplicationSample (1'000'000'000, -3, 1, 2, 48000), 1'000'010'417, -1);
            expectAligned (alignToApplicationSample (1'000'000'000, -80, 147, 160, 44100), 1'000'011'338, -73);
        }

        beginTest ("A negative position between samples moves to the nearest one");
        {
            // -147/160 = -0.91875 -> -1, back 13/160 of a period
            expectAligned (alignToApplicationSample (1'000'000'000, -1, 147, 160, 44100), 999'998'158, -1);
        }

        beginTest ("A zero native time is a time like any other");
        {
            // 1 tick of a 10 MHz clock = 0.00441 samples at 44.1 kHz -> 0, back 100 ns
            expectAligned (alignToApplicationSample (0, 1, 44100, 10'000'000, 44100), -100, 0);
            expectAligned (alignToApplicationSample (0, 48'000, 1, 1, 48000), 0, 48'000);
        }

        beginTest ("Large counters do not overflow, though position * numerator does not fit in 64 bits");
        {
            // 10^17 * 147/160 = 91,875,000,000,000,000 exactly; one more native sample adds 147/160
            expectAligned (alignToApplicationSample (7'000'000'000, 100'000'000'000'000'000, 147, 160, 44100),
                           7'000'000'000, 91'875'000'000'000'000);
            expectAligned (alignToApplicationSample (7'000'000'000, 100'000'000'000'000'001, 147, 160, 44100),
                           7'000'001'842, 91'875'000'000'000'001);

            // 120 days of a 10 MHz clock at 192 kHz: 1.0368e14 ticks = 1,990,656,000,000 samples; one more
            // tick is 0.0192 samples, which rounds back down: exactly 100 ns earlier
            expectAligned (alignToApplicationSample (9'000'000'000'000, 103'680'000'000'000, 192000, 10'000'000, 192000),
                           9'000'000'000'000, 1'990'656'000'000);
            expectAligned (alignToApplicationSample (9'000'000'000'000, 103'680'000'000'001, 192000, 10'000'000, 192000),
                           8'999'999'999'900, 1'990'656'000'000);
        }

        beginTest ("The correction is the residual's own, never more than half a sample period plus 1 ns");
        {
            // Checked against the exact fraction for every native position in [-2000, 2000]:
            // |n - x| <= 1/2 with ties up, and the time moved by (n - x) / rate to within half a ns.
            struct Ratio { int64_t numerator, denominator, rate; };
            const Ratio ratios[] { { 147, 160, 44100 }, { 160, 147, 48000 }, { 1, 2, 48000 }, { 2, 1, 96000 },
                                   { 44100, 10'000'000, 44100 }, { 48000, 384000, 48000 } };
            const int64_t nativeTime = 3'000'000'000;
            int numWrong = 0;
            String firstWrong;

            for (const auto& r : ratios)
            {
                for (int64_t p = -2000; p <= 2000; ++p)
                {
                    const auto aligned = alignToApplicationSample (nativeTime, p, r.numerator, r.denominator, r.rate);

                    // (n - x) * denominator and the time's shift, exactly; each step only runs when the
                    // previous one keeps the next product inside 64 bits
                    const auto shiftNs = aligned.timeNs - nativeTime;
                    const auto bounded = std::abs (shiftNs) <= 1'000'000'000 && std::abs (aligned.position) <= 1'000'000
                                          && std::abs (shiftNs) * 2 * r.rate <= 1'000'000'000 + 2 * r.rate;
                    const auto residual = bounded ? aligned.position * r.denominator - p * r.numerator : 0;
                    const auto nearest = bounded && 2 * residual <= r.denominator && 2 * residual > -r.denominator;
                    const auto timeMatches = nearest && std::abs (2 * shiftNs * r.rate * r.denominator - 2'000'000'000 * residual)
                                                        <= r.rate * r.denominator;

                    if (! timeMatches && numWrong++ == 0)
                        firstWrong = String (r.numerator) + "/" + String (r.denominator) + " p=" + String (p)
                                     + " gave " + String (aligned.timeNs) + " ns at " + String (aligned.position);
                }
            }

            expectEquals (numWrong, 0, firstWrong);
        }

        beginTest ("A chunk keeps its observation's time and flags, and moves the offset by the chunk's start");
        {
            const AudioIODeviceTimestamp t { 5'000'000'123, 37, AudioIODeviceTimestamp::nativeDiscontinuity };
            expectTimestamp (forChunkAt (t, 0),   { 5'000'000'123, 37,   AudioIODeviceTimestamp::nativeDiscontinuity });
            expectTimestamp (forChunkAt (t, 512), { 5'000'000'123, -475, AudioIODeviceTimestamp::nativeDiscontinuity });

            const AudioIODeviceTimestamp zero { 0, -96, AudioIODeviceTimestamp::nativeReducedAccuracy };
            expectTimestamp (forChunkAt (zero, 1024), { 0, -1120, AudioIODeviceTimestamp::nativeReducedAccuracy });
        }

        beginTest ("Chunks of chunks add up exactly, however large");
        {
            const AudioIODeviceTimestamp t { 9'000'000'000'000'000'000, 0, AudioIODeviceTimestamp::nativeTimestampError };
            expectTimestamp (forChunkAt (forChunkAt (t, 512), 512), forChunkAt (t, 1024));
            expectTimestamp (forChunkAt (forChunkAt (t, 1'099'511'627'776), 488),
                             { 9'000'000'000'000'000'000, -1'099'511'628'264, AudioIODeviceTimestamp::nativeTimestampError });
        }
    }

private:
    void expectAligned (AlignedSample actual, int64_t expectedTimeNs, int64_t expectedPosition)
    {
        expect (actual.timeNs == expectedTimeNs && actual.position == expectedPosition,
                "got " + String (actual.timeNs) + " ns at " + String (actual.position)
                + ", expected " + String (expectedTimeNs) + " ns at " + String (expectedPosition));
    }

    void expectTimestamp (const AudioIODeviceTimestamp& actual, const AudioIODeviceTimestamp& expected)
    {
        expect (isSameTimestamp (actual, expected), "got " + describe (actual) + ", expected " + describe (expected));
    }
};

static CallbackTimestampAlignmentTests callbackTimestampAlignmentTests;

//==============================================================================
/*  Acceptance tests for P013.1 (Oboe device timestamps, streams at the application's rate). The
    stream timestamp is a fake; the expected offset is its frame minus the buffer's first frame,
    worked by hand, and the expected time is the fake's own.
*/
class OboeStreamTimestampTests final : public UnitTest
{
public:
    OboeStreamTimestampTests() : UnitTest ("Oboe stream timestamps", UnitTestCategories::audio) {}

    void runTest() override
    {
        beginTest ("Input: the offset is the stream timestamp's frame minus the buffer's first frame, and the time is the stream's");
        {
            // 1920 frames read, 480 of them just now: the buffer starts at frame 1440
            expectTimestamp (getOboeInputTimestamp (StreamTimestamp { 1440, 7'000'000'000 }, 1920, 480), { 7'000'000'000, 0, 0 });
            expectTimestamp (getOboeInputTimestamp (StreamTimestamp { 1920, 7'000'000'000 }, 1920, 480), { 7'000'000'000, 480, 0 });
            expectTimestamp (getOboeInputTimestamp (StreamTimestamp { 960,  7'000'000'000 }, 1920, 480), { 7'000'000'000, -480, 0 });
        }

        beginTest ("Input: a short read starts the buffer at its own first frame");
        {
            // 96 of 480 frames read: frames 1824 to 1919 start the buffer, and padding follows them
            expectTimestamp (getOboeInputTimestamp (StreamTimestamp { 1920, 7'000'000'000 }, 1920, 96), { 7'000'000'000, 96, 0 });
            expectTimestamp (getOboeInputTimestamp (StreamTimestamp { 1919, 7'000'000'000 }, 1920, 1), { 7'000'000'000, 0, 0 });
        }

        beginTest ("Input: nothing read, or no stream timestamp, gives nothing");
        {
            expectTimestamp (getOboeInputTimestamp (StreamTimestamp { 1920, 7'000'000'000 }, 1920, 480), { 7'000'000'000, 480, 0 });
            expectAbsent (getOboeInputTimestamp (StreamTimestamp { 1920, 7'000'000'000 }, 1920, 0));
            expectAbsent (getOboeInputTimestamp (StreamTimestamp { 1920, 7'000'000'000 }, 1920, -1));
            expectAbsent (getOboeInputTimestamp (std::nullopt, 1920, 480));
        }

        beginTest ("Output: the offset is the stream timestamp's frame minus the frames written before the callback");
        {
            expectTimestamp (getOboeOutputTimestamp (StreamTimestamp { 48'000, 5'000'000'000 }, 48'480), { 5'000'000'000, -480, 0 });
            expectTimestamp (getOboeOutputTimestamp (StreamTimestamp { 48'960, 5'000'000'000 }, 48'480), { 5'000'000'000, 480, 0 });
            expectAbsent (getOboeOutputTimestamp (std::nullopt, 48'480));
        }

        beginTest ("Zero is a valid time and frame");
        {
            expectTimestamp (getOboeOutputTimestamp (StreamTimestamp { 0, 0 }, 0), { 0, 0, 0 });
            expectTimestamp (getOboeInputTimestamp (StreamTimestamp { 0, 0 }, 480, 480), { 0, 0, 0 });
        }

        beginTest ("Large counters keep exact offsets");
        {
            expectTimestamp (getOboeOutputTimestamp (StreamTimestamp { 4'000'000'000'000, 9'000'000'000'000'000'000 }, 4'000'000'000'480),
                             { 9'000'000'000'000'000'000, -480, 0 });
            expectTimestamp (getOboeInputTimestamp (StreamTimestamp { 4'611'686'018'427'387'904, 1 }, 4'611'686'018'427'387'904, 480),
                             { 1, 480, 0 });
        }
    }

private:
    void expectTimestamp (const std::optional<AudioIODeviceTimestamp>& actual, const AudioIODeviceTimestamp& expected)
    {
        expect (actual.has_value() && isSameTimestamp (*actual, expected), "got " + describe (actual) + ", expected " + describe (expected));
    }

    void expectAbsent (const std::optional<AudioIODeviceTimestamp>& actual)
    {
        expect (! actual.has_value(), "got " + describe (actual) + ", expected absent");
    }
};

static OboeStreamTimestampTests oboeStreamTimestampTests;

//==============================================================================
/*  Acceptance tests for P014.1 (CoreAudio) and the stamp conversion that P020 (iOS) shares. The
    fake stamp has CoreAudio's field names and flag values of its own; its host time is in ticks of
    24 per 1000 ns (a 125/3 timebase), so 240,000,000 ticks are 10 s. Expected values are worked by
    hand. The hostTimeNs expectations are upstream JUCE 8.0.12's processBufferChunk.
*/
class CoreAudioChunkTimestampTests final : public UnitTest
{
public:
    CoreAudioChunkTimestampTests() : UnitTest ("CoreAudio chunk timestamps", UnitTestCategories::audio) {}

    void runTest() override
    {
        const FakeStamp input { hostTimeValid, 240'000'000 }, output { hostTimeValid | sampleTimeValid, 240'240'000 };

        beginTest ("A buffer stamp with a valid host time gives that time at offset 0, zero included");
        {
            expectTimestamp (getBufferStartTimestamp (&input, hostTimeValid, toNanos), { 10'000'000'000, 0, 0 });

            const FakeStamp zero { hostTimeValid, 0 };
            expectTimestamp (getBufferStartTimestamp (&zero, hostTimeValid, toNanos), { 0, 0, 0 });
        }

        beginTest ("A stamp without a valid host time, or no stamp, gives nothing");
        {
            const FakeStamp sampleTimeOnly { sampleTimeValid, 240'000'000 };
            expectTimestamp (getBufferStartTimestamp (&output, hostTimeValid, toNanos), { 10'010'000'000, 0, 0 });
            expectAbsent (getBufferStartTimestamp (&sampleTimeOnly, hostTimeValid, toNanos));
            expectAbsent (getBufferStartTimestamp ((const FakeStamp*) nullptr, hostTimeValid, toNanos));
        }

        beginTest ("Each chunk keeps the stamps' times, and the chunk at k has offset -k");
        {
            const auto first = chunk (&input, &output, 2, 2, 0);
            expectTimestamp (first.inputStream,  { 10'000'000'000, 0, 0 });
            expectTimestamp (first.outputStream, { 10'010'000'000, 0, 0 });

            const auto third = chunk (&input, &output, 2, 2, 1024);
            expectTimestamp (third.inputStream,  { 10'000'000'000, -1024, 0 });
            expectTimestamp (third.outputStream, { 10'010'000'000, -1024, 0 });
        }

        beginTest ("A direction has an observation only with channels and a valid stamp");
        {
            const FakeStamp invalid { sampleTimeValid, 240'000'000 };

            const auto outputOnly = chunk (&input, &output, 0, 2, 512);
            expectAbsent (outputOnly.inputStream);
            expectTimestamp (outputOnly.outputStream, { 10'010'000'000, -512, 0 });

            const auto inputOnly = chunk (&input, &output, 2, 0, 512);
            expectTimestamp (inputOnly.inputStream, { 10'000'000'000, -512, 0 });
            expectAbsent (inputOnly.outputStream);

            const auto invalidInput = chunk (&invalid, &output, 2, 2, 0);
            expectAbsent (invalidInput.inputStream);
            expectTimestamp (invalidInput.outputStream, { 10'010'000'000, 0, 0 });
        }

        beginTest ("hostTimeNs is upstream's: the first chunk only, the output stamp when there are outputs, flags unchecked");
        {
            expectHost (chunk (&input, &output, 2, 2, 0).hostTimeNs,   10'010'000'000);
            expectHost (chunk (&input, &output, 2, 0, 0).hostTimeNs,   10'000'000'000);
            expectHost (chunk (&input, &output, 0, 0, 0).hostTimeNs,   10'000'000'000);
            expectNoHost (chunk (&input, &output, 2, 2, 512).hostTimeNs);

            // Upstream converts a stamp's host time whatever its flags say, and with outputs it never
            // falls back to the input stamp
            const FakeStamp invalid { sampleTimeValid, 240'000'000 };
            expectHost (chunk (&input, &invalid, 2, 2, 0).hostTimeNs, 10'000'000'000);
            expectNoHost (chunk (&input, nullptr, 2, 2, 0).hostTimeNs);
        }

        beginTest ("A combined device's callback keeps the input device's hostTimeNs and input observation, and nothing else");
        {
            const uint64_t host = 7'000;
            const AudioIODeviceTimestamp in { 5'000, -3, 0 }, out { 9'000, 0, 0 }, cycle { 8'000, 0, 0 };
            const auto combined = getCombinedCallbackContext ({ &host, &in, &out, &cycle });
            expect (combined.hostTimeNs == &host);
            expect (combined.inputStream == &in);
            expect (combined.outputStream == nullptr);
            expect (combined.processingCycle == nullptr);

            const auto noInput = getCombinedCallbackContext ({ &host, nullptr, &out, nullptr });
            expect (noInput.hostTimeNs == &host);
            expect (noInput.inputStream == nullptr && noInput.outputStream == nullptr);
        }
    }

private:
    struct FakeStamp
    {
        uint32_t mFlags;
        uint64_t mHostTime;
    };

    static constexpr uint32_t sampleTimeValid = 1, hostTimeValid = 2;

    static uint64_t toNanos (uint64_t ticks)    { return ticks * 125 / 3; }

    static CoreAudioChunkTimes chunk (const FakeStamp* in, const FakeStamp* out, int numIns, int numOuts, int64_t start)
    {
        return getCoreAudioChunkTimes (in, out, numIns, numOuts, start, hostTimeValid, toNanos);
    }

    void expectTimestamp (const std::optional<AudioIODeviceTimestamp>& actual, const AudioIODeviceTimestamp& expected)
    {
        expect (actual.has_value() && isSameTimestamp (*actual, expected), "got " + describe (actual) + ", expected " + describe (expected));
    }

    void expectAbsent (const std::optional<AudioIODeviceTimestamp>& actual)
    {
        expect (! actual.has_value(), "got " + describe (actual) + ", expected absent");
    }

    void expectHost (std::optional<uint64_t> actual, uint64_t expected)
    {
        expect (actual == expected, "got " + (actual.has_value() ? String (*actual) : String ("none")) + ", expected " + String (expected));
    }

    void expectNoHost (std::optional<uint64_t> actual)
    {
        expect (! actual.has_value(), "got " + String (actual.value_or (0)) + ", expected none");
    }
};

static CoreAudioChunkTimestampTests coreAudioChunkTimestampTests;

//==============================================================================
/*  Acceptance tests for P014.1's combined-device output role. FIFO addresses, stamps and times are
    fakes; each expected offset is the stamped frame's FIFO address minus the application buffer's,
    worked by hand.
*/
class CombinedOutputCorrespondenceTests final : public UnitTest
{
public:
    CombinedOutputCorrespondenceTests() : UnitTest ("CoreAudio combined output timestamps", UnitTestCategories::audio) {}

    void runTest() override
    {
        using Ts = AudioIODeviceTimestamp;
        const Ts stamp { 5'000'000'000, 0, 0 };

        beginTest ("The output device's stamp is at its frame's address minus the application buffer's");
        {
            CombinedOutputCorrespondence c;
            c.wrote (1000, 512);
            c.read (1000, 512, 0, stamp);
            expectTimestamp (c.observationFor (1512), { 5'000'000'000, -512, 0 });
        }

        beginTest ("An output device several buffers behind gives a further negative offset, repeated until replaced");
        {
            CombinedOutputCorrespondence c;
            c.wrote (1000, 512);
            c.wrote (1512, 512);
            c.wrote (2024, 512);
            c.read (1512, 256, 0, stamp);
            expectTimestamp (c.observationFor (2536), { 5'000'000'000, -1024, 0 });

            c.wrote (2536, 512);
            expectTimestamp (c.observationFor (3048), { 5'000'000'000, -1536, 0 });

            c.read (1768, 256, 0, Ts { 5'005'333'333, 0, Ts::nativeReducedAccuracy });
            expectTimestamp (c.observationFor (3048), { 5'005'333'333, -1280, Ts::nativeReducedAccuracy });
        }

        beginTest ("A stamped frame that was padding, or outside this read, gives no observation");
        {
            CombinedOutputCorrespondence c;
            c.wrote (1000, 512);
            c.read (1000, 512, 100, stamp);                         // the stamped first frame was silence
            expectAbsent (c.observationFor (1512));
            c.read (1000, 512, 0, Ts { 5'000'000'000, -512, 0 });  // a later chunk's view of an earlier frame
            expectAbsent (c.observationFor (1512));
            c.read (1000, 512, 0, std::nullopt);                    // no stamp at all
            expectAbsent (c.observationFor (1512));

            // A stamp later in the read, after the padding, is a real frame: address 1000 + 200
            c.read (1000, 512, 100, Ts { 5'000'000'000, 200, 0 });
            expectTimestamp (c.observationFor (1512), { 5'000'000'000, -312, 0 });
        }

        beginTest ("A frame read from an address the application has not written since the reset gives no observation");
        {
            CombinedOutputCorrespondence c;
            c.wrote (1000, 512);
            c.read (488, 512, 0, stamp);
            expectAbsent (c.observationFor (1512));
            c.read (1512, 512, 0, stamp);
            expectAbsent (c.observationFor (1512));
            c.read (1000, 512, 0, stamp);
            expectTimestamp (c.observationFor (1512), { 5'000'000'000, -512, 0 });
        }

        beginTest ("A write that does not follow the previous one loses the correspondence until a new frame is read");
        {
            CombinedOutputCorrespondence c;
            c.wrote (1000, 512);
            c.read (1000, 512, 0, stamp);
            c.wrote (2000, 512);                                    // addresses 1512 to 1999 were never written
            expectAbsent (c.observationFor (2512));

            c.read (1000, 512, 0, stamp);                           // a frame from before the gap does not count
            expectAbsent (c.observationFor (2512));

            c.read (2000, 512, 0, Ts { 6'000'000'000, 0, 0 });
            expectTimestamp (c.observationFor (2512), { 6'000'000'000, -512, 0 });
        }

        beginTest ("An application buffer that does not continue the written run gets no observation");
        {
            CombinedOutputCorrespondence c;
            c.wrote (1000, 512);
            c.read (1000, 512, 0, stamp);
            expectTimestamp (c.observationFor (1512), { 5'000'000'000, -512, 0 });
            expectAbsent (c.observationFor (1600));
        }

        beginTest ("A reset loses the correspondence until frames are written and read again");
        {
            CombinedOutputCorrespondence c;
            c.wrote (1000, 512);
            c.read (1000, 512, 0, stamp);
            c.reset();
            expectAbsent (c.observationFor (1512));

            c.wrote (1512, 512);
            expectAbsent (c.observationFor (2024));
            c.read (1512, 512, 0, Ts { 7'000'000'000, 0, 0 });
            expectTimestamp (c.observationFor (2024), { 7'000'000'000, -512, 0 });
        }

        beginTest ("Large FIFO addresses keep exact offsets");
        {
            constexpr uint64_t base = 4'611'686'018'427'387'904;    // 2^62
            CombinedOutputCorrespondence c;
            c.wrote (base, 512);
            c.wrote (base + 512, 512);
            c.read (base, 512, 0, stamp);
            expectTimestamp (c.observationFor (base + 1024), { 5'000'000'000, -1024, 0 });
        }
    }

private:
    void expectTimestamp (const std::optional<AudioIODeviceTimestamp>& actual, const AudioIODeviceTimestamp& expected)
    {
        expect (actual.has_value() && isSameTimestamp (*actual, expected), "got " + describe (actual) + ", expected " + describe (expected));
    }

    void expectAbsent (const std::optional<AudioIODeviceTimestamp>& actual)
    {
        expect (! actual.has_value(), "got " + describe (actual) + ", expected absent");
    }
};

static CombinedOutputCorrespondenceTests combinedOutputCorrespondenceTests;

//==============================================================================
/*  Acceptance tests for P021 (ASIO processing-cycle time), revised per SPEC "ASIO: hardware
    findings and revision". Pairs, clock readings and offsets are fakes; expected values are
    hand-worked integers, checked with Python on 2026-11-11 (rails author's oracle run). Numbers of
    the measured size: interrupt time 118'186.640 ms behind QPC and timeGetTime 118'195 ms behind
    (hw-windows/RESULTS.md, Focusrite, 2026-11-11); 256-frame blocks at 48 kHz (5'333'333 ns).
    Whether a driver's clock really is the one identified stays a hardware check (RAILS.md).
*/
class AsioProcessingCycleTests final : public UnitTest
{
public:
    AsioProcessingCycleTests() : UnitTest ("ASIO processing cycle timestamps", UnitTestCategories::audio) {}

    static AsioSwitchTime pair (uint32_t systemHi, uint32_t systemLo, uint32_t positionHi, uint32_t positionLo)
    {
        AsioSwitchTime t;
        t.systemTimeValid = t.samplePositionValid = true;
        t.systemTimeHi = systemHi;
        t.systemTimeLo = systemLo;
        t.samplePositionHi = positionHi;
        t.samplePositionLo = positionLo;
        return t;
    }

    // A pair from whole values (split into halves as the driver delivers them)
    static AsioSwitchTime pair (int64_t systemNs, int64_t position)
    {
        return pair ((uint32_t) (systemNs >> 32), (uint32_t) systemNs, (uint32_t) (position >> 32), (uint32_t) position);
    }

    void runTest() override
    {
        constexpr int64_t noOffset = 0;

        beginTest ("This switch's own pair: its system time plus the run's clock offset, at offset 0");
        {
            AsioBlockTimes blocks;
            blocks.reset (256);

            // hi 205, lo 0x4F1B5F8C: 881'795'489'676 ns on the interrupt-time clock, + 118'186'640'000
            expectTimestamp (blocks.next (pair (205, 0x4F1B5F8Cu,             0, 0),   118'186'640'000), { 999'982'129'676, 0, 0 });
            expectTimestamp (blocks.next (pair (205, 0x4F1B5F8Cu + 5'333'333, 0, 256), 118'186'640'000), { 999'987'463'009, 0, 0 });
            expectTimestamp (blocks.next (pair (205, 0x4F1B5F8Cu + 10'666'666, 0, 512), 118'186'640'000), { 999'992'796'342, 0, 0 });
        }

        beginTest ("The previous switch's pair, repeated: its time, at its offset from this block's start");
        {
            AsioBlockTimes blocks;
            blocks.reset (256);

            // back-to-back switches that get the same pair: blocks start at 1024, 1280, 1536, then 1792
            expectTimestamp (blocks.next (pair (2'000'000'000, 1024), noOffset), { 2'000'000'000, 0, 0 });
            expectTimestamp (blocks.next (pair (2'000'000'000, 1024), noOffset), { 2'000'000'000, -256, 0 });
            expectTimestamp (blocks.next (pair (2'000'000'000, 1024), noOffset), { 2'000'000'000, -512, 0 });
            expectTimestamp (blocks.next (pair (2'016'000'000, 1792), noOffset), { 2'016'000'000, 0, 0 });
        }

        // F2 (SPEC "Rulings on the prosecution review"): a pair the tracking cannot reconcile is
        // absent, and the next valid pair anchors again with nativeDiscontinuity. Expected values
        // from the rails author's Python oracle of that rule (2026-11-11); W1a and W1b are the
        // prosecution's witnesses (review-witnesses/repro.cpp), where the replaced rule moved the
        // tracking and every later offset was wrong, unflagged.
        constexpr int64_t period = 5'333'333;
        constexpr auto discontinuity = AudioIODeviceTimestamp::nativeDiscontinuity;

        beginTest ("A pair ahead of the tracked start (the next switch's pair read early, or missed switches) is absent, then the tracking anchors again");
        {
            // W1b: switch 5 is handed switch 6's pair (true offset +256, not 0)
            constexpr int64_t t = 1'000'000'000;
            AsioBlockTimes blocks;
            blocks.reset (256);

            for (int k = 0; k < 5; ++k)
                expectTimestamp (blocks.next (pair (t + k * period, k * 256), noOffset), { t + k * period, 0, 0 });

            expectAbsent    (blocks.next (pair (t + 6 * period, 6 * 256), noOffset));
            expectTimestamp (blocks.next (pair (t + 6 * period, 6 * 256), noOffset), { 1'031'999'998, 0, discontinuity });
            expectTimestamp (blocks.next (pair (t + 7 * period, 7 * 256), noOffset), { 1'037'333'331, 0, 0 });

            for (int k = 8; k < 5000; ++k)
                blocks.next (pair (t + k * period, k * 256), noOffset);

            expectTimestamp (blocks.next (pair (t + 5000 * period, 5000 * 256), noOffset), { 27'666'665'000, 0, 0 });

            // a missed switch: tracked 768, the pair says 1024
            AsioBlockTimes skipped;
            skipped.reset (256);
            expectTimestamp (skipped.next (pair (4'000'000'000, 512),  noOffset), { 4'000'000'000, 0, 0 });
            expectAbsent    (skipped.next (pair (4'010'666'667, 1024), noOffset));
            expectTimestamp (skipped.next (pair (4'016'000'000, 1280), noOffset), { 4'016'000'000, 0, discontinuity });
            expectTimestamp (skipped.next (pair (4'021'333'333, 1536), noOffset), { 4'021'333'333, 0, 0 });
        }

        beginTest ("A position before the newest one seen (a rewind) is absent, then the tracking anchors again");
        {
            // W1a: the driver's position falls back to 0 (with kSampleRateChanged), then counts on from there
            AsioBlockTimes blocks;
            blocks.reset (256);
            int64_t t = 1'000'000'000;

            expectTimestamp (blocks.next (pair (t, 1'000'000), noOffset), { 1'000'000'000, 0, 0 });
            expectTimestamp (blocks.next (pair (t += period, 1'000'256), noOffset), { 1'005'333'333, 0, 0 });

            auto rewound = pair (t += period, 0);
            rewound.sampleRateChanged = true;
            expectAbsent    (blocks.next (rewound, noOffset));
            expectTimestamp (blocks.next (pair (t += period, 256), noOffset), { 1'015'999'999, 0, discontinuity });

            for (int k = 4; k < 1000; ++k)
                blocks.next (pair (t += period, (k - 2) * 256), noOffset);

            expectTimestamp (blocks.next (pair (t += period, 998 * 256), noOffset), { 6'333'333'000, 0, 0 });

            // a rewind to a whole number of blocks before the tracked start: 0, with 1536 tracked
            AsioBlockTimes aligned;
            aligned.reset (256);
            expectTimestamp (aligned.next (pair (2'000'000'000, 1024), noOffset), { 2'000'000'000, 0, 0 });
            expectTimestamp (aligned.next (pair (2'005'333'333, 1280), noOffset), { 2'005'333'333, 0, 0 });
            expectAbsent    (aligned.next (pair (2'010'666'667, 0),    noOffset));
            expectTimestamp (aligned.next (pair (2'016'000'000, 256),  noOffset), { 2'016'000'000, 0, discontinuity });
            expectTimestamp (aligned.next (pair (2'021'333'333, 512),  noOffset), { 2'021'333'333, 0, 0 });
        }

        beginTest ("A position not a whole number of blocks from the tracked start is absent; the flag waits for the next delivered observation");
        {
            // tracked 1280, the pair says 1100; the next pair, 1600, is the new anchor
            AsioBlockTimes blocks;
            blocks.reset (256);
            expectTimestamp (blocks.next (pair (3'000'000'000, 1024), noOffset), { 3'000'000'000, 0, 0 });
            expectAbsent    (blocks.next (pair (3'005'333'333, 1100), noOffset));
            expectTimestamp (blocks.next (pair (3'010'666'667, 1600), noOffset), { 3'010'666'667, 0, discontinuity });
            expectTimestamp (blocks.next (pair (3'016'000'000, 1856), noOffset), { 3'016'000'000, 0, 0 });

            // the anchoring switch has no valid system time: the next observation delivered carries the flag
            AsioBlockTimes waiting;
            waiting.reset (256);
            auto anchorWithoutTime = pair (4'010'666'667, 1256);
            anchorWithoutTime.systemTimeValid = false;

            expectTimestamp (waiting.next (pair (4'000'000'000, 0), noOffset), { 4'000'000'000, 0, 0 });
            expectAbsent    (waiting.next (pair (4'005'333'333, 1000), noOffset));
            expectAbsent    (waiting.next (anchorWithoutTime, noOffset));
            expectTimestamp (waiting.next (pair (4'016'000'000, 1512), noOffset), { 4'016'000'000, 0, discontinuity });
            expectTimestamp (waiting.next (pair (4'021'333'333, 1768), noOffset), { 4'021'333'333, 0, 0 });
        }

        beginTest ("Both halves of the system time and the sample position count, the low halves as unsigned");
        {
            AsioBlockTimes blocks;
            blocks.reset (256);

            // time 2 * 2^32 + 2^31 = 10'737'418'240; position 2^32 + 2^31 = 6'442'450'944
            expectTimestamp (blocks.next (pair (2, 0x80000000u, 1, 0x80000000u), noOffset), { 10'737'418'240, 0, 0 });
            // time 2^48 + 2^32 - 1 = 281'479'271'677'951; position 6'442'451'200, the next block
            expectTimestamp (blocks.next (pair (0x00010000u, 0xffffffffu, 1, 0x80000100u), noOffset), { 281'479'271'677'951, 0, 0 });
        }

        beginTest ("An invalid sample position gives nothing, and the switch still counts");
        {
            AsioBlockTimes blocks;
            blocks.reset (256);

            auto noPosition = pair (5'005'333'333, 0);
            noPosition.samplePositionValid = false;

            expectTimestamp (blocks.next (pair (5'000'000'000, 0), noOffset), { 5'000'000'000, 0, 0 });
            expectAbsent    (blocks.next (noPosition, noOffset));
            // the first pair again, two switches on: -512 (-256 if the invalid switch had not counted)
            expectTimestamp (blocks.next (pair (5'000'000'000, 0), noOffset), { 5'000'000'000, -512, 0 });
        }

        beginTest ("Nothing before a valid position in the run, without a valid system time, or without a clock offset");
        {
            AsioBlockTimes blocks;
            blocks.reset (256);

            auto noPosition = pair (6'000'000'000, 0);
            noPosition.samplePositionValid = false;
            auto noTime = pair (6'005'333'333, 256);
            noTime.systemTimeValid = false;

            expectAbsent    (blocks.next (noPosition, noOffset));                      // no block start yet
            expectAbsent    (blocks.next (noTime, noOffset));                          // position 256 sets it
            expectAbsent    (blocks.next (pair (6'010'666'667, 512), std::nullopt));   // no clock offset
            expectTimestamp (blocks.next (pair (6'016'000'000, 768), noOffset), { 6'016'000'000, 0, 0 });
        }

        beginTest ("A sample-rate or clock-source change sets nativeDiscontinuity on that switch only");
        {
            AsioBlockTimes blocks;
            blocks.reset (256);

            auto rate = pair (7'000'000'000, 0);
            rate.sampleRateChanged = true;
            auto source = pair (7'005'333'333, 256);
            source.clockSourceChanged = true;

            expectTimestamp (blocks.next (rate, noOffset),   { 7'000'000'000, 0, AudioIODeviceTimestamp::nativeDiscontinuity });
            expectTimestamp (blocks.next (source, noOffset), { 7'005'333'333, 0, AudioIODeviceTimestamp::nativeDiscontinuity });
            expectTimestamp (blocks.next (pair (7'010'666'667, 512), noOffset), { 7'010'666'667, 0, 0 });
        }

        beginTest ("reset starts a new run: no block start is kept");
        {
            AsioBlockTimes blocks;
            blocks.reset (256);
            expectTimestamp (blocks.next (pair (8'000'000'000, 4096), noOffset), { 8'000'000'000, 0, 0 });
            expectTimestamp (blocks.next (pair (8'005'333'333, 4352), noOffset), { 8'005'333'333, 0, 0 });

            blocks.reset (256);
            expectTimestamp (blocks.next (pair (9'000'000'000, 100), noOffset), { 9'000'000'000, 0, 0 });
        }

        // Clock identification. Candidates as measured: QPC 1000 s, interrupt time 118'186.640 ms
        // behind, timeGetTime 118'195 ms behind. maxAge: two 256-frame blocks + 32 ms.
        constexpr AsioClockCandidates now { 1'000'000'000'000, 881'813'360'000, 881'805'000'000 };
        constexpr int64_t maxAge = 2 * 5'333'333 + 32'000'000;

        beginTest ("An interrupt-time driver: the offset is QPC - interrupt time");
        {
            expectOffset (identify (times (now.interruptTimeNs - 3'100'300), now, maxAge), 118'186'640'000);

            // whole milliseconds near timeGetTime, but one time is not: not a timeGetTime clock, and
            // interrupt time (13.36 ms later) matches
            auto mixed = times (now.timeGetTimeNs - 5'000'000, 5'000'000);
            mixed[3] += 100;
            expectOffset (identify (mixed, now, maxAge), 118'186'640'000);
        }

        beginTest ("A QPC driver: the offset is 0");
        {
            expectOffset (identify (times (now.qpcNs - 1'234'567), now, maxAge), 0);
        }

        beginTest ("A timeGetTime driver (every time a whole millisecond): the offset is QPC - timeGetTime");
        {
            expectOffset (identify (times (now.timeGetTimeNs - 5'000'000, 5'000'000), now, maxAge), 118'195'000'000);
            expectOffset (identify (times (now.timeGetTimeNs - 60'000'000, 5'000'000), now, maxAge), std::nullopt);
        }

        beginTest ("QPC and interrupt time both match: interrupt time, whichever is ahead (SPEC \"ASIO ambiguous clock\")");
        {
            // interrupt time 20 ms behind QPC: a time 3 ms before interrupt time matches both;
            // QPC - interrupt time = 20'000'000
            constexpr AsioClockCandidates behind { 1'000'000'000'000, 999'980'000'000, 999'975'000'000 };
            expectOffset (identify (times (behind.interruptTimeNs - 3'000'100), behind, maxAge), 20'000'000);

            // interrupt time 10 ms ahead of QPC: a time 3 ms before QPC matches both; -10'000'000
            constexpr AsioClockCandidates ahead { 1'000'000'000'000, 1'000'010'000'000, 1'000'005'000'000 };
            expectOffset (identify (times (ahead.qpcNs - 3'000'100), ahead, maxAge), -10'000'000);
        }

        beginTest ("No candidate, or fewer than 8 times: nothing for the run");
        {
            expectOffset (identify (times (now.qpcNs - 200'000'000'000), now, maxAge), std::nullopt);

            const auto seven = times (now.interruptTimeNs - 3'100'300);
            expectOffset (identifyAsioClockOffset (seven.data() + 1, 7, now, maxAge), std::nullopt);
        }

        beginTest ("A candidate matches from maxAge before it up to its own reading");
        {
            expectOffset (identify (times (now.interruptTimeNs - maxAge), now, maxAge), 118'186'640'000);
            expectOffset (identify (times (now.interruptTimeNs - maxAge - 1), now, maxAge), std::nullopt);
            expectOffset (identify (times (now.interruptTimeNs), now, maxAge), 118'186'640'000);
            expectOffset (identify (times (now.interruptTimeNs + 1), now, maxAge), std::nullopt);
        }

        // F5 (SPEC "Rulings on the prosecution review"): 2^32 ms = 4'294'967'296'000'000 ns.
        // Expected values from the rails author's Python oracle (2026-11-11).
        beginTest ("A timeGetTime clock continues exactly across its 2^32 ms wrap, however many wraps");
        {
            // no wrap: unchanged
            expectUnwrapped (1'005'000'000, 1'000'000'000, 1'005'000'000);

            // W2: 4'294'967'290 ms, then the count wraps to 2 ms, 8 ms later
            expectUnwrapped (2'000'000, 4'294'967'290'000'000, 4'294'967'298'000'000);

            // the first switch after an identification at 4'294'967'295 ms, after the wrap
            expectUnwrapped (1'000'000, 4'294'967'295'000'000, 4'294'967'297'000'000);

            // a time from before the wrap, after the count has wrapped (a stale pair): not moved on
            expectUnwrapped (4'294'967'295'000'000, 4'294'967'298'000'000, 4'294'967'295'000'000);

            // the second wrap (99.4 days)
            expectUnwrapped (3'000'000, 8'589'934'590'000'000, 8'589'934'595'000'000);
        }
    }

    // Eight driver times ending at newest, step apart, oldest first
    static std::vector<int64_t> times (int64_t newest, int64_t step = 5'333'300)
    {
        std::vector<int64_t> t;
        for (int k = 7; k >= 0; --k)
            t.push_back (newest - step * k);
        return t;
    }

    static std::optional<int64_t> identify (const std::vector<int64_t>& t, const AsioClockCandidates& c, int64_t maxAge)
    {
        return identifyAsioClockOffset (t.data(), (int) t.size(), c, maxAge);
    }

private:
    void expectUnwrapped (int64_t driverTimeNs, int64_t referenceNs, int64_t expected)
    {
        const auto actual = unwrapTimeGetTimeNs (driverTimeNs, referenceNs);
        expect (actual == expected, "got " + String (actual) + ", expected " + String (expected));
    }

    void expectOffset (std::optional<int64_t> actual, std::optional<int64_t> expected)
    {
        const auto text = [] (std::optional<int64_t> v) { return v.has_value() ? String (*v) : String ("absent"); };
        expect (actual == expected, "got " + text (actual) + ", expected " + text (expected));
    }

    void expectTimestamp (const std::optional<AudioIODeviceTimestamp>& actual, const AudioIODeviceTimestamp& expected)
    {
        expect (actual.has_value() && isSameTimestamp (*actual, expected), "got " + describe (actual) + ", expected " + describe (expected));
    }

    void expectAbsent (const std::optional<AudioIODeviceTimestamp>& actual)
    {
        expect (! actual.has_value(), "got " + describe (actual) + ", expected absent");
    }
};

static AsioProcessingCycleTests asioProcessingCycleTests;

//==============================================================================
/*  Acceptance tests for P022 (ALSA device timestamps). The readings are fakes; the expected
    hardware-pointer offsets are worked by hand from the ring arithmetic in the seam's contract
    (capture: frames transferred + avail; playback: frames transferred - (ring - avail)).
*/
class AlsaTimestampTests final : public UnitTest
{
public:
    AlsaTimestampTests() : UnitTest ("ALSA device timestamps", UnitTestCategories::audio) {}

    void runTest() override
    {
        // A 4096-frame ring
        constexpr int64_t ring = 4096;

        beginTest ("Capture: the frame at the hardware pointer, after the frames already read");
        {
            // 9600 frames read when 256 more were available: the pointer is at 9856. A buffer
            // starting at 9600 (read just after the reading) sees it at +256.
            expectTimestamp (getAlsaTimestamp (true, Reading { 256, 12, 345'678'901 }, true, 9600, 9600, ring), { 12'345'678'901, 256, 0 });

            // The same reading seen from a buffer read before it, starting at 9088
            expectTimestamp (getAlsaTimestamp (true, Reading { 256, 12, 345'678'901 }, true, 9600, 9088, ring), { 12'345'678'901, 768, 0 });
        }

        beginTest ("Playback: the frame at the hardware pointer, behind the frames already written");
        {
            // 19200 written with 1024 of the 4096 free: 3072 are queued, so the pointer is at 16128;
            // from a buffer starting at 19200 that is -3072
            expectTimestamp (getAlsaTimestamp (false, Reading { 1024, 12, 0 }, true, 19200, 19200, ring), { 12'000'000'000, -3072, 0 });

            // An empty ring (all 4096 free): the pointer has caught up with the writes
            expectTimestamp (getAlsaTimestamp (false, Reading { 4096, 12, 0 }, true, 19200, 19200, ring), { 12'000'000'000, 0, 0 });
        }

        beginTest ("The time is the reading's seconds and nanoseconds, exactly, from zero to large values");
        {
            expectTimestamp (getAlsaTimestamp (true, Reading { 0, 0, 0 }, true, 0, 0, ring), { 0, 0, 0 });
            expectTimestamp (getAlsaTimestamp (true, Reading { 0, 9'000'000'000, 999'999'999 }, true, 0, 0, ring),
                             { 9'000'000'000'999'999'999, 0, 0 });
        }

        beginTest ("Large frame counts keep exact offsets");
        {
            expectTimestamp (getAlsaTimestamp (true, Reading { 100, 1, 0 }, true, 1'099'511'627'776, 1'099'511'627'776, ring), { 1'000'000'000, 100, 0 });
            expectTimestamp (getAlsaTimestamp (false, Reading { 96, 1, 0 }, true, 1'099'511'627'776, 1'099'511'627'776, ring), { 1'000'000'000, -4000, 0 });
        }

        beginTest ("No reading, another clock, or a count outside the ring gives nothing");
        {
            expectTimestamp (getAlsaTimestamp (true, Reading { 256, 1, 0 }, true, 9600, 9600, ring), { 1'000'000'000, 256, 0 });
            expectAbsent (getAlsaTimestamp (true, std::nullopt, true, 9600, 9600, ring));
            expectAbsent (getAlsaTimestamp (true, Reading { 256, 1, 0 }, false, 9600, 9600, ring));
            expectAbsent (getAlsaTimestamp (true, Reading { 4097, 1, 0 }, true, 9600, 9600, ring));
            expectAbsent (getAlsaTimestamp (false, Reading { 4097, 1, 0 }, true, 9600, 9600, ring));
            expectAbsent (getAlsaTimestamp (false, Reading { -1, 1, 0 }, true, 9600, 9600, ring));
        }
    }

private:
    using Reading = AlsaPositionReading;

    void expectTimestamp (const std::optional<AudioIODeviceTimestamp>& actual, const AudioIODeviceTimestamp& expected)
    {
        expect (actual.has_value() && isSameTimestamp (*actual, expected), "got " + describe (actual) + ", expected " + describe (expected));
    }

    void expectAbsent (const std::optional<AudioIODeviceTimestamp>& actual)
    {
        expect (! actual.has_value(), "got " + describe (actual) + ", expected absent");
    }
};

static AlsaTimestampTests alsaTimestampTests;

//==============================================================================
/*  Acceptance tests for P023 (JACK processing-cycle time), SPEC "JACK clock: ruling 8 replaced".
    Each call passes one callback's readings { CLOCK_MONOTONIC ns, jack_get_time() us,
    CLOCK_MONOTONIC_RAW ns }. Readings and cycle times are fakes; expected times are hand-worked
    integers, checked with Python fractions on 2026-11-11 (rails author's oracle run). The
    MONOTONIC - RAW differences used, 62.6 ms and 450.8 ms, are of the size measured on WSL2 and on
    the GitHub Ubuntu runners (2026-11-11).
*/
class JackProcessingCycleTests final : public UnitTest
{
public:
    JackProcessingCycleTests() : UnitTest ("JACK processing cycle timestamps", UnitTestCategories::audio) {}

    void runTest() override
    {
        beginTest ("A CLOCK_MONOTONIC_RAW server: the cycle time moves onto CLOCK_MONOTONIC by the current difference");
        {
            // jack_get_time 500 ns from RAW, 62.6 ms from MONOTONIC: 29'995'000'000 + (30'062'600'250 - 30'000'000'500)
            expectTimestamp (cycle (0, 29'995'000, 30'062'600'250, 30'000'000, 30'000'000'500), { 30'057'599'750, 0, 0 });

            // 1 us from RAW, 450.8 ms from MONOTONIC: 1'199'994'667'000 + 450'777'583
            expectTimestamp (cycle (0, 1'199'994'667, 1'200'450'777'583, 1'199'999'999, 1'200'000'000'000), { 1'200'445'444'583, 0, 0 });
        }

        beginTest ("A CLOCK_MONOTONIC server: the cycle time is kept, x 1000 exactly, from zero to large values");
        {
            // jack_get_time 250 ns from MONOTONIC, 60 ms from RAW
            expectTimestamp (cycle (0, 6'994'667, 7'000'000'250, 7'000'000, 6'940'000'400), { 6'994'667'000, 0, 0 });
            expectTimestamp (cycle (0, 0, 7'000'000'250, 7'000'000, 6'940'000'400), { 0, 0, 0 });

            // 2^53 + 1 us (285 years) is still exact
            expectTimestamp (cycle (0, 9'007'199'254'740'993, 7'000'000'250, 7'000'000, 6'940'000'400), { 9'007'199'254'740'993'000, 0, 0 });
        }

        beginTest ("Both clocks equal: the cycle time is kept");
        {
            expectTimestamp (cycle (0, 499'994'667, 500'000'000'123, 500'000'000, 500'000'000'123), { 499'994'667'000, 0, 0 });
        }

        beginTest ("jack_get_time exactly between the two clocks: CLOCK_MONOTONIC, nothing added");
        {
            // 500 us from each; RAW would give 2'995'000'000 - 1'000'000
            expectTimestamp (cycle (0, 2'995'000, 3'000'000'000, 3'000'500, 3'001'000'000), { 2'995'000'000, 0, 0 });
        }

        beginTest ("Neither clock within 1 ms, or a failed query: absent");
        {
            // 50 ms from MONOTONIC, 150 ms from RAW
            expectAbsent (cycle (0, 10'045'000, 10'000'000'000, 10'050'000, 9'900'000'000));

            // exactly 1 ms is within, 1 ms + 1 ns is not: after and before MONOTONIC, then RAW
            expectTimestamp (cycle (0, 2'000'000, 2'000'000'000, 2'001'000, 1'000'000'000), { 2'000'000'000, 0, 0 });
            expectAbsent    (cycle (0, 2'000'000, 1'999'999'999, 2'001'000, 1'000'000'000));
            expectTimestamp (cycle (0, 2'000'000, 2'000'000'000, 1'999'000, 1'000'000'000), { 2'000'000'000, 0, 0 });
            expectAbsent    (cycle (0, 2'000'000, 2'000'000'001, 1'999'000, 1'000'000'000));
            expectTimestamp (cycle (0, 2'000'000, 3'000'000'000, 2'001'000, 2'000'000'000), { 3'000'000'000, 0, 0 });
            expectAbsent    (cycle (0, 2'000'000, 3'000'000'000, 2'001'000, 1'999'999'999));

            // a nonzero jack_get_cycle_times result, with readings that are otherwise present
            expectAbsent (cycle (-1, 29'995'000, 30'062'600'250, 30'000'000, 30'000'000'500));
            expectAbsent (cycle (1, 6'994'667, 7'000'000'250, 7'000'000, 6'940'000'400));
        }

        beginTest ("The conversion: microseconds x 1000, plus the readings' MONOTONIC - RAW difference with its sign");
        {
            // RAW 250 ms ahead of MONOTONIC: 100'244'667'000 - 250'000'000
            expectTimestamp (cycle (0, 100'244'667, 100'000'000'000, 100'250'000, 100'250'000'000), { 99'994'667'000, 0, 0 });

            // the difference to the ns, not to the microsecond: 4'999'999'000 + 1'234'567'891
            expectTimestamp (cycle (0, 4'999'999, 6'234'567'891, 5'000'000, 5'000'000'000), { 6'234'566'891, 0, 0 });

            // near 2^63 ns, beyond double precision: 9'007'199'254'735'660'000 + 450'777'583
            expectTimestamp (cycle (0, 9'007'199'254'735'660, 9'007'199'255'191'770'583, 9'007'199'254'740'993, 9'007'199'254'740'993'000),
                             { 9'007'199'255'186'437'583, 0, 0 });
        }

        beginTest ("Each callback is converted from its own readings: nothing is carried between callbacks");
        {
            expectTimestamp (cycle (0, 29'995'000, 30'062'600'250, 30'000'000, 30'000'000'500), { 30'057'599'750, 0, 0 });
            expectTimestamp (cycle (0, 30'000'000, 30'068'000'000, 30'005'333, 30'005'333'600), { 30'062'666'400, 0, 0 });   // a new difference
            expectAbsent    (cycle (0, 30'015'000, 30'073'000'000, 30'020'000, 30'010'666'000));                            // neither clock
            expectTimestamp (cycle (0, 30'072'667, 30'078'000'000, 30'078'000, 30'015'000'000), { 30'072'667'000, 0, 0 });   // now MONOTONIC
            expectTimestamp (cycle (0, 29'995'000, 30'062'600'250, 30'000'000, 30'000'000'500), { 30'057'599'750, 0, 0 });   // the first again
        }
    }

    static std::optional<AudioIODeviceTimestamp> cycle (int result, uint64_t currentUsecs,
                                                        int64_t monotonicNs, uint64_t jackTimeUsecs, int64_t monotonicRawNs)
    {
        return getJackProcessingCycle (result, currentUsecs, JackClockReadings { monotonicNs, jackTimeUsecs, monotonicRawNs });
    }

private:
    void expectTimestamp (const std::optional<AudioIODeviceTimestamp>& actual, const AudioIODeviceTimestamp& expected)
    {
        expect (actual.has_value() && isSameTimestamp (*actual, expected), "got " + describe (actual) + ", expected " + describe (expected));
    }

    void expectAbsent (const std::optional<AudioIODeviceTimestamp>& actual)
    {
        expect (! actual.has_value(), "got " + describe (actual) + ", expected absent");
    }
};

static JackProcessingCycleTests jackProcessingCycleTests;

} // namespace juce::CallbackTimeHelpers
