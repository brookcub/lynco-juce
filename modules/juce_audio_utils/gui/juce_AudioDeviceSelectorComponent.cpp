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

namespace juce
{

struct SimpleDeviceManagerInputLevelMeter final : public Component,
                                                  public Timer
{
    SimpleDeviceManagerInputLevelMeter (AudioDeviceManager& m)  : manager (m)
    {
        startTimerHz (20);
        inputLevelGetter = manager.getInputLevelGetter();
    }

    void timerCallback() override
    {
        if (isShowing())
        {
            auto newLevel = (float) inputLevelGetter->getCurrentLevel();

            if (std::abs (level - newLevel) > 0.005f)
            {
                level = newLevel;
                repaint();
            }
        }
        else
        {
            level = 0;
        }
    }

    void paint (Graphics& g) override
    {
        // (add a bit of a skew to make the level more obvious)
        getLookAndFeel().drawLevelMeter (g, getWidth(), getHeight(),
                                         (float) std::exp (std::log (level) / 3.0));
    }

    AudioDeviceManager& manager;
    AudioDeviceManager::LevelMeter::Ptr inputLevelGetter;
    float level = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SimpleDeviceManagerInputLevelMeter)
};

static void drawTextLayout (Graphics& g, Component& owner, StringRef text, const Rectangle<int>& textBounds, bool enabled)
{
    const auto textColour = owner.findColour (ListBox::textColourId, true).withMultipliedAlpha (enabled ? 1.0f : 0.6f);

    AttributedString attributedString { text };
    attributedString.setColour (textColour);
    attributedString.setFont (owner.withDefaultMetrics (FontOptions { (float) textBounds.getHeight() * 0.6f }));
    attributedString.setJustification (Justification::centredLeft);
    attributedString.setWordWrap (AttributedString::WordWrap::none);

    TextLayout textLayout;
    textLayout.createLayout (attributedString,
                             (float) textBounds.getWidth(),
                             (float) textBounds.getHeight());
    textLayout.draw (g, textBounds.toFloat());
}


//==============================================================================
class AudioDeviceSelectorComponent::MidiInputSelectorComponentListBox final : public ListBox,
                                                                              private ListBoxModel
{
public:
    MidiInputSelectorComponentListBox (AudioDeviceManager& dm, const String& noItems)
        : ListBox ({}, nullptr),
          deviceManager (dm),
          noItemsMessage (noItems)
    {
        updateDevices();
        setModel (this);
        setOutlineThickness (1);
    }

    void updateDevices()
    {
        items = MidiInput::getAvailableDevices();
    }

    int getNumRows() override
    {
        return items.size();
    }

    void paintListBoxItem (int row, Graphics& g, int width, int height, bool rowIsSelected) override
    {
        if (isPositiveAndBelow (row, items.size()))
        {
            if (rowIsSelected)
                g.fillAll (findColour (TextEditor::highlightColourId)
                               .withMultipliedAlpha (0.3f));

            auto item = items[row];
            bool enabled = deviceManager.isMidiInputDeviceEnabled (item.identifier);

            auto x = getTickX();
            auto tickW = (float) height * 0.75f;

            getLookAndFeel().drawTickBox (g, *this, (float) x - tickW, ((float) height - tickW) * 0.5f, tickW, tickW,
                                          enabled, true, true, false);

            drawTextLayout (g, *this, item.name, { x + 5, 0, width - x - 5, height }, enabled);
        }
    }

    void listBoxItemClicked (int row, const MouseEvent& e) override
    {
        selectRow (row);

        if (e.x < getTickX())
            flipEnablement (row);
    }

    void listBoxItemDoubleClicked (int row, const MouseEvent&) override
    {
        flipEnablement (row);
    }

    void returnKeyPressed (int row) override
    {
        flipEnablement (row);
    }

    void paint (Graphics& g) override
    {
        ListBox::paint (g);

        if (items.isEmpty())
        {
            g.setColour (Colours::grey);
            g.setFont (0.5f * (float) getRowHeight());
            g.drawText (noItemsMessage,
                        0, 0, getWidth(), getHeight() / 2,
                        Justification::centred, true);
        }
    }

    int getBestHeight (int preferredHeight)
    {
        auto extra = getOutlineThickness() * 2;

        return jmax (getRowHeight() * 2 + extra,
                     jmin (getRowHeight() * getNumRows() + extra,
                           preferredHeight));
    }

private:
    //==============================================================================
    AudioDeviceManager& deviceManager;
    const String noItemsMessage;
    Array<MidiDeviceInfo> items;

    void flipEnablement (const int row)
    {
        if (isPositiveAndBelow (row, items.size()))
        {
            auto identifier = items[row].identifier;
            deviceManager.setMidiInputDeviceEnabled (identifier, ! deviceManager.isMidiInputDeviceEnabled (identifier));
        }
    }

    int getTickX() const
    {
        return getRowHeight();
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiInputSelectorComponentListBox)
};


//==============================================================================
struct AudioDeviceSetupDetails
{
    AudioDeviceManager* manager;
    int minNumInputChannels, maxNumInputChannels;
    int minNumOutputChannels, maxNumOutputChannels;
    bool useStereoPairs;
};

static String getNoDeviceString()   { return "<< " + TRANS ("none") + " >>"; }

//==============================================================================
class AudioDeviceSelectorComponent::MidiOutputSelector final : public Component,
                                                               private ChangeListener
{
public:
    explicit MidiOutputSelector (AudioDeviceManager& dm)
        : deviceManager (dm)
    {
        deviceManager.addChangeListener (this);
        selector.onChange = [&]
        {
            const auto selectedId = selector.getSelectedId();
            jassert (selectedId != 0);

            const auto deviceId = selectedId == -1
                                ? String{}
                                : MidiOutput::getAvailableDevices()[selectedId - 1].identifier;
            deviceManager.setDefaultMidiOutputDevice (deviceId);
        };

        updateListOfDevices();
        addAndMakeVisible (selector);
    }

    ~MidiOutputSelector() final
    {
        deviceManager.removeChangeListener (this);
    }

    void resized() final { selector.setBounds (getLocalBounds()); }

private:
    void updateListOfDevices()
    {
        selector.clear();

        const auto midiOutputs = MidiOutput::getAvailableDevices();

        selector.addItem (getNoDeviceString(), -1);
        selector.setSelectedId (-1, dontSendNotification);
        selector.addSeparator();

        for (auto [id, midiOutput] : enumerate (midiOutputs, 1))
        {
            selector.addItem (midiOutput.name, id);

            if (midiOutput.identifier == deviceManager.getDefaultMidiOutputIdentifier())
                selector.setSelectedId (id, dontSendNotification);
        }
    }

    void changeListenerCallback (ChangeBroadcaster*) final { updateListOfDevices(); }

    ComboBox selector;
    AudioDeviceManager& deviceManager;
};

//==============================================================================
class AudioDeviceSettingsPanel : public Component,
                                 private ChangeListener
{
public:
    AudioDeviceSettingsPanel (AudioIODeviceType& t, AudioDeviceSetupDetails& setupDetails,
                              const bool hideAdvancedOptionsWithButton,
                              AudioDeviceSelectorComponent& p)
        : type (t), setup (setupDetails), parent (p)
    {
        if (hideAdvancedOptionsWithButton)
        {
            showAdvancedSettingsButton = std::make_unique <TextButton> (TRANS ("Show advanced settings..."));
            addAndMakeVisible (showAdvancedSettingsButton.get());
            showAdvancedSettingsButton->setClickingTogglesState (true);
            showAdvancedSettingsButton->onClick = [this] { toggleAdvancedSettings(); };
        }

        type.scanForDevices();

        setup.manager->addChangeListener (this);

        updateAllControls();
    }

    ~AudioDeviceSettingsPanel() override
    {
        setup.manager->removeChangeListener (this);
    }

    void resized() override
    {
        Rectangle<int> r (proportionOfWidth (0.35f), 0, proportionOfWidth (0.6f), 3000);

        const int maxListBoxHeight = 100;
        const int h = parent.getItemHeight();
        const int space = h / 4;

        if (outputDeviceDropDown != nullptr)
        {
            auto row = r.removeFromTop (h);

            if (testButton != nullptr)
            {
                testButton->changeWidthToFitText (h);
                testButton->setBounds (row.removeFromRight (testButton->getWidth()));
                row.removeFromRight (space);
            }

            outputDeviceDropDown->setBounds (row);
            r.removeFromTop (space);
        }

        if (inputDeviceDropDown != nullptr)
        {
            auto row = r.removeFromTop (h);

            inputLevelMeter->setBounds (row.removeFromRight (testButton != nullptr ? testButton->getWidth() : row.getWidth() / 6));
            row.removeFromRight (space);
            inputDeviceDropDown->setBounds (row);
            r.removeFromTop (space);
        }

        if (outputChanList != nullptr)
        {
            outputChanList->setRowHeight (jmin (22, h));
            outputChanList->setBounds (r.removeFromTop (outputChanList->getBestHeight (maxListBoxHeight)));
            outputChanLabel->setBounds (0, outputChanList->getBounds().getCentreY() - h / 2, r.getX(), h);
            r.removeFromTop (space);
        }

        if (inputChanList != nullptr)
        {
            inputChanList->setRowHeight (jmin (22, h));
            inputChanList->setBounds (r.removeFromTop (inputChanList->getBestHeight (maxListBoxHeight)));
            inputChanLabel->setBounds (0, inputChanList->getBounds().getCentreY() - h / 2, r.getX(), h);
            r.removeFromTop (space);
        }

        r.removeFromTop (space * 2);

        if (showAdvancedSettingsButton != nullptr
            && sampleRateDropDown != nullptr && bufferSizeDropDown != nullptr)
        {
            showAdvancedSettingsButton->setBounds (r.removeFromTop (h));
            r.removeFromTop (space);
            showAdvancedSettingsButton->changeWidthToFitText();
        }

        auto advancedSettingsVisible = showAdvancedSettingsButton == nullptr
                                          || showAdvancedSettingsButton->getToggleState();

        if (sampleRateDropDown != nullptr)
        {
            sampleRateDropDown->setVisible (advancedSettingsVisible);

            if (advancedSettingsVisible)
            {
                sampleRateDropDown->setBounds (r.removeFromTop (h));
                r.removeFromTop (space);
            }
        }

        if (bufferSizeDropDown != nullptr)
        {
            bufferSizeDropDown->setVisible (advancedSettingsVisible);

            if (advancedSettingsVisible)
            {
                bufferSizeDropDown->setBounds (r.removeFromTop (h));
                r.removeFromTop (space);
            }
        }

        r.removeFromTop (space);

        if (showUIButton != nullptr || resetDeviceButton != nullptr)
        {
            auto buttons = r.removeFromTop (h);

            if (showUIButton != nullptr)
            {
                showUIButton->setVisible (advancedSettingsVisible);
                showUIButton->changeWidthToFitText (h);
                showUIButton->setBounds (buttons.removeFromLeft (showUIButton->getWidth()));
                buttons.removeFromLeft (space);
            }

            if (resetDeviceButton != nullptr)
            {
                resetDeviceButton->setVisible (advancedSettingsVisible);
                resetDeviceButton->changeWidthToFitText (h);
                resetDeviceButton->setBounds (buttons.removeFromLeft (resetDeviceButton->getWidth()));
            }

            r.removeFromTop (space);
        }

        setSize (getWidth(), r.getY());
    }

    void updateConfig (bool updateOutputDevice, bool updateInputDevice, bool updateSampleRate, bool updateBufferSize)
    {
        const auto chosenName = [] (const std::unique_ptr<ComboBox>& box) -> std::optional<String>
        {
            if (box == nullptr)
                return {};

            return box->getSelectedId() < 0 ? String() : box->getText();
        };

        const auto updateDevice = updateOutputDevice || updateInputDevice;
        const auto outputName = chosenName (outputDeviceDropDown);
        const auto inputName = chosenName (inputDeviceDropDown);
        const auto separateInputsAndOutputs = type.hasSeparateInputsAndOutputs();
        const auto sampleRate = sampleRateDropDown != nullptr ? sampleRateDropDown->getSelectedId() : 0;
        const auto bufferSize = bufferSizeDropDown != nullptr ? bufferSizeDropDown->getSelectedId() : 0;

        if (! updateDevice && (updateSampleRate ? sampleRate : bufferSize) <= 0)
            return;

        // The controls are read now; the setup they change is read when the change runs.
        parent.submitDeviceChange ([=, manager = setup.manager]
        {
            auto config = manager->getAudioDeviceSetup();

            if (updateDevice)
            {
                if (outputName.has_value())
                    config.outputDeviceName = *outputName;

                if (inputName.has_value())
                    config.inputDeviceName = *inputName;

                if (! separateInputsAndOutputs)
                    config.inputDeviceName = config.outputDeviceName;

                if (updateInputDevice)
                    config.useDefaultInputChannels = true;
                else
                    config.useDefaultOutputChannels = true;
            }
            else if (updateSampleRate)
            {
                config.sampleRate = sampleRate;
            }
            else if (updateBufferSize)
            {
                config.bufferSize = bufferSize;
            }

            return manager->setAudioDeviceSetup (config, true);
        });
    }

    bool showDeviceControlPanel()
    {
        if (auto* device = setup.manager->getCurrentAudioDevice())
        {
            Component modalWindow;
            modalWindow.setOpaque (true);
            modalWindow.addToDesktop (0);
            modalWindow.enterModalState();

            return device->showControlPanel();
        }

        return false;
    }

    void toggleAdvancedSettings()
    {
        showAdvancedSettingsButton->setButtonText ((showAdvancedSettingsButton->getToggleState() ? "Hide " : "Show ")
                                                   + String ("advanced settings..."));
        resized();
    }

    void showDeviceUIPanel()
    {
        if (showDeviceControlPanel())
        {
            resetDevice();
            getTopLevelComponent()->toFront (true);
        }
    }

    void playTestSound()
    {
        setup.manager->playTestSound();
    }

    void updateAllControls()
    {
        updateOutputsComboBox();
        updateInputsComboBox();

        updateControlPanelButton();
        updateResetButton();

        if (auto* currentDevice = setup.manager->getCurrentAudioDevice())
        {
            if (setup.maxNumOutputChannels > 0
                 && setup.minNumOutputChannels < setup.manager->getCurrentAudioDevice()->getOutputChannelNames().size())
            {
                if (outputChanList == nullptr)
                {
                    outputChanList = std::make_unique<ChannelSelectorListBox> (parent, setup, ChannelSelectorListBox::audioOutputType,
                                                                               TRANS ("(no audio output channels found)"));
                    addAndMakeVisible (outputChanList.get());
                    outputChanLabel = std::make_unique<Label> (String{}, TRANS ("Active output channels:"));
                    outputChanLabel->setJustificationType (Justification::centredRight);
                    outputChanLabel->attachToComponent (outputChanList.get(), true);
                }

                outputChanList->refresh();
            }
            else
            {
                outputChanLabel.reset();
                outputChanList.reset();
            }

            if (setup.maxNumInputChannels > 0
                 && setup.minNumInputChannels < setup.manager->getCurrentAudioDevice()->getInputChannelNames().size())
            {
                if (inputChanList == nullptr)
                {
                    inputChanList = std::make_unique<ChannelSelectorListBox> (parent, setup, ChannelSelectorListBox::audioInputType,
                                                                              TRANS ("(no audio input channels found)"));
                    addAndMakeVisible (inputChanList.get());
                    inputChanLabel = std::make_unique<Label> (String{}, TRANS ("Active input channels:"));
                    inputChanLabel->setJustificationType (Justification::centredRight);
                    inputChanLabel->attachToComponent (inputChanList.get(), true);
                }

                inputChanList->refresh();
            }
            else
            {
                inputChanLabel.reset();
                inputChanList.reset();
            }

            updateSampleRateComboBox (currentDevice);
            updateBufferSizeComboBox (currentDevice);
        }
        else
        {
            jassert (setup.manager->getCurrentAudioDevice() == nullptr); // not the correct device type!

            inputChanLabel.reset();
            outputChanLabel.reset();
            sampleRateLabel.reset();
            bufferSizeLabel.reset();

            inputChanList.reset();
            outputChanList.reset();
            sampleRateDropDown.reset();
            bufferSizeDropDown.reset();

            if (outputDeviceDropDown != nullptr)
                outputDeviceDropDown->setSelectedId (-1, dontSendNotification);

            if (inputDeviceDropDown != nullptr)
                inputDeviceDropDown->setSelectedId (-1, dontSendNotification);
        }

        sendLookAndFeelChange();
        resized();
        setSize (getWidth(), getLowestY() + 4);
    }

    void changeListenerCallback (ChangeBroadcaster*) override
    {
        if (! parent.deviceChangeRunning)
            updateAllControls();
    }

    void resetDevice()
    {
        parent.submitDeviceChange ([manager = setup.manager]
        {
            manager->closeAudioDevice();
            manager->restartLastAudioDevice();
            return String();
        });
    }

private:
    AudioIODeviceType& type;
    const AudioDeviceSetupDetails setup;
    AudioDeviceSelectorComponent& parent;

    std::unique_ptr<ComboBox> outputDeviceDropDown, inputDeviceDropDown, sampleRateDropDown, bufferSizeDropDown;
    std::unique_ptr<Label> outputDeviceLabel, inputDeviceLabel, sampleRateLabel, bufferSizeLabel, inputChanLabel, outputChanLabel;
    std::unique_ptr<TextButton> testButton;
    std::unique_ptr<Component> inputLevelMeter;
    std::unique_ptr<TextButton> showUIButton, showAdvancedSettingsButton, resetDeviceButton;

    int findSelectedDeviceIndex (bool isInput) const
    {
        const auto device = setup.manager->getAudioDeviceSetup();
        const auto deviceName = isInput ? device.inputDeviceName : device.outputDeviceName;
        return type.getDeviceNames (isInput).indexOf (deviceName);
    }

    void updateSelectedInput()
    {
        showCorrectDeviceName (inputDeviceDropDown.get(), true);
    }

    void updateSelectedOutput()
    {
        constexpr auto isInput = false;
        showCorrectDeviceName (outputDeviceDropDown.get(), isInput);

        if (testButton != nullptr)
            testButton->setEnabled (findSelectedDeviceIndex (isInput) >= 0);
    }

    void showCorrectDeviceName (ComboBox* box, bool isInput)
    {
        if (box == nullptr)
            return;

        const auto index = findSelectedDeviceIndex (isInput);
        box->setSelectedId (index < 0 ? index : index + 1, dontSendNotification);
    }

    void addNamesToDeviceBox (ComboBox& combo, bool isInputs)
    {
        const StringArray devs (type.getDeviceNames (isInputs));

        combo.clear (dontSendNotification);

        for (int i = 0; i < devs.size(); ++i)
            combo.addItem (devs[i], i + 1);

        combo.addItem (getNoDeviceString(), -1);
        combo.setSelectedId (-1, dontSendNotification);
    }

    int getLowestY() const
    {
        int y = 0;

        for (auto* c : getChildren())
            y = jmax (y, c->getBottom());

        return y;
    }

    void updateControlPanelButton()
    {
        auto* currentDevice = setup.manager->getCurrentAudioDevice();
        showUIButton.reset();

        if (currentDevice != nullptr && currentDevice->hasControlPanel())
        {
            showUIButton = std::make_unique<TextButton> (TRANS ("Control Panel"),
                                                         TRANS ("Opens the device's own control panel"));
            addAndMakeVisible (showUIButton.get());
            showUIButton->onClick = [this] { showDeviceUIPanel(); };
        }

        resized();
    }

    void updateResetButton()
    {
        if (auto* currentDevice = setup.manager->getCurrentAudioDevice())
        {
            if (currentDevice->hasControlPanel())
            {
                if (resetDeviceButton == nullptr)
                {
                    resetDeviceButton = std::make_unique<TextButton> (TRANS ("Reset Device"),
                                                                      TRANS ("Resets the audio interface - sometimes needed after changing a device's properties in its custom control panel"));
                    addAndMakeVisible (resetDeviceButton.get());
                    resetDeviceButton->onClick = [this] { resetDevice(); };
                    resized();
                }

                return;
            }
        }

        resetDeviceButton.reset();
    }

    void updateOutputsComboBox()
    {
        if (setup.maxNumOutputChannels > 0 || ! type.hasSeparateInputsAndOutputs())
        {
            if (outputDeviceDropDown == nullptr)
            {
                outputDeviceDropDown = std::make_unique<ComboBox>();
                outputDeviceDropDown->onChange = [this] { updateConfig (true, false, false, false); };

                addAndMakeVisible (outputDeviceDropDown.get());

                outputDeviceLabel = std::make_unique<Label> (String{}, type.hasSeparateInputsAndOutputs() ? TRANS ("Output:")
                                                                                                          : TRANS ("Device:"));
                outputDeviceLabel->attachToComponent (outputDeviceDropDown.get(), true);

                if (setup.maxNumOutputChannels > 0)
                {
                    testButton = std::make_unique<TextButton> (TRANS ("Test"), TRANS ("Plays a test tone"));
                    addAndMakeVisible (testButton.get());
                    testButton->onClick = [this] { playTestSound(); };
                }
            }

            addNamesToDeviceBox (*outputDeviceDropDown, false);
        }

        updateSelectedOutput();
    }

    void updateInputsComboBox()
    {
        if (setup.maxNumInputChannels > 0 && type.hasSeparateInputsAndOutputs())
        {
            if (inputDeviceDropDown == nullptr)
            {
                inputDeviceDropDown = std::make_unique<ComboBox>();
                inputDeviceDropDown->onChange = [this] { updateConfig (false, true, false, false); };
                addAndMakeVisible (inputDeviceDropDown.get());

                inputDeviceLabel = std::make_unique<Label> (String{}, TRANS ("Input:"));
                inputDeviceLabel->attachToComponent (inputDeviceDropDown.get(), true);

                inputLevelMeter = std::make_unique<SimpleDeviceManagerInputLevelMeter> (*setup.manager);
                addAndMakeVisible (inputLevelMeter.get());
            }

            addNamesToDeviceBox (*inputDeviceDropDown, true);
        }

        updateSelectedInput();
    }

    void updateSampleRateComboBox (AudioIODevice* currentDevice)
    {
        if (sampleRateDropDown == nullptr)
        {
            sampleRateDropDown = std::make_unique<ComboBox>();
            addAndMakeVisible (sampleRateDropDown.get());

            sampleRateLabel = std::make_unique<Label> (String{}, TRANS ("Sample rate:"));
            sampleRateLabel->attachToComponent (sampleRateDropDown.get(), true);
        }
        else
        {
            sampleRateDropDown->clear (dontSendNotification);
            sampleRateDropDown->onChange = nullptr;
        }

        const auto getFrequencyString = [] (int rate) { return String (rate) + " Hz"; };

        for (auto rate : currentDevice->getAvailableSampleRates())
        {
            const auto intRate = roundToInt (rate);
            sampleRateDropDown->addItem (getFrequencyString (intRate), intRate);
        }

        const auto intRate = roundToInt (currentDevice->getCurrentSampleRate());
        sampleRateDropDown->setText (getFrequencyString (intRate), dontSendNotification);

        sampleRateDropDown->onChange = [this] { updateConfig (false, false, true, false); };
    }

    void updateBufferSizeComboBox (AudioIODevice* currentDevice)
    {
        if (bufferSizeDropDown == nullptr)
        {
            bufferSizeDropDown = std::make_unique<ComboBox>();
            addAndMakeVisible (bufferSizeDropDown.get());

            bufferSizeLabel = std::make_unique<Label> (String{}, TRANS ("Audio buffer size:"));
            bufferSizeLabel->attachToComponent (bufferSizeDropDown.get(), true);
        }
        else
        {
            bufferSizeDropDown->clear (dontSendNotification);
            bufferSizeDropDown->onChange = nullptr;
        }

        auto currentRate = currentDevice->getCurrentSampleRate();

        if (exactlyEqual (currentRate, 0.0))
            currentRate = 48000.0;

        for (auto bs : currentDevice->getAvailableBufferSizes())
            bufferSizeDropDown->addItem (getBufferSizeText (bs, currentRate), bs);

        const auto bufferSizeSamples = currentDevice->getCurrentBufferSizeSamples();
        bufferSizeDropDown->setText (getBufferSizeText (bufferSizeSamples, currentRate), dontSendNotification);

        bufferSizeDropDown->onChange = [this] { updateConfig (false, false, false, true); };
    }

    String getBufferSizeText (int bs, double currentRate) const
    {
        return String (bs) + " samples (" + String (bs * 1000.0 / currentRate, 1) + " ms)";
    }

public:
    //==============================================================================
    class ChannelSelectorListBox final : public ListBox,
                                         private ListBoxModel
    {
    public:
        enum BoxType
        {
            audioInputType,
            audioOutputType
        };

        //==============================================================================
        ChannelSelectorListBox (AudioDeviceSelectorComponent& selector, const AudioDeviceSetupDetails& setupDetails,
                                BoxType boxType, const String& noItemsText)
           : ListBox ({}, nullptr), owner (selector), setup (setupDetails), type (boxType), noItemsMessage (noItemsText)
        {
            refresh();
            setModel (this);
            setOutlineThickness (1);
        }

        void refresh()
        {
            items.clear();
            activeChannels.clear();

            if (auto* currentDevice = setup.manager->getCurrentAudioDevice())
            {
                const auto config = setup.manager->getAudioDeviceSetup();
                activeChannels = type == audioInputType ? config.inputChannels : config.outputChannels;

                if (type == audioInputType)
                    items = currentDevice->getInputChannelNames();
                else if (type == audioOutputType)
                    items = currentDevice->getOutputChannelNames();

                if (setup.useStereoPairs)
                {
                    StringArray pairs;

                    for (int i = 0; i < items.size(); i += 2)
                    {
                        auto& name = items[i];

                        if (i + 1 >= items.size())
                            pairs.add (name.trim());
                        else
                            pairs.add (getNameForChannelPair (name, items[i + 1]));
                    }

                    items = pairs;
                }
            }

            updateContent();
            repaint();
        }

        int getNumRows() override
        {
            return items.size();
        }

        void paintListBoxItem (int row, Graphics& g, int width, int height, bool) override
        {
            if (isPositiveAndBelow (row, items.size()))
            {
                g.fillAll (findColour (ListBox::backgroundColourId));

                auto item = items[row];
                const auto enabled = setup.useStereoPairs ? activeChannels[row * 2] || activeChannels[row * 2 + 1]
                                                          : activeChannels[row];

                auto x = getTickX();
                auto tickW = (float) height * 0.75f;

                getLookAndFeel().drawTickBox (g, *this, (float) x - tickW, ((float) height - tickW) * 0.5f, tickW, tickW,
                                              enabled, true, true, false);

                drawTextLayout (g, *this, item, { x + 5, 0, width - x - 5, height }, enabled);
            }
        }

        void listBoxItemClicked (int row, const MouseEvent& e) override
        {
            selectRow (row);

            if (e.x < getTickX())
                flipEnablement (row);
        }

        void listBoxItemDoubleClicked (int row, const MouseEvent&) override
        {
            flipEnablement (row);
        }

        void returnKeyPressed (int row) override
        {
            flipEnablement (row);
        }

        void paint (Graphics& g) override
        {
            ListBox::paint (g);

            if (items.isEmpty())
            {
                g.setColour (Colours::grey);
                g.setFont (0.5f * (float) getRowHeight());
                g.drawText (noItemsMessage,
                            0, 0, getWidth(), getHeight() / 2,
                            Justification::centred, true);
            }
        }

        int getBestHeight (int maxHeight)
        {
            return getRowHeight() * jlimit (2, jmax (2, maxHeight / getRowHeight()),
                                            getNumRows())
                       + getOutlineThickness() * 2;
        }

    private:
        //==============================================================================
        AudioDeviceSelectorComponent& owner;
        const AudioDeviceSetupDetails setup;
        const BoxType type;
        const String noItemsMessage;
        StringArray items;
        BigInteger activeChannels;

        static String getNameForChannelPair (const String& name1, const String& name2)
        {
            String commonBit;

            for (int j = 0; j < name1.length(); ++j)
                if (name1.substring (0, j).equalsIgnoreCase (name2.substring (0, j)))
                    commonBit = name1.substring (0, j);

            // Make sure we only split the name at a space, because otherwise, things
            // like "input 11" + "input 12" would become "input 11 + 2"
            while (commonBit.isNotEmpty() && ! CharacterFunctions::isWhitespace (commonBit.getLastCharacter()))
                commonBit = commonBit.dropLastCharacters (1);

            return name1.trim() + " + " + name2.substring (commonBit.length()).trim();
        }

        void flipEnablement (int row)
        {
            jassert (type == audioInputType || type == audioOutputType);

            if (! isPositiveAndBelow (row, items.size()))
                return;

            owner.submitDeviceChange ([details = setup, boxType = type, row]
            {
                auto config = details.manager->getAudioDeviceSetup();

                if (details.useStereoPairs)
                {
                    BigInteger bits;
                    auto& original = (boxType == audioInputType ? config.inputChannels
                                                             : config.outputChannels);

                    for (int i = 0; i < 256; i += 2)
                        bits.setBit (i / 2, original[i] || original[i + 1]);

                    if (boxType == audioInputType)
                    {
                        config.useDefaultInputChannels = false;
                        flipBit (bits, row, details.minNumInputChannels / 2, details.maxNumInputChannels / 2);
                    }
                    else
                    {
                        config.useDefaultOutputChannels = false;
                        flipBit (bits, row, details.minNumOutputChannels / 2, details.maxNumOutputChannels / 2);
                    }

                    for (int i = 0; i < 256; ++i)
                        original.setBit (i, bits[i / 2]);
                }
                else
                {
                    if (boxType == audioInputType)
                    {
                        config.useDefaultInputChannels = false;
                        flipBit (config.inputChannels, row, details.minNumInputChannels, details.maxNumInputChannels);
                    }
                    else
                    {
                        config.useDefaultOutputChannels = false;
                        flipBit (config.outputChannels, row, details.minNumOutputChannels, details.maxNumOutputChannels);
                    }
                }

                return details.manager->setAudioDeviceSetup (config, true);
            });
        }

        static void flipBit (BigInteger& chans, int index, int minNumber, int maxNumber)
        {
            auto numActive = chans.countNumberOfSetBits();

            if (chans[index])
            {
                if (numActive > minNumber)
                    chans.setBit (index, false);
            }
            else
            {
                if (numActive >= maxNumber)
                {
                    auto firstActiveChan = chans.findNextSetBit (0);
                    chans.clearBit (index > firstActiveChan ? firstActiveChan : chans.getHighestBit());
                }

                chans.setBit (index, true);
            }
        }

        int getTickX() const
        {
            return getRowHeight();
        }

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChannelSelectorListBox)
    };

private:
    std::unique_ptr<ChannelSelectorListBox> inputChanList, outputChanList;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioDeviceSettingsPanel)
};


//==============================================================================
AudioDeviceSelectorComponent::AudioDeviceSelectorComponent (AudioDeviceManager& dm,
                                                            int minInputChannelsToUse,
                                                            int maxInputChannelsToUse,
                                                            int minOutputChannelsToUse,
                                                            int maxOutputChannelsToUse,
                                                            bool showMidiInputOptions,
                                                            bool showMidiOutputSelector,
                                                            bool showChannelsAsStereoPairsToUse,
                                                            bool hideAdvancedOptionsWithButtonToUse)
    : deviceManager (dm),
      itemHeight (24),
      minOutputChannels (minOutputChannelsToUse),
      maxOutputChannels (maxOutputChannelsToUse),
      minInputChannels (minInputChannelsToUse),
      maxInputChannels (maxInputChannelsToUse),
      showChannelsAsStereoPairs (showChannelsAsStereoPairsToUse),
      hideAdvancedOptionsWithButton (hideAdvancedOptionsWithButtonToUse)
{
    jassert (minOutputChannels >= 0 && minOutputChannels <= maxOutputChannels);
    jassert (minInputChannels >= 0 && minInputChannels <= maxInputChannels);

    const OwnedArray<AudioIODeviceType>& types = deviceManager.getAvailableDeviceTypes();

    if (types.size() > 1)
    {
        deviceTypeDropDown = std::make_unique<ComboBox>();

        for (int i = 0; i < types.size(); ++i)
            deviceTypeDropDown->addItem (types.getUnchecked (i)->getTypeName(), i + 1);

        addAndMakeVisible (deviceTypeDropDown.get());
        deviceTypeDropDown->onChange = [this] { updateDeviceType(); };

        deviceTypeDropDownLabel = std::make_unique<Label> (String{}, TRANS ("Audio device type:"));
        deviceTypeDropDownLabel->setJustificationType (Justification::centredRight);
        deviceTypeDropDownLabel->attachToComponent (deviceTypeDropDown.get(), true);
    }

    if (showMidiInputOptions)
    {
        midiInputsList = std::make_unique <MidiInputSelectorComponentListBox> (deviceManager,
                                                                               "(" + TRANS ("No MIDI inputs available") + ")");
        addAndMakeVisible (midiInputsList.get());

        midiInputsLabel = std::make_unique<Label> (String{}, TRANS ("Active MIDI inputs:"));
        midiInputsLabel->setJustificationType (Justification::topRight);
        midiInputsLabel->attachToComponent (midiInputsList.get(), true);

        if (BluetoothMidiDevicePairingDialogue::isAvailable())
        {
            bluetoothButton = std::make_unique<TextButton> (TRANS ("Bluetooth MIDI"), TRANS ("Scan for bluetooth MIDI devices"));
            addAndMakeVisible (bluetoothButton.get());
            bluetoothButton->onClick = [this] { handleBluetoothButton(); };
        }
    }
    else
    {
        midiInputsList.reset();
        midiInputsLabel.reset();
        bluetoothButton.reset();
    }

    if (showMidiOutputSelector)
    {
        midiOutputSelector = std::make_unique<MidiOutputSelector> (deviceManager);
        addAndMakeVisible (midiOutputSelector.get());

        midiOutputLabel = std::make_unique<Label> ("lm", TRANS ("MIDI Output:"));
        midiOutputLabel->attachToComponent (midiOutputSelector.get(), true);
    }
    else
    {
        midiOutputSelector.reset();
        midiOutputLabel.reset();
    }

    deviceManager.addChangeListener (this);
    updateAllControls();
}

AudioDeviceSelectorComponent::~AudioDeviceSelectorComponent()
{
    deviceManager.removeChangeListener (this);
}

void AudioDeviceSelectorComponent::setItemHeight (int newItemHeight)
{
    itemHeight = newItemHeight;
    resized();
}

void AudioDeviceSelectorComponent::resized()
{
    Rectangle<int> r (proportionOfWidth (0.35f), 15, proportionOfWidth (0.6f), 3000);
    auto space = itemHeight / 4;

    if (deviceTypeDropDown != nullptr)
    {
        deviceTypeDropDown->setBounds (r.removeFromTop (itemHeight));
        r.removeFromTop (space * 3);
    }

    if (audioDeviceSettingsComp != nullptr)
    {
        audioDeviceSettingsComp->resized();
        audioDeviceSettingsComp->setBounds (r.removeFromTop (audioDeviceSettingsComp->getHeight())
                                                .withX (0).withWidth (getWidth()));
        r.removeFromTop (space);
    }

    if (midiInputsList != nullptr)
    {
        midiInputsList->setRowHeight (jmin (22, itemHeight));
        midiInputsList->setBounds (r.removeFromTop (midiInputsList->getBestHeight (jmin (itemHeight * 8,
                                                                                         getHeight() - r.getY() - space - itemHeight))));
        r.removeFromTop (space);
    }

    if (bluetoothButton != nullptr)
    {
        bluetoothButton->setBounds (r.removeFromTop (24));
        r.removeFromTop (space);
    }

    if (midiOutputSelector != nullptr)
        midiOutputSelector->setBounds (r.removeFromTop (itemHeight));

    r.removeFromTop (itemHeight);
    setSize (getWidth(), r.getY());
}

void AudioDeviceSelectorComponent::childBoundsChanged (Component* child)
{
    if (child == audioDeviceSettingsComp.get())
        resized();
}

void AudioDeviceSelectorComponent::updateDeviceType()
{
    if (auto* type = deviceManager.getAvailableDeviceTypes() [deviceTypeDropDown->getSelectedId() - 1])
    {
        audioDeviceSettingsComp.reset();

        submitDeviceChange ([manager = &deviceManager, name = type->getTypeName()]
        {
            manager->setCurrentAudioDeviceType (name, true);
            return String();
        });
    }
}

void AudioDeviceSelectorComponent::changeListenerCallback (ChangeBroadcaster*)
{
    if (! deviceChangeRunning)
        updateAllControls();
}

void AudioDeviceSelectorComponent::submitDeviceChange (std::function<String()> change)
{
    if (deviceChangeRunning)
        return;

    if (runDeviceChange == nullptr)
    {
        finishDeviceChange (change());
        return;
    }

    deviceChangeRunning = true;
    setEnabled (false);

    runDeviceChange (std::move (change), [safeThis = SafePointer<AudioDeviceSelectorComponent> (this)] (const String& error)
    {
        if (safeThis != nullptr)
            safeThis->finishDeviceChange (error);
    });
}

void AudioDeviceSelectorComponent::finishDeviceChange (const String& error)
{
    JUCE_ASSERT_MESSAGE_THREAD

    // Posted, because this can run inside the onChange of a control that the refresh rebuilds.
    MessageManager::callAsync ([safeThis = SafePointer<AudioDeviceSelectorComponent> (this)]
    {
        if (safeThis == nullptr)
            return;

        safeThis->updateAllControls();

        if (auto* panel = dynamic_cast<AudioDeviceSettingsPanel*> (safeThis->audioDeviceSettingsComp.get()))
            panel->updateAllControls();

        if (std::exchange (safeThis->deviceChangeRunning, false))
            safeThis->setEnabled (true);
    });

    if (error.isNotEmpty())
        messageBox = AlertWindow::showScopedAsync (MessageBoxOptions().withIconType (MessageBoxIconType::WarningIcon)
                                                                      .withTitle (TRANS ("Error when trying to open audio device!"))
                                                                      .withMessage (error)
                                                                      .withButton (TRANS ("OK")),
                                                   nullptr);
}

void AudioDeviceSelectorComponent::updateAllControls()
{
    if (deviceTypeDropDown != nullptr)
        deviceTypeDropDown->setText (deviceManager.getCurrentAudioDeviceType(), dontSendNotification);

    if (audioDeviceSettingsComp == nullptr
         || audioDeviceSettingsCompType != deviceManager.getCurrentAudioDeviceType())
    {
        audioDeviceSettingsCompType = deviceManager.getCurrentAudioDeviceType();
        audioDeviceSettingsComp.reset();

        if (auto* type = deviceManager.getAvailableDeviceTypes() [deviceTypeDropDown == nullptr
                                                                   ? 0 : deviceTypeDropDown->getSelectedId() - 1])
        {
            AudioDeviceSetupDetails details;
            details.manager = &deviceManager;
            details.minNumInputChannels = minInputChannels;
            details.maxNumInputChannels = maxInputChannels;
            details.minNumOutputChannels = minOutputChannels;
            details.maxNumOutputChannels = maxOutputChannels;
            details.useStereoPairs = showChannelsAsStereoPairs;

            audioDeviceSettingsComp = std::make_unique<AudioDeviceSettingsPanel> (*type, details, hideAdvancedOptionsWithButton, *this);
            addAndMakeVisible (audioDeviceSettingsComp.get());
        }
    }

    if (midiInputsList != nullptr)
    {
        midiInputsList->updateDevices();
        midiInputsList->updateContent();
        midiInputsList->repaint();
    }

    resized();
}

void AudioDeviceSelectorComponent::handleBluetoothButton()
{
    if (RuntimePermissions::isGranted (RuntimePermissions::bluetoothMidi))
    {
        BluetoothMidiDevicePairingDialogue::open();
    }
    else
    {
        RuntimePermissions::request (RuntimePermissions::bluetoothMidi, [] (auto)
        {
            if (RuntimePermissions::isGranted (RuntimePermissions::bluetoothMidi))
                BluetoothMidiDevicePairingDialogue::open();
        });
    }
}

ListBox* AudioDeviceSelectorComponent::getMidiInputSelectorListBox() const noexcept
{
    return midiInputsList.get();
}


//==============================================================================
#if JUCE_UNIT_TESTS && (JUCE_WINDOWS || JUCE_LINUX)

namespace detail
{
bool dispatchNextMessageOnSystemQueue (bool returnIfNoPendingMessages);
} // namespace detail

class AudioDeviceSelectorComponentTests final : public UnitTest
{
public:
    AudioDeviceSelectorComponentTests() : UnitTest ("AudioDeviceSelectorComponent", UnitTestCategories::audio) {}

    void runTest() override
    {
        Fakes fakes;
        AudioDeviceManager manager;
        manager.addAudioDeviceType (std::make_unique<FakeType> (fakes, "Fake", StringArray { "A", "B", "Broken" }));
        manager.addAudioDeviceType (std::make_unique<FakeType> (fakes, "Other", StringArray { "C", "D" }));
        const auto initialiseError = manager.initialise (2, 2, nullptr, false);

        EventRecorder events;
        manager.addAudioCallback (&events);

        auto selector = std::make_unique<AudioDeviceSelectorComponent> (manager, 0, 2, 0, 2, false, false, false, false);
        selector->setSize (500, 600);

        std::thread worker;
        String lastError;
        bool changeFinished = false;

        selector->runDeviceChange = [&] (std::function<String()> change, std::function<void (const String&)> done)
        {
            if (worker.joinable())
                worker.join();

            fakes.changeRunning = true;
            changeFinished = false;

            worker = std::thread ([&, change = std::move (change), done = std::move (done)]
            {
                auto error = change();

                MessageManager::callAsync ([&, error, done]
                {
                    fakes.changeRunning = false;
                    changeFinished = true;
                    lastError = error;
                    done ({}); // the error isn't passed on, so that no alert window opens during the test
                });
            });
        };

        const auto finishChange = [&]
        {
            expect (pumpUntil ([&] { return changeFinished; }, 5000), "the change finished");
            pumpFor (50); // the selector's refresh is posted
        };

        beginTest ("Switching device keeps the message thread running and the selector off the manager");
        {
            expect (initialiseError.isEmpty());
            pumpFor (50);
            events.clear();
            fakes.blockOpens = true;

            choose (*selector, "Output:", "B");

            expect (pumpUntil ([&] { return fakes.openStarted.load(); }, 2000), "the open started");
            expect (! selector->isEnabled());

            bool dispatched = false;
            MessageManager::callAsync ([&] { dispatched = true; });
            expect (pumpUntil ([&] { return dispatched; }, 1000), "messages are dispatched while the open is blocked");

            manager.sendChangeMessage();
            pumpFor (50);
            selector->createComponentSnapshot (selector->getLocalBounds());

            expect (! selector->isEnabled());
            expectEquals (fakes.messageThreadQueries.load(), 0);

            fakes.blockOpens = false;
            fakes.openGate.signal();
            finishChange();

            expect (selector->isEnabled());
            expectEquals (fakes.messageThreadQueries.load(), 0);
            expectEquals (manager.getAudioDeviceSetup().outputDeviceName, String ("B"));
            expectEquals (findControl<ComboBox> (*selector, "Output:")->getText(), String ("B"));
            expectEquals (events.get().joinIntoString (","), String ("stopped,aboutToStart"));
        }

        beginTest ("Channel choices survive an unrelated change");
        {
            auto* inputs = findControl<ListBox> (*selector, "Active input channels:");
            expect (inputs != nullptr);
            inputs->selectRow (0);
            inputs->keyPressed (KeyPress (KeyPress::returnKey));
            finishChange();

            const auto onlySecond = BigInteger (2);
            expect (manager.getAudioDeviceSetup().inputChannels == onlySecond);

            const auto newRate = manager.getAudioDeviceSetup().sampleRate == 48000.0 ? 44100 : 48000;
            findControl<ComboBox> (*selector, "Sample rate:")->setSelectedId (newRate, sendNotificationSync);
            finishChange();

            expectEquals (manager.getAudioDeviceSetup().sampleRate, (double) newRate);
            expect (manager.getAudioDeviceSetup().inputChannels == onlySecond);
            expectEquals (findControl<ComboBox> (*selector, "Sample rate:")->getText(), String (newRate) + " Hz");
            expectEquals (fakes.messageThreadQueries.load(), 0);
        }

        beginTest ("A failed open is reported and can be retried");
        {
            choose (*selector, "Output:", "Broken");
            finishChange();

            expectEquals (lastError, String ("Broken can't open"));
            expect (selector->isEnabled());
            expect (manager.getCurrentAudioDevice() == nullptr);
            expectEquals (findControl<ComboBox> (*selector, "Output:")->getSelectedId(), -1);

            choose (*selector, "Output:", "A");
            finishChange();

            expect (lastError.isEmpty());
            expect (manager.getCurrentAudioDevice() != nullptr);
            expectEquals (findControl<ComboBox> (*selector, "Output:")->getText(), String ("A"));
        }

        beginTest ("Selecting none closes the device");
        {
            for (auto* label : { "Input:", "Output:" })
            {
                auto* box = findControl<ComboBox> (*selector, label);

                if (box->getSelectedId() != -1)
                {
                    box->setSelectedId (-1, sendNotificationSync);
                    finishChange();
                }
            }

            expect (lastError.isEmpty());
            expect (manager.getCurrentAudioDevice() == nullptr);
            expect (selector->isEnabled());
        }

        beginTest ("Changing the device type opens that type's device");
        {
            choose (*selector, "Audio device type:", "Other");
            finishChange();

            expectEquals (manager.getCurrentAudioDeviceType(), String ("Other"));
            expectEquals (manager.getAudioDeviceSetup().outputDeviceName, String ("C"));
            expectEquals (findControl<ComboBox> (*selector, "Output:")->getText(), String ("C"));
            expectEquals (fakes.messageThreadQueries.load(), 0);
        }

        beginTest ("Deleting the selector during a change is safe");
        {
            fakes.blockOpens = true;
            fakes.openStarted = false;

            choose (*selector, "Output:", "D");
            expect (pumpUntil ([&] { return fakes.openStarted.load(); }, 2000), "the open started");

            selector.reset();
            fakes.blockOpens = false;
            fakes.openGate.signal();
            expect (pumpUntil ([&] { return changeFinished; }, 5000), "the change finished");

            expectEquals (manager.getAudioDeviceSetup().outputDeviceName, String ("D"));
        }

        worker.join();
        manager.removeAudioCallback (&events);
    }

private:
    struct Fakes
    {
        // Counts calls into the fake devices made on the message thread while a change is running.
        void noteQuery() { if (changeRunning && MessageManager::existsAndIsCurrentThread()) ++messageThreadQueries; }

        std::atomic<bool> changeRunning { false }, blockOpens { false }, openStarted { false };
        std::atomic<int> messageThreadQueries { 0 };
        WaitableEvent openGate;
    };

    struct FakeDevice final : public AudioIODevice
    {
        FakeDevice (Fakes& f, const String& deviceName, const String& typeName)
            : AudioIODevice (deviceName, typeName), fakes (f) {}

        StringArray getOutputChannelNames() override        { fakes.noteQuery(); return { "o1", "o2" }; }
        StringArray getInputChannelNames() override         { fakes.noteQuery(); return { "i1", "i2" }; }
        Array<double> getAvailableSampleRates() override    { fakes.noteQuery(); return { 44100.0, 48000.0 }; }
        Array<int> getAvailableBufferSizes() override       { fakes.noteQuery(); return { 128, 256 }; }
        int getDefaultBufferSize() override                 { fakes.noteQuery(); return 128; }

        String open (const BigInteger& ins, const BigInteger& outs, double rate, int size) override
        {
            if (fakes.blockOpens)
            {
                fakes.openStarted = true;
                fakes.openGate.wait();
            }

            if (getName() == "Broken")
                return "Broken can't open";

            inputs = ins;
            outputs = outs;
            sampleRate = rate;
            bufferSize = size;
            isDeviceOpen = true;
            return {};
        }

        void close() override                               { isDeviceOpen = false; }
        bool isOpen() override                              { return isDeviceOpen; }

        void start (AudioIODeviceCallback* newCallback) override
        {
            callback = newCallback;
            callback->audioDeviceAboutToStart (this);
        }

        void stop() override
        {
            if (auto* c = std::exchange (callback, nullptr))
                c->audioDeviceStopped();
        }

        bool isPlaying() override                           { return callback != nullptr; }
        String getLastError() override                      { return {}; }
        int getCurrentBufferSizeSamples() override          { fakes.noteQuery(); return bufferSize; }
        double getCurrentSampleRate() override              { fakes.noteQuery(); return sampleRate; }
        int getCurrentBitDepth() override                   { return 32; }
        BigInteger getActiveOutputChannels() const override { return outputs; }
        BigInteger getActiveInputChannels() const override  { return inputs; }
        int getOutputLatencyInSamples() override            { return 0; }
        int getInputLatencyInSamples() override             { return 0; }

        Fakes& fakes;
        AudioIODeviceCallback* callback = nullptr;
        BigInteger inputs, outputs;
        double sampleRate = 0;
        int bufferSize = 0;
        bool isDeviceOpen = false;
    };

    struct FakeType final : public AudioIODeviceType
    {
        FakeType (Fakes& f, const String& typeName, StringArray names)
            : AudioIODeviceType (typeName), fakes (f), deviceNames (std::move (names)) {}

        void scanForDevices() override {}
        StringArray getDeviceNames (bool) const override    { fakes.noteQuery(); return deviceNames; }
        int getDefaultDeviceIndex (bool) const override     { return 0; }
        int getIndexOfDevice (AudioIODevice* d, bool) const override { return deviceNames.indexOf (d->getName()); }
        bool hasSeparateInputsAndOutputs() const override   { return true; }

        AudioIODevice* createDevice (const String& outputName, const String& inputName) override
        {
            return new FakeDevice (fakes, outputName.isNotEmpty() ? outputName : inputName, getTypeName());
        }

        Fakes& fakes;
        const StringArray deviceNames;
    };

    struct EventRecorder final : public AudioIODeviceCallback
    {
        void audioDeviceIOCallbackWithContext (const float* const*, int, float* const*, int, int,
                                               const AudioIODeviceCallbackContext&) override {}
        void audioDeviceAboutToStart (AudioIODevice*) override { const ScopedLock sl (lock); events.add ("aboutToStart"); }
        void audioDeviceStopped() override                     { const ScopedLock sl (lock); events.add ("stopped"); }

        void clear()           { const ScopedLock sl (lock); events.clear(); }
        StringArray get() const { const ScopedLock sl (lock); return events; }

        CriticalSection lock;
        StringArray events;
    };

    template <typename Fn>
    static bool pumpUntil (Fn&& done, int timeoutMs)
    {
        const auto end = Time::getMillisecondCounter() + (uint32) timeoutMs;

        while (! done())
        {
            if (Time::getMillisecondCounter() > end)
                return false;

            if (! detail::dispatchNextMessageOnSystemQueue (true))
                Thread::sleep (1);
        }

        return true;
    }

    static void pumpFor (int ms)
    {
        const auto end = Time::getMillisecondCounter() + (uint32) ms;
        pumpUntil ([&] { return Time::getMillisecondCounter() > end; }, ms + 1000);
    }

    // Finds the control that the label with this text is attached to.
    template <typename ComponentType>
    static ComponentType* findControl (Component& root, const String& labelText)
    {
        for (auto* child : root.getChildren())
        {
            if (auto* label = dynamic_cast<Label*> (child); label != nullptr && label->getText() == labelText)
                return dynamic_cast<ComponentType*> (label->getAttachedComponent());

            if (auto* found = findControl<ComponentType> (*child, labelText))
                return found;
        }

        return nullptr;
    }

    void choose (Component& root, const String& labelText, const String& itemText)
    {
        auto* box = findControl<ComboBox> (root, labelText);
        expect (box != nullptr, labelText);

        for (int i = 0; i < box->getNumItems(); ++i)
            if (box->getItemText (i) == itemText)
                return box->setSelectedId (box->getItemId (i), sendNotificationSync);

        expect (false, "no item " + itemText);
    }
};

static AudioDeviceSelectorComponentTests audioDeviceSelectorComponentTests;

#endif

} // namespace juce
