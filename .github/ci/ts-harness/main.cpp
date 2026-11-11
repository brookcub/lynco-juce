// Integration harness for the ALSA and JACK timestamp roles. Not part of the fork: it opens a
// device through AudioDeviceManager, records the callback context of every callback, and checks
// the RAILS.md P022/P023 assertions.
//
//   ts-harness jack  <seconds> [require-present|require-absent]
//   ts-harness alsa  <outputNameSubstring> <inputNameSubstring> <seconds> [present <outHwParams> <inHwParams> | absent]
//   ts-harness list
#include <juce_audio_devices/juce_audio_devices.h>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <dlfcn.h>
#include <fstream>
#include <string>

using namespace juce;

struct Rec
{
    int64_t nowNs;
    int numSamples;
    bool hostNull, inP, outP, cycP;
    AudioIODeviceTimestamp in, out, cyc;
};

static int64_t clockNs (clockid_t id)
{
    timespec t;
    clock_gettime (id, &t);
    return (int64_t) t.tv_sec * 1'000'000'000 + t.tv_nsec;
}

struct Recorder final : AudioIODeviceCallback
{
    std::vector<Rec> recs;
    std::atomic<bool> recording { false };

    Recorder() { recs.reserve (400000); }

    void audioDeviceAboutToStart (AudioIODevice*) override {}
    void audioDeviceStopped() override {}

    void audioDeviceIOCallbackWithContext (const float* const*, int, float* const* out, int numOut, int n,
                                           const AudioIODeviceCallbackContext& c) override
    {
        for (int i = 0; i < numOut; ++i)
            FloatVectorOperations::clear (out[i], n);

        if (! recording || recs.size() == recs.capacity())
            return;

        Rec r {};
        r.nowNs = clockNs (CLOCK_MONOTONIC);
        r.numSamples = n;
        r.hostNull = c.hostTimeNs == nullptr;
        if ((r.inP = c.inputStream != nullptr))       r.in = *c.inputStream;
        if ((r.outP = c.outputStream != nullptr))     r.out = *c.outputStream;
        if ((r.cycP = c.processingCycle != nullptr))  r.cyc = *c.processingCycle;
        recs.push_back (r);
    }
};

static int failures = 0;
static void check (bool ok, const String& what)
{
    std::printf ("%s  %s\n", ok ? "PASS" : "FAIL", what.toRawUTF8());
    if (! ok) ++failures;
}

static void list (AudioDeviceManager& m)
{
    for (auto* t : m.getAvailableDeviceTypes())
    {
        t->scanForDevices();
        std::printf ("type \"%s\"\n", t->getTypeName().toRawUTF8());
        for (auto& n : t->getDeviceNames (true))  std::printf ("   in : \"%s\"\n", n.toRawUTF8());
        for (auto& n : t->getDeviceNames (false)) std::printf ("   out: \"%s\"\n", n.toRawUTF8());
    }
}

static String find (AudioIODeviceType& t, bool input, const String& sub)
{
    for (auto& n : t.getDeviceNames (input))
        if (n.containsIgnoreCase (sub))
            return n;
    return {};
}

// The expectation for the JACK processingCycle role, decided here and not by the backend (SPEC
// ruling 8): present iff jack_get_time() agrees with CLOCK_MONOTONIC within 1 ms. Read while the
// device is open, so libjack is loaded and initialised. 1 = present, 0 = absent, -1 = within
// 0.5..2 ms of the boundary, too close to decide (no presence assertion).
static int jackClockExpectation()
{
    auto* lib = dlopen ("libjack.so.0", RTLD_NOW | RTLD_NOLOAD);
    auto* getTime = lib != nullptr ? (uint64_t (*)()) dlsym (lib, "jack_get_time") : nullptr;

    if (getTime == nullptr) { std::printf ("clock: jack_get_time not found\n"); return -1; }

    const auto before = clockNs (CLOCK_MONOTONIC);
    const auto jackNs = (int64_t) getTime() * 1000;
    const auto after = clockNs (CLOCK_MONOTONIC);
    const auto raw = clockNs (CLOCK_MONOTONIC_RAW);
    const auto outside = jackNs < before ? before - jackNs : (jackNs > after ? jackNs - after : 0);

    std::printf ("clock: jack_get_time - CLOCK_MONOTONIC = %lld ns; CLOCK_MONOTONIC - CLOCK_MONOTONIC_RAW = %lld ns\n",
                 (long long) (jackNs - (before + after) / 2), (long long) (after - raw));

    if (outside <= 500'000)    return 1;
    if (outside >= 2'000'000)  return 0;
    return -1;
}

// "buffer_size: N" from /proc/asound/.../hw_params of an open substream; 0 if unreadable
static int64_t ringFrames (const char* hwParamsPath)
{
    std::ifstream f (hwParamsPath);
    for (std::string line; std::getline (f, line);)
        if (line.rfind ("buffer_size:", 0) == 0)
            return std::stoll (line.substr (12));
    return 0;
}

int main (int argc, char** argv)
{
    ScopedJuceInitialiser_GUI init;
    AudioDeviceManager manager;
    manager.initialise (0, 0, nullptr, false);

    const String mode = argc > 1 ? argv[1] : "list";
    if (mode == "list") { list (manager); return 0; }

    const bool jack = mode == "jack";
    const double seconds = std::atof (argv[jack ? 2 : 4]);
    const String expectation = argc > (jack ? 3 : 5) ? argv[jack ? 3 : 5] : (jack ? "" : "present");

    AudioIODeviceType* type = nullptr;
    for (auto* t : manager.getAvailableDeviceTypes())
        if (t->getTypeName().containsIgnoreCase (jack ? "JACK" : "ALSA"))
            type = t;

    if (type == nullptr) { std::printf ("no such device type\n"); return 2; }
    manager.setCurrentAudioDeviceType (type->getTypeName(), true);
    type->scanForDevices();

    AudioDeviceManager::AudioDeviceSetup setup;
    manager.getAudioDeviceSetup (setup);
    setup.sampleRate = 48000;
    setup.bufferSize = 256;
    setup.useDefaultInputChannels = setup.useDefaultOutputChannels = false;
    setup.inputChannels.setRange (0, 2, true);
    setup.outputChannels.setRange (0, 2, true);

    if (jack)
    {
        setup.inputDeviceName  = type->getDeviceNames (true)[0];
        setup.outputDeviceName = type->getDeviceNames (false)[0];
    }
    else
    {
        setup.outputDeviceName = find (*type, false, argv[2]);
        setup.inputDeviceName  = find (*type, true, argv[3]);
    }

    std::printf ("in=\"%s\" out=\"%s\"\n", setup.inputDeviceName.toRawUTF8(), setup.outputDeviceName.toRawUTF8());
    Recorder rec;
    manager.addAudioCallback (&rec);
    auto err = manager.setAudioDeviceSetup (setup, true);
    if (err.isNotEmpty()) { std::printf ("setup error: %s\n", err.toRawUTF8()); return 2; }
    auto* dev = manager.getCurrentAudioDevice();
    if (dev == nullptr || setup.inputDeviceName.isEmpty() || setup.outputDeviceName.isEmpty()) { std::printf ("FAIL  no such device\n"); return 2; }
    std::printf ("device \"%s\" type %s rate %g buffer %d\n", dev->getName().toRawUTF8(),
                 dev->getTypeName().toRawUTF8(), dev->getCurrentSampleRate(), dev->getCurrentBufferSizeSamples());

    Thread::sleep (1000);

    const int jackExpected = jack ? jackClockExpectation() : -1;
    const bool alsaPresent = ! jack && expectation == "present";
    const int64_t outRing = alsaPresent && argc > 6 ? ringFrames (argv[6]) : 0;
    const int64_t inRing  = alsaPresent && argc > 7 ? ringFrames (argv[7]) : 0;

    rec.recording = true;
    Thread::sleep ((int) (seconds * 1000));
    rec.recording = false;
    Thread::sleep (100);
    manager.removeAudioCallback (&rec);
    manager.closeAudioDevice();

    auto& r = rec.recs;
    const double rate = 48000.0;
    const int n = 256;
    std::printf ("callbacks recorded: %zu\n", r.size());
    check (! r.empty(), "callbacks received");
    if (r.empty()) return 1;

    int hostNonNull = 0, inP = 0, outP = 0, cycP = 0;
    for (auto& x : r) { hostNonNull += ! x.hostNull; inP += x.inP; outP += x.outP; cycP += x.cycP; }
    const double N = (double) r.size();
    std::printf ("present: in %.1f%% out %.1f%% cycle %.1f%%\n", 100 * inP / N, 100 * outP / N, 100 * cycP / N);
    check (hostNonNull == 0, "hostTimeNs always null");

    // Per-1 s span: the change in (time, offset+frames delivered) must agree with 48 kHz within 1 %.
    auto spans = [&] (auto present, auto get, bool useOffset, const char* name)
    {
        int tested = 0, bad = 0;
        size_t a = 0;
        while (a < r.size())
        {
            if (! present (r[a])) { ++a; continue; }
            size_t b = a + 1;
            int64_t frames = 0;
            while (b < r.size() && (r[b].nowNs - r[a].nowNs) < 1'000'000'000) { frames += r[b - 1].numSamples; ++b; }
            if (b >= r.size()) break;
            while (b < r.size() && ! present (r[b])) { frames += r[b - 1].numSamples; ++b; }
            if (b >= r.size()) break;
            const auto ta = get (r[a]), tb = get (r[b]);
            const double dt = (double) (tb.timeNs - ta.timeNs) * 1e-9;
            // native frames elapsed = frames delivered between the buffers + change of offset
            const double df = (double) frames + (useOffset ? (double) (tb.sampleOffset - ta.sampleOffset) : 0.0);
            if (dt > 0.5)
            {
                ++tested;
                const double implied = df / dt;
                if (std::abs (implied / rate - 1.0) > 0.01) { ++bad; if (bad < 4) std::printf ("   span: dt %.6f frames %.0f implied %.1f Hz\n", dt, df, implied); }
            }
            a = b;
        }
        check (tested > 0 && bad == 0, String (name) + ": time/offset spans agree with 48 kHz within 1% (" + String (tested) + " spans, " + String (bad) + " bad)");
    };

    if (! jack && expectation == "absent")
    {
        check (inP == 0 && outP == 0, "inputStream/outputStream absent in every callback (plugin devices)");
        check (cycP == 0, "processingCycle absent");
    }
    else if (jack)
    {
        // The run must exercise the path the workflow names; which path applies is the clock's call
        std::printf ("clock decides: %s\n", jackExpected == 1 ? "present" : jackExpected == 0 ? "absent" : "undecided");
        if (expectation == "require-present") check (jackExpected == 1, "the clocks agree, so this run exercises the present path");
        if (expectation == "require-absent")  check (jackExpected == 0, "the clocks disagree, so this run exercises the absent path");

        check (inP == 0 && outP == 0, "inputStream/outputStream absent");

        if (jackExpected == 0)
            check (cycP == 0, "processingCycle absent in every callback (jack_get_time is not CLOCK_MONOTONIC)");
        else if (jackExpected == 1)
            check (cycP >= 0.99 * N, "processingCycle present in >= 99% of callbacks");

        if (cycP > 0)
        {
            bool offsetsZero = true, increasing = true, nearNow = true, precedesEntry = true;
            int repeats = 0;
            int64_t minLead = INT64_MAX, maxLead = INT64_MIN;
            for (size_t i = 0; i < r.size(); ++i)
            {
                if (! r[i].cycP) continue;
                offsetsZero = offsetsZero && r[i].cyc.sampleOffset == 0 && r[i].cyc.nativeFlags == 0;
                nearNow = nearNow && std::abs (r[i].cyc.timeNs - r[i].nowNs) < 100'000'000;

                // The current cycle began before this callback was entered (same clock): kills a
                // next-cycle time, which lies about one period ahead. Margin: see the review.
                const auto lead = r[i].cyc.timeNs - r[i].nowNs;
                minLead = std::min (minLead, lead);
                maxLead = std::max (maxLead, lead);
                precedesEntry = precedesEntry && lead <= 1'000'000;

                if (i > 0 && r[i - 1].cycP)
                {
                    const auto d = r[i].cyc.timeNs - r[i - 1].cyc.timeNs;
                    // An exact repeat less than 1 ms after the previous callback is tolerated (a
                    // catch-up read of an unchanged cycle time); time never runs backwards.
                    const bool repeat = d == 0 && r[i].nowNs - r[i - 1].nowNs < 1'000'000;
                    increasing = increasing && (d > 0 || repeat);
                    repeats += repeat;
                    if (d <= 0 && ! repeat) std::printf ("   non-increasing at %zu: d=%lld ns, now-delta %lld\n", i, (long long) d, (long long) (r[i].nowNs - r[i - 1].nowNs));
                }
            }
            std::printf ("cycle time - callback entry: min %lld ns, max %lld ns\n", (long long) minLead, (long long) maxLead);
            check (offsetsZero, "processingCycle offset is always 0 and flags 0");
            check (increasing, "processingCycle times strictly increase (" + String (repeats) + " exact catch-up repeats < 1 ms apart)");
            check (nearNow, "processingCycle.timeNs within 100 ms of CLOCK_MONOTONIC at callback entry");
            check (precedesEntry, "processingCycle.timeNs is not after callback entry (1 ms margin)");
            spans ([] (const Rec& x) { return x.cycP; }, [] (const Rec& x) { return x.cyc; }, false, "cycle");
        }
    }
    else
    {
        // Offsets from the backend's definition (getAlsaTimestamp): output = avail - ring, in
        // [-ring, 0]; input = numSamples + avail, in [numSamples, numSamples + ring]. The ring is
        // the PCM's own buffer_size, read from /proc while the device was open.
        std::printf ("ring: out %lld in %lld\n", (long long) outRing, (long long) inRing);
        check (outRing > 0 && inRing > 0, "ring sizes read from hw_params");
        check (inP >= 0.99 * N && outP >= 0.99 * N, "both roles present in >= 99% of callbacks");
        check (cycP == 0, "processingCycle absent");
        bool outOk = true, inOk = true;
        for (auto& x : r)
        {
            if (x.outP) outOk = outOk && x.out.sampleOffset >= -outRing && x.out.sampleOffset <= 0;
            if (x.inP)  inOk  = inOk  && x.in.sampleOffset >= x.numSamples && x.in.sampleOffset <= x.numSamples + inRing;
        }
        check (outOk, "output offsets in [-ring, 0]");
        check (inOk, "input offsets in [numSamples, numSamples + ring]");
        spans ([] (const Rec& x) { return x.outP; }, [] (const Rec& x) { return x.out; }, true, "output");
        spans ([] (const Rec& x) { return x.inP; },  [] (const Rec& x) { return x.in; },  true, "input");
    }

    std::printf ("%s\n", failures == 0 ? "ALL PASS" : "FAILURES");
    return failures == 0 ? 0 : 1;
}
