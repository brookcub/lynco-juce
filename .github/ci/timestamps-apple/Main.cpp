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
        int numSamples = 0;
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
    ScopedJuceInitialiser_GUI juceInit;

    const auto inputName = String ("BlackHole 2ch"), outputName = String ("BlackHole 16ch");

    AudioDeviceManager manager;
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
    check (withOutput >= counted * 0.90, "outputStream present in >= 90% of callbacks");
    check (withCycle == 0, "processingCycle is never present");

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
        check (absent >= 1, "outputStream is absent for at least one callback after the xrun");
        check (returned > firstAbsent && firstAbsent >= 0, "outputStream returns after the xrun");
    }
    else
    {
        check (false, "the blocking callback ran");
    }

    std::cout << (failures == 0 ? "ALL PASS" : "FAILURES: " + String (failures)) << std::endl;
    return failures == 0 ? 0 : 1;
}
