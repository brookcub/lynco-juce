#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <thread>

class OboeTestApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "Oboe lifecycle tests"; }
    const juce::String getApplicationVersion() override { return "1"; }
    void initialise (const juce::String&) override
    {
        juce::MessageManager::callAsync ([this]
        {
            juce::UnitTestRunner runner;
            runner.setAssertOnFailure (false);
            runner.runTestsInCategory ("Android audio hardware");
            int passes = 0, failures = 0;
            for (int i = 0; i < runner.getNumResults(); ++i)
            {
                passes += runner.getResult (i)->passes;
                failures += runner.getResult (i)->failures;
            }
            dispatchTests = std::thread ([this, passes, failures]() mutable
            {
                juce::UnitTestRunner dispatchRunner;
                dispatchRunner.setAssertOnFailure (false);
                dispatchRunner.runTestsInCategory ("Android audio hardware");
                for (int i = 0; i < dispatchRunner.getNumResults(); ++i)
                {
                    passes += dispatchRunner.getResult (i)->passes;
                    failures += dispatchRunner.getResult (i)->failures;
                }
                juce::Logger::outputDebugString ("OBOE TESTS: passes=" + juce::String (passes)
                                            + " failures=" + juce::String (failures));
                juce::MessageManager::callAsync ([this, passes, failures]
                {
                    setApplicationReturnValue (failures != 0 || passes == 0 ? 1 : 0);
                    quit();
                });
            });
        });
    }
    // The worker needs the message loop until the suite has completed.
    void systemRequestedQuit() override {}
    void shutdown() override { if (dispatchTests.joinable()) dispatchTests.join(); }
private:
    std::thread dispatchTests;
};

START_JUCE_APPLICATION (OboeTestApplication)
