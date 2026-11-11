// Integration check for the separate-device combiner's timestamps. Opens one virtual HAL device
// as the input and another as the output through AudioDeviceManager, so that JUCE takes the
// AudioIODeviceCombiner path, records every callback's context, and asserts the rules in the
// timestamp rails (RAILS.md, "macOS separate-device combiner output role").

#include <juce_audio_utils/juce_audio_utils.h>
#include <cmath>

using namespace juce;

namespace
{
    struct Record
    {
        int numSamples = 0, xruns = 0;
        bool hasHost = false, hasInput = false, hasOutput = false, hasCycle = false;
        int64_t inTime = 0, inOffset = 0, outTime = 0, outOffset = 0;
        uint32_t outFlags = 0;
    };

    constexpr double sampleRate = 48000.0;
    constexpr int bufferSize = 256;
    constexpr int runSeconds = 20;
    constexpr int xrunAfterCallbacks = 2400;       // about 12.8 s in
    constexpr int blockMilliseconds = 200;

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
              << " rate=" << device->getCurrentSampleRate() << " buffer=" << device->getCurrentBufferSizeSamples() << std::endl;

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

    std::cout << (failures == 0 ? "ALL PASS" : "FAILURES: " + String (failures)) << std::endl;
    return failures == 0 ? 0 : 1;
}
