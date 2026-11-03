/* Android regression tests for LynCo patch P016.
   Run with the supplied runner, with and without RECORD_AUDIO permission; see
   extras/OboeLifecycleTests/README.md. This file is included by the backend. */

namespace juce
{
class OboeLifecycleTests final : public UnitTest
{
public:
    OboeLifecycleTests() : UnitTest ("Oboe lifecycle", "Android audio hardware") {}

    void runTest() override
    {
        if (! MessageManager::existsAndIsCurrentThread())
        {
            checkInputNotifications();
            return;
        }

        checkInputBuffers<float>();
        checkInputBuffers<int16>();

        beginTest ("Hardware prerequisites");
        expect (MessageManager::getInstance()->isThisTheMessageThread());
        if (! RuntimePermissions::isGranted (RuntimePermissions::recordAudio))
        {
            beginTest ("Denied microphone permission creates no session");
            auto device = makeDevice();
            BigInteger inputs, outputs;
            inputs.setBit (0);
            outputs.setRange (0, 2, true);
            const auto error = device->open (inputs, outputs, 48000, 1920);
            expect (error.contains ("android.permission.RECORD_AUDIO"));
            expect (! device->isOpen());
            expect (! device->isPlaying());
            expect (device->session == nullptr);
            expectEquals (device->getLastError(), error);
            inputs.clear();
            expect (device->open (inputs, outputs, 48000, 1920).isEmpty());
            expect (device->isOpen());
            device->close();
            return;
        }

        beginTest ("Playback stays true inside an audio callback");
        {
            auto device = makeDevice();
            Callback client;
            WaitableEvent processed;
            std::atomic<bool> playingInCallback { false };
            expect (! device->isPlaying());
            if (open (*device))
            {
                client.processing = [&]
                {
                    playingInCallback = device->isPlaying();
                    processed.signal();
                };
                device->start (&client);
                expect (processed.wait (3000), "No audio callback arrived");
                expect (playingInCallback.load());
                device->stop();
                expect (! device->isPlaying());
                device->close();
            }
        }

        beginTest ("Open-state query does not wait for lifecycle operations inside an audio callback");
        {
            auto device = makeDevice();
            Callback client;
            WaitableEvent queried;
            std::atomic<bool> requestQuery { false }, openInCallback { false };
            if (open (*device))
            {
                client.processing = [&]
                {
                    if (requestQuery.exchange (false))
                    {
                        openInCallback = device->isOpen();
                        queried.signal();
                    }
                };
                device->start (&client);
                bool completedUnderLock;
                {
                    // stop() holds this lock while waiting for the audio callback.
                    const ScopedLock lock (device->control->mutex);
                    requestQuery = true;
                    completedUnderLock = queried.wait (3000);
                }
                expect (completedUnderLock, "isOpen() blocked behind the lifecycle lock");
                if (! completedUnderLock)
                    queried.wait (3000);
                expect (openInCallback.load());
                device->stop();
                expect (device->isOpen());
                device->close();
                expect (! device->isOpen());
            }
        }

        beginTest ("Input and output scratch cover callback capacity; oversized blocks are silent");
        {
            auto device = makeDevice();
            if (open (*device))
            {
                auto* session = dynamic_cast<OboeAudioIODevice::OboeSessionImpl<float>*> (device->session.get());
                expect (session != nullptr, "Float stream required for scratch capacity check");
                if (session != nullptr)
                {
                    expect (session->inputStreamSampleBuffer.getNumSamples() >= session->getBufferCapacityInFrames (false));
                    expect (session->inputStreamSampleBuffer.getNumSamples() >= session->getBufferCapacityInFrames (true));
                    expect (session->inputStreamSampleBuffer.getNumSamples() >= 1920);
                    const auto frames = session->bufferCapacity + 1;
                    std::vector<float> output ((size_t) (frames * 2 + 1), 0.5f);
                    expect (session->onAudioReady (session->outputStream->getNativeStream().get(), output.data(), frames)
                            == oboe::DataCallbackResult::Continue);
                    expect (std::all_of (output.begin(), output.end() - 1, [] (float value) { return value == 0.0f; }));
                    expectEquals (output.back(), 0.5f);
                }
                device->close();
            }
        }

        for (const auto action : { Action::none, Action::stop, Action::close, Action::destroy })
        {
            beginTest ("Disconnected recovery: " + name (action));
            auto device = makeDevice();
            Callback client;
            if (! open (*device))
                continue;

            device->start (&client);
            expect (device->getLastError().isEmpty(), device->getLastError());
            client.stopped = [&] { act (action, device); };
            const auto failure = device->handleStreamError (oboe::Result::ErrorDisconnected);
            expect (failure.first == nullptr, failure.second);
            expectEquals (client.starts, action == Action::none ? 2 : 1);
            expectEquals (client.stops, 1);
            expectEquals (client.errors, 0);
            if (device != nullptr)
            {
                expect (device->isPlaying() == (action == Action::none));
                if (action == Action::close)
                    expect (! device->isOpen());
                device->close();
            }
            else
                expect (action == Action::destroy);
        }

        for (const auto duringRecovery : { true, false })
        {
            beginTest (duringRecovery ? "Recovery preserves client replacement" : "Failed start preserves client replacement");
            auto device = makeDevice();
            Callback client, replacement;
            if (! open (*device))
                continue;

            if (duringRecovery)
                device->start (&client);
            else
                device->session->outputStream->getNativeStream()->close();

            client.stopped = [&]
            {
                if (open (*device))
                    device->start (&replacement);
            };
            if (duringRecovery)
            {
                const auto failure = device->handleStreamError (oboe::Result::ErrorDisconnected);
                expect (failure.first == nullptr, failure.second);
            }
            else
                device->start (&client);

            expectEquals (client.starts, 1);
            expectEquals (client.stops, 1);
            expectEquals (client.errors, 0);
            expectEquals (replacement.starts, 1);
            expectEquals (replacement.stops, 0);
            device->session->stop();
            expect (device->callback.get() == &replacement);
            expect (device->isOpen() && device->isPlaying());
            expect (device->getLastError().isEmpty());
            device->close();
        }

        for (const auto action : { Action::stop, Action::close, Action::destroy })
        {
            beginTest ("Preparation cancellation: " + name (action));
            auto device = makeDevice();
            Callback client;
            if (! open (*device))
                continue;

            client.preparing = [&] { act (action, device); };
            device->start (&client);
            expectEquals (client.starts, 1);
            expectEquals (client.errors, 0);
            if (device != nullptr)
            {
                expect (! device->isPlaying());
                device->close();
            }
        }

        for (const auto input : { true, false })
            for (const auto action : { Action::none, Action::close, Action::destroy })
            {
                beginTest (String (input ? "Input" : "Output") + " start failure: " + name (action));
                auto device = makeDevice();
                Callback client;
                if (! open (*device))
                    continue;

                // A real closed native stream returns ErrorClosed from start().
                auto& stream = input ? device->session->inputStream : device->session->outputStream;
                stream->getNativeStream()->close();
                client.stopped = [&] { act (action, device); };
                device->start (&client);
                expectEquals (client.starts, 1);
                expectEquals (client.stops, 1);
                expectEquals (client.errors, action == Action::none ? 1 : 0);
                if (device != nullptr)
                {
                    expect (! device->isOpen());
                    expect (! device->isPlaying());
                    expect (device->getLastError().isNotEmpty());
                    expect (device->session->inputStream == nullptr);
                    expect (device->session->outputStream == nullptr);
                    device->close();
                }
            }
    }

private:
    static void onMessageThread (const std::function<void()>& fn)
    {
        MessageManager::getInstance()->callFunctionOnMessageThread ([] (void* context) -> void*
        {
            (*static_cast<const std::function<void()>*> (context))();
            return nullptr;
        }, const_cast<std::function<void()>*> (&fn));
    }

    void checkInputNotifications()
    {
        bool permitted = false;
        onMessageThread ([&] { permitted = RuntimePermissions::isGranted (RuntimePermissions::recordAudio); });
        if (! permitted)
            return;

        for (const auto error : { oboe::Result::ErrorInvalidState, oboe::Result::ErrorTimeout,
                                 oboe::Result::ErrorDisconnected })
            for (const auto cancel : { false, true })
            {
                beginTest ("Input read notification: " + getOboeString (error) + (cancel ? " cancelled" : " delivered"));
                std::unique_ptr<OboeAudioIODevice> device;
                Callback client;
                WaitableEvent reported;
                std::atomic<bool> injected { false };
                onMessageThread ([&]
                {
                    device = makeDevice();
                    if (! open (*device)) return;
                    auto* session = dynamic_cast<OboeAudioIODevice::OboeSessionImpl<float>*> (device->session.get());
                    expect (session != nullptr);
                    if (session == nullptr) return;
                    client.processing = [&, session]
                    {
                        if (! injected.exchange (true))
                        {
                            session->inputErrorDispatcher->report (error);
                            session->inputErrorDispatcher->report (error);
                            reported.signal();
                        }
                    };
                    device->start (&client);
                    // Keep the message thread inside this phase until cancellation.
                    expect (reported.wait (3000), "No audio callback arrived");
                    if (cancel) device.reset();
                });
                Thread::sleep (500); // Let the real Android message loop deliver queued recovery.
                onMessageThread ([&]
                {
                    const auto recovered = error == oboe::Result::ErrorDisconnected && ! cancel;
                    expectEquals (client.starts, recovered ? 2 : 1);
                    expectEquals (client.stops, recovered ? 1 : 0);
                    expectEquals (client.errors, 0);
                    if (device != nullptr) device->close();
                    device.reset();
                });
            }

        beginTest ("Input disconnection followed by failed reopen reports one terminal error");
        std::unique_ptr<OboeAudioIODevice> device;
        Callback client;
        WaitableEvent failed;
        std::atomic<bool> injected { false };
        onMessageThread ([&]
        {
            device = makeDevice();
            if (! open (*device)) return;
            auto* session = dynamic_cast<OboeAudioIODevice::OboeSessionImpl<float>*> (device->session.get());
            expect (session != nullptr);
            if (session == nullptr) return;
            client.processing = [&, session]
            {
                if (! injected.exchange (true))
                    session->inputErrorDispatcher->report (oboe::Result::ErrorDisconnected);
            };
            client.preparing = [&]
            {
                if (client.starts == 2)
                    device->session->inputStream->getNativeStream()->close();
            };
            client.failed = [&] { failed.signal(); };
            device->start (&client);
        });
        expect (failed.wait (3000), "No terminal input error arrived");
        onMessageThread ([&]
        {
            expectEquals (client.starts, 2);
            expectEquals (client.stops, 2);
            expectEquals (client.errors, 1);
            expect (! device->isPlaying());
            expect (! device->isOpen());
            device->close();
            device.reset();
        });
    }

    template <typename SampleType>
    void checkInputBuffers()
    {
        constexpr int frames = 8;
        const auto sample = [] (int channel) -> SampleType
        {
            if constexpr (std::is_same_v<SampleType, float>)
                return 0.25f * (float) (channel + 1);
            else
                return (SampleType) (8192 * (channel + 1));
        };

        for (const auto channels : { 1, 2 })
        {
            beginTest ("Input buffer: " + getOboeString (OboeAudioIODeviceBufferHelpers<SampleType>::oboeAudioFormat())
                       + " channels=" + String (channels));
            std::vector<SampleType> native ((size_t) (frames * channels), sample (0));
            AudioBuffer<float> buffer (channels, frames * 2);
            for (const auto received : { frames, 3, 0, -1, frames })
            {
                // Poison the unread tail; only the returned frames contain new samples.
                std::fill (native.begin(), native.end(), sample (1));
                for (int frame = 0; frame < jmax (0, received); ++frame)
                    for (int channel = 0; channel < channels; ++channel)
                        native[(size_t) (frame * channels + channel)] = sample (channel);

                const auto result = received < 0 ? oboe::ResultWithValue<int32_t> (oboe::Result::ErrorDisconnected)
                                                 : oboe::ResultWithValue<int32_t> (received);
                expectEquals (OboeAudioIODevice::prepareInputBuffer (native.data(), buffer, frames, result), jmax (0, received));
                expect (buffer.getNumSamples() >= frames);
                if constexpr (std::is_same_v<SampleType, float>)
                    if (channels == 1)
                    {
                        expectEquals (buffer.getNumSamples(), frames);
                        expect (buffer.getReadPointer (0) == native.data());
                    }

                for (int frame = 0; frame < frames; ++frame)
                    for (int channel = 0; channel < channels; ++channel)
                        expectWithinAbsoluteError (buffer.getSample (channel, frame),
                                                   frame < received ? 0.25f * (float) (channel + 1) : 0.0f, 0.0001f);
            }
        }
    }

    enum class Action { none, stop, close, destroy };

    static String name (Action action)
    {
        switch (action)
        {
            case Action::none:    return "continue";
            case Action::stop:    return "stop";
            case Action::close:   return "close";
            case Action::destroy: return "destroy";
        }
        return {};
    }

    static void act (Action action, std::unique_ptr<OboeAudioIODevice>& device)
    {
        switch (action)
        {
            case Action::none:    break;
            case Action::stop:    device->stop(); break;
            case Action::close:   device->close(); break;
            case Action::destroy: device.reset(); break;
        }
    }

    static std::unique_ptr<OboeAudioIODevice> makeDevice()
    {
        return std::make_unique<OboeAudioIODevice> ("P016 regression", oboe::kUnspecified,
                                                   Array<int> { 48000 }, 1, oboe::kUnspecified,
                                                   Array<int> { 48000 }, 2);
    }

    bool open (OboeAudioIODevice& device)
    {
        BigInteger inputs, outputs;
        inputs.setBit (0);
        outputs.setRange (0, 2, true);
        const auto error = device.open (inputs, outputs, 48000, 1920);
        expect (error.isEmpty(), error);
        return error.isEmpty();
    }

    struct Callback final : AudioIODeviceCallback
    {
        void audioDeviceAboutToStart (AudioIODevice*) override
        {
            ++starts;
            if (preparing) preparing();
        }
        void audioDeviceStopped() override
        {
            ++stops;
            if (stopped) stopped();
        }
        void audioDeviceError (const String&) override { ++errors; if (failed) failed(); }
        void audioDeviceIOCallbackWithContext (const float* const*, int, float* const* output,
                                               int channels, int frames,
                                               const AudioIODeviceCallbackContext&) override
        {
            if (processing) processing();
            for (int i = 0; i < channels; ++i)
                FloatVectorOperations::clear (output[i], frames);
        }
        std::function<void()> preparing, stopped, processing, failed;
        int starts = 0, stops = 0, errors = 0;
    };
};

static OboeLifecycleTests oboeLifecycleTests;
}
