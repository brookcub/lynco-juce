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

//==============================================================================
/*  The observation of an Apple AudioTimeStamp that belongs to the first frame of a buffer, as
    CoreAudio's IOProc stamps and the iOS render callback's stamp do: the host time converted with
    hostTimeToNanos, at offset 0. The stamp is anything with mFlags and mHostTime.

    nullopt if there is no stamp or its flags do not include hostTimeValidFlag.

    Stub: the patch that implements it is P014.1; P020 uses it for the iOS render cycle.
*/
template <typename Stamp, typename Flags, typename HostTimeToNanos>
std::optional<AudioIODeviceTimestamp> getBufferStartTimestamp ([[maybe_unused]] const Stamp* stamp,
                                                               [[maybe_unused]] Flags hostTimeValidFlag,
                                                               [[maybe_unused]] HostTimeToNanos&& hostTimeToNanos)
{
    return {};
}

/** What one chunk of a CoreAudio IOProc buffer passes to its callback. */
struct CoreAudioChunkTimes
{
    std::optional<uint64_t> hostTimeNs;
    std::optional<AudioIODeviceTimestamp> inputStream, outputStream;
};

/*  The times of the chunk that starts chunkStart samples into the IOProc's buffers. The input and
    output observations are the IOProc's inInputTime and inOutputTime stamps (getBufferStartTimestamp)
    seen from the chunk (forChunkAt), each only when the callback has channels in that direction.

    hostTimeNs is upstream JUCE 8.0.12's: on the first chunk only, the host time of the output stamp
    when the callback has outputs and of the input stamp otherwise, converted without checking the
    stamp's flags; nullopt on later chunks or without that stamp.

    Stub: the patch that implements it is P014.1.
*/
template <typename Stamp, typename Flags, typename HostTimeToNanos>
CoreAudioChunkTimes getCoreAudioChunkTimes ([[maybe_unused]] const Stamp* inputStamp,
                                            [[maybe_unused]] const Stamp* outputStamp,
                                            [[maybe_unused]] int numInputChannels,
                                            [[maybe_unused]] int numOutputChannels,
                                            [[maybe_unused]] int64_t chunkStart,
                                            [[maybe_unused]] Flags hostTimeValidFlag,
                                            [[maybe_unused]] HostTimeToNanos&& hostTimeToNanos)
{
    return {};
}

/*  The context that AudioIODeviceCombiner passes its callback from its input device's callback. It
    keeps the input device's hostTimeNs, as upstream does, and its input observation. The callback's
    outputs are presented later on another device, through a FIFO whose correspondence to the
    output device's stamps is not established, so there is no output or cycle observation.

    Stub: the patch that implements it is P014.1.
*/
inline AudioIODeviceCallbackContext getCombinedCallbackContext ([[maybe_unused]] const AudioIODeviceCallbackContext& inputDeviceContext) noexcept
{
    return {};
}

//==============================================================================
/*  Which application output frame an AudioIODeviceCombiner's output device presented, so that the
    application's callback can carry the output device's inOutputTime.

    The application's callback runs from the input device and writes its output to the combiner's
    FIFO; the output device's callback reads the FIFO later, on its own thread, and its stamp names
    the first frame of what it read. Both sides use the FIFO's absolute addresses. While the
    application's buffers occupy consecutive addresses, the distance between two addresses is the
    distance between their frames, so an observation read at address A is at offset A - W for an
    application buffer written at address W. The latest observation is repeated until a newer one
    replaces it.

    Called from both devices' threads; the implementation must not lock or allocate.

    Stub: the patch that implements it is P014.1.
*/
class CombinedOutputCorrespondence
{
public:
    /*  Forgets the correspondence, as when the device starts or an xrun invalidates both FIFO
        positions. There is no observation until frames are written and a stamped frame is read again.
    */
    void reset() noexcept {}

    /*  The application's buffer of numFrames frames went to FIFO addresses [writeStart, writeStart +
        numFrames). A write that does not follow the previous one (frames were dropped or the addresses
        jumped) loses the correspondence, as reset() does, and starts a new run of addresses.
    */
    void wrote ([[maybe_unused]] uint64_t writeStart, [[maybe_unused]] int numFrames) noexcept {}

    /*  The output device's callback filled numFrames frames: the first numPaddingFrames of them with
        silence, and the rest from FIFO addresses readStart + numPaddingFrames onwards. outputStamp is
        that callback's output observation, relative to its own buffer. It becomes the observation if
        the frame it stamps was read from the FIFO in this callback (not padding) and was written by
        the application since the last reset; otherwise the previous observation is kept.
    */
    void read ([[maybe_unused]] uint64_t readStart,
               [[maybe_unused]] int numFrames,
               [[maybe_unused]] int numPaddingFrames,
               [[maybe_unused]] const std::optional<AudioIODeviceTimestamp>& outputStamp) noexcept {}

    /*  The output observation for the application buffer about to be written at writeStart: the
        latest observation's time and flags, at offset (its address - writeStart). nullopt if there is
        no observation, or writeStart does not continue the run of addresses written since the reset.
    */
    std::optional<AudioIODeviceTimestamp> observationFor ([[maybe_unused]] uint64_t writeStart) const noexcept { return {}; }
};

//==============================================================================
/*  The processing-cycle observation of an ASIO buffer switch: the current block's systemTime, at
    offset 0. systemTime is the ASIOTimeStamp's two 32-bit halves (ns on the driver's clock), and
    clockOffsetNs the offset from the driver's clock to the Time::getHighResolutionTicks() timeline,
    identified and measured once when the device started, or nullopt if that was not established.
    The time is systemTime + clockOffsetNs; no per-callback adjustment of any kind.

    nullopt if systemTimeValid (kSystemTimeValid) is false or there is no clock offset.

    Stub: the patch that implements it is P021.
*/
inline std::optional<AudioIODeviceTimestamp> getAsioProcessingCycle ([[maybe_unused]] bool systemTimeValid,
                                                                     [[maybe_unused]] uint32_t systemTimeHi,
                                                                     [[maybe_unused]] uint32_t systemTimeLo,
                                                                     [[maybe_unused]] std::optional<int64_t> clockOffsetNs) noexcept
{
    return {};
}

//==============================================================================
/** One snd_pcm_htimestamp reading: availFrames were available when the hardware pointer was last
    updated, at tstamp (seconds and nanoseconds).
*/
struct AlsaPositionReading
{
    int64_t availFrames = 0;
    int64_t seconds = 0;
    int64_t nanoseconds = 0;
};

/*  The observation of an ALSA PCM from one coherent snd_pcm_htimestamp reading: the frame at the
    hardware pointer, at the reading's time converted exactly to ns.

    applicationFramesAtReading is the application's own count of frames transferred (read for
    capture, written for playback) when the reading was taken, and bufferStartFrame the frame index
    of the callback buffer's first frame in the same count. The hardware pointer is at
    applicationFramesAtReading + availFrames for capture, and applicationFramesAtReading -
    (ringBufferFrames - availFrames) for playback; the offset is that minus bufferStartFrame.

    nullopt if there is no reading, if the PCM's timestamps are not on CLOCK_MONOTONIC (JUCE's clock
    on Linux), or if availFrames is outside [0, ringBufferFrames] (an overrun or underrun, where the
    count no longer describes the ring).

    Stub: the patch that implements it is P022.
*/
inline std::optional<AudioIODeviceTimestamp> getAlsaTimestamp ([[maybe_unused]] bool isCapture,
                                                               [[maybe_unused]] const std::optional<AlsaPositionReading>& reading,
                                                               [[maybe_unused]] bool timestampsAreMonotonic,
                                                               [[maybe_unused]] int64_t applicationFramesAtReading,
                                                               [[maybe_unused]] int64_t bufferStartFrame,
                                                               [[maybe_unused]] int64_t ringBufferFrames) noexcept
{
    return {};
}

//==============================================================================
/*  The processing-cycle observation of a JACK process callback, from jack_get_cycle_times: the
    current cycle's start, currentUsecs, converted exactly to ns (x 1000), at offset 0.
    clockIsMonotonic says that this JACK's clock is CLOCK_MONOTONIC in microseconds, which JUCE's
    clock on Linux is too.

    nullopt if jack_get_cycle_times failed (a nonzero result) or the clock is not known to be
    CLOCK_MONOTONIC.

    Stub: the patch that implements it is P023.
*/
inline std::optional<AudioIODeviceTimestamp> getJackProcessingCycle ([[maybe_unused]] int getCycleTimesResult,
                                                                     [[maybe_unused]] uint64_t currentUsecs,
                                                                     [[maybe_unused]] bool clockIsMonotonic) noexcept
{
    return {};
}

} // namespace juce::CallbackTimeHelpers
