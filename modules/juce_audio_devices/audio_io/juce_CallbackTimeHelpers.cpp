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

/*  Arithmetic that more than one backend needs to fill AudioIODeviceTimestamp. Each function is
    the single owner of its rule.

    The functions marked as stubs only declare their contract; their tests are the acceptance
    tests of the patch that implements them.
*/

//==============================================================================
/** A native position on the application's sample grid, and the time that goes with it. */
struct AlignedSample
{
    int64_t timeNs = 0;
    int64_t position = 0;
};

/*  Aligns a native observation to the nearest application sample: the bounded fractional-sample
    alignment that AudioIODeviceTimestamp describes, and the only time adjustment it allows.

    The source says that its position nativePosition passed at nativeTimeNs. In application
    samples that position is x = nativePosition * numerator / denominator, exactly. The result is
    n = floor (x + 1/2), the nearest application sample (a half-sample residual rounds up), with
    the time nativeTimeNs + (n - x) * 1e9 / applicationSampleRate rounded to the nearest ns. An
    integral x keeps its time unchanged.

    numerator, denominator and applicationSampleRate are positive and below 2^31. nativePosition
    may be negative, and so large that nativePosition * numerator does not fit in 64 bits; the
    result is exact as long as n does.

    Stub: the patch that implements it is P007.1.
*/
inline AlignedSample alignToApplicationSample ([[maybe_unused]] int64_t nativeTimeNs,
                                               [[maybe_unused]] int64_t nativePosition,
                                               [[maybe_unused]] int64_t numerator,
                                               [[maybe_unused]] int64_t denominator,
                                               [[maybe_unused]] int64_t applicationSampleRate) noexcept
{
    return {};
}

/*  The same observation seen from a chunk of the callback's buffer that starts chunkStart samples
    in, as when a buffer is split into smaller callbacks: the time and flags are unchanged and the
    offset is chunkStart smaller. Chunks of chunks add up exactly.

    Stub: the patch that implements it is P007.1.
*/
inline AudioIODeviceTimestamp forChunkAt ([[maybe_unused]] const AudioIODeviceTimestamp& timestamp,
                                          [[maybe_unused]] int64_t chunkStart) noexcept
{
    return {};
}

//==============================================================================
/** A timestamp as Oboe's getTimestamp reports it: frame framePosition of the stream passed the
    device at timeNs (CLOCK_MONOTONIC).
*/
struct StreamTimestamp
{
    int64_t framePosition = 0;
    int64_t timeNs = 0;
};

/*  The input observation of an Oboe callback whose input was just read. framesReadAfterRead is the
    stream's getFramesRead() after the read, and framesJustRead the frames it delivered, which fill
    the start of the buffer (a short read is padded at the end). The offset is the stream
    timestamp's frame minus the buffer's first frame, and the time is the stream's, unchanged.

    nullopt if the stream gave no timestamp or no frame was read.

    Stub: the patch that implements it is P013.1.
*/
inline std::optional<AudioIODeviceTimestamp> getOboeInputTimestamp ([[maybe_unused]] const std::optional<StreamTimestamp>& stream,
                                                                    [[maybe_unused]] int64_t framesReadAfterRead,
                                                                    [[maybe_unused]] int64_t framesJustRead) noexcept
{
    return {};
}

/*  The output observation of an Oboe callback: framesWrittenBeforeCallback is the stream's
    getFramesWritten() at the start of the callback, the frame index of the buffer's first frame.
    The offset is the stream timestamp's frame minus that index, and the time is the stream's.

    nullopt if the stream gave no timestamp.

    Stub: the patch that implements it is P013.1.
*/
inline std::optional<AudioIODeviceTimestamp> getOboeOutputTimestamp ([[maybe_unused]] const std::optional<StreamTimestamp>& stream,
                                                                     [[maybe_unused]] int64_t framesWrittenBeforeCallback) noexcept
{
    return {};
}

} // namespace juce::CallbackTimeHelpers
