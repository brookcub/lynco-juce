// Real Oboe conversion with a deterministic stream, not a model of its counters.
#include <oboe/Oboe.h>
#include "common/FilterAudioStream.h"
#include <algorithm>
#include <cstdint>
#include <cmath>
#include <limits>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace oboe;

int checks = 0, failures = 0;
void check(bool condition, const char* expression, int line)
{
    ++checks;
    if (!condition) {
        ++failures;
        std::cerr << "line " << line << ": " << expression << '\n';
    }
}
#define CHECK(condition) check(static_cast<bool>(condition), #condition, __LINE__)

float pulse(int64_t frame, int64_t centre)
{
    if (centre < 0 || std::abs(frame - centre) > 32) return 0.0f;
    const auto distance = static_cast<double>(frame - centre) / 4.0;
    return static_cast<float>(std::exp(-0.5 * distance * distance));
}

class ChildStream : public AudioStream
{
public:
    explicit ChildStream(const AudioStreamBuilder& builder) : AudioStream(builder)
    {
        mFramesPerBurst = 96;
        mBufferCapacityInFrames = 1024;
    }
    Result requestStart() override { return Result::OK; }
    Result requestPause() override { return Result::OK; }
    Result requestFlush() override { return Result::OK; }
    Result requestStop() override { return Result::OK; }
    StreamState getState() override { return StreamState::Started; }
    Result waitForStateChange(StreamState, StreamState* next, int64_t) override
    { *next = StreamState::Started; return Result::OK; }
    bool isXRunCountSupported() const override { return false; }
    AudioApi getAudioApi() const override { return AudioApi::AAudio; }
    void updateFramesWritten() override {}
    void updateFramesRead() override {}
    Result getTimestamp(clockid_t, int64_t* position, int64_t* time) override
    {
        // A repeated native pair is valid; projecting to successive app blocks must advance.
        if (timestampError != Result::OK) return timestampError;
        if (position) *position = timestampFrame;
        *time = 1000000000 + static_cast<int64_t>(std::llround(timestampFrame * 1e9 / getSampleRate()));
        return Result::OK;
    }
    ResultWithValue<int32_t> read(void* buffer, int32_t frames, int64_t) override
    {
        if (readError != Result::OK) return ResultWithValue<int32_t>(readError);
        frames = std::min(frames, readLimit);
        auto* samples = static_cast<float*>(buffer);
        std::fill_n(samples, frames * getChannelCount(), 0.0f);
        const auto start = mFramesRead.load();
        for (int i = 0; i < frames; ++i)
            samples[i * getChannelCount()] = pulse(start + i, pulseFrame);
        mFramesRead += frames;
        return ResultWithValue<int32_t>(frames);
    }
    Result timestampError = Result::OK, readError = Result::OK;
    int64_t timestampFrame = 123;
    int readLimit = std::numeric_limits<int>::max();
    int64_t pulseFrame = -1;
    std::vector<float> rendered;
    void capture(int frames)
    {
        std::vector<float> input(frames * getChannelCount());
        mFramesWritten += frames;
        getDataCallback()->onAudioReady(this, input.data(), frames);
        mFramesRead += frames;
    }
    void render(int frames)
    {
        std::vector<float> output(frames * getChannelCount());
        getDataCallback()->onAudioReady(this, output.data(), frames);
        rendered.insert(rendered.end(), output.begin(), output.end());
        mFramesWritten += frames;
    }
};

class CallbackProbe : public AudioStreamDataCallback
{
public:
    DataCallbackResult onAudioReady(AudioStream* stream, void* data, int32_t frames) override
    {
        const auto target = stream->getDirection() == Direction::Output
                ? stream->getFramesWritten() : stream->getFramesRead();
        if (calls > 0 && target == previousTarget) ++repeatedTargets;
        if (target != delivered) ++wrongTargets;
        previousTarget = target;
        const auto start = delivered;
        delivered += frames;
        ++calls;
        std::fill_n(static_cast<float*>(data), frames * stream->getChannelCount(), 0.0f);
        for (int i = 0; i < frames; ++i)
            static_cast<float*>(data)[i * stream->getChannelCount()] = pulse(start + i, pulseFrame);
        return DataCallbackResult::Continue;
    }
    int64_t pulseFrame = -1;
    int calls = 0, wrongTargets = 0, repeatedTargets = 0;
    int64_t delivered = 0, previousTarget = -1;
};

AudioStreamBuilder settings(Direction direction, int rate,
        SampleRateConversionQuality quality = SampleRateConversionQuality::Medium)
{
    AudioStreamBuilder builder;
    builder.setDirection(direction)->setSampleRate(rate)->setChannelCount(1)
           ->setFormat(AudioFormat::Float)->setFramesPerDataCallback(96)
           ->setSampleRateConversionQuality(quality);
    return builder;
}


double energyCentre(const std::vector<float>& data)
{
    double sum = 0.0, weighted = 0.0;
    for (size_t i = 0; i < data.size(); ++i) {
        const auto energy = static_cast<double>(data[i]) * data[i];
        sum += energy;
        weighted += i * energy;
    }
    return weighted / sum;
}

bool timestampResponse(int appRate, int childRate, SampleRateConversionQuality quality)
{
    CallbackProbe probe;
    probe.pulseFrame = 256;
    auto app = settings(Direction::Output, appRate, quality);
    app.setDataCallback(&probe);
    auto native = settings(Direction::Output, childRate, quality);
    native.setDataCallback(&probe);
    auto child = std::make_shared<ChildStream>(native);
    FilterAudioStream output(app, child);
    if (output.configureFlowGraph() != Result::OK) return false;
    for (int i = 0; i < 20; ++i) child->render(96);
    int64_t position = 0, time = 0;
    if (output.getTimestamp(CLOCK_MONOTONIC, &position, &time) != Result::OK) return false;
    const auto outputPredicted = time / 1e9 + (256.0 - position) / appRate;
    const auto outputMeasured = 1.0 + energyCentre(child->rendered) / childRate;
    const auto outputErrorFrames = (outputPredicted - outputMeasured) * childRate;

    auto inputChild = std::make_shared<ChildStream>(settings(Direction::Input, childRate, quality));
    inputChild->pulseFrame = 256;
    FilterAudioStream input(settings(Direction::Input, appRate, quality), inputChild);
    if (input.configureFlowGraph() != Result::OK) return false;
    std::vector<float> captured(1920);
    auto read = input.read(captured.data(), static_cast<int>(captured.size()), 0);
    if (!read || read.value() != static_cast<int>(captured.size())) return false;
    if (input.getTimestamp(CLOCK_MONOTONIC, &position, &time) != Result::OK) return false;
    const auto inputPredicted = time / 1e9 + (energyCentre(captured) - position) / appRate;
    const auto inputMeasured = 1.0 + 256.0 / childRate;
    const auto inputErrorFrames = (inputPredicted - inputMeasured) * childRate;
    std::cout << "pulse app=" << appRate << " child=" << childRate
              << " quality=" << static_cast<int>(quality)
              << " output_error_native_frames=" << outputErrorFrames
              << " input_error_native_frames=" << inputErrorFrames << '\n';
    // Fractional-rate pulse energy is sampled on the destination grid.
    return std::abs(outputErrorFrames) < 0.02 && std::abs(inputErrorFrames) < 0.02;
}

// Acceptance test for P018's timestamp alignment (the builder does not edit it). The converted
// position must be the nearest application frame to the native position, n = floor(x + 1/2), and
// the time must move by (n - x) application frames, rounded to the nearest ns. x is computed here
// as an exact fraction from the child timestamp, the rates and the converter delay; the delay is
// half the resampler's taps in source frames (2, 4, 8, 16, 32 taps for Fastest to Best in
// MultiChannelResampler::make), the model whose physical timing timestampResponse() measures.
int64_t floorDiv(int64_t a, int64_t b)
{
    const auto q = a / b;
    return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
}

int64_t sourceDelay(int appRate, int childRate, SampleRateConversionQuality quality)
{
    if (appRate == childRate) return 0;
    switch (quality) {
        case SampleRateConversionQuality::Fastest: return 1;
        case SampleRateConversionQuality::Low:     return 2;
        case SampleRateConversionQuality::High:    return 8;
        case SampleRateConversionQuality::Best:    return 16;
        default:                                   return 4;
    }
}

int alignmentMismatches(Direction direction, int appRate, int childRate, SampleRateConversionQuality quality)
{
    auto child = std::make_shared<ChildStream>(settings(direction, childRate, quality));
    FilterAudioStream stream(settings(direction, appRate, quality), child);
    if (stream.configureFlowGraph() != Result::OK) return 1;
    const auto delay = sourceDelay(appRate, childRate, quality);
    int wrong = 0;
    // 2 * childRate / 100 consecutive native frames cover every residual of these rate pairs
    for (int64_t frame = 0; frame < 2 * childRate / 100; ++frame) {
        child->timestampFrame = frame;
        int64_t childPosition = 0, childTime = 0, position = 0, time = 0;
        child->getTimestamp(CLOCK_MONOTONIC, &childPosition, &childTime);
        if (stream.getTimestamp(CLOCK_MONOTONIC, &position, &time) != Result::OK) { ++wrong; continue; }
        // x = numerator / childRate application frames: delay is in application frames for output
        // (the converter's source) and in child frames for input
        const int64_t numerator = direction == Direction::Output
                ? frame * appRate - delay * childRate
                : (frame + delay) * appRate;
        const int64_t nearest = floorDiv(2 * numerator + childRate, 2 * static_cast<int64_t>(childRate));
        const int64_t residual = position * childRate - numerator;  // (n - x) * childRate
        const int64_t scale = static_cast<int64_t>(childRate) * appRate;
        const bool positionOk = position == nearest && 2 * residual <= childRate && 2 * residual > -childRate;
        const bool timeOk = positionOk && std::abs(time - childTime) <= 100000
                && std::abs(2 * (time - childTime) * scale - 2000000000LL * residual) <= scale;
        if (!timeOk) {
            if (wrong == 0)
                std::cerr << "alignment " << (direction == Direction::Output ? "output" : "input")
                          << " app=" << appRate << " child=" << childRate << " quality=" << static_cast<int>(quality)
                          << " frame=" << frame << " got position " << position << " time " << time - childTime
                          << " ns from the child's, expected position " << nearest << '\n';
            ++wrong;
        }
    }
    return wrong;
}

int main(int argc, char** argv)
{
    for (const auto rates : { std::pair{44100,48000}, std::pair{48000,44100}, std::pair{48000,96000},
                             std::pair{96000,48000}, std::pair{48000,48000} })
        for (auto direction : { Direction::Output, Direction::Input })
            for (auto quality : { SampleRateConversionQuality::Fastest, SampleRateConversionQuality::Low,
                                  SampleRateConversionQuality::Medium, SampleRateConversionQuality::High,
                                  SampleRateConversionQuality::Best }) {
                const auto wrong = alignmentMismatches(direction, rates.first, rates.second, quality);
                std::cout << "alignment " << (direction == Direction::Output ? "output" : "input")
                          << " app=" << rates.first << " child=" << rates.second
                          << " quality=" << static_cast<int>(quality) << " wrong=" << wrong << '\n';
                CHECK(wrong == 0);
            }

    const bool requireReadErrors = argc > 1 && std::string(argv[1]) == "--require-read-errors";
    bool errorsPropagated = true;
    for (const auto rates : { std::pair{96000,48000}, std::pair{48000,96000},
                             std::pair{44100,48000}, std::pair{48000,48000} })
    {
        CallbackProbe probe;
        auto app = settings(Direction::Output, rates.first);
        app.setDataCallback(&probe);
        auto childSettings = settings(Direction::Output, rates.second);
        childSettings.setDataCallback(&probe);
        auto child = std::make_shared<ChildStream>(childSettings);
        FilterAudioStream output(app, child);
        if (output.configureFlowGraph() != Result::OK) return 2;
        for (int i=0; i<100; ++i) child->render(96);
        const bool outputPass = probe.calls > 0 && probe.wrongTargets == 0;
        std::cout << "output app=" << rates.first << " child=" << rates.second
                  << " callbacks=" << probe.calls << " wrong_targets=" << probe.wrongTargets
                  << " repeated_targets=" << probe.repeatedTargets << '\n';
        CHECK(outputPass);

        CallbackProbe inputProbe;
        auto callbackInput = settings(Direction::Input, rates.first);
        callbackInput.setDataCallback(&inputProbe);
        auto callbackChildSettings = settings(Direction::Input, rates.second);
        callbackChildSettings.setDataCallback(&inputProbe);
        auto callbackChild = std::make_shared<ChildStream>(callbackChildSettings);
        FilterAudioStream callbackStream(callbackInput, callbackChild);
        if (callbackStream.configureFlowGraph() != Result::OK) return 2;
        for (int i = 0; i < 100; ++i) callbackChild->capture(96);
        CHECK(inputProbe.calls > 0 && inputProbe.wrongTargets == 0);
        std::cout << "input_callback app=" << rates.first << " child=" << rates.second
                  << " callbacks=" << inputProbe.calls << " wrong_targets=" << inputProbe.wrongTargets << '\n';

        auto inputApp = settings(Direction::Input, rates.first);
        auto inputChild = std::make_shared<ChildStream>(settings(Direction::Input, rates.second));
        FilterAudioStream input(inputApp, inputChild);
        if (input.configureFlowGraph() != Result::OK) return 2;
        std::vector<float> captured(96);
        int wrong = 0, repeated = 0;
        int64_t consumed = 0, previous = -1;
        for (int i=0; i<100; ++i)
        {
            const auto read = input.read(captured.data(), 96, 0);
            if (!read || read.value() <= 0) return 2;
            const auto target = input.getFramesRead() - read.value();
            if (target != consumed) ++wrong;
            if (i > 0 && target == previous) ++repeated;
            previous = target;
            consumed += read.value();
        }
        std::cout << "input app=" << rates.first << " child=" << rates.second
                  << " reads=100 wrong_targets=" << wrong << " repeated_targets=" << repeated << '\n';
        CHECK(wrong == 0);
        const auto outputBeforeStop = output.getFramesWritten();
        output.requestStop(); output.requestStart();
        for (int i = 0; i < 10; ++i) child->render(96);
        CHECK(output.getFramesWritten() >= outputBeforeStop && probe.wrongTargets == 0);
        int64_t unchangedPosition = 17, unchangedTime = 23;
        child->timestampError = Result::ErrorInvalidState;
        CHECK(output.getTimestamp(CLOCK_MONOTONIC, &unchangedPosition, &unchangedTime) == Result::ErrorInvalidState);
        CHECK(unchangedPosition == 17 && unchangedTime == 23);
        for (auto quality : { SampleRateConversionQuality::Fastest, SampleRateConversionQuality::Low,
                              SampleRateConversionQuality::Medium, SampleRateConversionQuality::High,
                              SampleRateConversionQuality::Best })
            CHECK(timestampResponse(rates.first, rates.second, quality));
        inputChild->readLimit = 7;
        for (int i = 0; i < 10; ++i) {
            const auto before = input.getFramesRead();
            auto part = input.read(captured.data(), 17, 0);
            CHECK(part && input.getFramesRead() == before + part.value());
        }
        const auto beforeStop = input.getFramesRead();
        input.requestStop(); input.requestStart();
        auto resumed = input.read(captured.data(), 31, 0);
        CHECK(resumed && input.getFramesRead() == beforeStop + resumed.value());
        auto empty = input.read(captured.data(), 0, 0);
        CHECK(empty && empty.value() == 0 && input.getFramesRead() == beforeStop + resumed.value());
        inputChild->readLimit = 0;
        bool observedEmpty = false;
        for (int i = 0; i < 32 && !observedEmpty; ++i) {
            const auto before = input.getFramesRead();
            auto read = input.read(captured.data(), 96, 0);
            if (!read) { CHECK(false); break; }
            CHECK(input.getFramesRead() == before + read.value());
            observedEmpty = read.value() == 0;
        }
        CHECK(observedEmpty);
        const auto beforeError = input.getFramesRead();
        inputChild->readError = Result::ErrorDisconnected;
        auto failedRead = input.read(captured.data(), 96, 0);
        CHECK(input.getFramesRead() == beforeError);
        errorsPropagated &= !failedRead && failedRead.error() == Result::ErrorDisconnected;
        std::cout << "native_read_error=-899 converted_result="
                  << (failedRead ? failedRead.value() : static_cast<int>(failedRead.error()))
                  << " counter_delta=" << input.getFramesRead() - beforeError << '\n';
    }
    std::cout << "checks=" << checks << " failures=" << failures << '\n';
    std::cout << "converter_regression=" << (failures == 0 ? "PASS" : "FAIL") << '\n';
    std::cout << "read_error_contract=" << (errorsPropagated ? "PASS" : "FAIL") << '\n';
    return failures == 0 && (!requireReadErrors || errorsPropagated) ? 0 : 1;
}
