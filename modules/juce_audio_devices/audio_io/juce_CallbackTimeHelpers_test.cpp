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

} // namespace juce::CallbackTimeHelpers
