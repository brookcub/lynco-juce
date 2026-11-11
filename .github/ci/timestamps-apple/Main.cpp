// Integration check for the separate-device combiner's timestamps. Opens one virtual HAL device
// as the input and another as the output through AudioDeviceManager, so that JUCE takes the
// AudioIODeviceCombiner path, records every callback's context, and asserts the rules in the
// timestamp rails (RAILS.md, "macOS separate-device combiner output role"). It also writes markers
// at known application output frames and captures them through the output device's loopback,
// opened as an ordinary input device, to check the output role's absolute association (RAILS.md,
// "Amendment 2026-11-11: combined-device absolute association").

#include <juce_audio_utils/juce_audio_utils.h>
#include <cmath>

#if JUCE_MAC
 #include <CoreAudio/CoreAudio.h>
#endif

using namespace juce;

namespace
{
    struct Record
    {
        int numSamples = 0, xruns = 0;
        bool hasHost = false, hasInput = false, hasOutput = false, hasCycle = false;
        int64_t inTime = 0, inOffset = 0, outTime = 0, outOffset = 0;
        uint32_t outFlags = 0;
        int markerFrame = -1;    // the frame of this buffer that carries a marker, if any
    };

    constexpr double sampleRate = 48000.0;
    constexpr int bufferSize = 256;
    constexpr int runSeconds = 20;
    constexpr int xrunAfterCallbacks = 2400;       // about 12.8 s in
    constexpr int blockMilliseconds = 200;
    constexpr int markerEvery = 47;                // callbacks, about 0.25 s

    // MEASURING (not yet pinned): see RAILS.md
    constexpr double absoluteToleranceFrames = 1.0e6;

    struct Recorder final : public AudioIODeviceCallback
    {
        Recorder() { records.resize (100000); }

        void audioDeviceAboutToStart (AudioIODevice*) override {}
        void audioDeviceStopped() override {}

        void audioDeviceIOCallbackWithContext (const float* const*, int, float* const* out, int numOut, int n,
                                               const AudioIODeviceCallbackContext& context) override
        {
            for (int c = 0; c < numOut; ++c)
                FloatVectorOperations::clear (out[c], n);

            const auto i = count.load();

            if (i < (int) records.size())
            {
                auto& r = records[(size_t) i];
                r.numSamples = n;
                r.xruns = device->getXRunCount();    // read after the context was built
                r.hasHost = context.hostTimeNs != nullptr;
                r.hasInput = context.inputStream != nullptr;
                r.hasOutput = context.outputStream != nullptr;
                r.hasCycle = context.processingCycle != nullptr;

                if (r.hasInput)   { r.inTime = context.inputStream->timeNs;   r.inOffset = context.inputStream->sampleOffset; }
                if (r.hasOutput)  { r.outTime = context.outputStream->timeNs; r.outOffset = context.outputStream->sampleOffset; r.outFlags = context.outputStream->nativeFlags; }

                // A one-sample marker on channel 0; successive markers step through every frame
                // position of the buffer (61 is coprime to 256)
                if (numOut > 0 && i % markerEvery == 0)
                {
                    r.markerFrame = (i / markerEvery * 61) % n;
                    out[0][r.markerFrame] = 1.0f;
                }

                count.store (i + 1);
            }

            if (i == xrunAfterCallbacks)
            {
                blockedAt.store (i);
                Thread::sleep (blockMilliseconds);
            }
        }

        AudioIODevice* device = nullptr;    // the combiner; its xrun count is atomic
        std::vector<Record> records;
        std::atomic<int> count { 0 }, blockedAt { -1 };
    };

    // The output device's loopback, opened as an ordinary (not combined) input device
    struct CaptureRecorder final : public AudioIODeviceCallback
    {
        struct Capture { int numSamples = 0; bool hasInput = false; int64_t inTime = 0, inOffset = 0; };
        struct Hit { int callback = 0, sample = 0; float value = 0; };

        CaptureRecorder() { records.resize (100000); hits.resize (4096); }

        void audioDeviceAboutToStart (AudioIODevice*) override {}
        void audioDeviceStopped() override {}

        void audioDeviceIOCallbackWithContext (const float* const* in, int numIn, float* const*, int, int n,
                                               const AudioIODeviceCallbackContext& context) override
        {
            const auto i = count.load();

            if (i >= (int) records.size() || numIn < 1)
                return;

            auto& r = records[(size_t) i];
            r.numSamples = n;
            r.hasInput = context.inputStream != nullptr;

            if (r.hasInput) { r.inTime = context.inputStream->timeNs; r.inOffset = context.inputStream->sampleOffset; }

            for (int s = 0; s < n; ++s)
            {
                const auto h = numHits.load();

                if (in[0][s] > 0.25f && h < (int) hits.size())
                {
                    hits[(size_t) h] = { i, s, in[0][s] };
                    numHits.store (h + 1);
                }
            }

            count.store (i + 1);
        }

        std::vector<Capture> records;
        std::vector<Hit> hits;
        std::atomic<int> count { 0 }, numHits { 0 };
    };

    // What the HAL device reports about its own latency, for the record
    void printDeviceLatencies ([[maybe_unused]] const String& name)
    {
       #if JUCE_MAC
        const auto get = [] (AudioObjectID id, AudioObjectPropertySelector selector, AudioObjectPropertyScope scope, void* data, UInt32 size)
        {
            const AudioObjectPropertyAddress address { selector, scope, kAudioObjectPropertyElementMain };
            return AudioObjectGetPropertyData (id, &address, 0, nullptr, &size, data) == noErr;
        };

        const AudioObjectPropertyAddress devicesAddress { kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
        UInt32 size = 0;
        AudioObjectGetPropertyDataSize (kAudioObjectSystemObject, &devicesAddress, 0, nullptr, &size);
        std::vector<AudioObjectID> ids (size / sizeof (AudioObjectID));
        AudioObjectGetPropertyData (kAudioObjectSystemObject, &devicesAddress, 0, nullptr, &size, ids.data());

        for (auto id : ids)
        {
            CFStringRef cfName = nullptr;

            if (! get (id, kAudioObjectPropertyName, kAudioObjectPropertyScopeGlobal, &cfName, sizeof (cfName)) || cfName == nullptr)
                continue;

            const auto deviceName = String::fromCFString (cfName);
            CFRelease (cfName);

            if (deviceName != name)
                continue;

            for (const auto& [scope, scopeName] : { std::pair { kAudioObjectPropertyScopeInput, "input" }, std::pair { kAudioObjectPropertyScopeOutput, "output" } })
            {
                UInt32 latency = 0, safety = 0, frames = 0, streamLatency = 0;
                AudioStreamID streams[64] {};
                get (id, kAudioDevicePropertyLatency, scope, &latency, sizeof (latency));
                get (id, kAudioDevicePropertySafetyOffset, scope, &safety, sizeof (safety));
                get (id, kAudioDevicePropertyBufferFrameSize, kAudioObjectPropertyScopeGlobal, &frames, sizeof (frames));

                if (get (id, kAudioDevicePropertyStreams, scope, streams, sizeof (streams)) && streams[0] != 0)
                    get (streams[0], kAudioStreamPropertyLatency, kAudioObjectPropertyScopeGlobal, &streamLatency, sizeof (streamLatency));

                std::cout << "HAL '" << name << "' " << scopeName << ": device latency " << latency << ", safety offset " << safety
                          << ", stream latency " << streamLatency << ", buffer " << frames << " frames" << std::endl;
            }
        }
       #endif
    }

    int failures = 0;

    void check (bool ok, const String& what)
    {
        std::cout << (ok ? "PASS  " : "FAIL  ") << what << std::endl;
        failures += ok ? 0 : 1;
    }
}

int main()
{
    std::cout << "start" << std::endl;
    ScopedJuceInitialiser_GUI juceInit;
    std::cout << "initialised" << std::endl;

    const auto inputName = String ("BlackHole 2ch"), outputName = String ("BlackHole 16ch");

    AudioDeviceManager manager;
    manager.getAvailableDeviceTypes();    // creates the device types
    manager.setCurrentAudioDeviceType ("CoreAudio", true);

    auto* type = manager.getCurrentDeviceTypeObject();
    std::cout << "Inputs:";
    for (auto& n : type->getDeviceNames (true))  std::cout << " [" << n << "]";
    std::cout << "\nOutputs:";
    for (auto& n : type->getDeviceNames (false)) std::cout << " [" << n << "]";
    std::cout << std::endl;

    // The output device's loopback as an ordinary input-only device (P014.1's ordinary path),
    // started before the combiner so that its start does not land inside the combiner's run
    CaptureRecorder captureRecorder;
    std::unique_ptr<AudioIODevice> capture (type->createDevice ({}, outputName));
    const auto captureError = capture != nullptr ? capture->open (BigInteger (1), {}, sampleRate, bufferSize) : String ("not created");

    if (captureError.isNotEmpty())
    {
        std::cout << "FAIL  could not open '" << outputName << "' as an input: " << captureError << std::endl;
        return 2;
    }

    std::cout << "Capture '" << capture->getName() << "' rate=" << capture->getCurrentSampleRate() << " buffer="
              << capture->getCurrentBufferSizeSamples() << " JUCE input latency=" << capture->getInputLatencyInSamples() << std::endl;
    capture->start (&captureRecorder);

    AudioDeviceManager::AudioDeviceSetup setup;
    setup.inputDeviceName = inputName;
    setup.outputDeviceName = outputName;
    setup.sampleRate = sampleRate;
    setup.bufferSize = bufferSize;
    setup.useDefaultInputChannels = true;
    setup.useDefaultOutputChannels = true;

    const auto error = manager.initialise (2, 2, nullptr, false, {}, &setup);

    if (error.isNotEmpty())
    {
        std::cout << "FAIL  could not open the devices: " << error << std::endl;
        return 2;
    }

    auto* device = manager.getCurrentAudioDevice();
    check (device != nullptr && device->getTypeName() == "CoreAudio", "the device is CoreAudio");

    if (device == nullptr)
        return 2;

    AudioDeviceManager::AudioDeviceSetup actual;
    manager.getAudioDeviceSetup (actual);
    std::cout << "Opened '" << device->getName() << "' in=" << actual.inputDeviceName << " out=" << actual.outputDeviceName
              << " rate=" << device->getCurrentSampleRate() << " buffer=" << device->getCurrentBufferSizeSamples()
              << " JUCE latency in=" << device->getInputLatencyInSamples() << " out=" << device->getOutputLatencyInSamples() << std::endl;
    printDeviceLatencies (inputName);
    printDeviceLatencies (outputName);

    // Two different physical devices are what makes JUCE build its combiner
    check (actual.inputDeviceName == inputName && actual.outputDeviceName == outputName && inputName != outputName,
           "input and output are two different devices");
    check (device->getActiveInputChannels().countNumberOfSetBits() > 0 && device->getActiveOutputChannels().countNumberOfSetBits() > 0,
           "both directions have active channels");

    Recorder recorder;
    recorder.device = device;
    manager.addAudioCallback (&recorder);
    MessageManager::getInstance()->runDispatchLoopUntil (runSeconds * 1000);
    manager.removeAudioCallback (&recorder);
    Thread::sleep (100);    // the last markers reach the loopback
    capture->stop();
    capture->close();

    const auto total = recorder.count.load();
    const auto& r = recorder.records;
    std::cout << "callbacks: " << total << ", xruns reported: " << device->getXRunCount() << ", blocked at: " << recorder.blockedAt.load() << std::endl;

    check (total > 3000, "callbacks arrived (" + String (total) + ")");

    if (total < 300)
        return 2;

    const int skip = (int) (sampleRate / bufferSize) + 1;    // the first second

    // Presence
    int withInput = 0, withOutput = 0, withCycle = 0, withHost = 0;
    for (int i = skip; i < total; ++i)
    {
        withInput += r[(size_t) i].hasInput;
        withOutput += r[(size_t) i].hasOutput;
        withCycle += r[(size_t) i].hasCycle;
        withHost += r[(size_t) i].hasHost;
    }
    const auto counted = total - skip;
    std::cout << "after 1 s: input " << withInput << ", output " << withOutput << ", cycle " << withCycle << ", host " << withHost << " of " << counted << std::endl;
    check (withInput >= counted * 0.99, "inputStream present in >= 99% of callbacks");
    check (withCycle == 0, "processingCycle is never present");

    // outputStream presence, independent of how many xruns the runner produces. An xrun resets the
    // correspondence; it is re-established once the output device has read, and stamped, a frame
    // written after the reset. The FIFO holds targetLatency + 2 buffers, and the writer cannot get
    // further ahead than that without another xrun, so the first new frame is read within
    // ceil (fifo / n) app callbacks of being written. Window = that + 4 callbacks: re-initialising
    // each side (2), a first read that starts on padding gives nothing (1), the read is seen by the
    // next app callback (1). Every callback outside such a window must have the role. The window
    // is excused on both sides of the count change: the count is read after the context was built,
    // so a reset can precede the callback that first shows it.
    const auto fifoFrames = device->getInputLatencyInSamples() + device->getOutputLatencyInSamples() + 2 * bufferSize;
    const int window = (fifoFrames + bufferSize - 1) / bufferSize + 4;
    int excused = 0, required = 0, missing = 0;
    for (int i = skip; i < total; ++i)
    {
        const auto from = std::max (0, i - window - 1), to = std::min (total - 1, i + 1);
        if (r[(size_t) to].xruns != r[(size_t) from].xruns) { ++excused; continue; }
        ++required;
        if (! r[(size_t) i].hasOutput && missing++ < 5)
            std::cout << "  outputStream missing at " << i << " (xruns " << r[(size_t) i].xruns << ")" << std::endl;
    }
    std::cout << "re-establishment window " << window << " callbacks (fifo " << fifoFrames << " frames); excused "
              << excused << ", required " << required << ", missing " << missing << std::endl;
    check (required >= (int) (sampleRate / bufferSize), "at least 1 s of callbacks lie outside xrun windows");
    check (missing == 0, "outputStream present in every callback outside an xrun's re-establishment window");

    // inputStream: the app callback runs inside the input device's callback, so the stamp names a
    // frame of this buffer (offset 0, or -k for a chunk at k), and across xrun-free stretches of at
    // least 1 s its time advances with the frames delivered at 48 kHz within 1 %.
    {
        bool offsetsOk = true;
        int inSpans = 0, inBad = 0;
        int64_t cumulative = 0;
        std::vector<int64_t> first ((size_t) total);
        for (int i = 0; i < total; ++i) { first[(size_t) i] = cumulative; cumulative += r[(size_t) i].numSamples; }

        for (int i = skip; i < total; ++i)
            offsetsOk = offsetsOk && (! r[(size_t) i].hasInput || r[(size_t) i].inOffset <= 0);

        for (int i = skip; i < total;)
        {
            int j = i + 1;
            while (j < total && r[(size_t) j].hasInput && r[(size_t) j].xruns == r[(size_t) i].xruns
                   && r[(size_t) j].inTime - r[(size_t) i].inTime < 1'000'000'000)
                ++j;

            if (j < total && r[(size_t) i].hasInput && r[(size_t) j].hasInput && r[(size_t) j].xruns == r[(size_t) i].xruns)
            {
                const auto frames = (double) ((first[(size_t) j] + r[(size_t) j].inOffset) - (first[(size_t) i] + r[(size_t) i].inOffset));
                const auto rate = frames / ((double) (r[(size_t) j].inTime - r[(size_t) i].inTime) * 1e-9);
                ++inSpans;
                if (std::abs (rate - sampleRate) > sampleRate * 0.01 && inBad++ < 5)
                    std::cout << "  input span " << i << " to " << j << ": " << rate << " frames/s" << std::endl;
            }

            i = j;
        }

        std::cout << "input 1 s spans: " << inSpans << ", off by more than 1%: " << inBad << std::endl;
        check (offsetsOk, "inputStream offsets are <= 0");
        check (inSpans >= 5 && inBad == 0, "inputStream time and frames agree with 48 kHz within 1% over xrun-free 1 s spans");
    }

    // While an observation repeats, its offset falls by the previous callback's numSamples
    int repeats = 0, badRepeats = 0;
    for (int i = skip + 1; i < total; ++i)
    {
        const auto& a = r[(size_t) i - 1];
        const auto& b = r[(size_t) i];

        if (a.hasOutput && b.hasOutput && a.outTime == b.outTime)
        {
            ++repeats;
            if (b.outOffset != a.outOffset - a.numSamples)
            {
                if (badRepeats++ < 5)
                    std::cout << "  repeat mismatch at " << i << ": " << a.outOffset << " -> " << b.outOffset << " after " << a.numSamples << std::endl;
            }
        }
    }
    std::cout << "repeats: " << repeats << ", mismatches: " << badRepeats << std::endl;
    check (repeats > 0 && badRepeats == 0, "a repeated outputStream's offset falls by exactly the previous numSamples");

    // Address change implied by the offsets against the time change, over spans of at least 1 s inside
    // a run of callbacks that all have the output role (an absence ends the run)
    int spans = 0, badSpans = 0;
    for (int start = skip; start < total;)
    {
        int end = start;
        while (end < total && r[(size_t) end].hasOutput)
            ++end;

        // cumulative application frames at each callback in this run
        std::vector<int64_t> first (end - start);
        int64_t cumulative = 0;
        for (int i = start; i < end; ++i) { first[(size_t) (i - start)] = cumulative; cumulative += r[(size_t) i].numSamples; }

        const auto address = [&] (int i) { return first[(size_t) (i - start)] + r[(size_t) i].outOffset; };

        for (int i = start; i < end; ++i)
        {
            if (i > start && r[(size_t) i].outTime == r[(size_t) i - 1].outTime)
                continue;    // not a new observation

            for (int j = i + 1; j < end; ++j)
            {
                if (r[(size_t) j].outTime == r[(size_t) j - 1].outTime || r[(size_t) j].outTime - r[(size_t) i].outTime < 1'000'000'000)
                    continue;

                const auto rate = (double) (address (j) - address (i)) / ((double) (r[(size_t) j].outTime - r[(size_t) i].outTime) * 1e-9);
                ++spans;
                if (std::abs (rate - sampleRate) > sampleRate * 0.01)
                {
                    if (badSpans++ < 5)
                        std::cout << "  span " << i << " to " << j << ": " << rate << " frames/s" << std::endl;
                }
                break;
            }
        }

        start = end + 1;
    }
    std::cout << "1 s spans: " << spans << ", off by more than 1%: " << badSpans << std::endl;
    check (spans >= 5 && badSpans == 0, "address change and time change agree with 48 kHz within 1% over 1 s spans");

    // The forced xrun: the role is absent for a while after it, and returns
    const auto blocked = recorder.blockedAt.load();
    if (blocked >= 0)
    {
        int absent = 0, firstAbsent = -1, returned = -1;
        for (int i = blocked + 1; i < total; ++i)
        {
            if (! r[(size_t) i].hasOutput)
            {
                ++absent;
                if (firstAbsent < 0) firstAbsent = i;
            }
            else if (firstAbsent >= 0 && returned < 0)
            {
                returned = i;
            }
        }
        std::cout << "after the block at " << blocked << ": absent in " << absent << " callbacks, first at " << firstAbsent << ", returned at " << returned << std::endl;

        // Blocking the input device's callback for 200 ms underruns the FIFO, which resets the
        // correspondence. The blocked callback's own write is then dropped, and the next callback's
        // write comes after its context is built, so that callback has no frame written since the
        // reset and cannot have an observation. (Background xruns make "absent somewhere later"
        // vacuous; this one is not.)
        check (blocked + 1 < total && r[(size_t) blocked + 1].xruns > r[(size_t) blocked].xruns, "the block produced an xrun");
        check (blocked + 1 < total && ! r[(size_t) blocked + 1].hasOutput, "outputStream is absent in the callback right after the block");
        check (returned > firstAbsent && firstAbsent >= 0, "outputStream returns after the xrun");
    }
    else
    {
        check (false, "the blocking callback ran");
    }

    // Absolute association of the output role. Predicted: marker frame f of app callback i leaves
    // at outputStream.timeNs + (f - outputStream.sampleOffset) / rate. Captured: loopback frame s of
    // capture callback j arrives at inputStream.timeNs + (s - inputStream.sampleOffset) / rate, the
    // ordinary device's input role. The loopback device writes and reads one ring at the IO cycle's
    // sample times, so both name the same instant up to the HAL's sample-to-host-time mapping.
    // A marker is judged only when the role is present in its callback and no xrun lands between
    // the callback before it and the end of its re-establishment window (an xrun after the
    // observation moves the FIFO, which no observation can foresee). Matching: the nearest
    // captured marker within 100 ms; markers are about 250 ms apart.
    {
        const auto numHits = captureRecorder.numHits.load();
        const auto framesToNs = [] (int64_t frames) { return (int64_t) std::llround ((double) frames * 1e9 / sampleRate); };

        std::vector<int64_t> hitTimes;
        int hitsWithoutTime = 0;
        float minValue = 2.0f, maxValue = 0.0f;

        for (int h = 0; h < numHits; ++h)
        {
            const auto& hit = captureRecorder.hits[(size_t) h];
            const auto& c = captureRecorder.records[(size_t) hit.callback];
            minValue = std::min (minValue, hit.value);
            maxValue = std::max (maxValue, hit.value);

            if (c.hasInput)
                hitTimes.push_back (c.inTime + framesToNs (hit.sample - c.inOffset));
            else
                ++hitsWithoutTime;
        }

        int markers = 0, notJudged = 0, unmatched = 0;
        std::vector<double> residuals;

        for (int i = skip; i < total; ++i)
        {
            const auto& m = r[(size_t) i];

            if (m.markerFrame < 0)
                continue;

            ++markers;
            const auto to = i + window + 1;

            if (! m.hasOutput || to >= total || r[(size_t) to].xruns != r[(size_t) i - 1].xruns)
            {
                ++notJudged;
                continue;
            }

            const auto predicted = m.outTime + framesToNs (m.markerFrame - m.outOffset);
            auto nearest = std::numeric_limits<int64_t>::max();

            for (auto t : hitTimes)
                if (std::abs (t - predicted) < std::abs (nearest))
                    nearest = t - predicted;

            if (std::abs (nearest) > 100'000'000)
            {
                if (unmatched++ < 5)
                    std::cout << "  marker at callback " << i << " frame " << m.markerFrame << ": no capture within 100 ms" << std::endl;
                continue;
            }

            residuals.push_back ((double) nearest * sampleRate * 1e-9);
        }

        const auto [lo, hi] = residuals.empty() ? std::pair { 0.0, 0.0 }
                                                : std::pair { *std::min_element (residuals.begin(), residuals.end()),
                                                              *std::max_element (residuals.begin(), residuals.end()) };
        std::cout << "markers: captured " << numHits << " (values " << minValue << " to " << maxValue << ", " << hitsWithoutTime
                  << " without an input time); written after 1 s " << markers << ", not judged " << notJudged << ", unmatched "
                  << unmatched << ", judged " << residuals.size() << std::endl;
        std::cout << "marker residual (captured - predicted), frames: min " << lo << ", max " << hi << "; all:";
        for (auto x : residuals) std::cout << " " << String (x, 3);
        std::cout << std::endl;

        check (residuals.size() >= 20, "at least 20 markers judged (" + String ((int) residuals.size()) + ")");
        check (unmatched == 0, "every judged marker was captured");
        check (std::abs (lo) <= absoluteToleranceFrames && std::abs (hi) <= absoluteToleranceFrames,
               "outputStream predicts each marker's capture within " + String (absoluteToleranceFrames) + " frames");
    }

    std::cout << (failures == 0 ? "ALL PASS" : "FAILURES: " + String (failures)) << std::endl;
    return failures == 0 ? 0 : 1;
}
