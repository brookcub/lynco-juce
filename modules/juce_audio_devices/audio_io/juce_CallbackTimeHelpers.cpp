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

    The arithmetic is done on exact integers without a wider type: the position is split into
    whole multiples of the denominator and a remainder, so that no intermediate product reaches
    2^63.
*/
inline AlignedSample alignToApplicationSample (int64_t nativeTimeNs,
                                               int64_t nativePosition,
                                               int64_t numerator,
                                               int64_t denominator,
                                               int64_t applicationSampleRate) noexcept
{
    const auto floorDiv = [] (int64_t a, int64_t b)
    {
        const auto q = a / b;
        return (a % b != 0 && (a < 0) != (b < 0)) ? q - 1 : q;
    };

    // position = whole * denominator + rest, with 0 <= rest < denominator
    const auto whole = floorDiv (nativePosition, denominator);
    const auto rest = nativePosition - whole * denominator;

    // x = whole * numerator + rest * numerator / denominator, and n = whole * numerator + extra
    const auto restScaled = rest * numerator;                                   // < 2^62
    const auto extra = floorDiv (2 * restScaled + denominator, 2 * denominator); // x + 1/2, floored

    // n - x, in units of 1 / denominator seconds-of-samples: |residual| <= denominator / 2
    const auto residual = extra * denominator - restScaled;

    // residual / denominator samples are residual * 1e9 / (denominator * rate) ns, nearest ns
    const auto num = residual * 1'000'000'000;
    const auto den = denominator * applicationSampleRate;

    return { nativeTimeNs + floorDiv (2 * num + den, 2 * den), whole * numerator + extra };
}

/*  The same observation seen from a chunk of the callback's buffer that starts chunkStart samples
    in, as when a buffer is split into smaller callbacks: the time and flags are unchanged and the
    offset is chunkStart smaller. Chunks of chunks add up exactly.

*/
inline AudioIODeviceTimestamp forChunkAt (const AudioIODeviceTimestamp& timestamp, int64_t chunkStart) noexcept
{
    auto result = timestamp;
    result.sampleOffset -= chunkStart;
    return result;
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
*/
inline std::optional<AudioIODeviceTimestamp> getOboeInputTimestamp (const std::optional<StreamTimestamp>& stream,
                                                                    int64_t framesReadAfterRead,
                                                                    int64_t framesJustRead) noexcept
{
    if (! stream.has_value() || framesJustRead <= 0)
        return {};

    return AudioIODeviceTimestamp { stream->timeNs, stream->framePosition - (framesReadAfterRead - framesJustRead), 0 };
}

/*  The output observation of an Oboe callback: framesWrittenBeforeCallback is the stream's
    getFramesWritten() at the start of the callback, the frame index of the buffer's first frame.
    The offset is the stream timestamp's frame minus that index, and the time is the stream's.

    nullopt if the stream gave no timestamp.
*/
inline std::optional<AudioIODeviceTimestamp> getOboeOutputTimestamp (const std::optional<StreamTimestamp>& stream,
                                                                     int64_t framesWrittenBeforeCallback) noexcept
{
    if (! stream.has_value())
        return {};

    return AudioIODeviceTimestamp { stream->timeNs, stream->framePosition - framesWrittenBeforeCallback, 0 };
}

//==============================================================================
/*  The observation of an Apple AudioTimeStamp that belongs to the first frame of a buffer, as
    CoreAudio's IOProc stamps and the iOS render callback's stamp do: the host time converted with
    hostTimeToNanos, at offset 0. The stamp is anything with mFlags and mHostTime.

    nullopt if there is no stamp or its flags do not include hostTimeValidFlag.

    Used by P014.1 (CoreAudio) and P020 (the iOS render cycle).
*/
template <typename Stamp, typename Flags, typename HostTimeToNanos>
std::optional<AudioIODeviceTimestamp> getBufferStartTimestamp (const Stamp* stamp,
                                                               Flags hostTimeValidFlag,
                                                               HostTimeToNanos&& hostTimeToNanos)
{
    if (stamp == nullptr || (stamp->mFlags & hostTimeValidFlag) == 0)
        return {};

    return AudioIODeviceTimestamp { (int64_t) hostTimeToNanos (stamp->mHostTime), 0, 0 };
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
*/
template <typename Stamp, typename Flags, typename HostTimeToNanos>
CoreAudioChunkTimes getCoreAudioChunkTimes (const Stamp* inputStamp,
                                            const Stamp* outputStamp,
                                            int numInputChannels,
                                            int numOutputChannels,
                                            int64_t chunkStart,
                                            Flags hostTimeValidFlag,
                                            HostTimeToNanos&& hostTimeToNanos)
{
    CoreAudioChunkTimes result;

    const auto seenFromChunk = [&] (const Stamp* stamp, int numChannels) -> std::optional<AudioIODeviceTimestamp>
    {
        if (numChannels <= 0)
            return {};

        if (auto start = getBufferStartTimestamp (stamp, hostTimeValidFlag, hostTimeToNanos))
            return forChunkAt (*start, chunkStart);

        return {};
    };

    result.inputStream  = seenFromChunk (inputStamp, numInputChannels);
    result.outputStream = seenFromChunk (outputStamp, numOutputChannels);

    if (const auto* hostStamp = numOutputChannels > 0 ? outputStamp : inputStamp; hostStamp != nullptr && chunkStart == 0)
        result.hostTimeNs = (uint64_t) hostTimeToNanos (hostStamp->mHostTime);

    return result;
}

/*  The context that AudioIODeviceCombiner passes its callback from its input device's callback. It
    keeps the input device's hostTimeNs, as upstream does, and its input observation. The callback's
    outputs are presented later on another device, through a FIFO whose correspondence to the
    output device's stamps is established by CombinedOutputCorrespondence, which the combiner adds
    itself, so the context built here has no output or cycle observation.
*/
inline AudioIODeviceCallbackContext getCombinedCallbackContext (const AudioIODeviceCallbackContext& inputDeviceContext) noexcept
{
    return { inputDeviceContext.hostTimeNs, inputDeviceContext.inputStream, nullptr, nullptr };
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

    Called from both devices' threads; the implementation does not lock or allocate. Each side
    owns its state and publishes it in a SeqLock; reset() only bumps an atomic epoch, so that
    neither thread ever writes the other's state.
*/
class CombinedOutputCorrespondence
{
public:
    /*  Forgets the correspondence, as when the device starts or an xrun invalidates both FIFO
        positions. There is no observation until frames are written and a stamped frame is read again.
    */
    void reset() noexcept
    {
        epoch.fetch_add (1);
    }

    /*  The application's buffer of numFrames frames went to FIFO addresses [writeStart, writeStart +
        numFrames). A write that does not follow the previous one (frames were dropped or the addresses
        jumped) loses the correspondence, as reset() does, and starts a new run of addresses.
    */
    void wrote (uint64_t writeStart, int numFrames) noexcept
    {
        const auto e = epoch.load();
        const auto run = runs.load();
        const auto end = writeStart + (uint64_t) numFrames;

        if (run && run->epoch == e && run->end == writeStart)
            runs.store (Run { e, run->id, run->start, end });
        else
            runs.store (Run { e, (run ? run->id : 0) + 1, writeStart, end });
    }

    /*  The output device's callback filled numFrames frames: the first numPaddingFrames of them with
        silence, and the rest from FIFO addresses readStart + numPaddingFrames onwards. outputStamp is
        that callback's output observation, relative to its own buffer. It becomes the observation if
        the frame it stamps was read from the FIFO in this callback (not padding) and was written by
        the application since the last reset; otherwise the previous observation is kept.
    */
    void read (uint64_t readStart,
               int numFrames,
               int numPaddingFrames,
               const std::optional<AudioIODeviceTimestamp>& outputStamp) noexcept
    {
        const auto run = runs.load();

        // The stamped frame's index in this callback's buffer must be one that was read, not padding
        if (! outputStamp || ! run || run->epoch != epoch.load()
            || outputStamp->sampleOffset < numPaddingFrames || outputStamp->sampleOffset >= numFrames)
            return;

        const auto address = readStart + (uint64_t) outputStamp->sampleOffset;

        if (address >= run->start && address < run->end)
            observations.store (Observation { run->id, outputStamp->timeNs, address, outputStamp->nativeFlags });
    }

    /*  The output observation for the application buffer about to be written at writeStart: the
        latest observation's time and flags, at offset (its address - writeStart). nullopt if there is
        no observation, or writeStart does not continue the run of addresses written since the reset.
    */
    std::optional<AudioIODeviceTimestamp> observationFor (uint64_t writeStart) const noexcept
    {
        const auto run = runs.load();
        const auto observation = observations.load();

        if (! run || ! observation || run->epoch != epoch.load() || run->end != writeStart || observation->runId != run->id)
            return {};

        return AudioIODeviceTimestamp { observation->timeNs, (int64_t) (observation->address - writeStart), (uint32_t) observation->flags };
    }

private:
    struct Run         { uint64_t epoch, id, start, end; };
    struct Observation { uint64_t runId; int64_t timeNs; uint64_t address; uint64_t flags; };

    /*  A value written by one thread and read by any, without locks: the writer never waits, and a
        reader that keeps colliding with a write gives up and sees no value (which only ever means
        "no observation"). The words are atomics so that a torn read is not a data race.
    */
    template <typename T>
    class SeqLock
    {
    public:
        void store (const T& value) noexcept
        {
            uint64_t source[numWords];
            std::memcpy (source, &value, sizeof (T));

            const auto s = sequence.load (std::memory_order_relaxed);
            sequence.store (s + 1, std::memory_order_relaxed);
            std::atomic_thread_fence (std::memory_order_release);

            for (size_t i = 0; i < numWords; ++i)
                words[i].store (source[i], std::memory_order_relaxed);

            sequence.store (s + 2, std::memory_order_release);
        }

        std::optional<T> load() const noexcept
        {
            for (int attempt = 0; attempt < 8; ++attempt)
            {
                const auto before = sequence.load (std::memory_order_acquire);

                if (before == 0)
                    return {};   // never stored

                if (before % 2 != 0)
                    continue;

                uint64_t copy[numWords];

                for (size_t i = 0; i < numWords; ++i)
                    copy[i] = words[i].load (std::memory_order_relaxed);

                std::atomic_thread_fence (std::memory_order_acquire);

                if (sequence.load (std::memory_order_relaxed) == before)
                {
                    T value;
                    std::memcpy (&value, copy, sizeof (T));
                    return value;
                }
            }

            return {};
        }

    private:
        static_assert (std::is_trivially_copyable_v<T> && sizeof (T) % sizeof (uint64_t) == 0);
        static constexpr size_t numWords = sizeof (T) / sizeof (uint64_t);
        std::atomic<uint64_t> sequence { 0 };
        std::atomic<uint64_t> words[numWords] {};
    };

    std::atomic<uint64_t> epoch { 0 };
    SeqLock<Run> runs;                  // written by wrote() only
    SeqLock<Observation> observations;  // written by read() only
};

//==============================================================================
/*  One buffer switch's native pair: from the ASIOTime that bufferSwitchTimeInfo delivers for that
    switch, or, for a driver that calls the plain bufferSwitch, from ASIOGetSamplePosition (its
    success sets both valid flags; it has no change flags).
*/
struct AsioSwitchTime
{
    bool systemTimeValid = false, samplePositionValid = false;   // kSystemTimeValid, kSamplePositionValid
    uint32_t systemTimeHi = 0, systemTimeLo = 0;                 // ASIOTimeStamp halves: ns on the driver's clock
    uint32_t samplePositionHi = 0, samplePositionLo = 0;         // ASIOSamples halves: the pair's sample position
    bool sampleRateChanged = false, clockSourceChanged = false;  // kSampleRateChanged, kClockSourceChanged
};

/*  The processing-cycle observation of each buffer switch of a run, on the callback thread.

    reset (blockSize) starts a run (open/start); nothing from an earlier run is kept.

    next() is called once for every buffer switch, in order. Both 64-bit values are their two
    32-bit halves combined as unsigned. Block-start bookkeeping: the first switch with a valid
    sample position sets the tracked start to that position; every later switch first advances the
    tracked start by blockSize, then, if its position is valid and greater, moves it up to that
    position (a driver's pair never names a later block than the current one; a pair ahead of the
    tracking means switches were missed). An invalid position still counts as a switch.

    The observation: timeNs = systemTime + clockOffsetNs; sampleOffset = samplePosition - tracked
    start (0 for this switch's own pair, -blockSize for the previous switch's, ...); nativeFlags =
    nativeDiscontinuity if sampleRateChanged or clockSourceChanged, else 0.

    nullopt if the system time or the sample position is not valid, there is no clock offset, or
    no valid position has been seen in this run.

    Implementers may add private members; the public interface is fixed.
*/
class AsioBlockTimes
{
public:
    void reset (int blockSize) noexcept
    {
        size = blockSize;
        start.reset();
    }

    std::optional<AudioIODeviceTimestamp> next (const AsioSwitchTime& switchTime, std::optional<int64_t> clockOffsetNs) noexcept
    {
        const auto position = (int64_t) (((uint64_t) switchTime.samplePositionHi << 32) | (uint64_t) switchTime.samplePositionLo);

        if (start.has_value())
            *start += size;

        if (switchTime.samplePositionValid && (! start.has_value() || position > *start))
            start = position;

        if (! switchTime.systemTimeValid || ! switchTime.samplePositionValid || ! clockOffsetNs.has_value())
            return {};

        // The halves are unsigned: the low one must not be sign-extended on its way into the sum
        const auto systemTimeNs = (int64_t) (((uint64_t) switchTime.systemTimeHi << 32) | (uint64_t) switchTime.systemTimeLo);
        const uint32_t flags = (switchTime.sampleRateChanged || switchTime.clockSourceChanged) ? (uint32_t) AudioIODeviceTimestamp::nativeDiscontinuity : 0u;

        return AudioIODeviceTimestamp { systemTimeNs + *clockOffsetNs, position - *start, flags };
    }

private:
    int size = 0;
    std::optional<int64_t> start;   // the current block's first frame, in the driver's count
};

/*  Three candidate clocks, read back to back at the instant timeGetTime()'s value changes, after
    the driver times were read: QPC (as Time::getHighResolutionTicks() in ns), interrupt time
    (QueryInterruptTimePrecise x 100) and timeGetTime() x 1'000'000, all in ns.
*/
struct AsioClockCandidates
{
    int64_t qpcNs, interruptTimeNs, timeGetTimeNs;
};

/*  Identifies the driver's systemTime clock once per run and returns the offset to add to it to
    reach the QPC timeline (candidates.qpcNs - the matching candidate), or nullopt (the role is
    absent for the run).

    driverTimesNs: successive system times of the run, oldest first. Needs at least 8. If every one
    is a whole millisecond the only candidate is timeGetTime; otherwise the candidates are QPC and
    interrupt time. A candidate c matches if the newest driver time t has c - maxAgeNs <= t <= c
    (the driver's time is of a switch that has already happened, at most maxAgeNs earlier). No
    match gives nullopt. If QPC and interrupt time both match (they are closer together than
    maxAgeNs, e.g. no sleep since boot), the clock is interrupt time (SPEC "ASIO ambiguous clock"):
    for a QPC driver the error is the measured |interrupt time - QPC| gap, constant for the run and
    at most maxAgeNs.
*/
inline std::optional<int64_t> identifyAsioClockOffset (const int64_t* driverTimesNs, int numDriverTimes,
                                                       const AsioClockCandidates& candidates, int64_t maxAgeNs) noexcept
{
    if (numDriverTimes < 8)
        return {};

    const auto newest = driverTimesNs[numDriverTimes - 1];
    const auto allWholeMilliseconds = std::all_of (driverTimesNs, driverTimesNs + numDriverTimes,
                                                   [] (int64_t t) { return t % 1'000'000 == 0; });

    const auto matches = [&] (int64_t candidate) { return candidate - maxAgeNs <= newest && newest <= candidate; };

    if (allWholeMilliseconds)
        return matches (candidates.timeGetTimeNs) ? std::optional<int64_t> (candidates.qpcNs - candidates.timeGetTimeNs) : std::nullopt;

    // Interrupt time first: if QPC matches as well (the two are closer than maxAgeNs), the driver's
    // clock is taken to be interrupt time, and a QPC driver then has a constant error of the gap
    if (matches (candidates.interruptTimeNs))
        return candidates.qpcNs - candidates.interruptTimeNs;

    return matches (candidates.qpcNs) ? std::optional<int64_t> (0) : std::nullopt;
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
*/
inline std::optional<AudioIODeviceTimestamp> getAlsaTimestamp (bool isCapture,
                                                               const std::optional<AlsaPositionReading>& reading,
                                                               bool timestampsAreMonotonic,
                                                               int64_t applicationFramesAtReading,
                                                               int64_t bufferStartFrame,
                                                               int64_t ringBufferFrames) noexcept
{
    if (! reading.has_value() || ! timestampsAreMonotonic
        || reading->availFrames < 0 || reading->availFrames > ringBufferFrames)
        return {};

    const auto hardwarePointer = isCapture ? applicationFramesAtReading + reading->availFrames
                                           : applicationFramesAtReading - (ringBufferFrames - reading->availFrames);

    return AudioIODeviceTimestamp { reading->seconds * 1'000'000'000 + reading->nanoseconds,
                                    hardwarePointer - bufferStartFrame,
                                    0 };
}

//==============================================================================
/*  The processing-cycle observation of a JACK process callback, from jack_get_cycle_times: the
    current cycle's start, currentUsecs, converted exactly to ns (x 1000), at offset 0.
    clockIsMonotonic says that this JACK's clock is CLOCK_MONOTONIC in microseconds, which JUCE's
    clock on Linux is too.

    nullopt if jack_get_cycle_times failed (a nonzero result) or the clock is not known to be
    CLOCK_MONOTONIC.
*/
inline std::optional<AudioIODeviceTimestamp> getJackProcessingCycle (int getCycleTimesResult,
                                                                     uint64_t currentUsecs,
                                                                     bool clockIsMonotonic) noexcept
{
    if (getCycleTimesResult != 0 || ! clockIsMonotonic)
        return {};

    return AudioIODeviceTimestamp { (int64_t) currentUsecs * 1000, 0, 0 };
}

/*  One process callback's clock readings, taken back to back in this order: CLOCK_MONOTONIC,
    jack_get_time(), CLOCK_MONOTONIC_RAW.
*/
struct JackClockReadings
{
    int64_t  monotonicNs;
    uint64_t jackTimeUsecs;
    int64_t  monotonicRawNs;
};

/*  SPEC "JACK clock: ruling 8 replaced". JACK's clock is whichever of CLOCK_MONOTONIC and
    CLOCK_MONOTONIC_RAW jack_get_time() (x 1000) is closer to; a tie is CLOCK_MONOTONIC. The cycle
    start, currentUsecs x 1000 exactly, moves onto CLOCK_MONOTONIC by adding (monotonicNs - that
    clock's reading): nothing for CLOCK_MONOTONIC, (monotonicNs - monotonicRawNs) for RAW. Offset 0,
    flags 0. Each callback is converted from its own readings only; nothing is carried over.

    nullopt if jack_get_cycle_times failed (nonzero), or jack_get_time() is more than 1 ms from both
    clocks.

    Supersedes the overload above, which P023's builder deletes with the backend's activation check.
*/
inline std::optional<AudioIODeviceTimestamp> getJackProcessingCycle (int getCycleTimesResult,
                                                                     uint64_t currentUsecs,
                                                                     const JackClockReadings& readings) noexcept
{
    ignoreUnused (getCycleTimesResult, currentUsecs, readings);
    return {};    // P023 implements
}

} // namespace juce::CallbackTimeHelpers
