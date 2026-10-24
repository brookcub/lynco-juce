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

#pragma once

namespace juce
{

//==============================================================================
/*  Works out when a frame of a stream passes the device, from a timestamp the stream
    reported: the OS says that frame number framePosition did so at timeNs.

    The frame we want is usually not the one the OS named. A callback's first input
    sample was captured a little before or after it, and its first output sample will
    be played a little after it, so the answer is the reported time plus the distance
    between the two frames at the stream's sample rate, rounded to the nearest ns.

    getTimestamp (int64_t& framePosition, int64_t& timeNs) returns false if the stream has
    no timestamp (the first callbacks of a stream have none). getFrameIndex() is only
    called once it has returned true. If timestampsSupported is false neither is called.
    The result is empty if either fails.
*/
template <typename GetTimestamp, typename GetFrameIndex>
inline std::optional<uint64_t> getStreamFrameTimeNs (bool timestampsSupported,
                                                     GetTimestamp&& getTimestamp,
                                                     GetFrameIndex&& getFrameIndex,
                                                     double sampleRate)
{
    int64_t framePosition = 0, timeNs = 0;

    if (! timestampsSupported || ! getTimestamp (framePosition, timeNs))
        return {};

    const auto framesAhead = (double) (getFrameIndex() - framePosition);
    return (uint64_t) (timeNs + (int64_t) std::llround (framesAhead * 1.0e9 / sampleRate));
}

/*  The same for the first frame of a block of input that has just been read. The stream
    counts the frames the app has read, this block included, so the block's first frame is
    that count less the number of frames just read. A short read still has a first frame.
    A read of nothing, or an error counted as zero, has none.
*/
template <typename GetTimestamp, typename GetFramesRead>
inline std::optional<uint64_t> getInputBlockTimeNs (bool timestampsSupported,
                                                    GetTimestamp&& getTimestamp,
                                                    GetFramesRead&& getFramesRead,
                                                    int64_t numFramesJustRead,
                                                    double sampleRate)
{
    if (numFramesJustRead <= 0)
        return {};

    return getStreamFrameTimeNs (timestampsSupported,
                                 getTimestamp,
                                 [&] { return getFramesRead() - numFramesJustRead; },
                                 sampleRate);
}

//==============================================================================
#if JUCE_UNIT_TESTS

class StreamFrameTimeTests final : public UnitTest
{
public:
    StreamFrameTimeTests() : UnitTest ("Stream frame times", UnitTestCategories::audio) {}

private:
    static auto timestamp (int64_t framePosition, int64_t timeNs)
    {
        return [=] (int64_t& positionOut, int64_t& timeOut) { positionOut = framePosition; timeOut = timeNs; return true; };
    }

    static auto frame (int64_t frameIndex)    { return [=] { return frameIndex; }; }

public:
    void runTest() override
    {
        // At 48 kHz, 48 frames last 1 ms and 480 frames last 10 ms.

        beginTest ("The frame the OS named has the time the OS gave");
        {
            expectTime (getStreamFrameTimeNs (true, timestamp (48000, 5'000'000'000), frame (48000), 48000.0), 5'000'000'000);
        }

        beginTest ("A frame after the named one is later, and one before it is earlier");
        {
            expectTime (getStreamFrameTimeNs (true, timestamp (48000, 5'000'000'000), frame (48480), 48000.0), 5'010'000'000);
            expectTime (getStreamFrameTimeNs (true, timestamp (48000, 5'000'000'000), frame (47520), 48000.0), 4'990'000'000);
        }

        beginTest ("The offset follows the sample rate");
        {
            expectTime (getStreamFrameTimeNs (true, timestamp (0, 1'000'000'000), frame (441), 44100.0), 1'010'000'000);
            expectTime (getStreamFrameTimeNs (true, timestamp (0, 1'000'000'000), frame (960), 96000.0), 1'010'000'000);
        }

        beginTest ("A fraction of a nanosecond is rounded to the nearest");
        {
            // One frame lasts 20,833.33 ns, and two last 41,666.67 ns
            expectTime (getStreamFrameTimeNs (true, timestamp (0, 1'000'000'000), frame (1), 48000.0), 1'000'020'833);
            expectTime (getStreamFrameTimeNs (true, timestamp (0, 1'000'000'000), frame (-1), 48000.0), 999'979'167);
            expectTime (getStreamFrameTimeNs (true, timestamp (0, 1'000'000'000), frame (2), 48000.0), 1'000'041'667);
        }

        beginTest ("Without timestamp support nothing is asked of the stream and there is no time");
        {
            int numCalls = 0;
            const auto countingTimestamp = [&] (int64_t&, int64_t&) { ++numCalls; return true; };
            const auto countingFrame = [&] { ++numCalls; return int64_t (0); };

            expectTime (getStreamFrameTimeNs (false, countingTimestamp, countingFrame, 48000.0), {});
            expectTime (getInputBlockTimeNs (false, countingTimestamp, countingFrame, 480, 48000.0), {});
            expectEquals (numCalls, 0);
        }

        beginTest ("A failed timestamp gives no time, and the frame count is not read");
        {
            int numFrameCalls = 0;
            const auto failingTimestamp = [] (int64_t&, int64_t&) { return false; };
            const auto countingFrame = [&] { ++numFrameCalls; return int64_t (0); };

            expectTime (getStreamFrameTimeNs (true, failingTimestamp, countingFrame, 48000.0), {});
            expectTime (getInputBlockTimeNs (true, failingTimestamp, countingFrame, 480, 48000.0), {});
            expectEquals (numFrameCalls, 0);
        }

        beginTest ("Input: the block's first frame is the frames read less the frames just read");
        {
            // 1920 frames have been read, 480 of them just now, so the block starts at frame 1440
            expectTime (getInputBlockTimeNs (true, timestamp (1920, 7'000'000'000), frame (1920), 480, 48000.0), 6'990'000'000);
            expectTime (getInputBlockTimeNs (true, timestamp (1440, 7'000'000'000), frame (1920), 480, 48000.0), 7'000'000'000);
            expectTime (getInputBlockTimeNs (true, timestamp (960,  7'000'000'000), frame (1920), 480, 48000.0), 7'010'000'000);
        }

        beginTest ("Input: a short read is timed from its own first frame");
        {
            // Of 480 requested, 96 were read: frames 1824 to 1919, which start 2 ms before frame 1920
            expectTime (getInputBlockTimeNs (true, timestamp (1920, 7'000'000'000), frame (1920), 96, 48000.0), 6'998'000'000);
        }

        beginTest ("Input: a read of nothing has no time, and nor has an error");
        {
            expectTime (getInputBlockTimeNs (true, timestamp (1920, 7'000'000'000), frame (1920), 0, 48000.0), {});
            expectTime (getInputBlockTimeNs (true, timestamp (1920, 7'000'000'000), frame (1920), -1, 48000.0), {});
        }
    }

private:
    static String describe (std::optional<uint64_t> time)   { return time.has_value() ? String (*time) : String ("no time"); }

    void expectTime (std::optional<uint64_t> actual, std::optional<uint64_t> expected)
    {
        expect (actual == expected, "got " + describe (actual) + ", expected " + describe (expected));
    }
};

static StreamFrameTimeTests streamFrameTimeTests;

#endif

} // namespace juce
