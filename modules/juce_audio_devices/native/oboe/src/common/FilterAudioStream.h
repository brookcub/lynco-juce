/*
 * Copyright 2019 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef OBOE_FILTER_AUDIO_STREAM_H
#define OBOE_FILTER_AUDIO_STREAM_H

#include <memory>
#include <cmath>
#include <oboe/AudioStream.h>
#include "DataConversionFlowGraph.h"

namespace oboe {

/**
 * An AudioStream that wraps another AudioStream and provides audio data conversion.
 * Operations may include channel conversion, data format conversion and/or sample rate conversion.
 */
class FilterAudioStream : public AudioStream, AudioStreamCallback {
public:

    /**
     * Construct an `AudioStream` using the given `AudioStreamBuilder` and a child AudioStream.
     *
     * This should only be called internally by AudioStreamBuilder.
     * Ownership of childStream will be passed to this object.
     *
     * @param builder containing all the stream's attributes
     */
    FilterAudioStream(const AudioStreamBuilder &builder, std::shared_ptr<AudioStream> childStream)
    : AudioStream(builder)
     , mChildStream(childStream) {
        // Intercept the callback if used.
        if (builder.isErrorCallbackSpecified()) {
            mErrorCallback = mChildStream->swapErrorCallback(this);
        }
        if (builder.isDataCallbackSpecified()) {
            mAppDataCallback = mChildStream->swapDataCallback(this);
            mDataCallback = this;
        } else {
            const int size = childStream->getFramesPerBurst() * childStream->getBytesPerFrame();
            mBlockingBuffer = std::make_unique<uint8_t[]>(size);
        }

        // Copy parameters that may not match builder.
        mBufferCapacityInFrames = mChildStream->getBufferCapacityInFrames();
        mPerformanceMode = mChildStream->getPerformanceMode();
        mSharingMode = mChildStream->getSharingMode();
        mInputPreset = mChildStream->getInputPreset();
        mFramesPerBurst = mChildStream->getFramesPerBurst();
        mDeviceId = mChildStream->getDeviceId();
        mHardwareSampleRate = mChildStream->getHardwareSampleRate();
        mHardwareChannelCount = mChildStream->getHardwareChannelCount();
        mHardwareFormat = mChildStream->getHardwareFormat();
    }

    virtual ~FilterAudioStream() = default;

    Result configureFlowGraph();

    // Close child and parent.
    Result close()  override {
        const Result result1 = mChildStream->close();
        const Result result2 = AudioStream::close();
        return (result1 != Result::OK ? result1 : result2);
    }

    /**
     * Start the stream asynchronously. Returns immediately (does not block). Equivalent to calling
     * `start(0)`.
     */
    Result requestStart() override {
        return mChildStream->requestStart();
    }

    /**
     * Pause the stream asynchronously. Returns immediately (does not block). Equivalent to calling
     * `pause(0)`.
     */
    Result requestPause() override {
        return mChildStream->requestPause();
    }

    /**
     * Flush the stream asynchronously. Returns immediately (does not block). Equivalent to calling
     * `flush(0)`.
     */
    Result requestFlush() override {
        return mChildStream->requestFlush();
    }

    /**
     * Stop the stream asynchronously. Returns immediately (does not block). Equivalent to calling
     * `stop(0)`.
     */
    Result requestStop() override {
        return mChildStream->requestStop();
    }

    ResultWithValue<int32_t> read(void *buffer,
            int32_t numFrames,
            int64_t timeoutNanoseconds) override;

    ResultWithValue<int32_t> write(const void *buffer,
            int32_t numFrames,
            int64_t timeoutNanoseconds) override;

    StreamState getState() override {
        return mChildStream->getState();
    }

    Result waitForStateChange(
            StreamState inputState,
            StreamState *nextState,
            int64_t timeoutNanoseconds) override {
        return mChildStream->waitForStateChange(inputState, nextState, timeoutNanoseconds);
    }

    bool isXRunCountSupported() const override {
        return mChildStream->isXRunCountSupported();
    }

    AudioApi getAudioApi() const override {
        return mChildStream->getAudioApi();
    }

    void updateFramesWritten() override {
        // Callback output advances when the application supplies a converted block.
        if (getDirection() != Direction::Output || !isDataCallbackSpecified()) {
            mFramesWritten = static_cast<int64_t>(mChildStream->getFramesWritten() * mRateScaler);
        }
    }

    void updateFramesRead() override {
        // Input advances when converted frames reach the application, including buffered reads.
        if (getDirection() != Direction::Input) {
            mFramesRead = static_cast<int64_t>(mChildStream->getFramesRead() * mRateScaler);
        }
    }

    void *getUnderlyingStream() const  override {
        return mChildStream->getUnderlyingStream();
    }

    ResultWithValue<int32_t> setBufferSizeInFrames(int32_t requestedFrames) override {
        return mChildStream->setBufferSizeInFrames(requestedFrames);
    }

    int32_t getBufferSizeInFrames() override {
        mBufferSizeInFrames = mChildStream->getBufferSizeInFrames();
        return mBufferSizeInFrames;
    }

    ResultWithValue<int32_t> getXRunCount() override {
        return mChildStream->getXRunCount();
    }

    ResultWithValue<double> calculateLatencyMillis() override {
        // This will automatically include the latency of the flowgraph?
        return mChildStream->calculateLatencyMillis();
    }

    Result getTimestamp(clockid_t clockId,
            int64_t *framePosition,
            int64_t *timeNanoseconds) override {
        int64_t childPosition = 0;
        Result result = mChildStream->getTimestamp(clockId, &childPosition, timeNanoseconds);
        if (result != Result::OK) return result;
        // The converted position x is placed on the nearest application frame, n = floor (x + 1/2)
        // (a half-frame residual rounds up), and the time moves by (n - x) / rate so that the
        // frame and time still describe one point of the stream. This is the one alignment, and
        // the same rule as CallbackTimeHelpers::alignToApplicationSample in juce_audio_devices,
        // which bundled Oboe cannot include. With a whole-frame delay x is an exact fraction
        // N / child, so the integer arithmetic below is exact; a fractional delay falls back to
        // doubles.
        const int64_t app = getSampleRate();
        const int64_t child = mChildStream->getSampleRate();
        const double delay = mFlowGraph->getSourceDelayInFrames();
        const auto wholeDelay = static_cast<int64_t>(std::floor(delay));
        const double fractionalDelay = delay - static_cast<double>(wholeDelay);
        const bool isOutput = getDirection() == Direction::Output;
        const int64_t numerator = isOutput ? childPosition * app - wholeDelay * child
                                           : (childPosition + wholeDelay) * app;
        const double extra = isOutput ? -fractionalDelay
                                      : fractionalDelay * static_cast<double>(app) / static_cast<double>(child);
        const auto floorDiv = [](int64_t a, int64_t b) {
            const int64_t q = a / b;
            return (a % b != 0 && (a < 0) != (b < 0)) ? q - 1 : q;
        };
        int64_t wholePosition;
        if (extra == 0.0) {
            wholePosition = floorDiv(2 * numerator + child, 2 * child);
            const int64_t residual = wholePosition * child - numerator; // (n - x) * child
            const int64_t num = residual * kNanosPerSecond;
            const int64_t den = child * app;
            *timeNanoseconds += floorDiv(2 * num + den, 2 * den);
        } else {
            const double position = static_cast<double>(numerator) / static_cast<double>(child) + extra;
            wholePosition = static_cast<int64_t>(std::floor(position + 0.5));
            *timeNanoseconds += static_cast<int64_t>(std::llround(
                    (static_cast<double>(wholePosition) - position) * kNanosPerSecond / app));
        }
        if (framePosition) *framePosition = wholePosition;
        return result;
    }

    DataCallbackResult onAudioReady(AudioStream *oboeStream,
            void *audioData,
            int32_t numFrames) override;

    bool onError(AudioStream * /*audioStream*/, Result error) override {
        if (mErrorCallback != nullptr) {
            return mErrorCallback->onError(this, error);
        }
        return false;
    }

    void onErrorBeforeClose(AudioStream * /*oboeStream*/, Result error) override {
        if (mErrorCallback != nullptr) {
            mErrorCallback->onErrorBeforeClose(this, error);
        }
    }

    void onErrorAfterClose(AudioStream * /*oboeStream*/, Result error) override {
        // Close this parent stream because the callback will only close the child.
        AudioStream::close();
        if (mErrorCallback != nullptr) {
            mErrorCallback->onErrorAfterClose(this, error);
        }
    }

    /**
     * @return last result passed from an error callback
     */
    oboe::Result getLastErrorCallbackResult() const override {
        return mChildStream->getLastErrorCallbackResult();
    }

private:

    AudioStreamDataCallback                *mAppDataCallback = nullptr;
    std::shared_ptr<AudioStream>             mChildStream; // this stream wraps the child stream
    std::unique_ptr<DataConversionFlowGraph> mFlowGraph; // for converting data
    std::unique_ptr<uint8_t[]>               mBlockingBuffer; // temp buffer for write()
    double                                   mRateScaler = 1.0; // ratio parent/child sample rates
};

} // oboe

#endif //OBOE_FILTER_AUDIO_STREAM_H
