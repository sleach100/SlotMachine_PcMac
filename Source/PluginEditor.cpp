#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "PolyrhythmVizComponent.h"
#include "BeatsQuickPickGrid.h"
#include "CountBeatMaskGrid.h"
#include "LicenseRegistry.h"
#include "LemonSqueezyAPI.h"
#include "InstanceIdentifier.h"
#include "UpdateChecker.h"

#include <memory>
#include <cmath>
#include <array>
#include <limits>
#include <functional>
#include <cstring>
#include <string>

#if __has_include("BinaryData.h")
#include "BinaryData.h"
#else
namespace BinaryData
{
    extern const unsigned char MuteOFF_png[];
    extern const int MuteOFF_pngSize;
    extern const unsigned char MuteON_png[];
    extern const int MuteON_pngSize;
    extern const unsigned char SoloOFF_png[];
    extern const int SoloOFF_pngSize;
    extern const unsigned char SoloON_png[];
    extern const int SoloON_pngSize;
    extern const char* SlotMachineUserManual_html;
    extern const int   SlotMachineUserManual_htmlSize;
    inline const void* getNamedResource(const char*, int& size) { size = 0; return nullptr; }
}
#endif

namespace license
{
    bool verifyLicense(const std::string& licenseKey,
        const std::string& firstName,
        const std::string& lastName,
        const std::string& email);
}

using APVTS = juce::AudioProcessorValueTreeState;
static const juce::Identifier kPatternNameProperty("name");
static const juce::Identifier kPatternRepeatProperty("repeat");
static const juce::Identifier kLastAudioExportPlaythroughProperty("lastAudioExportPlaythrough");
static const juce::Identifier kLastMidiExportPlaythroughProperty("lastMidiExportPlaythrough");

static int ensurePatternRepeatProperty(juce::ValueTree pattern)
{
    if (!pattern.isValid())
        return 0;

    const auto valueVar = pattern.getProperty(kPatternRepeatProperty);
    if (valueVar.isVoid())
    {
        pattern.setProperty(kPatternRepeatProperty, 0, nullptr);
        return 0;
    }

    const int repeat = juce::jmax(0, valueVar.toString().getIntValue());
    pattern.setProperty(kPatternRepeatProperty, repeat, nullptr);
    return repeat;
}

static int computePatternPlayThroughCycles(juce::ValueTree pattern)
{
    const int repeat = ensurePatternRepeatProperty(pattern);
    return juce::jmax(1, 1 + repeat);
}

constexpr float kPlayThroughWrapGuardThreshold = 0.02f;

static juce::Font createBoldFont(float size);
static juce::Font createRegularFont(float size);

namespace
{
    template <typename Group>
    auto tryGetSlotTitleLabel(Group& group, int) -> decltype(&group.getTextLabel())
    {
        return &group.getTextLabel();
    }

    template <typename Group>
    juce::Label* tryGetSlotTitleLabel(Group&, long)
    {
        return nullptr;
    }

    juce::Label* getSlotTitleLabelIfAvailable(juce::GroupComponent& group)
    {
        return tryGetSlotTitleLabel(group, 0);
    }

    constexpr int kBeatsQuickPickDefaultMax = 32;

    constexpr auto kStandaloneWindowTitle = "";// This sets the text in the title bar of the standalone app
    constexpr int kMasterControlsYOffset = 70;
    constexpr int kMasterLabelExtraYOffset = 35;
    constexpr float kBannerScaleMultiplier = 2.24f;

    void confirmWarningWithContinue(juce::Component* parent,
        const juce::String& title,
        const juce::String& message,
        std::function<void()> onConfirm)
    {
        auto options = juce::MessageBoxOptions()
            .withIconType(juce::MessageBoxIconType::WarningIcon)
            .withTitle(title)
            .withMessage(message)
            .withButton("Continue")
            .withButton("Cancel");

        if (parent != nullptr)
            options = options.withAssociatedComponent(parent);

        juce::AlertWindow::showAsync(options,
            juce::ModalCallbackFunction::create([fn = std::move(onConfirm)](int result)
            {
                if (result == 1 && fn)
                    fn();
            }));
    }

    class ExportCyclesDialog : public juce::Component,
                               private juce::Button::Listener,
                               private juce::TextEditor::Listener
    {
    public:
        class ToggleLabel;

        using ConfirmHandler = std::function<void(int, bool)>;
        using CancelHandler = std::function<void()>;

        ExportCyclesDialog(int defaultCycles,
            bool includePlaythroughOptions,
            bool playthroughInitiallySelected,
            ConfirmHandler onConfirmFn,
            CancelHandler onCancelFn)
            : onConfirm(std::move(onConfirmFn))
            , onCancel(std::move(onCancelFn))
            , includePlaythrough(includePlaythroughOptions)
        {
            instruction.setText("How many cycles would you like to export?", juce::dontSendNotification);
            instruction.setJustificationType(juce::Justification::centredLeft);
            addAndMakeVisible(instruction);

            cyclesLabel.setText("Cycles:", juce::dontSendNotification);
            cyclesLabel.setJustificationType(juce::Justification::centredRight);
            addAndMakeVisible(cyclesLabel);

            const int initialCycles = juce::jmax(1, defaultCycles);
            cyclesEditor.setText(juce::String(initialCycles));
            cyclesEditor.setInputRestrictions(0, "0123456789");
            cyclesEditor.setJustification(juce::Justification::centredLeft);
            cyclesEditor.setSelectAllWhenFocused(true);
            cyclesEditor.addListener(this);
            addAndMakeVisible(cyclesEditor);

            if (includePlaythrough)
            {
                exportCurrentTabButton.setButtonText({});
                exportCurrentTabButton.getProperties().set("accessibilityName", "Export currently selected Tab");
                exportCurrentTabButton.setRadioGroupId(1);
                exportCurrentTabButton.setToggleState(!playthroughInitiallySelected, juce::dontSendNotification);
                addAndMakeVisible(exportCurrentTabButton);

                exportCurrentTabLabel.setText("Export currently selected Tab", juce::dontSendNotification);
                exportCurrentTabLabel.setFont(createRegularFont(15.0f));
                exportCurrentTabLabel.setJustificationType(juce::Justification::centredLeft);
                exportCurrentTabLabel.setTarget(&exportCurrentTabButton);
                addAndMakeVisible(exportCurrentTabLabel);

                exportPlaythroughButton.setButtonText({});
                exportPlaythroughButton.getProperties().set("accessibilityName", "Export Tab Play Through");
                exportPlaythroughButton.setRadioGroupId(1);
                exportPlaythroughButton.setToggleState(playthroughInitiallySelected, juce::dontSendNotification);
                addAndMakeVisible(exportPlaythroughButton);

                exportPlaythroughLabel.setText("Export Tab Play Through", juce::dontSendNotification);
                exportPlaythroughLabel.setFont(createRegularFont(15.0f));
                exportPlaythroughLabel.setJustificationType(juce::Justification::centredLeft);
                exportPlaythroughLabel.setTarget(&exportPlaythroughButton);
                addAndMakeVisible(exportPlaythroughLabel);
            }

            errorLabel.setJustificationType(juce::Justification::centredLeft);
            errorLabel.setColour(juce::Label::textColourId, juce::Colours::orange);
            addAndMakeVisible(errorLabel);

            okButton.addListener(this);
            okButton.setButtonText("OK");
            addAndMakeVisible(okButton);

            cancelButton.addListener(this);
            cancelButton.setButtonText("Cancel");
            addAndMakeVisible(cancelButton);
        }

        ~ExportCyclesDialog() override
        {
            if (!hasResolved)
            {
                if (auto cancelCopy = onCancel)
                    cancelCopy();
            }
        }

        void resized() override
        {
            auto bounds = getLocalBounds().reduced(20);

            auto messageBounds = bounds.removeFromTop(48);
            instruction.setBounds(messageBounds);

            bounds.removeFromTop(8);
            auto inputRow = bounds.removeFromTop(28);
            auto labelWidth = 90;
            cyclesLabel.setBounds(inputRow.removeFromLeft(labelWidth));
            inputRow.removeFromLeft(12);
            cyclesEditor.setBounds(inputRow.removeFromLeft(120));

            if (includePlaythrough)
            {
                bounds.removeFromTop(12);
                const int toggleSize = 20;
                const int spacing = 8;
                const int rowHeight = 24;
                auto optionBounds = bounds.removeFromTop(rowHeight * 2 + spacing);

                auto layoutToggleRow = [toggleSize](juce::Rectangle<int> area, juce::ToggleButton& toggle, ToggleLabel& label)
                {
                        auto toggleBounds = juce::Rectangle<int>(area.getX(),
                            area.getCentreY() - (toggleSize / 2),
                            toggleSize + 4, // give a little breathing room
                            toggleSize + 2); // helps with anti-alias edges
                        toggleBounds.translate(-2, 0); // shift left slightly to avoid right-edge clip
                        toggle.setBounds(toggleBounds);
                        toggle.setPaintingIsUnclipped(true); // allow full draw

                        auto labelBounds = area;
                        constexpr int labelIndent = 14;
                        labelBounds.setX(toggleBounds.getRight() + labelIndent);
                        label.setBounds(labelBounds);

                };

                auto currentBounds = optionBounds.removeFromTop(rowHeight);
                layoutToggleRow(currentBounds, exportCurrentTabButton, exportCurrentTabLabel);

                optionBounds.removeFromTop(spacing);
                auto playthroughBounds = optionBounds.removeFromTop(rowHeight);
                layoutToggleRow(playthroughBounds, exportPlaythroughButton, exportPlaythroughLabel);
            }

            bounds.removeFromTop(6);
            errorLabel.setBounds(bounds.removeFromTop(20));

            bounds.removeFromBottom(8);
            auto buttonsArea = bounds.removeFromBottom(32);
            auto rightSection = buttonsArea.removeFromRight(180);
            okButton.setBounds(rightSection.removeFromRight(80));
            rightSection.removeFromRight(16);
            cancelButton.setBounds(rightSection.removeFromRight(80));
        }

        void visibilityChanged() override
        {
            if (isVisible())
            {
                cyclesEditor.grabKeyboardFocus();
                cyclesEditor.selectAll();
            }
        }

    private:
        void buttonClicked(juce::Button* button) override
        {
            if (button == &okButton)
                handleOk();
            else if (button == &cancelButton)
                handleCancel();
        }

        void textEditorReturnKeyPressed(juce::TextEditor&) override
        {
            handleOk();
        }

        void textEditorEscapeKeyPressed(juce::TextEditor&) override
        {
            handleCancel();
        }

        void textEditorTextChanged(juce::TextEditor&) override
        {
            errorLabel.setText({}, juce::dontSendNotification);
        }

        void handleOk()
        {
            const auto text = cyclesEditor.getText().trim();
            if (text.isEmpty())
            {
                showError();
                return;
            }

            const int cycles = text.getIntValue();
            if (cycles <= 0)
            {
                showError();
                return;
            }

            hasResolved = true;

            auto confirmCopy = onConfirm;
            if (auto* window = findParentComponentOfClass<juce::DialogWindow>())
                window->exitModalState(1);

            if (confirmCopy != nullptr)
            {
                const bool exportPlaythroughSelected = includePlaythrough && exportPlaythroughButton.getToggleState();

                juce::MessageManager::callAsync([confirmCopy, cycles, exportPlaythroughSelected]() mutable
                {
                    confirmCopy(cycles, exportPlaythroughSelected);
                });
            }
        }

        void handleCancel()
        {
            hasResolved = true;

            auto cancelCopy = onCancel;
            if (auto* window = findParentComponentOfClass<juce::DialogWindow>())
                window->exitModalState(0);

            if (cancelCopy != nullptr)
                cancelCopy();
        }

        void showError()
        {
            errorLabel.setText("Please enter a positive whole number of cycles.", juce::dontSendNotification);
            cyclesEditor.grabKeyboardFocus();
            cyclesEditor.selectAll();
        }

        juce::Label instruction;
        juce::Label cyclesLabel;
        juce::TextEditor cyclesEditor;
        class ToggleLabel : public juce::Label
        {
        public:
            ToggleLabel()
            {
                setMouseCursor(juce::MouseCursor::PointingHandCursor);
            }

            void setTarget(juce::ToggleButton* buttonToToggle)
            {
                target = buttonToToggle;
            }

            void mouseUp(const juce::MouseEvent& event) override
            {
                juce::Label::mouseUp(event);

                if (target != nullptr && event.mouseWasClicked())
                    target->triggerClick();
            }

        private:
            juce::ToggleButton* target = nullptr;
        };

        juce::ToggleButton exportCurrentTabButton;
        ToggleLabel exportCurrentTabLabel;
        juce::ToggleButton exportPlaythroughButton;
        ToggleLabel exportPlaythroughLabel;
        juce::Label errorLabel;
        juce::TextButton okButton;
        juce::TextButton cancelButton;

        ConfirmHandler onConfirm;
        CancelHandler onCancel;
        bool hasResolved = false;
        bool includePlaythrough = false;
    };

    class LoopPlaythroughDialog : public juce::Component,
                                  private juce::Button::Listener
    {
    public:
        using ConfirmHandler = std::function<void(bool)>;
        using CancelHandler = std::function<void()>;

        LoopPlaythroughDialog(bool initialValue,
            ConfirmHandler onConfirmFn,
            CancelHandler onCancelFn)
            : onConfirm(std::move(onConfirmFn))
            , onCancel(std::move(onCancelFn))
        {
            instruction.setText("Choose how Play Through should behave when it reaches the end of the pattern list.",
                juce::dontSendNotification);
            instruction.setJustificationType(juce::Justification::centredLeft);
            addAndMakeVisible(instruction);

            optionLabel.setText("Loop Playthrough =", juce::dontSendNotification);
            optionLabel.setJustificationType(juce::Justification::centredLeft);
            addAndMakeVisible(optionLabel);

            loopOn.setButtonText("True");
            loopOn.setRadioGroupId(1);
            loopOn.setToggleState(initialValue, juce::dontSendNotification);
            addAndMakeVisible(loopOn);

            loopOff.setButtonText("False");
            loopOff.setRadioGroupId(1);
            loopOff.setToggleState(!initialValue, juce::dontSendNotification);
            addAndMakeVisible(loopOff);

            okButton.addListener(this);
            okButton.setButtonText("OK");
            addAndMakeVisible(okButton);

            cancelButton.addListener(this);
            cancelButton.setButtonText("Cancel");
            addAndMakeVisible(cancelButton);
        }

        ~LoopPlaythroughDialog() override
        {
            if (!hasResolved)
            {
                if (auto cancelCopy = onCancel)
                    cancelCopy();
            }
        }

        void resized() override
        {
            auto bounds = getLocalBounds().reduced(20);

            auto buttonsArea = bounds.removeFromBottom(32);
            auto rightSection = buttonsArea.removeFromRight(180);
            okButton.setBounds(rightSection.removeFromRight(80));
            rightSection.removeFromRight(16);
            cancelButton.setBounds(rightSection.removeFromRight(80));

            bounds.removeFromBottom(12);

            auto messageBounds = bounds.removeFromTop(64);
            instruction.setBounds(messageBounds);

            bounds.removeFromTop(8);
            auto labelBounds = bounds.removeFromTop(24);
            optionLabel.setBounds(labelBounds);

            bounds.removeFromTop(8);
            auto optionsRow = bounds.removeFromTop(28);
            const int spacing = 16;
            const int availableForButtons = juce::jmax(0, optionsRow.getWidth() - spacing);
            const int buttonWidth = juce::jmax(80, availableForButtons / 2);
            loopOn.setBounds(optionsRow.removeFromLeft(buttonWidth));
            optionsRow.removeFromLeft(spacing);
            loopOff.setBounds(optionsRow.removeFromLeft(buttonWidth));
        }

    private:
        void buttonClicked(juce::Button* button) override
        {
            if (button == &okButton)
                handleOk();
            else if (button == &cancelButton)
                handleCancel();
        }

        void handleOk()
        {
            hasResolved = true;

            const bool shouldLoop = loopOn.getToggleState();
            auto confirmCopy = onConfirm;

            if (auto* window = findParentComponentOfClass<juce::DialogWindow>())
                window->exitModalState(1);

            if (confirmCopy != nullptr)
            {
                juce::MessageManager::callAsync([confirmCopy, shouldLoop]() mutable
                {
                    confirmCopy(shouldLoop);
                });
            }
        }

        void handleCancel()
        {
            hasResolved = true;

            auto cancelCopy = onCancel;
            if (auto* window = findParentComponentOfClass<juce::DialogWindow>())
                window->exitModalState(0);

            if (cancelCopy != nullptr)
                cancelCopy();
        }

        juce::Label instruction;
        juce::Label optionLabel;
        juce::ToggleButton loopOn;
        juce::ToggleButton loopOff;
        juce::TextButton okButton;
        juce::TextButton cancelButton;

        ConfirmHandler onConfirm;
        CancelHandler onCancel;
        bool hasResolved = false;
    };

    class UnlockDialogComponent : public juce::Component,
                                  private juce::Button::Listener,
                                  private juce::TextEditor::Listener
    {
    public:
        struct Result
        {
            juce::String firstName;
            juce::String lastName;
            juce::String email;
            juce::String licenseKey;
        };

        using ResultHandler = std::function<void(bool accepted, const Result& result)>;

        UnlockDialogComponent(const Result& initialValues, ResultHandler handler)
            : onResult(std::move(handler))
        {
            instruction.setText("Enter your license details to unlock Slot Machine.", juce::dontSendNotification);
            instruction.setJustificationType(juce::Justification::centredLeft);
            instruction.setColour(juce::Label::textColourId, juce::Colours::whitesmoke);
            addAndMakeVisible(instruction);

            configureLabel(firstLabel, "First Name");
            configureLabel(lastLabel, "Last Name");
            configureLabel(emailLabel, "Email");
            configureLabel(licenseLabel, "License Key");

            configureEditor(firstEditor, initialValues.firstName);
            configureEditor(lastEditor, initialValues.lastName);
            configureEditor(emailEditor, initialValues.email);
            configureEditor(licenseEditor, initialValues.licenseKey);

            unlockButton.setButtonText("Unlock");
            unlockButton.addListener(this);
            addAndMakeVisible(unlockButton);

            cancelButton.setButtonText("Cancel");
            cancelButton.addListener(this);
            addAndMakeVisible(cancelButton);
        }

        void setDialogWindow(juce::DialogWindow& dialogWindow)
        {
            owner = &dialogWindow;
        }

        void focusFirstField()
        {
            firstEditor.grabKeyboardFocus();
            firstEditor.selectAll();
        }

        void resized() override
        {
            auto bounds = getLocalBounds().reduced(20);

            auto instructionBounds = bounds.removeFromTop(48);
            instruction.setBounds(instructionBounds);

            layoutField(bounds, firstLabel, firstEditor);
            layoutField(bounds, lastLabel, lastEditor);
            layoutField(bounds, emailLabel, emailEditor);
            layoutField(bounds, licenseLabel, licenseEditor);

            bounds.removeFromBottom(12);
            auto buttonsArea = bounds.removeFromBottom(34);
            auto right = buttonsArea.removeFromRight(220);
            unlockButton.setBounds(right.removeFromRight(100));
            right.removeFromRight(20);
            cancelButton.setBounds(right.removeFromRight(100));
        }

    private:
        void configureLabel(juce::Label& label, const juce::String& text)
        {
            label.setText(text, juce::dontSendNotification);
            label.setColour(juce::Label::textColourId, juce::Colours::whitesmoke);
            label.setJustificationType(juce::Justification::centredLeft);
            addAndMakeVisible(label);
        }

        void configureEditor(juce::TextEditor& editor, const juce::String& initial)
        {
            editor.setText(initial, juce::dontSendNotification);
            editor.setSelectAllWhenFocused(true);
            editor.addListener(this);
            addAndMakeVisible(editor);
        }

        void layoutField(juce::Rectangle<int>& area, juce::Label& label, juce::TextEditor& editor)
        {
            const int labelHeight = 18;
            const int editorHeight = 24;
            const int gap = 4;

            auto labelBounds = area.removeFromTop(labelHeight);
            label.setBounds(labelBounds);
            area.removeFromTop(gap);
            auto editorBounds = area.removeFromTop(editorHeight);
            editor.setBounds(editorBounds);
            area.removeFromTop(gap + 6);
        }

        void buttonClicked(juce::Button* button) override
        {
            if (button == &unlockButton)
            {
                commit(true);
            }
            else if (button == &cancelButton)
            {
                commit(false);
            }
        }

        void textEditorReturnKeyPressed(juce::TextEditor&) override
        {
            commit(true);
        }

        void textEditorEscapeKeyPressed(juce::TextEditor&) override
        {
            commit(false);
        }

        void commit(bool accepted)
        {
            if (hasCommitted)
                return;

            hasCommitted = true;

            Result result;
            result.firstName = firstEditor.getText();
            result.lastName = lastEditor.getText();
            result.email = emailEditor.getText();
            result.licenseKey = licenseEditor.getText();

            auto handler = onResult;
            if (handler)
                handler(accepted, result);

            if (owner != nullptr)
                owner->closeButtonPressed();
        }

        juce::Label instruction;
        juce::Label firstLabel;
        juce::Label lastLabel;
        juce::Label emailLabel;
        juce::Label licenseLabel;
        juce::TextEditor firstEditor;
        juce::TextEditor lastEditor;
        juce::TextEditor emailEditor;
        juce::TextEditor licenseEditor;
        juce::TextButton unlockButton;
        juce::TextButton cancelButton;

        ResultHandler onResult;
        juce::DialogWindow* owner = nullptr;
        bool hasCommitted = false;
    };

    class AboutComponent : public juce::Component,
                           public juce::Button::Listener
    {
    public:
        AboutComponent(const juce::String& registrationInfo,
                      std::function<void()> deactivateCallback,
                      bool showDeactivateButton,
                      std::function<void()> checkForUpdatesCallback = nullptr)
            : onDeactivate(std::move(deactivateCallback))
            , onCheckForUpdates(std::move(checkForUpdatesCallback))
            , shouldShowDeactivateButton(showDeactivateButton)
        {
            logo = juce::ImageCache::getFromMemory(BinaryData::LonePearLogic_png,
                                                   BinaryData::LonePearLogic_pngSize);

            logoComponent.setImage(logo,
                                   juce::RectanglePlacement::centred | juce::RectanglePlacement::onlyReduceInSize);
            addAndMakeVisible(logoComponent);

            aboutLabel.setText("Slot Machine by Lone Pear Logic.  Copyright 2025.",
                               juce::dontSendNotification);
            aboutLabel.setJustificationType(juce::Justification::centred);
            aboutLabel.setColour(juce::Label::textColourId, juce::Colours::whitesmoke);
            aboutLabel.setFont(createBoldFont(16.0f));
            addAndMakeVisible(aboutLabel);

            auto versionInfo = UpdateChecker::getInstalledVersion();
            versionLabel.setText("Version " + versionInfo.toString(),
                                 juce::dontSendNotification);
            versionLabel.setJustificationType(juce::Justification::centredRight);
            versionLabel.setColour(juce::Label::textColourId, juce::Colours::whitesmoke);
            versionLabel.setFont(createRegularFont(14.0f));
            addAndMakeVisible(versionLabel);

            checkForUpdatesButton.setButtonText("Check for updates");
            checkForUpdatesButton.addListener(this);
            addAndMakeVisible(checkForUpdatesButton);

            contactLabel.setText("Contact:  lonepearlogic@gmail.com",
                                 juce::dontSendNotification);
            contactLabel.setJustificationType(juce::Justification::centred);
            contactLabel.setColour(juce::Label::textColourId, juce::Colours::whitesmoke);
            contactLabel.setFont(createRegularFont(15.0f));
            addAndMakeVisible(contactLabel);

            registrationLabel.setText("Registered to: " + registrationInfo,
                                      juce::dontSendNotification);
            registrationLabel.setJustificationType(juce::Justification::centred);
            registrationLabel.setColour(juce::Label::textColourId, juce::Colours::whitesmoke);
            registrationLabel.setFont(createRegularFont(15.0f));
            addAndMakeVisible(registrationLabel);

            if (shouldShowDeactivateButton)
            {
                deactivateButton.setButtonText("Deactivate License");
                deactivateButton.addListener(this);
                addAndMakeVisible(deactivateButton);
            }
        }

        void paint(juce::Graphics& g) override
        {
            g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
        }

        void resized() override
        {
            auto bounds = getLocalBounds().reduced(20);

            const int aboutLabelHeight = 48;
            const int versionLabelHeight = 24;
            const int contactLabelHeight = 32;
            const int registrationLabelHeight = 32;
            const int buttonHeight = 32;
            const int buttonSpacing = shouldShowDeactivateButton ? buttonHeight + 20 : 0;
            auto imageArea = bounds.removeFromTop(juce::jmax(120, bounds.getHeight() - aboutLabelHeight - versionLabelHeight - contactLabelHeight - registrationLabelHeight - buttonSpacing - 50));
            logoComponent.setBounds(imageArea);

            bounds.removeFromTop(20);
            aboutLabel.setBounds(bounds.removeFromTop(aboutLabelHeight));

            bounds.removeFromTop(5);
            auto versionRow = bounds.removeFromTop(versionLabelHeight);
            const int buttonWidth = 120;
            const int spacing = 8;
            const int totalWidth = 100 + spacing + buttonWidth; // version label width + spacing + button
            auto centeredRow = versionRow.withSizeKeepingCentre(totalWidth, versionLabelHeight);
            versionLabel.setBounds(centeredRow.removeFromLeft(100));
            centeredRow.removeFromLeft(spacing);
            checkForUpdatesButton.setBounds(centeredRow);

            bounds.removeFromTop(10);
            contactLabel.setBounds(bounds.removeFromTop(contactLabelHeight));

            bounds.removeFromTop(10);
            registrationLabel.setBounds(bounds.removeFromTop(registrationLabelHeight));

            if (shouldShowDeactivateButton)
            {
                bounds.removeFromTop(20);
                auto buttonBounds = bounds.removeFromTop(buttonHeight);
                deactivateButton.setBounds(buttonBounds.withSizeKeepingCentre(180, buttonHeight));
            }
        }

        void buttonClicked(juce::Button* button) override
        {
            if (button == &deactivateButton && onDeactivate)
            {
                onDeactivate();
            }
            else if (button == &checkForUpdatesButton && onCheckForUpdates)
            {
                onCheckForUpdates();
            }
        }

    private:
        juce::Image logo;
        juce::ImageComponent logoComponent;
        juce::Label aboutLabel;
        juce::Label versionLabel;
        juce::Label contactLabel;
        juce::Label registrationLabel;
        juce::TextButton deactivateButton;
        juce::TextButton checkForUpdatesButton;
        std::function<void()> onDeactivate;
        std::function<void()> onCheckForUpdates;
        bool shouldShowDeactivateButton;
    };
}

// ===== PatternTabs =====
SlotMachineAudioProcessorEditor::PatternTabs::PatternTabs()
{
    setInterceptsMouseClicks(true, true);
}

void SlotMachineAudioProcessorEditor::PatternTabs::setTabs(const juce::StringArray& names)
{
    resetDragState();

    for (int i = buttons.size(); --i >= 0;)
    {
        if (auto* button = buttons.getUnchecked(i))
        {
            button->removeListener(this);
            removeChildComponent(button);
        }
    }

    buttons.clear();

    for (int i = 0; i < names.size(); ++i)
    {
        auto* button = new TabButton(*this);
        button->index = i;
        button->setButtonText(names[i]);
        button->setClickingTogglesState(true);
        button->setRadioGroupId(1);
        button->setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        button->setColour(juce::TextButton::buttonOnColourId, juce::Colours::dimgrey);
        button->setColour(juce::TextButton::textColourOffId, juce::Colours::whitesmoke);
        button->setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        button->addListener(this);
        addAndMakeVisible(button);
        buttons.add(button);
    }

    currentIndex = juce::jlimit(0, juce::jmax(0, buttons.size() - 1), currentIndex);
    updateToggleStates();
    resized();
    repaint();
}

void SlotMachineAudioProcessorEditor::PatternTabs::setReorderingEnabled(bool shouldEnable)
{
    if (allowReordering == shouldEnable)
        return;

    allowReordering = shouldEnable;

    if (!allowReordering)
        resetDragState();
}

void SlotMachineAudioProcessorEditor::PatternTabs::setCurrentIndex(int index, bool notify)
{
    if (buttons.isEmpty())
    {
        currentIndex = 0;
        return;
    }

    index = juce::jlimit(0, buttons.size() - 1, index);

    if (currentIndex == index)
        return;

    currentIndex = index;
    updateToggleStates();

    if (notify && tabSelected)
        tabSelected(currentIndex);
}

juce::Rectangle<int> SlotMachineAudioProcessorEditor::PatternTabs::getTabBoundsInParent(int index) const
{
    if (juce::isPositiveAndBelow(index, buttons.size()))
    {
        if (auto* button = buttons[index])
            return button->getBoundsInParent().translated(getX(), getY());
    }

    return getBounds();
}

void SlotMachineAudioProcessorEditor::PatternTabs::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setColour(juce::Colours::black.withAlpha(0.35f));
    g.fillRoundedRectangle(bounds, 6.0f);
    g.setColour(juce::Colours::white.withAlpha(0.12f));
    g.drawRoundedRectangle(bounds, 6.0f, 1.2f);
}

void SlotMachineAudioProcessorEditor::PatternTabs::resized()
{
    const int count = buttons.size();
    if (count <= 0)
        return;

    auto area = getLocalBounds();
    const int baseWidth = area.getWidth() / count;
    int remainder = area.getWidth() - baseWidth * count;
    int x = area.getX();

    for (int i = 0; i < count; ++i)
    {
        int w = baseWidth;
        if (remainder > 0)
        {
            ++w;
            --remainder;
        }

        if (auto* button = buttons[i])
            button->setBounds(x, area.getY(), w, area.getHeight());
        x += w;
    }
}

void SlotMachineAudioProcessorEditor::PatternTabs::handleTabMouseDown(TabButton& button, const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        return;

    resetDragState();

    if (!allowReordering)
        return;

    dragButton = &button;
    dragStartIndex = dragButton->index;
    dragCurrentIndex = dragStartIndex;
    dragStartScreenX = e.getScreenX();
    dragging = false;
}

void SlotMachineAudioProcessorEditor::PatternTabs::handleTabMouseDrag(TabButton& button, const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        return;

    if (&button != dragButton)
        return;

    if (!allowReordering)
        return;

    if (buttons.size() <= 1)
        return;

    const int delta = e.getScreenX() - dragStartScreenX;
    const int distance = delta >= 0 ? delta : -delta;
    if (!dragging)
    {
        if (distance < 4)
            return;

        dragging = true;
        suppressNextClick = true;
    }

    const int localX = e.getScreenX() - getScreenX();
    const int target = getDropIndexForPosition(localX);

    if (target >= 0 && target != dragCurrentIndex)
    {
        reorderTab(dragCurrentIndex, target, false);
        dragCurrentIndex = target;
    }

}

void SlotMachineAudioProcessorEditor::PatternTabs::handleTabMouseUp(TabButton& button, const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        resetDragState();

        if (button.index != currentIndex)
            setCurrentIndex(button.index, true);

        if (rightClick)
            rightClick(e);
        return;
    }

    if (!allowReordering)
    {
        resetDragState(false);
        return;
    }

    if (dragging)
    {
        if (dragStartIndex != -1 && dragCurrentIndex != -1 && dragStartIndex != dragCurrentIndex)
        {
            if (tabReordered)
                tabReordered(dragStartIndex, dragCurrentIndex);
        }

        resetDragState(false);
        suppressNextClick = true;
        return;
    }

    resetDragState(false);
}

void SlotMachineAudioProcessorEditor::PatternTabs::mouseUp(const juce::MouseEvent& e)
{
    if (!e.mods.isPopupMenu())
        return;

    resetDragState();

    const juce::Point<int> pos(e.getScreenX() - getScreenX(),
                               e.getScreenY() - getScreenY());

    for (int i = 0; i < buttons.size(); ++i)
    {
        if (auto* buttonAtPos = buttons[i])
        {
            if (buttonAtPos->getBoundsInParent().contains(pos))
            {
                if (buttonAtPos->index != currentIndex)
                    setCurrentIndex(buttonAtPos->index, true);

                if (rightClick)
                    rightClick(e);
                return;
            }
        }
    }

    if (rightClick)
        rightClick(e);
}

void SlotMachineAudioProcessorEditor::PatternTabs::buttonClicked(juce::Button* b)
{
    if (suppressNextClick)
    {
        suppressNextClick = false;
        return;
    }

    auto* tabButton = dynamic_cast<TabButton*>(b);
    if (tabButton == nullptr)
        return;

    if (tabSelected)
        tabSelected(tabButton->index);
}

void SlotMachineAudioProcessorEditor::PatternTabs::updateToggleStates()
{
    for (int i = 0; i < buttons.size(); ++i)
    {
        if (auto* button = buttons[i])
            button->setToggleState(button->index == currentIndex, juce::dontSendNotification);
    }
}

void SlotMachineAudioProcessorEditor::PatternTabs::reorderTab(int fromIndex, int toIndex, bool notify)
{
    if (!allowReordering)
        return;

    if (fromIndex == toIndex)
        return;

    if (!juce::isPositiveAndBelow(fromIndex, buttons.size())
        || !juce::isPositiveAndBelow(toIndex, buttons.size()))
        return;

    auto* button = buttons[fromIndex];
    buttons.remove(fromIndex, false);
    buttons.insert(toIndex, button);

    for (int i = 0; i < buttons.size(); ++i)
    {
        if (auto* b = buttons[i])
            b->index = i;
    }

    if (currentIndex == fromIndex)
        currentIndex = toIndex;
    else if (currentIndex > fromIndex && currentIndex <= toIndex)
        --currentIndex;
    else if (currentIndex < fromIndex && currentIndex >= toIndex)
        ++currentIndex;

    updateToggleStates();
    resized();
    repaint();

    if (notify && tabReordered)
        tabReordered(fromIndex, toIndex);
}

int SlotMachineAudioProcessorEditor::PatternTabs::getDropIndexForPosition(int x) const
{
    if (buttons.isEmpty())
        return -1;

    int clampedX = juce::jlimit(0, getWidth(), x);
    int result = buttons.size() - 1;

    for (int i = 0; i < buttons.size(); ++i)
    {
        if (auto* button = buttons[i])
        {
            const int boundary = button->getBounds().getCentreX();
            if (clampedX < boundary)
            {
                result = i;
                break;
            }
        }
    }

    return result;
}

void SlotMachineAudioProcessorEditor::PatternTabs::resetDragState(bool clearSuppressed)
{
    dragButton = nullptr;
    dragStartIndex = -1;
    dragCurrentIndex = -1;
    dragStartScreenX = 0;
    dragging = false;

    if (clearSuppressed)
        suppressNextClick = false;
}

// ===== RenamePatternComponent =====
SlotMachineAudioProcessorEditor::RenamePatternComponent::RenamePatternComponent(const juce::String& currentName,
    ResultHandler handler)
    : prompt({}, "Enter a new name:"),
      onResult(std::move(handler))
{
    prompt.setJustificationType(juce::Justification::centredLeft);
    {
        juce::Font f{ juce::FontOptions(15.0f) };
        f.setBold(true);
        prompt.setFont(f);
    }
    addAndMakeVisible(prompt);

    editor.setSelectAllWhenFocused(true);
    editor.setText(currentName, juce::dontSendNotification);
    editor.addListener(this);
    addAndMakeVisible(editor);

    okButton.addListener(this);
    cancelButton.addListener(this);
    addAndMakeVisible(okButton);
    addAndMakeVisible(cancelButton);

    setSize(260, 110);
}

SlotMachineAudioProcessorEditor::RenamePatternComponent::~RenamePatternComponent()
{
    if (!hasCommitted && onResult)
    {
        auto handler = std::move(onResult);
        handler(false, editor.getText());
    }
}

void SlotMachineAudioProcessorEditor::RenamePatternComponent::setCallOutBox(juce::CallOutBox& box)
{
    owner = &box;
}

void SlotMachineAudioProcessorEditor::RenamePatternComponent::focusEditor()
{
    juce::MessageManager::callAsync([safe = juce::Component::SafePointer<RenamePatternComponent>(this)]
    {
        if (safe != nullptr)
        {
            safe->editor.grabKeyboardFocus();
            safe->editor.selectAll();
        }
    });
}

void SlotMachineAudioProcessorEditor::RenamePatternComponent::resized()
{
    auto area = getLocalBounds().reduced(12);

    prompt.setBounds(area.removeFromTop(24));
    area.removeFromTop(6);

    editor.setBounds(area.removeFromTop(28));
    area.removeFromTop(12);

    auto buttonsArea = area.removeFromTop(28);
    auto ok = buttonsArea.removeFromLeft(buttonsArea.getWidth() / 2).reduced(4, 0);
    auto cancel = buttonsArea.reduced(4, 0);
    okButton.setBounds(ok);
    cancelButton.setBounds(cancel);
}

void SlotMachineAudioProcessorEditor::RenamePatternComponent::buttonClicked(juce::Button* button)
{
    if (button == &okButton)
    {
        commit(true);
    }
    else if (button == &cancelButton)
    {
        commit(false);
    }
}

void SlotMachineAudioProcessorEditor::RenamePatternComponent::textEditorReturnKeyPressed(juce::TextEditor&)
{
    commit(true);
}

void SlotMachineAudioProcessorEditor::RenamePatternComponent::textEditorEscapeKeyPressed(juce::TextEditor&)
{
    commit(false);
}

void SlotMachineAudioProcessorEditor::RenamePatternComponent::commit(bool accepted)
{
    if (hasCommitted)
        return;

    hasCommitted = true;
    auto handler = std::move(onResult);

    if (handler)
        handler(accepted, editor.getText());

    if (owner != nullptr)
    {
        owner->dismiss();
        owner = nullptr;
    }
}

// ===== EditPatternRepeatComponent =====
SlotMachineAudioProcessorEditor::EditPatternRepeatComponent::EditPatternRepeatComponent(int currentRepeat,
    ResultHandler handler)
    : prompt({}, "Enter repeat value:"),
      onResult(std::move(handler))
{
    prompt.setJustificationType(juce::Justification::centredLeft);
    {
        juce::Font f{ juce::FontOptions(15.0f) };
        f.setBold(true);
        prompt.setFont(f);
    }
    addAndMakeVisible(prompt);

    editor.setSelectAllWhenFocused(true);
    editor.setInputRestrictions(0, "0123456789");
    editor.setText(juce::String(juce::jmax(0, currentRepeat)), juce::dontSendNotification);
    editor.addListener(this);
    addAndMakeVisible(editor);

    okButton.addListener(this);
    cancelButton.addListener(this);
    addAndMakeVisible(okButton);
    addAndMakeVisible(cancelButton);

    setSize(260, 110);
}

SlotMachineAudioProcessorEditor::EditPatternRepeatComponent::~EditPatternRepeatComponent()
{
    if (!hasCommitted && onResult)
    {
        auto handler = std::move(onResult);
        handler(false, parseEditorValue());
    }
}

void SlotMachineAudioProcessorEditor::EditPatternRepeatComponent::setCallOutBox(juce::CallOutBox& box)
{
    owner = &box;
}

void SlotMachineAudioProcessorEditor::EditPatternRepeatComponent::focusEditor()
{
    juce::MessageManager::callAsync([safe = juce::Component::SafePointer<EditPatternRepeatComponent>(this)]
    {
        if (safe != nullptr)
        {
            safe->editor.grabKeyboardFocus();
            safe->editor.selectAll();
        }
    });
}

void SlotMachineAudioProcessorEditor::EditPatternRepeatComponent::resized()
{
    auto area = getLocalBounds().reduced(12);

    prompt.setBounds(area.removeFromTop(24));
    area.removeFromTop(6);

    editor.setBounds(area.removeFromTop(28));
    area.removeFromTop(12);

    auto buttonsArea = area.removeFromTop(28);
    auto ok = buttonsArea.removeFromLeft(buttonsArea.getWidth() / 2).reduced(4, 0);
    auto cancel = buttonsArea.reduced(4, 0);
    okButton.setBounds(ok);
    cancelButton.setBounds(cancel);
}

void SlotMachineAudioProcessorEditor::EditPatternRepeatComponent::buttonClicked(juce::Button* button)
{
    if (button == &okButton)
    {
        commit(true);
    }
    else if (button == &cancelButton)
    {
        commit(false);
    }
}

void SlotMachineAudioProcessorEditor::EditPatternRepeatComponent::textEditorReturnKeyPressed(juce::TextEditor&)
{
    commit(true);
}

void SlotMachineAudioProcessorEditor::EditPatternRepeatComponent::textEditorEscapeKeyPressed(juce::TextEditor&)
{
    commit(false);
}

int SlotMachineAudioProcessorEditor::EditPatternRepeatComponent::parseEditorValue() const
{
    return juce::jmax(0, editor.getText().trim().getIntValue());
}

void SlotMachineAudioProcessorEditor::EditPatternRepeatComponent::commit(bool accepted)
{
    if (hasCommitted)
        return;

    hasCommitted = true;
    auto handler = std::move(onResult);

    if (handler)
        handler(accepted, parseEditorValue());

    if (owner != nullptr)
    {
        owner->dismiss();
        owner = nullptr;
    }
}

// ===== Font helpers =====
static juce::Font createBoldFont(float size)
{
    juce::Font f{ juce::FontOptions(size) };
    f.setBold(true);
    return f;
}

static juce::Font createRegularFont(float size)
{
    return juce::Font{ juce::FontOptions(size) };
}

// ===== Knob helper =====
static juce::Slider& setupKnob(juce::Slider& s,
    double min, double max, double inc,
    const juce::String& name,
    int numDecimals = -1)
{
    s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 54, 18);
    s.setRange(min, max, inc);
    s.setName(name);

    if (numDecimals >= 0)
        s.setNumDecimalPlacesToDisplay(numDecimals);

    return s;
}

class SlotMachineAudioProcessorEditor::EmbeddedSampleSelector : public juce::Component
{
public:
    EmbeddedSampleSelector(const EmbeddedCatalog& catalog)
    {
        content = std::make_unique<ListContent>(*this, catalog);
        viewport.setViewedComponent(content.get(), false);
        viewport.setScrollBarsShown(true, false);
        addAndMakeVisible(viewport);

        const int width = 260;
        const int maxHeight = 320;
        const int minHeight = 140;
        const int targetHeight = juce::jlimit<int>(minHeight, maxHeight, content->getHeight() + 12);
        setSize(width, targetHeight);
    }

    ~EmbeddedSampleSelector() override = default;

    std::function<void(const juce::String&)> onPick;
    std::function<void(const juce::String&)> onPreview;

    void resized() override
    {
        viewport.setBounds(getLocalBounds());
        if (content != nullptr)
            content->setSize(getWidth(), content->getHeight());
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff1a1a1a));
    }

private:
    class SpeakerButton : public juce::Button
    {
    public:
        SpeakerButton()
            : juce::Button("speaker")
        {
            setTooltip("Preview sample");

            speakerImage = juce::ImageCache::getFromMemory(BinaryData::SpeakerIcon_png,
                BinaryData::SpeakerIcon_pngSize);
        }

        void paintButton(juce::Graphics& g, bool isOver, bool isDown) override
        {
            const auto area = getLocalBounds();
            juce::Colour background = juce::Colours::black.withAlpha(0.25f);
            if (isDown)
                background = background.brighter(0.25f);
            else if (isOver)
                background = background.brighter(0.15f);

            g.setColour(background);
            g.fillRoundedRectangle(area.toFloat().reduced(4.0f), 4.0f);

            if (speakerImage.isValid())
            {
                const auto imageBounds = area.reduced(6);
                const auto placement = juce::RectanglePlacement::centred;
                const float opacity = isDown ? 1.0f : (isOver ? 0.95f : 0.85f);
                g.setOpacity(opacity);
                g.drawImageWithin(speakerImage, imageBounds.getX(), imageBounds.getY(),
                    imageBounds.getWidth(), imageBounds.getHeight(), placement, false);
                g.setOpacity(1.0f);
            }
        }

    private:
        juce::Image speakerImage;
    };

    class ListContent : public juce::Component
    {
    public:
        ListContent(EmbeddedSampleSelector& ownerRef, const EmbeddedCatalog& catalog)
            : owner(ownerRef)
        {
            for (const auto& entry : catalog)
            {
                Section section;

                auto label = std::make_unique<juce::Label>();
                label->setText(entry.first, juce::dontSendNotification);
                label->setFont(createBoldFont(14.0f));
                label->setJustificationType(juce::Justification::centredLeft);
                label->setColour(juce::Label::textColourId, juce::Colours::whitesmoke);
                addAndMakeVisible(label.get());
                section.label = std::move(label);

                for (const auto& sample : entry.second)
                {
                    auto row = std::make_unique<Row>(owner, sample);
                    addAndMakeVisible(row.get());
                    section.rows.push_back(std::move(row));
                }

                sections.push_back(std::move(section));
            }

            updateContentSize();
        }

        void resized() override
        {
            const int width = getWidth();
            int y = 0;
            const int labelHeight = 22;
            const int rowHeight = 28;
            const int sectionSpacing = 8;

            for (auto& section : sections)
            {
                if (section.label != nullptr)
                {
                    section.label->setBounds(0, y, width, labelHeight);
                    y += labelHeight;
                }

                for (auto& row : section.rows)
                {
                    row->setBounds(0, y, width, rowHeight);
                    y += rowHeight;
                }

                y += sectionSpacing;
            }
        }

    private:
        class Row : public juce::Component
        {
        public:
            Row(EmbeddedSampleSelector& ownerRef, const EmbeddedSample& sample)
                : owner(ownerRef), resource(sample.resourceName)
            {
                textButton = std::make_unique<RowButton>(sample.display);
                addAndMakeVisible(textButton.get());
                textButton->setMouseCursor(juce::MouseCursor::PointingHandCursor);
                textButton->onClick = [this]()
                {
                    if (owner.onPick)
                        owner.onPick(resource);

                    if (auto* callout = owner.findParentComponentOfClass<juce::CallOutBox>())
                        callout->dismiss();
                };

                speaker = std::make_unique<SpeakerButton>();
                addAndMakeVisible(speaker.get());
                speaker->setMouseCursor(juce::MouseCursor::PointingHandCursor);
                speaker->onClick = [this]()
                {
                    if (owner.onPreview)
                        owner.onPreview(resource);
                };
            }

            void resized() override
            {
                auto bounds = getLocalBounds();
                const int previewWidth = 36;
                auto previewBounds = bounds.removeFromRight(previewWidth);
                speaker->setBounds(previewBounds);
                textButton->setBounds(bounds);
            }

        private:
            class RowButton : public juce::Button
            {
            public:
                explicit RowButton(const juce::String& text)
                    : juce::Button(text)
                {
                }

                void paintButton(juce::Graphics& g, bool isOver, bool isDown) override
                {
                    auto area = getLocalBounds();
                    juce::Colour background = juce::Colours::black.withAlpha(0.45f);
                    if (isDown)
                        background = background.brighter(0.35f);
                    else if (isOver)
                        background = background.brighter(0.2f);

                    g.setColour(background);
                    g.fillRect(area);

                    g.setColour(juce::Colours::whitesmoke);
                    g.setFont(juce::Font{ juce::FontOptions(14.0f) });
                    g.drawText(getButtonText(), area.reduced(10, 0), juce::Justification::centredLeft, true);
                }
            };

            EmbeddedSampleSelector& owner;
            juce::String resource;
            std::unique_ptr<RowButton> textButton;
            std::unique_ptr<SpeakerButton> speaker;
        };

        struct Section
        {
            std::unique_ptr<juce::Label> label;
            std::vector<std::unique_ptr<Row>> rows;
        };

        void updateContentSize()
        {
            const int labelHeight = 22;
            const int rowHeight = 28;
            const int sectionSpacing = 8;
            int total = 0;

            for (const auto& section : sections)
            {
                if (section.label != nullptr)
                    total += labelHeight;

                total += (int)section.rows.size() * rowHeight;
                total += sectionSpacing;
            }

            if (total > 0)
                total -= sectionSpacing;

            contentHeight = total;
            setSize(260, juce::jmax(0, contentHeight));
            resized();
        }

        EmbeddedSampleSelector& owner;
        std::vector<Section> sections;
        int contentHeight = 0;
    };

    juce::Viewport viewport;
    std::unique_ptr<ListContent> content;
};

SlotMachineAudioProcessorEditor::SlotUI::FileButton::FileButton()
    : juce::TextButton("Load")
{
}

bool SlotMachineAudioProcessorEditor::SlotUI::FileButton::isInterestedInFileDrag(const juce::StringArray& files)
{
    if (!isEnabled())
        return false;

    return containsSupportedFile(files);
}

void SlotMachineAudioProcessorEditor::SlotUI::FileButton::fileDragEnter(const juce::StringArray& files, int, int)
{
    if (!isEnabled())
        return;

    updateDragHighlight(containsSupportedFile(files));
}

void SlotMachineAudioProcessorEditor::SlotUI::FileButton::fileDragExit(const juce::StringArray& files)
{
    juce::ignoreUnused(files);

    if (!isEnabled())
        return;

    updateDragHighlight(false);
}

void SlotMachineAudioProcessorEditor::SlotUI::FileButton::filesDropped(const juce::StringArray& files, int, int)
{
    if (!isEnabled())
        return;

    updateDragHighlight(false);

    if (!onFileDropped)
        return;

    for (const auto& path : files)
    {
        const juce::File file(path);
        if (isSupportedFile(file))
        {
            onFileDropped(file);
            break;
        }
    }
}

void SlotMachineAudioProcessorEditor::SlotUI::FileButton::mouseUp(const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        if (onRightClick)
            onRightClick(e);
        return;
    }

    juce::TextButton::mouseUp(e);
}

void SlotMachineAudioProcessorEditor::SlotUI::FileButton::paintButton(juce::Graphics& g, bool isMouseOverButton, bool isButtonDown)
{
    juce::TextButton::paintButton(g, isMouseOverButton || dragActive, isButtonDown);

    if (dragActive)
    {
        auto bounds = getLocalBounds().toFloat().reduced(1.0f);
        auto highlightColour = findColour(juce::TextButton::textColourOffId).withAlpha(0.85f);
        g.setColour(highlightColour);
        g.drawRoundedRectangle(bounds, 4.0f, 2.0f);
    }
}

bool SlotMachineAudioProcessorEditor::SlotUI::FileButton::containsSupportedFile(const juce::StringArray& files)
{
    for (const auto& path : files)
    {
        if (isSupportedFile(juce::File(path)))
            return true;
    }

    return false;
}

bool SlotMachineAudioProcessorEditor::SlotUI::FileButton::isSupportedFile(const juce::File& file)
{
    if (!file.existsAsFile())
        return false;

    auto ext = file.getFileExtension();
    if (ext.isEmpty())
        return false;

    ext = ext.trimCharactersAtStart(".").toLowerCase();

    return ext == "wav" || ext == "aiff" || ext == "aif" || ext == "flac";
}

void SlotMachineAudioProcessorEditor::SlotUI::FileButton::updateDragHighlight(bool shouldHighlight)
{
    if (dragActive == shouldHighlight)
        return;

    dragActive = shouldHighlight;
    repaint();
}

void SlotMachineAudioProcessorEditor::buildEmbeddedSampleCatalog()
{
    if (embeddedCatalogBuilt)
        return;

    embeddedCatalog.clear();
    embeddedSampleLookup.clear();

#if __has_include("BinaryData.h")
    const int totalResources = BinaryData::namedResourceListSize;
    for (int i = 0; i < totalResources; ++i)
    {
        const char* resourceName = BinaryData::namedResourceList[i];
        if (resourceName == nullptr)
            continue;

        const juce::String originalName = BinaryData::getNamedResourceOriginalFilename(resourceName);
        if (originalName.isEmpty() || !originalName.endsWithIgnoreCase(".wav"))
            continue;

        juce::String fileName = originalName;
        const int slashIndex = juce::jmax(fileName.lastIndexOfChar('/'), fileName.lastIndexOfChar('\\'));
        if (slashIndex >= 0)
            fileName = fileName.substring(slashIndex + 1);

        const int dashIndex = fileName.indexOfChar('-');
        juce::String category = dashIndex >= 0 ? fileName.substring(0, dashIndex).trim() : juce::String();
        if (category.isEmpty())
            category = "Misc";

        juce::String display = dashIndex >= 0 ? fileName.substring(dashIndex + 1) : fileName;
        display = display.upToLastOccurrenceOf(".", false, false).trim();
        if (display.isEmpty())
            display = juce::File(fileName).getFileNameWithoutExtension();

        EmbeddedSample sample{ category, display, juce::String(resourceName) };
        embeddedCatalog[sample.category].add(sample);
        embeddedSampleLookup[sample.resourceName] = sample;
    }

    struct SampleComparator
    {
        int compareElements(const EmbeddedSample& first, const EmbeddedSample& second) const
        {
            return first.display.compareIgnoreCase(second.display);
        }
    } comparator;

    for (auto& entry : embeddedCatalog)
        entry.second.sort(comparator);
#endif

    embeddedCatalogBuilt = true;
}

juce::String SlotMachineAudioProcessorEditor::getEmbeddedSampleDisplay(const juce::String& resourceName) const
{
    if (resourceName.isEmpty())
        return {};

    auto it = embeddedSampleLookup.find(resourceName);
    if (it != embeddedSampleLookup.end())
        return it->second.display;

    return {};
}

void SlotMachineAudioProcessorEditor::openEmbeddedSampleSelectorForSlot(int slotIndex, const juce::MouseEvent& e)
{
    buildEmbeddedSampleCatalog();
    if (embeddedCatalog.empty())
        return;

    auto selector = std::make_unique<EmbeddedSampleSelector>(embeddedCatalog);

    selector->onPreview = [this](const juce::String& resource)
    {
        int size = 0;
        const void* bytes = BinaryData::getNamedResource(resource.toRawUTF8(), size);
        if (bytes != nullptr && size > 0)
            processor.previewEmbeddedWav(bytes, size);
    };

    selector->onPick = [this, slotIndex](const juce::String& resource)
    {
        int size = 0;
        const void* bytes = BinaryData::getNamedResource(resource.toRawUTF8(), size);
        juce::Array<int> failed;

        if (bytes != nullptr && size > 0
            && processor.loadSampleForSlotFromMemory(slotIndex, bytes, size, resource))
        {
            embeddedSlotResourceNames[(size_t)slotIndex] = resource;
        }
        else
        {
            embeddedSlotResourceNames[(size_t)slotIndex].clear();
            failed.add(slotIndex);
        }

        refreshSlotFileLabels(failed);
        showPatternWarning(failed);
        saveCurrentPattern();
        repaint();
    };

    const auto screenPos = e.getScreenPosition().roundToInt();
    const juce::Rectangle<int> anchorBounds(screenPos.x, screenPos.y, 1, 1);
    juce::CallOutBox::launchAsynchronously(std::move(selector), anchorBounds, nullptr);
}

void SlotMachineAudioProcessorEditor::SlotUI::updateTimingModeVisibility(int timingMode)
{
    const bool showRate = (timingMode == 0);
    const bool showCount = !showRate;

    const auto configureSlider = [](juce::Slider& slider, bool shouldShow)
    {
        slider.setVisible(shouldShow);
        slider.setEnabled(shouldShow);
        slider.setInterceptsMouseClicks(shouldShow, shouldShow);
        slider.setAlpha(shouldShow ? 1.0f : 0.35f);
        slider.setWantsKeyboardFocus(shouldShow);

        if (shouldShow)
            slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 54, 18);
        else
            slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    };

    configureSlider(rate, showRate);
    configureSlider(count, showCount);

    showRateLabel = showRate;
    showCountLabel = showCount;
}

// ===== Standalone persistence for Options =====
static const juce::StringArray kOptionParamIds{
    "optShowMasterBar", "optShowSlotBars", "optShowVisualizer", "optVisualizerEdgeWalk", "optVisualizerMasterPulse", "optVisualizerBreathe",
    "optSampleRate", "optTimingMode",
    "optSlotScale",
    "optGlowColor", "optGlowAlpha", "optGlowWidth",
    "optPulseColor", "optPulseAlpha", "optPulseWidth"
};

static bool isOptionParameter(const juce::String& paramID)
{
    return kOptionParamIds.contains(paramID);
}

static juce::File optionsFile()
{
    auto dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile(JucePlugin_Manufacturer)
        .getChildFile(JucePlugin_Name);
    dir.createDirectory();
    return dir.getChildFile("options.xml");
}

static void saveOptionsToDisk(juce::AudioProcessorValueTreeState& apvts)
{
    juce::ValueTree vt("OPTIONS");
    for (auto& id : kOptionParamIds)
    {
        if (auto* p = apvts.getParameter(id))
        {
            if (auto* b = dynamic_cast<juce::AudioParameterBool*>(p))
                vt.setProperty(id, (int)b->get(), nullptr);
            else if (auto* ip = dynamic_cast<juce::AudioParameterInt*>(p))
                vt.setProperty(id, (int)ip->get(), nullptr);
            else if (auto* fp = dynamic_cast<juce::AudioParameterFloat*>(p))
                vt.setProperty(id, (double)fp->get(), nullptr);
        }
    }
    if (auto xml = vt.createXml())
        xml->writeTo(optionsFile());
}

static void loadOptionsFromDiskIfNoHostState(juce::AudioProcessorValueTreeState& apvts)
{
    auto f = optionsFile();
    if (!f.existsAsFile()) return;

    std::unique_ptr<juce::XmlElement> xml(juce::XmlDocument::parse(f));
    if (!xml) return;

    juce::ValueTree vt = juce::ValueTree::fromXml(*xml);
    if (!vt.isValid() || vt.getType() != juce::Identifier("OPTIONS")) return;

    for (auto& id : kOptionParamIds)
    {
        if (!vt.hasProperty(id)) continue;

        if (auto* p = apvts.getParameter(id))
        {
            if (auto* b = dynamic_cast<juce::AudioParameterBool*>(p))
            {
                b->beginChangeGesture();
                *b = (int)vt.getProperty(id) != 0;
                b->endChangeGesture();
            }
            else if (auto* ip = dynamic_cast<juce::AudioParameterInt*>(p))
            {
                ip->beginChangeGesture();
                *ip = (int)vt.getProperty(id);
                ip->endChangeGesture();
            }
            else if (auto* fp = dynamic_cast<juce::AudioParameterFloat*>(p))
            {
                fp->beginChangeGesture();
                *fp = (float)(double)vt.getProperty(id);
                fp->endChangeGesture();
            }
        }
    }
}

// ===== Options helpers =====
namespace Opt
{
    static inline bool getBool(APVTS& apvts, const juce::String& id, bool def)
    {
        if (auto* p = apvts.getParameter(id))
            if (auto* b = dynamic_cast<juce::AudioParameterBool*>(p))
                return b->get();
        return def;
    }
    static inline float getFloat(APVTS& apvts, const juce::String& id, float def)
    {
        if (auto* p = apvts.getParameter(id))
            if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (p))
                return f->get();
        return def;
    }
    static inline int getInt(APVTS& apvts, const juce::String& id, int def)
    {
        if (auto* p = apvts.getParameter(id))
            if (auto* i = dynamic_cast<juce::AudioParameterInt*> (p))
                return i->get();
        return def;
    }
    static inline juce::Colour rgbParam(APVTS& apvts, const juce::String& id, int defRGB, float alpha)
    {
        const int rgb = getInt(apvts, id, defRGB);
        juce::Colour c((juce::uint8)((rgb >> 16) & 0xFF),
            (juce::uint8)((rgb >> 8) & 0xFF),
            (juce::uint8)(rgb & 0xFF));
        return c.withAlpha(juce::jlimit(0.0f, 1.0f, alpha));
    }
}

class SlotMachineAudioProcessorEditor::VisualizerWindow : public juce::DocumentWindow
{
public:
    explicit VisualizerWindow(SlotMachineAudioProcessorEditor& ownerRef)
        : juce::DocumentWindow("Polyrhythm Visualizer",
                               juce::Colours::darkgrey,
                               juce::DocumentWindow::closeButton)
        , owner(ownerRef)
    {
        setUsingNativeTitleBar(true);
        setResizable(true, true);
        setAlwaysOnTop(true);
    }

    void closeButtonPressed() override
    {
        owner.handleVisualizerWindowCloseRequest();
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
        {
            showContextMenu();
        }
        else
        {
            juce::DocumentWindow::mouseDown(e);
        }
    }

    void showContextMenu()
    {
        juce::PopupMenu menu;
        menu.addItem(1, "Align to right side");

        menu.addSeparator();

        // Get current visualizer mode (0=Edge, 1=Orbit, 2=Mixed)
        const int currentMode = Opt::getInt(owner.apvts, "optVisualizerEdgeWalk", 0);

        // Add visualizer mode menu items with checkmarks
        menu.addItem(2, "Beads: Edge Walk", true, currentMode == 0);
        menu.addItem(3, "Beads: Orbit", true, currentMode == 1);
        menu.addItem(4, "Beads: Mixed", true, currentMode == 2);

        menu.addSeparator();

        // Get master pulse state
        const bool masterPulseEnabled = Opt::getBool(owner.apvts, "optVisualizerMasterPulse", false);
        menu.addItem(5, "Master Pulse", true, masterPulseEnabled);

        // Get breathe state
        const bool breatheEnabled = Opt::getBool(owner.apvts, "optVisualizerBreathe", true);
        menu.addItem(6, "Breathe", true, breatheEnabled);

        // Get mouse position and create target area with offset
        auto mousePos = juce::Desktop::getMousePosition();
        constexpr int MENU_VERTICAL_OFFSET = 0;  // <-- Adjust this value to fine-tune menu position/ *** NO NEED FOR OFFSET AFTER ALL.  WORKING ON WRONG WINDOW.

        // Create a target rectangle: the menu will appear below this rectangle
        // By making the rectangle's height equal to the offset, the menu appears offset from mouse position
        auto targetRect = juce::Rectangle<int>(mousePos.x, mousePos.y, 1, MENU_VERTICAL_OFFSET);

        auto options = juce::PopupMenu::Options()
                          .withTargetScreenArea(targetRect);

        menu.showMenuAsync(options,
            [this](int result)
            {
                if (result == 1)
                {
                    alignToRightSide();
                }
                else if (result == 2)
                {
                    setVisualizerMode(0);  // Edge Walk
                }
                else if (result == 3)
                {
                    setVisualizerMode(1);  // Orbit
                }
                else if (result == 4)
                {
                    setVisualizerMode(2);  // Mixed
                }
                else if (result == 5)
                {
                    toggleMasterPulse();
                }
                else if (result == 6)
                {
                    toggleBreathe();
                }
            });
    }

    void alignToRightSide()
    {
        // Get the main application window bounds
        auto* topLevelComp = owner.getTopLevelComponent();
        if (topLevelComp == nullptr)
            return;

        // Set visualizer to a smaller size if it's currently too large
        int vizWidth = getWidth();
        int vizHeight = getHeight();

        // Make it smaller (480x480) if it's the default size or larger
        if (vizWidth >= 640 || vizHeight >= 640)
        {
            vizWidth = 480;
            vizHeight = 480;
            setSize(vizWidth, vizHeight);
        }

        // Get the native window bounds for both windows including title bars
        auto* mainPeer = topLevelComp->getPeer();
        auto* vizPeer = getPeer();

        if (mainPeer == nullptr || vizPeer == nullptr)
            return;

        // Get actual native window bounds (includes title bar and borders)
        auto mainBounds = mainPeer->getBounds();
        auto vizBounds = vizPeer->getBounds();

        // Calculate position for upper right alignment
        // Position it directly adjacent to the main window with no gap
        int newX = mainBounds.getRight();

        // Manual vertical offset adjustment for fine-tuning visualizer alignment
        constexpr int VISUALIZER_VERTICAL_OFFSET = 30;  // <-- Adjust this value to fine-tune window position

        // Calculate the visualizer's title bar height
        // vizBounds includes the title bar, getHeight() does not
        int titleBarHeight = vizBounds.getHeight() - getHeight();

        // Align at the same Y position but lower by the title bar height plus manual offset
        // This ensures the visualizer's content area aligns with the main window's top
        int newY = mainBounds.getY() + titleBarHeight + VISUALIZER_VERTICAL_OFFSET;

        // Make sure the window stays on screen
        auto displays = juce::Desktop::getInstance().getDisplays();
        auto mainDisplay = displays.getDisplayForRect(mainBounds);
        if (mainDisplay != nullptr)
        {
            auto displayArea = mainDisplay->userArea;
            newX = juce::jlimit(displayArea.getX(),
                              displayArea.getRight() - vizBounds.getWidth(),
                              newX);
            newY = juce::jlimit(displayArea.getY(),
                              displayArea.getBottom() - vizBounds.getHeight(),
                              newY);
        }

        // Set the native window position
        vizPeer->setBounds(juce::Rectangle<int>(newX, newY, vizBounds.getWidth(), vizBounds.getHeight()), false);
    }

    void setVisualizerMode(int mode)
    {
        // Update the visualizer mode parameter (0=Edge Walk, 1=Orbit, 2=Mixed)
        if (auto* param = dynamic_cast<juce::AudioParameterInt*>(owner.apvts.getParameter("optVisualizerEdgeWalk")))
        {
            param->beginChangeGesture();
            *param = juce::jlimit(0, 2, mode);
            param->endChangeGesture();
        }
    }

    void toggleMasterPulse()
    {
        // Toggle the master pulse parameter
        if (auto* param = dynamic_cast<juce::AudioParameterBool*>(owner.apvts.getParameter("optVisualizerMasterPulse")))
        {
            param->beginChangeGesture();
            *param = !param->get();
            param->endChangeGesture();
            saveOptionsToDisk(owner.apvts);
        }
    }

    void toggleBreathe()
    {
        // Toggle the breathe parameter
        if (auto* param = dynamic_cast<juce::AudioParameterBool*>(owner.apvts.getParameter("optVisualizerBreathe")))
        {
            param->beginChangeGesture();
            *param = !param->get();
            param->endChangeGesture();
            saveOptionsToDisk(owner.apvts);
        }
    }

private:
    SlotMachineAudioProcessorEditor& owner;
};

// ===== Neon frame rendering =====
namespace
{
    inline void drawNeonFrame(juce::Graphics& g,
        juce::Rectangle<float> frame,
        float cornerRadius,
        juce::Colour baseColour,
        int   layers,
        float baseThicknessPx,
        juce::Colour pulseColour,
        float pulseThicknessPx,
        float pulse)
    {
        if (baseColour.getFloatAlpha() > 0.001f && layers > 0)
        {
            for (int l = 0; l < layers; ++l)
            {
                const float t = (layers <= 1 ? 0.f : (float)l / (float)(layers - 1));
                const float a = baseColour.getFloatAlpha() * (1.0f - 0.75f * t);
                const float w = baseThicknessPx + 3.5f * t * layers;
                g.setColour(baseColour.withAlpha(a));
                g.drawRoundedRectangle(frame, cornerRadius, w);
            }
        }

        if (pulse > 0.001f && pulseColour.getFloatAlpha() > 0.001f)
        {
            const float p = juce::jlimit(0.0f, 1.0f, pulse);
            const float auraThick = juce::jlimit(0.5f, 72.0f, pulseThicknessPx);

            g.setColour(pulseColour.withAlpha(pulseColour.getFloatAlpha() * p));
            g.drawRoundedRectangle(frame, cornerRadius, auraThick);

            g.setColour(juce::Colours::white.withAlpha(0.35f * p));
            g.drawRoundedRectangle(frame.reduced(3.0f, 3.0f), cornerRadius - 2.0f, 2.0f + 2.0f * p);
        }
    }
}

// ===== Options modal component =====
// ===== Options modal component =====
class OptionsComponent : public juce::Component,
    private juce::Button::Listener,
    private juce::ChangeListener,
    private juce::Slider::Listener
{
public:
    explicit OptionsComponent(APVTS& s, std::function<void(float)> slotScaleChangedCallback = {})
        : apvts(s), slotScaleChanged(slotScaleChangedCallback)
    {
        // toggles
        addAndMakeVisible(showMasterBar);
        showMasterBar.setButtonText("Show Master BPM progress bar");
        showMasterBar.addListener(this);

        addAndMakeVisible(showSlotBars);
        showSlotBars.setButtonText("Show slot progress bars");
        showSlotBars.addListener(this);

        addAndMakeVisible(visualizerModeLabel);
        visualizerModeLabel.setText("Visualizer Path", juce::dontSendNotification);
        visualizerModeLabel.setJustificationType(juce::Justification::centredLeft);
        visualizerModeLabel.setColour(juce::Label::textColourId, juce::Colours::white);

        addAndMakeVisible(visualizerModeCombo);
        visualizerModeCombo.setJustificationType(juce::Justification::centredLeft);
        visualizerModeCombo.addItem("Edge Walk (perimeter)", 1);
        visualizerModeCombo.addItem("Orbit (circular)", 2);
        visualizerModeCombo.addItem("Mixed (every 3rd orbits)", 3);
        visualizerModeCombo.onChange = [this]() { handleVisualizerModeSelection(); };

        addAndMakeVisible(masterPulseToggle);
        masterPulseToggle.setButtonText("Visualizer Master Pulse");
        masterPulseToggle.addListener(this);

        addAndMakeVisible(breatheToggle);
        breatheToggle.setButtonText("Visualizer Breathe");
        breatheToggle.addListener(this);

        // sample rate
        sampleRateLabel.setText("Export Sample Rate", juce::dontSendNotification);
        sampleRateLabel.setColour(juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible(sampleRateLabel);

        addAndMakeVisible(sampleRateCombo);
        sampleRateCombo.setJustificationType(juce::Justification::centredLeft);
        for (int i = 0; i < (int)sampleRateValues.size(); ++i)
        {
            const int value = sampleRateValues[(size_t)i];
            sampleRateCombo.addItem(juce::String(value) + " Hz", i + 1);
        }
        sampleRateCombo.onChange = [this]() { handleSampleRateSelection(); };

        // timing mode
        timingModeLabel.setText("Timing Mode", juce::dontSendNotification);
        timingModeLabel.setColour(juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible(timingModeLabel);

        addAndMakeVisible(timingModeCombo);
        timingModeCombo.setJustificationType(juce::Justification::centredLeft);
        timingModeCombo.addItem("Rate (Decimal/1-4)", 1);
        timingModeCombo.addItem("Count (Beats/Cycle)", 2);
        timingModeCombo.onChange = [this]() { handleTimingModeSelection(); };

        // slot scale
        slotScaleLabel.setText("Slot Row Density", juce::dontSendNotification);
        slotScaleLabel.setColour(juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible(slotScaleLabel);

        addAndMakeVisible(slotScaleCombo);
        slotScaleCombo.setJustificationType(juce::Justification::centredLeft);
        for (int i = 0; i < (int)slotScaleValues.size(); ++i)
        {
            const float value = slotScaleValues[(size_t)i];
            const auto label = juce::String(juce::roundToInt(value * 100.0f)) + "%";
            slotScaleCombo.addItem(label, i + 1);
        }
        slotScaleCombo.onChange = [this]() { handleSlotScaleSelection(); };

        // colour selectors
        addAndMakeVisible(glowColourSel);
        glowColourSel.setColour(juce::ColourSelector::backgroundColourId, juce::Colours::black);
        glowColourSel.setCurrentColour(defaultGlowColour());
        glowColourSel.addChangeListener(this);

        addAndMakeVisible(pulseColourSel);
        pulseColourSel.setColour(juce::ColourSelector::backgroundColourId, juce::Colours::black);
        pulseColourSel.setCurrentColour(defaultPulseColour());
        pulseColourSel.addChangeListener(this);

        glowLabel.setText("Selected Glow Colour", juce::dontSendNotification);
        glowLabel.setColour(juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible(glowLabel);

        pulseLabel.setText("Pulse Colour", juce::dontSendNotification);
        pulseLabel.setColour(juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible(pulseLabel);

        // sliders
        setupSlider(glowAlpha, 0.0, 1.0, 0.001, "Glow Alpha");
        setupSlider(glowWidth, 0.5, 24.0, 0.01, "Glow Width (px)");
        setupSlider(pulseAlpha, 0.0, 1.0, 0.001, "Pulse Alpha");
        setupSlider(pulseWidth, 0.5, 36.0, 0.01, "Pulse Width (px)");

        glowAlpha.addListener(this);
        glowWidth.addListener(this);
        pulseAlpha.addListener(this);
        pulseWidth.addListener(this);

        addAndMakeVisible(glowAlpha);
        addAndMakeVisible(glowWidth);
        addAndMakeVisible(pulseAlpha);
        addAndMakeVisible(pulseWidth);

        // captions above sliders
        prepCaption(glowAlphaCaption, "Glow Alpha");
        prepCaption(glowWidthCaption, "Glow Width (px)");
        prepCaption(pulseAlphaCaption, "Pulse Alpha");
        prepCaption(pulseWidthCaption, "Pulse Width (px)");

        // initialize + close
        addAndMakeVisible(btnResetDefaults);
        btnResetDefaults.setButtonText("Reset to Defaults");
        btnResetDefaults.addListener(this);

        addAndMakeVisible(btnClose);
        btnClose.setButtonText("Close");
        btnClose.addListener(this);

        refreshFromState();
    }

    void resized() override
    {
        auto a = getLocalBounds().reduced(12);

        auto toggleArea = a.removeFromTop(28);
        showMasterBar.setBounds(toggleArea.removeFromLeft(getWidth() / 2 - 16));
        showSlotBars.setBounds(toggleArea);

        a.removeFromTop(8);

        auto vizRow = a.removeFromTop(48);
        visualizerModeLabel.setBounds(vizRow.removeFromLeft(150));
        vizRow.removeFromLeft(12);
        visualizerModeCombo.setBounds(vizRow.removeFromLeft(200).reduced(0, 8));

        auto pulseToggleRow = a.removeFromTop(28);
        masterPulseToggle.setBounds(pulseToggleRow);

        auto breatheToggleRow = a.removeFromTop(28);
        breatheToggle.setBounds(breatheToggleRow);

        a.removeFromTop(8);

        auto sampleRateRow = a.removeFromTop(48);
        sampleRateLabel.setBounds(sampleRateRow.removeFromLeft(getWidth() / 2 - 16));
        sampleRateCombo.setBounds(sampleRateRow.removeFromLeft(180).reduced(0, 8));

        auto timingRow = a.removeFromTop(48);
        timingModeLabel.setBounds(timingRow.removeFromLeft(getWidth() / 2 - 16));
        timingModeCombo.setBounds(timingRow.removeFromLeft(220).reduced(0, 8));

        auto scaleRow = a.removeFromTop(48);
        slotScaleLabel.setBounds(scaleRow.removeFromLeft(getWidth() / 2 - 16));
        slotScaleCombo.setBounds(scaleRow.removeFromLeft(180).reduced(0, 8));

        a.removeFromTop(6);

        auto row1 = a.removeFromTop(210);
        {
            auto left = row1.removeFromLeft(getWidth() / 2 - 16);
            glowLabel.setBounds(left.removeFromTop(22));
            glowColourSel.setBounds(left);

            auto right = row1;
            pulseLabel.setBounds(right.removeFromTop(22));
            pulseColourSel.setBounds(right);
        }

        a.removeFromTop(8);

        auto row2 = a.removeFromTop(80);
        layoutSlider(row2.removeFromLeft(getWidth() / 2 - 16), glowAlpha);
        layoutSlider(row2, pulseAlpha);

        positionCaption(glowAlphaCaption, glowAlpha);
        positionCaption(pulseAlphaCaption, pulseAlpha);

        a.removeFromTop(8);

        auto row3 = a.removeFromTop(80);
        layoutSlider(row3.removeFromLeft(getWidth() / 2 - 16), glowWidth);
        layoutSlider(row3, pulseWidth);

        positionCaption(glowWidthCaption, glowWidth);
        positionCaption(pulseWidthCaption, pulseWidth);

        a.removeFromTop(8);

        auto bottom = a.removeFromBottom(40);
        btnResetDefaults.setBounds(bottom.removeFromLeft(180));
        btnClose.setBounds(bottom.removeFromRight(120));
    }

private:
    APVTS& apvts;

    juce::ToggleButton showMasterBar, showSlotBars;
    juce::Label visualizerModeLabel;
    juce::ComboBox visualizerModeCombo;
    juce::ToggleButton masterPulseToggle;
    juce::ToggleButton breatheToggle;

    juce::Label sampleRateLabel;
    juce::ComboBox sampleRateCombo;
    juce::Label timingModeLabel;
    juce::ComboBox timingModeCombo;

    juce::Label slotScaleLabel;
    juce::ComboBox slotScaleCombo;

    juce::Label glowLabel, pulseLabel;
    juce::ColourSelector glowColourSel{ juce::ColourSelector::showColourAtTop
                                       | juce::ColourSelector::showSliders
                                       | juce::ColourSelector::showColourspace };
    juce::ColourSelector pulseColourSel{ juce::ColourSelector::showColourAtTop
                                       | juce::ColourSelector::showSliders
                                       | juce::ColourSelector::showColourspace };

    juce::Slider glowAlpha, glowWidth, pulseAlpha, pulseWidth;
    juce::TextButton btnResetDefaults, btnClose;

    juce::Label glowAlphaCaption, glowWidthCaption, pulseAlphaCaption, pulseWidthCaption;

    static constexpr int sliderLabelHeight = 18;
    static constexpr int sliderLabelGap = 2;
    static constexpr int sliderLabelTopPadding = 4;
    static constexpr int sliderLabelYOffset = 0;
    static constexpr int sliderHorizontalPadding = 8;
    static constexpr int sliderVerticalPadding = 8;

    std::function<void(float)> slotScaleChanged;
    std::array<int, 2>   sampleRateValues{ { 48000, 44100 } };
    std::array<int, 2>   timingModeValues{ { 0, 1 } };
    bool blockSampleRateUpdate = false;
    bool blockTimingModeUpdate = false;
    bool blockVisualizerModeUpdate = false;
    std::array<float, 6> slotScaleValues{ { 0.75f, 0.8f, 0.85f, 0.9f, 0.95f, 1.0f } };
    bool blockSlotScaleUpdate = false;

    // helpers
    static void setupSlider(juce::Slider& s, double mn, double mx, double inc, const juce::String& name)
    {
        s.setSliderStyle(juce::Slider::LinearHorizontal);
        s.setTextBoxStyle(juce::Slider::TextBoxRight, false, 70, 20);
        s.setRange(mn, mx, inc);
        s.setName(name);
    }
    static void layoutSlider(juce::Rectangle<int> area, juce::Slider& s)
    {
        auto sliderBounds = area.reduced(sliderHorizontalPadding, sliderVerticalPadding);
        const int top = area.getY() + sliderLabelTopPadding + sliderLabelHeight + sliderLabelGap;
        sliderBounds.setTop(std::min(top, sliderBounds.getBottom()));
        s.setBounds(sliderBounds);
    }
    void prepCaption(juce::Label& L, const juce::String& txt)
    {
        L.setText(txt, juce::dontSendNotification);
        L.setJustificationType(juce::Justification::centredLeft);
        L.setColour(juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible(L);
    }
    void positionCaption(juce::Label& caption, juce::Slider& slider)
    {
        caption.setBounds(slider.getX(),
                          slider.getY() - sliderLabelHeight - sliderLabelGap + sliderLabelYOffset,
                          slider.getWidth(),
                          sliderLabelHeight);
    }

    juce::Colour defaultGlowColour()  const { return juce::Colour::fromRGB(0x69, 0x94, 0xFC); }
    juce::Colour defaultPulseColour() const { return juce::Colour::fromRGB(0xD5, 0xCF, 0xEE); }

    void refreshFromState()
    {
        // toggles
        showMasterBar.setToggleState(Opt::getBool(apvts, "optShowMasterBar", true), juce::dontSendNotification);
        showSlotBars.setToggleState(Opt::getBool(apvts, "optShowSlotBars", true), juce::dontSendNotification);
        masterPulseToggle.setToggleState(Opt::getBool(apvts, "optVisualizerMasterPulse", false), juce::dontSendNotification);
        breatheToggle.setToggleState(Opt::getBool(apvts, "optVisualizerBreathe", true), juce::dontSendNotification);

        const int visualizerMode = Opt::getInt(apvts, "optVisualizerEdgeWalk", 0);  // 0=Edge, 1=Orbit, 2=Mixed
        blockVisualizerModeUpdate = true;
        visualizerModeCombo.setSelectedId(visualizerMode + 1, juce::dontSendNotification);  // Map to combo ID
        blockVisualizerModeUpdate = false;

        const int timingModeValue = Opt::getInt(apvts, "optTimingMode", timingModeValues.back());

        const int currentSampleRate = Opt::getInt(apvts, "optSampleRate", sampleRateValues.front());
        int sampleRateId = 1;
        for (int i = 0; i < (int)sampleRateValues.size(); ++i)
        {
            if (sampleRateValues[(size_t)i] == currentSampleRate)
            {
                sampleRateId = i + 1;
                break;
            }
        }

        blockSampleRateUpdate = true;
        sampleRateCombo.setSelectedId(sampleRateId, juce::dontSendNotification);
        blockSampleRateUpdate = false;

        int timingModeId = 1;
        for (int i = 0; i < (int)timingModeValues.size(); ++i)
        {
            if (timingModeValues[(size_t)i] == timingModeValue)
            {
                timingModeId = i + 1;
                break;
            }
        }

        blockTimingModeUpdate = true;
        timingModeCombo.setSelectedId(timingModeId, juce::dontSendNotification);
        blockTimingModeUpdate = false;

        const float currentScale = Opt::getFloat(apvts, "optSlotScale", 0.8f);
        int bestId = 1;
        float bestDiff = std::numeric_limits<float>::max();
        for (int i = 0; i < (int)slotScaleValues.size(); ++i)
        {
            const float diff = std::abs(currentScale - slotScaleValues[(size_t)i]);
            if (diff < bestDiff)
            {
                bestDiff = diff;
                bestId = i + 1;
            }
        }

        blockSlotScaleUpdate = true;
        slotScaleCombo.setSelectedId(bestId, juce::dontSendNotification);
        blockSlotScaleUpdate = false;

        // colours
        glowColourSel.setCurrentColour(Opt::rgbParam(apvts, "optGlowColor", 0x6994FC, 1.0f));
        pulseColourSel.setCurrentColour(Opt::rgbParam(apvts, "optPulseColor", 0xD5CFEE, 1.0f));

        // sliders
        glowAlpha.setValue(Opt::getFloat(apvts, "optGlowAlpha", 0.431f), juce::dontSendNotification);
        glowWidth.setValue(Opt::getFloat(apvts, "optGlowWidth", 1.34f), juce::dontSendNotification);
        pulseAlpha.setValue(Opt::getFloat(apvts, "optPulseAlpha", 1.0f), juce::dontSendNotification);
        pulseWidth.setValue(Opt::getFloat(apvts, "optPulseWidth", 4.0f), juce::dontSendNotification);
    }

    // Button::Listener
    void buttonClicked(juce::Button* b) override
    {
        if (b == &showMasterBar)
            setBoolParam("optShowMasterBar", showMasterBar.getToggleState());
        else if (b == &showSlotBars)
            setBoolParam("optShowSlotBars", showSlotBars.getToggleState());
        else if (b == &masterPulseToggle)
            setBoolParam("optVisualizerMasterPulse", masterPulseToggle.getToggleState());
        else if (b == &breatheToggle)
            setBoolParam("optVisualizerBreathe", breatheToggle.getToggleState());
        else if (b == &btnResetDefaults)
            resetToDefaultOptions();
        else if (b == &btnClose)
            if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
                dw->closeButtonPressed();
    }

    // ChangeListener for colour selectors
    void changeListenerCallback(juce::ChangeBroadcaster* src) override
    {
        if (src == &glowColourSel)
        {
            const auto c = glowColourSel.getCurrentColour();
            setIntParam("optGlowColor", (int)((c.getRed() << 16) | (c.getGreen() << 8) | c.getBlue()));
            saveOptionsToDisk(apvts);
        }
        else if (src == &pulseColourSel)
        {
            const auto c = pulseColourSel.getCurrentColour();
            setIntParam("optPulseColor", (int)((c.getRed() << 16) | (c.getGreen() << 8) | c.getBlue()));
            saveOptionsToDisk(apvts);
        }
    }

    // Slider::Listener
    void sliderValueChanged(juce::Slider* s) override
    {
        if (s == &glowAlpha)  setFloatParam("optGlowAlpha", (float)glowAlpha.getValue());
        if (s == &glowWidth)  setFloatParam("optGlowWidth", (float)glowWidth.getValue());
        if (s == &pulseAlpha) setFloatParam("optPulseAlpha", (float)pulseAlpha.getValue());
        if (s == &pulseWidth) setFloatParam("optPulseWidth", (float)pulseWidth.getValue());
    }

    void handleVisualizerModeSelection()
    {
        if (blockVisualizerModeUpdate)
            return;

        const int id = visualizerModeCombo.getSelectedId();
        // Map combo ID (1,2,3) to parameter value (0,1,2)
        const int mode = juce::jlimit(0, 2, id - 1);  // 1→0 (Edge), 2→1 (Orbit), 3→2 (Mixed)
        setIntParam("optVisualizerEdgeWalk", mode);
    }

    void handleSampleRateSelection()
    {
        if (blockSampleRateUpdate)
            return;

        const int id = sampleRateCombo.getSelectedId();
        if (id <= 0 || id > (int)sampleRateValues.size())
            return;

        const int value = sampleRateValues[(size_t)(id - 1)];
        setIntParam("optSampleRate", value);
    }

    void handleSlotScaleSelection()
    {
        if (blockSlotScaleUpdate)
            return;

        const int id = slotScaleCombo.getSelectedId();
        if (id <= 0 || id > (int)slotScaleValues.size())
            return;

        const float value = slotScaleValues[(size_t)(id - 1)];
        setFloatParam("optSlotScale", value);

        if (slotScaleChanged)
            slotScaleChanged(value);
    }

    void handleTimingModeSelection()
    {
        if (blockTimingModeUpdate)
            return;

        const int id = timingModeCombo.getSelectedId();
        if (id <= 0 || id > (int)timingModeValues.size())
            return;

        const int value = timingModeValues[(size_t)(id - 1)];
        setIntParam("optTimingMode", value);
    }

    void resetToDefaultOptions()
    {
        constexpr float kDefaultSlotScale = 0.80f;
        constexpr int   kDefaultGlowRGB = 0x6994FC;
        constexpr float kDefaultGlowAlpha = 0.431f;
        constexpr float kDefaultGlowWidth = 1.34f;
        constexpr int   kDefaultPulseRGB = 0xD5CFEE;
        constexpr float kDefaultPulseAlpha = 1.0f;
        constexpr float kDefaultPulseWidth = 4.0f;
        constexpr int   kDefaultSampleRate = 48000;
        constexpr int   kDefaultTimingMode = 1;

        setBoolParam("optShowMasterBar", true);
        setBoolParam("optShowSlotBars", true);
        setIntParam("optVisualizerEdgeWalk", 0);  // 0=Edge Walk (default)
        setBoolParam("optVisualizerMasterPulse", false);
        setBoolParam("optVisualizerBreathe", true);
        setIntParam("optSampleRate", kDefaultSampleRate);
        setIntParam("optTimingMode", kDefaultTimingMode);
        setFloatParam("optSlotScale", kDefaultSlotScale);
        setIntParam("optGlowColor", kDefaultGlowRGB);
        setFloatParam("optGlowAlpha", kDefaultGlowAlpha);
        setFloatParam("optGlowWidth", kDefaultGlowWidth);
        setIntParam("optPulseColor", kDefaultPulseRGB);
        setFloatParam("optPulseAlpha", kDefaultPulseAlpha);
        setFloatParam("optPulseWidth", kDefaultPulseWidth);

        refreshFromState();

        if (slotScaleChanged)
            slotScaleChanged(Opt::getFloat(apvts, "optSlotScale", kDefaultSlotScale));
    }

    // param setters
    void setBoolParam(const juce::String& id, bool v)
    {
        if (auto* b = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter(id)))
        {
            b->beginChangeGesture(); *b = v; b->endChangeGesture();
            saveOptionsToDisk(apvts);
        }
    }
    void setIntParam(const juce::String& id, int v)
    {
        if (auto* ip = dynamic_cast<juce::AudioParameterInt*>(apvts.getParameter(id)))
        {
            ip->beginChangeGesture(); *ip = v; ip->endChangeGesture();
            saveOptionsToDisk(apvts);
        }
    }
    void setFloatParam(const juce::String& id, float v)
    {
        if (auto* fp = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter(id)))
        {
            fp->beginChangeGesture(); *fp = v; fp->endChangeGesture();
            saveOptionsToDisk(apvts);
        }
    }

};

//==============================================================================
// VST3 Lock Overlay - Blocks unregistered VST3 users from using the plugin
//==============================================================================
class SlotMachineAudioProcessorEditor::VST3LockOverlay : public juce::Component
{
public:
    VST3LockOverlay()
    {
        setInterceptsMouseClicks(true, true);

        // Title
        titleLabel.setText("VST3 Activation Required", juce::dontSendNotification);
        titleLabel.setFont(createBoldFont(24.0f));
        titleLabel.setJustificationType(juce::Justification::centred);
        titleLabel.setColour(juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible(titleLabel);

        // Message
        messageLabel.setText(
            "This VST3 build requires activation.\n\n"
            "Please unlock in the standalone app.",
            juce::dontSendNotification);
        messageLabel.setFont(createRegularFont(16.0f));
        messageLabel.setJustificationType(juce::Justification::centred);
        messageLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
        addAndMakeVisible(messageLabel);

        // Close button
        closeButton.setButtonText("Close");
        closeButton.onClick = [this]()
        {
            if (onCloseRequested)
                onCloseRequested();
        };
        addAndMakeVisible(closeButton);
    }

    void paint(juce::Graphics& g) override
    {
        // Semi-transparent dark overlay
        g.fillAll(juce::Colour(0, 0, 0).withAlpha(0.92f));

        // Draw a subtle border around the message area
        auto centerBounds = getLocalBounds().reduced(80).withSizeKeepingCentre(400, 250);
        g.setColour(juce::Colours::darkgrey);
        g.drawRoundedRectangle(centerBounds.toFloat(), 10.0f, 2.0f);

        // Fill with slightly lighter background
        g.setColour(juce::Colour(30, 30, 35));
        g.fillRoundedRectangle(centerBounds.toFloat().reduced(1), 9.0f);
    }

    void resized() override
    {
        auto centerBounds = getLocalBounds().reduced(80).withSizeKeepingCentre(400, 250);
        auto area = centerBounds.reduced(20);

        titleLabel.setBounds(area.removeFromTop(40));
        area.removeFromTop(20);
        messageLabel.setBounds(area.removeFromTop(80));
        area.removeFromTop(20);

        auto buttonArea = area.removeFromTop(36);
        closeButton.setBounds(buttonArea.withSizeKeepingCentre(120, 32));
    }

    // Block all mouse events from reaching components behind
    void mouseDown(const juce::MouseEvent&) override {}
    void mouseUp(const juce::MouseEvent&) override {}
    void mouseDrag(const juce::MouseEvent&) override {}
    void mouseMove(const juce::MouseEvent&) override {}
    void mouseDoubleClick(const juce::MouseEvent&) override {}
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override {}

    std::function<void()> onCloseRequested;

private:
    juce::Label titleLabel;
    juce::Label messageLabel;
    juce::TextButton closeButton;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VST3LockOverlay)
};

//==============================================================================
// VST3 Runtime Detection Helper
//==============================================================================
bool SlotMachineAudioProcessorEditor::isRunningAsVST3() const
{
    return processor.wrapperType == juce::AudioProcessor::wrapperType_VST3;
}

void SlotMachineAudioProcessorEditor::showVST3LockOverlayIfNeeded()
{
    // Only show overlay for unregistered VST3 users
    if (!isRunningAsVST3() || isUnlocked)
    {
        // Hide overlay if it exists
        if (vst3LockOverlay)
        {
            vst3LockOverlay->setVisible(false);
        }
        return;
    }

    // Create overlay if it doesn't exist
    if (!vst3LockOverlay)
    {
        vst3LockOverlay = std::make_unique<VST3LockOverlay>();
        vst3LockOverlay->onCloseRequested = [this]()
        {
            // Request the host to close this plugin window
            // This is DAW-friendly: we just hide ourselves and let the DAW handle it
            if (auto* topLevel = getTopLevelComponent())
            {
                if (auto* window = dynamic_cast<juce::DocumentWindow*>(topLevel))
                {
                    window->closeButtonPressed();
                }
                else
                {
                    // For plugin windows in DAWs, hide the component
                    setVisible(false);
                }
            }
        };
        addAndMakeVisible(vst3LockOverlay.get());
    }

    // Ensure overlay covers entire editor and is on top
    vst3LockOverlay->setBounds(getLocalBounds());
    vst3LockOverlay->toFront(false);
    vst3LockOverlay->setVisible(true);
}

// ===== Editor =====
SlotMachineAudioProcessorEditor::SlotMachineAudioProcessorEditor(SlotMachineAudioProcessor& p, APVTS& state)
    : juce::AudioProcessorEditor(&p), processor(p), apvts(state), tooltipWindow(this, 600)   // <— add this here
{
    setLookAndFeel(&appLF);
    appLF.setCornerRadius(6.0f);

    setWantsKeyboardFocus(true);

    logoImage = juce::ImageCache::getFromMemory(BinaryData::SM5_png, BinaryData::SM5_pngSize);

    slotScale = juce::jlimit(0.75f, 1.0f, Opt::getFloat(apvts, "optSlotScale", 0.8f));
    const int initialTimingMode = Opt::getInt(apvts, "optTimingMode", 1);
    lastTimingMode = initialTimingMode;

    lastAudioExportPlaythrough = (bool)apvts.state.getProperty(kLastAudioExportPlaythroughProperty, false);
    lastMidiExportPlaythrough = (bool)apvts.state.getProperty(kLastMidiExportPlaythroughProperty, false);

    constexpr int slotColumns = 4;
    const int slotRows = juce::jmax(1, (kNumSlots + slotColumns - 1) / slotColumns);
    const int slotRowHeight = scaleDimension(220);
    const int chromeHeight = scaleDimension(160) + kMasterControlsYOffset; // top/bottom padding + master row + tabs space
    setSize(1280, chromeHeight + slotRows * slotRowHeight);

    if (processor.consumeInitialiseOnFirstEditor())
        processor.initialiseStateForFirstEditor();

    updateStandaloneWindowTitle();

    // Master row
    addAndMakeVisible(masterLabel);
    masterLabel.setJustificationType(juce::Justification::bottomLeft);
    masterLabel.setColour(juce::Label::textColourId, juce::Colours::whitesmoke);
    masterLabel.setFont(createBoldFont(18.0f));
    masterLabel.setMouseCursor(juce::MouseCursor::PointingHandCursor);
    masterLabel.setTooltip("Tap tempo");
    masterLabel.addMouseListener(this, false);

    addAndMakeVisible(masterBPM);
    masterBPM.setSliderStyle(juce::Slider::LinearHorizontal);
    masterBPM.setRange(10.0, 1000.0, 0.01);
    masterBPM.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 70, 22);
    masterBPM.setName("Master BPM");
    masterBPMA = std::make_unique<APVTS::SliderAttachment>(apvts, "masterBPM", masterBPM);

    masterRunA = std::make_unique<APVTS::ButtonAttachment>(apvts, "masterRun", startToggle);

    auto beautify = [](juce::TextButton& b)
        {
            b.setClickingTogglesState(false);
            b.setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
            b.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
            b.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
            b.setColour(juce::TextButton::buttonOnColourId, juce::Colours::grey);
        };

    addAndMakeVisible(btnStart);       beautify(btnStart);       btnStart.addListener(this);
    btnStart.addShortcut(juce::KeyPress(juce::KeyPress::spaceKey));
    addAndMakeVisible(btnSave);        beautify(btnSave);        btnSave.addListener(this);
    addAndMakeVisible(btnLoad);        beautify(btnLoad);        btnLoad.addListener(this);
    addAndMakeVisible(btnResetLoop);   beautify(btnResetLoop);   btnResetLoop.addListener(this);
    addAndMakeVisible(btnReset);       beautify(btnReset);       btnReset.addListener(this);
    addAndMakeVisible(btnInitialize);  beautify(btnInitialize);  btnInitialize.addListener(this);
    addAndMakeVisible(btnOptions);     beautify(btnOptions);     btnOptions.addListener(this);
    addAndMakeVisible(btnExportMidi);   beautify(btnExportMidi);   btnExportMidi.addListener(this);
    addAndMakeVisible(btnExportAudio);  beautify(btnExportAudio);  btnExportAudio.addListener(this);
    addAndMakeVisible(btnVisualizer);   beautify(btnVisualizer);   btnVisualizer.addListener(this);
    addAndMakeVisible(btnTutorial);     beautify(btnTutorial);     btnTutorial.addListener(this);
    addAndMakeVisible(btnUserManual);   beautify(btnUserManual);   btnUserManual.addListener(this);
    addAndMakeVisible(btnAbout);        beautify(btnAbout);        btnAbout.addListener(this);
#if JUCE_DEBUG
    addAndMakeVisible(btnLock);
#else
    addChildComponent(btnLock);
    btnLock.setVisible(false);
#endif
    beautify(btnLock);
    btnLock.addListener(this);
    addAndMakeVisible(btnUnlock);       beautify(btnUnlock);       btnUnlock.addListener(this);

    lockIconImage = juce::ImageCache::getFromMemory(BinaryData::LockIcon2_png, BinaryData::LockIcon2_pngSize);

    auto configureLockOverlay = [this](juce::ImageComponent& component)
    {
        component.setImage(lockIconImage);
        component.setInterceptsMouseClicks(false, false);
        component.setAlwaysOnTop(true);
        addAndMakeVisible(component);
        component.setVisible(false);
    };

    configureLockOverlay(lockIconLoad);
    configureLockOverlay(lockIconExportAudio);
    configureLockOverlay(lockIconExportMidi);

    addAndMakeVisible(patternTabs);
    patternTabs.setReorderingEnabled(!startToggle.getToggleState());
    patternTabs.onTabSelected([this](int index)
        {
            if (fileDialogActive)
            {
                patternTabs.setCurrentIndex(currentPatternIndex);
                return;
            }

            if (index == currentPatternIndex)
                return;

            applyPattern(index, true, true, true);
        });

    patternTabs.onTabBarRightClick([this](const juce::MouseEvent& e)
        {
            handlePatternContextMenu(e);
        });

    patternTabs.onTabReordered([this](int fromIndex, int toIndex)
        {
            reorderPatterns(fromIndex, toIndex);
        });

    addAndMakeVisible(patternWarningLabel);
    patternWarningLabel.setColour(juce::Label::textColourId, juce::Colours::orange.withAlpha(0.85f));
    patternWarningLabel.setJustificationType(juce::Justification::centredRight);
    patternWarningLabel.setVisible(false);
    patternWarningLabel.setFont(createBoldFont(13.0f));

    // Slots
    const juce::Image muteOffImage = juce::ImageCache::getFromMemory(BinaryData::MuteOFF_png, BinaryData::MuteOFF_pngSize);
    const juce::Image muteOnImage  = juce::ImageCache::getFromMemory(BinaryData::MuteON_png,  BinaryData::MuteON_pngSize);
    const juce::Image soloOffImage = juce::ImageCache::getFromMemory(BinaryData::SoloOFF_png, BinaryData::SoloOFF_pngSize);
    const juce::Image soloOnImage  = juce::ImageCache::getFromMemory(BinaryData::SoloON_png,  BinaryData::SoloON_pngSize);

    const auto configureToggleImageButton = [](juce::ImageButton& button,
                                               const juce::Image& offImage,
                                               const juce::Image& onImage)
    {
        auto updateImages = [offImage, onImage, &button]()
        {
            const auto& source = button.getToggleState() ? onImage : offImage;

            button.setImages(false, true, true,
                source, 1.0f, juce::Colours::transparentBlack,
                source, 1.0f, juce::Colours::transparentBlack,
                source, 1.0f, juce::Colours::transparentBlack);
        };

        button.setClickingTogglesState(true);
        updateImages();
        button.onStateChange = updateImages;
    };

    for (int i = 0; i < kNumSlots; ++i)
    {
        auto ui = std::make_unique<SlotUI>();
        const int slotIndex = i;
        const int idx = slotIndex + 1;

        ui->group.setText("SLOT " + juce::String(idx));
        addAndMakeVisible(ui->group);

        ui->group.setInterceptsMouseClicks(true, true);
        ui->group.addMouseListener(this, true);   // listen to the group + its children


        addAndMakeVisible(ui->fileBtn);
        addAndMakeVisible(ui->clearBtn);                // NEW
        ui->clearBtn.setTooltip("Clear sample");        // NEW
        ui->clearBtn.addListener(this);                 // NEW

        ui->clearBtn.setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        ui->clearBtn.setColour(juce::TextButton::textColourOffId, juce::Colours::white.withAlpha(0.85f));
        ui->clearBtn.setConnectedEdges(juce::Button::ConnectedOnLeft); // makes it look attached to Load

        addAndMakeVisible(ui->fileLabel);
        ui->fileLabel.setText("No file", juce::dontSendNotification);
        ui->fileLabel.setJustificationType(juce::Justification::centredLeft);
        ui->fileBtn.addListener(this);
        ui->fileBtn.addMouseListener(this, false);
        ui->fileBtn.onFileDropped = [this, slotIndex](const juce::File& file)
        {
            handleSlotFileSelection(slotIndex, file);
        };
        ui->fileBtn.onRightClick = [this, slotIndex](const juce::MouseEvent& e)
        {
            suppressNextFileBtnClick = true;
            openEmbeddedSampleSelectorForSlot(slotIndex, e);
            e.source.enableUnboundedMouseMovement(false);
        };

        addAndMakeVisible(ui->midiChannel);
        addAndMakeVisible(ui->muteBtn);
        addAndMakeVisible(ui->soloBtn);
        addAndMakeVisible(ui->muteLabel);
        addAndMakeVisible(ui->soloLabel);

        ui->midiChannel.setName("MidiChannel" + juce::String(idx));
        ui->midiChannel.setJustificationType(juce::Justification::centred);
        ui->midiChannel.setTooltip("Select the MIDI channel used when this slot triggers events");
        ui->midiChannel.setTextWhenNothingSelected("Ch " + juce::String(idx));
        for (int ch = 1; ch <= 16; ++ch)
            ui->midiChannel.addItem("Ch " + juce::String(ch), ch);

        ui->muteBtn.setName("MuteButton" + juce::String(idx));
        configureToggleImageButton(ui->muteBtn, muteOffImage, muteOnImage);

        ui->soloBtn.setName("SoloButton" + juce::String(idx));
        configureToggleImageButton(ui->soloBtn, soloOffImage, soloOnImage);

        ui->muteLabel.setText("Mute", juce::dontSendNotification);
        ui->muteLabel.setJustificationType(juce::Justification::centred);
        ui->muteLabel.setColour(juce::Label::textColourId, juce::Colours::whitesmoke);
        ui->muteLabel.setMouseCursor(juce::MouseCursor::PointingHandCursor);
        ui->muteLabel.target = &ui->muteBtn;

        ui->soloLabel.setText("Solo", juce::dontSendNotification);
        ui->soloLabel.setJustificationType(juce::Justification::centred);
        ui->soloLabel.setColour(juce::Label::textColourId, juce::Colours::whitesmoke);
        ui->soloLabel.setMouseCursor(juce::MouseCursor::PointingHandCursor);
        ui->soloLabel.target = &ui->soloBtn;

        ui->muteBtn.addListener(this);
        ui->soloBtn.addListener(this);

        addAndMakeVisible(ui->count);
        addAndMakeVisible(ui->rate);
        addAndMakeVisible(ui->gain);
        addAndMakeVisible(ui->decay);

        setupKnob(ui->count, 1.0, (double)SlotMachineAudioProcessorEditor::kMaxBeatsPerSlot, 1.0, "Beats/Cycle (Count)");
        ui->count.setNumDecimalPlacesToDisplay(0);
		// ui->count.setTooltip("Number of beats in one shared cycle.");  // Disable Tooltip text in Count slider to avoid confusion with Quick Pick feature
        setupKnob(ui->rate, 0.0625, 4.00, 0.0001, "Rate", 4);
        setupKnob(ui->gain, 0.0, 100.0, 0.1, "Gain");
        setupKnob(ui->decay, 1.0, 100.0, 0.1, "Decay (ms)");

        ui->rate.onValueChange = [this, slotIndex]()
        {
            if (auto* slot = slots[(size_t)slotIndex].get())
                handleSlotRateChanged(slotIndex, *slot);
        };
        ui->count.onValueChange = [this, slotIndex]()
        {
            if (auto* slot = slots[(size_t)slotIndex].get())
            {
                slot->beatsQuickPickExpanded = juce::roundToInt(slot->count.getValue()) > kBeatsQuickPickDefaultMax;
                handleSlotCountChanged(slotIndex, *slot);
            }
        };

        ui->count.addMouseListener(this, true);

        ui->muteA = std::make_unique<APVTS::ButtonAttachment>(apvts, "slot" + juce::String(idx) + "_Mute", ui->muteBtn);
        ui->soloA = std::make_unique<APVTS::ButtonAttachment>(apvts, "slot" + juce::String(idx) + "_Solo", ui->soloBtn);

        if (ui->muteBtn.onStateChange)
            ui->muteBtn.onStateChange();

        if (ui->soloBtn.onStateChange)
            ui->soloBtn.onStateChange();

        ui->countA = std::make_unique<APVTS::SliderAttachment>(apvts, "slot" + juce::String(idx) + "_Count", ui->count);
        ui->rateA = std::make_unique<APVTS::SliderAttachment>(apvts, "slot" + juce::String(idx) + "_Rate", ui->rate);
        ui->gainA = std::make_unique<APVTS::SliderAttachment>(apvts, "slot" + juce::String(idx) + "_Gain", ui->gain);
        ui->decayA = std::make_unique<APVTS::SliderAttachment>(apvts, "slot" + juce::String(idx) + "_Decay", ui->decay);
        ui->midiChannelA = std::make_unique<APVTS::ComboBoxAttachment>(apvts, "slot" + juce::String(idx) + "_MidiChannel", ui->midiChannel);

        ui->hasFile = processor.slotHasSample(i);
        auto existing = processor.getSlotFilePath(i);
        if (existing.isNotEmpty())
            ui->fileLabel.setText(juce::File(existing).getFileName(), juce::dontSendNotification);

        slots[(size_t)i] = std::move(ui);

        if (auto* slotPtr = slots[(size_t)i].get())
            initialiseSlotTimingPair(i, *slotPtr);
    }

    refreshSlotTimingModeUI(initialTimingMode);

    const bool initialRateMode = (initialTimingMode == 0);
    btnVisualizer.setEnabled(!initialRateMode);
    btnVisualizer.setAlpha(initialRateMode ? 0.35f : 1.0f);
    apvts.addParameterListener("optTimingMode", this);

    initialisePatterns();

    scopeTemp.setSize(1, SlotMachineAudioProcessor::kScopeBlockSize * SlotMachineAudioProcessor::kScopeBlocks);
    scopeTemp.clear();
    lastSampleRate = processor.getSampleRate();
    lastBpmForSizing = processor.getBpm();
    lastBeatsPerBar = processor.getBeatsPerBar();
    refreshSamplesPerBar();

    startTimerHz(60);
    lastPhase = (float)processor.getMasterPhase();

    lastStartToggleState = startToggle.getToggleState();
    cachedStartGlowColour = Opt::rgbParam(apvts, "optGlowColor", 0x6994FC, 1.0f);
    cachedStartPulseColour = Opt::rgbParam(apvts, "optPulseColor", 0xD5CFEE, 1.0f);
    cachedStartGlowAlpha = Opt::getFloat(apvts, "optGlowAlpha", 0.431f);
    cachedStartGlowWidth = Opt::getFloat(apvts, "optGlowWidth", 1.34f);
    updateStartButtonVisuals(lastStartToggleState, cachedStartGlowColour,
        cachedStartPulseColour, cachedStartGlowAlpha, cachedStartGlowWidth);
    updateSliderKnobColours(cachedStartPulseColour);

    resized();
    repaint();

    // Standalone fallback: load Options from disk if host didn't restore
    const float slotScaleBeforeOptionsLoad = slotScale;
    loadOptionsFromDiskIfNoHostState(apvts);

    const float startupSlotScale = Opt::getFloat(apvts, "optSlotScale", slotScaleBeforeOptionsLoad);
    if (std::abs(startupSlotScale - slotScaleBeforeOptionsLoad) < 0.0001f)
    {
        refreshSizeForSlotScale();
        resized();
        repaint();
    }
    else
    {
        applySlotScale(startupSlotScale);
    }

    refreshSlotTimingModeUI();

    // Visualizer is now only opened when the user clicks the Visualize button
    // Force visualizer to be off at startup regardless of saved state
    lastShowVisualizer = false;
    if (auto* param = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter("optShowVisualizer")))
    {
        if (param->get())
        {
            *param = false;
            saveOptionsToDisk(apvts);
        }
    }

    // Check if this is a post-update launch (standalone only)
    checkForUpdateCompletedMessage();

    // Check for updates (standalone only, before license check)
    checkForUpdatesOnStartup();

    initialiseLicenseState();
}

void SlotMachineAudioProcessorEditor::checkForUpdatesOnStartup()
{
    // Use a weak reference to safely access 'this' in the callback
    juce::Component::SafePointer<SlotMachineAudioProcessorEditor> safeThis(this);
    const bool isVST3 = isRunningAsVST3();

    // For VST3, always force check (ignore declined state) since we just show info message
    const bool forceCheck = isVST3;

    updateChecker.checkForUpdatesAsync([safeThis, isVST3](UpdateChecker::CheckResult result,
                                                   const UpdateChecker::VersionInfo& latestVersion)
    {
        // Check if the editor is still alive
        if (safeThis == nullptr)
            return;

        switch (result)
        {
            case UpdateChecker::CheckResult::UpdateAvailable:
            {
                DBG("Update available: " + latestVersion.toString());

                if (isVST3)
                {
                    // VST3: Show informational message, cannot install from VST3
                    auto options = juce::MessageBoxOptions()
                        .withIconType(juce::MessageBoxIconType::InfoIcon)
                        .withTitle("Update Available")
                        .withMessage("Update available, but cannot be installed from the VST3 version.\n\n"
                                     "Please run the standalone version to perform the update.")
                        .withButton("OK");

                    if (safeThis.getComponent() != nullptr)
                        options = options.withAssociatedComponent(safeThis.getComponent());

                    juce::AlertWindow::showAsync(options, nullptr);
                }
                else
                {
                    // Standalone: Show update dialog with install option
                    UpdateChecker::showUpdateDialog(
                        safeThis.getComponent(),
                        latestVersion,
                        // On Accept
                        []()
                        {
                            DBG("User accepted update");
                            UpdateChecker::launchUpdaterAndTerminate();
                        },
                        // On Decline
                        []()
                        {
                            DBG("User declined update");
                            UpdateChecker::recordUpdateDeclined();
                        });
                }
                break;
            }

            case UpdateChecker::CheckResult::UpToDate:
                DBG("Application is up to date");
                break;

            case UpdateChecker::CheckResult::DeclinedRecently:
                DBG("Update available but user declined recently, skipping prompt");
                break;

            case UpdateChecker::CheckResult::NetworkError:
                DBG("Could not check for updates (network error)");
                break;

            case UpdateChecker::CheckResult::ParseError:
                DBG("Could not parse updates.txt");
                break;
        }
    }, forceCheck);
}

void SlotMachineAudioProcessorEditor::checkForUpdateCompletedMessage()
{
#if JUCE_STANDALONE_APPLICATION
    // Get command line parameters
    auto* app = juce::JUCEApplicationBase::getInstance();
    if (app == nullptr)
        return;

    juce::String commandLine = app->getCommandLineParameters();
    juce::StringArray args = juce::StringArray::fromTokens(commandLine, true);

    // Check if we have at least 2 arguments and first is /updatecompleted
    if (args.size() >= 2 && args[0].equalsIgnoreCase("/updatecompleted"))
    {
        juce::String newVersion = args[1];

        // Create a nicely formatted message
        juce::String message = juce::String::fromUTF8("🎉 Update Complete! 🎉\n\n")
                             + "S.L.O.T. Machine has been successfully updated to version " + newVersion + ".\n\n"
                             + "You're now running the latest version with all the newest features and improvements.\n\n"
                             + "Thank you for keeping your software up to date!";

        auto options = juce::MessageBoxOptions()
            .withIconType(juce::MessageBoxIconType::InfoIcon)
            .withTitle("Update Successful")
            .withMessage(message)
            .withButton("Awesome!");

        options = options.withAssociatedComponent(this);

        juce::AlertWindow::showAsync(options, nullptr);

        DBG("Update completed message displayed for version: " + newVersion);
    }
#endif
}

void SlotMachineAudioProcessorEditor::initialiseLicenseState()
{
    storedFirstName.clear();
    storedLastName.clear();
    storedEmail.clear();
    storedLicenseKey.clear();

    // Try to load Lemon Squeezy cached license first
    std::string licenseKey, instanceId, cachedJson, licenseeName, licenseeEmail;
    int64_t validationTimestamp = 0;

    if (LemonSqueezyCache::loadLicenseCache(licenseKey, instanceId, cachedJson,
                                             licenseeName, licenseeEmail, validationTimestamp))
    {
        // We have a cached license - parse it to verify it's still valid
        auto cachedResult = LemonSqueezyAPI::parseCachedResponse(juce::String(cachedJson));

        if (cachedResult.valid)
        {
            // Cache is valid - unlock the plugin
            storedLicenseKey = licenseKey;
            storedFirstName = licenseeName;
            storedLastName = "";  // Not used with Lemon Squeezy
            storedEmail = licenseeEmail;

            setUnlocked(true);

            // Try to revalidate online in the background (non-blocking)
            juce::Thread::launch([this, licenseKey, instanceId]()
            {
                auto onlineResult = LemonSqueezyAPI::validateLicense(
                    juce::String(licenseKey),
                    juce::String(instanceId));

                if (onlineResult.valid)
                {
                    // Update cache with fresh validation
                    juce::MessageManager::callAsync([this, onlineResult]()
                    {
                        LemonSqueezyCache::saveLicenseCache(
                            onlineResult.licenseKey.toStdString(),
                            onlineResult.instanceId.toStdString(),
                            onlineResult.rawJsonResponse.toStdString(),
                            onlineResult.licenseeName.toStdString(),
                            onlineResult.licenseeEmail.toStdString(),
                            onlineResult.validatedAt.toMilliseconds());
                    });
                }
                // If online validation fails, we still use the cache (offline mode)
            });

            return;
        }
    }

    // No Lemon Squeezy cache found - plugin starts in locked state
    setUnlocked(false);
}

void SlotMachineAudioProcessorEditor::setUnlocked(bool unlocked)
{
    isUnlocked = unlocked;

    btnUnlock.setEnabled(!unlocked);
    btnUnlock.setButtonText(unlocked ? "Unlocked" : "Unlock");
    btnUnlock.setVisible(!unlocked);
#if JUCE_DEBUG
    btnLock.setVisible(true);
#else
    btnLock.setVisible(false);
#endif
    btnLock.setEnabled(unlocked);
    btnLock.setButtonText(unlocked ? "Lock" : "Locked");

    updateLockIconPositions();

    // VST3: Show or hide the lock overlay based on registration state
    showVST3LockOverlayIfNeeded();
}

void SlotMachineAudioProcessorEditor::showUnlockDialog()
{
    UnlockDialogComponent::Result initialValues;
    initialValues.firstName = juce::String(storedFirstName);
    initialValues.lastName = juce::String(storedLastName);
    initialValues.email = juce::String(storedEmail);
    initialValues.licenseKey = juce::String(storedLicenseKey);

    auto unlockComponent = std::make_unique<UnlockDialogComponent>(initialValues,
        [safeThis = juce::Component::SafePointer<SlotMachineAudioProcessorEditor>(this)](bool accepted, const UnlockDialogComponent::Result& result)
        {
            if (auto* editor = safeThis.getComponent())
            {
                editor->handleUnlockDialogResult(accepted,
                    result.firstName,
                    result.lastName,
                    result.email,
                    result.licenseKey);
            }
        });

    auto* componentPtr = unlockComponent.get();
    componentPtr->setSize(440, 320);

    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = "Unlock Slot Machine";
    options.dialogBackgroundColour = getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId);
    options.content.setOwned(unlockComponent.release());
    options.componentToCentreAround = this;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;

    if (auto* window = options.launchAsync())
    {
        componentPtr->setDialogWindow(*window);
        window->centreAroundComponent(this, window->getWidth(), window->getHeight());
        componentPtr->focusFirstField();
    }
}

void SlotMachineAudioProcessorEditor::handleUnlockDialogResult(bool accepted,
    const juce::String& firstName,
    const juce::String& lastName,
    const juce::String& email,
    const juce::String& licenseKey)
{
    if (!accepted)
        return;

    const auto trimmedLicense = licenseKey.trim();
    const auto trimmedFirstName = firstName.trim();
    const auto trimmedLastName = lastName.trim();
    const auto trimmedEmail = email.trim();

    // Validate required fields
    if (trimmedLicense.isEmpty())
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
            "Unlock Slot Machine",
            "Please enter your license key.");
        return;
    }

    if (trimmedFirstName.isEmpty() || trimmedEmail.isEmpty())
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
            "Unlock Slot Machine",
            "Please enter your first name and email address.");
        return;
    }

    // Get or create instance ID for this machine
    juce::String instanceId = InstanceIdentifier::getOrCreateInstanceID();

    // Validate with Lemon Squeezy API (synchronous for now)
    auto result = LemonSqueezyAPI::validateLicense(trimmedLicense, instanceId);

    // For testing: Accept licenses where valid=true and status=inactive
    // Test licenses remain inactive but should unlock the product
    // Later we'll change this to require status=active
    if (result.hasError || !result.valid)
    {
        juce::String errorMsg = "The license key could not be validated.";

        if (result.errorCode == "activation_limit_reached")
        {
            errorMsg = juce::String("Activation limit reached (") +
                       juce::String(result.activationUsage) + " of " +
                       juce::String(result.activationLimit) + " used).\n\n" +
                       "Please deactivate the license on another machine first.";
        }
        else if (result.errorCode == "license_inactive")
        {
            errorMsg = "This license key is inactive.\n\nPlease activate it in your Lemon Squeezy dashboard.";
        }
        else if (result.errorCode == "license_expired")
        {
            errorMsg = "This license key has expired.";
        }
        else if (result.hasError && result.errorMessage.isNotEmpty())
        {
            errorMsg = result.errorMessage;
        }

        // Add debug info for troubleshooting
        if (result.errorCode.isNotEmpty())
        {
            errorMsg += juce::String("\n\nError code: ") + result.errorCode;
        }

        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
            "Unlock Slot Machine",
            errorMsg);
        return;
    }

    // Validate that the user-entered information matches the license registration
    // Compare email (case-insensitive)
    if (!trimmedEmail.equalsIgnoreCase(result.licenseeEmail))
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
            "Unlock Slot Machine",
            "The email address you entered does not match the email registered for this license key.");
        return;
    }

    // Compare name - both first and last names must match exactly
    // The API returns a full name, so parse it into first and last name components
    // and validate each separately (case-insensitive)

    // Require both first and last names to be provided
    if (trimmedLastName.isEmpty())
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
            "Unlock Slot Machine",
            "Please enter both your first and last name.");
        return;
    }

    // Parse the license name into first and last components
    juce::String licenseFirstName, licenseLastName;
    int spacePos = result.licenseeName.indexOfChar(' ');
    if (spacePos > 0)
    {
        licenseFirstName = result.licenseeName.substring(0, spacePos).trim();
        licenseLastName = result.licenseeName.substring(spacePos + 1).trim();
    }
    else
    {
        // License name has no space - treat entire string as first name
        licenseFirstName = result.licenseeName.trim();
        licenseLastName = "";
    }

    // Validate both first and last names match exactly (case-insensitive)
    if (!trimmedFirstName.equalsIgnoreCase(licenseFirstName) ||
        !trimmedLastName.equalsIgnoreCase(licenseLastName))
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
            "Unlock Slot Machine",
            "The name you entered does not match the name registered for this license key.");
        return;
    }

    // Validation successful - save to cache
    storedLicenseKey = result.licenseKey.toStdString();
    storedFirstName = result.licenseeName.toStdString();
    storedLastName = "";  // Not used with Lemon Squeezy
    storedEmail = result.licenseeEmail.toStdString();

    if (!LemonSqueezyCache::saveLicenseCache(
            result.licenseKey.toStdString(),
            result.instanceId.toStdString(),
            result.rawJsonResponse.toStdString(),
            result.licenseeName.toStdString(),
            result.licenseeEmail.toStdString(),
            result.validatedAt.toMilliseconds()))
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
            "Unlock Slot Machine",
            "Unable to save the license information. The plugin may not work offline.");
        // Continue anyway - the license is valid, we just can't cache it
    }

    setUnlocked(true);

    juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon,
        "Unlock Slot Machine",
        "Thank you! The application has been unlocked.");
}

void SlotMachineAudioProcessorEditor::showTrialModeDialog()
{
    juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
        "Feature Locked",
        "This function is not available in trial mode.  Please consider purchasing a key to unlock.");
}

void SlotMachineAudioProcessorEditor::updateLockIconPositions()
{
    const bool showLocks = lockIconImage.isValid() && !isUnlocked;

    auto updateIcon = [this, showLocks](juce::Component& button, juce::ImageComponent& icon)
    {
        if (!showLocks)
        {
            icon.setVisible(false);
            return;
        }

        auto bounds = button.getBounds();
        if (bounds.isEmpty())
        {
            icon.setVisible(false);
            return;
        }

        const float aspect = (float)lockIconImage.getHeight() / (float)juce::jmax(1, lockIconImage.getWidth());
        int iconWidth = juce::jmin(lockIconImage.getWidth(), juce::jmax(12, bounds.getWidth() / 4));
        int iconHeight = juce::roundToInt((float)iconWidth * aspect);

        const int availableHeight = juce::jmax(1, bounds.getHeight());
        if (iconHeight > availableHeight)
        {
            iconHeight = juce::jmax(12, availableHeight);
            iconWidth = juce::roundToInt((float)iconHeight / aspect);
        }

        const int horizontalInset = juce::roundToInt((float)iconWidth * 0.35f);
        const int verticalInset = juce::roundToInt((float)iconHeight * 0.35f);

        const int x = bounds.getX() - horizontalInset + 4;
        const int y = bounds.getY() - verticalInset;

        icon.setBounds(x, y, juce::jmax(1, iconWidth), juce::jmax(1, iconHeight));
        icon.setVisible(true);
        icon.toFront(false);
    };

    updateIcon(btnLoad, lockIconLoad);
    updateIcon(btnExportAudio, lockIconExportAudio);
    updateIcon(btnExportMidi, lockIconExportMidi);
}

juce::String SlotMachineAudioProcessorEditor::getRegistrationDisplayName() const
{
    if (!isUnlocked)
        return "Not Registered";

    juce::StringArray parts;

    if (!storedFirstName.empty())
        parts.add(juce::String(storedFirstName));
    if (!storedLastName.empty())
        parts.add(juce::String(storedLastName));

    auto combined = parts.joinIntoString(" ").trim();

    if (combined.isEmpty() && !storedEmail.empty())
        combined = juce::String(storedEmail);

    if (combined.isEmpty())
        combined = "Not Registered";

    return combined;
}

SlotMachineAudioProcessorEditor::~SlotMachineAudioProcessorEditor()
{
    // Clean up power monitor before JUCE shuts down
    #if JUCE_WINDOWS && JUCE_STANDALONE_APPLICATION
    extern void shutdownStandalonePowerMonitor();
    shutdownStandalonePowerMonitor();
    #endif

    setLookAndFeel(nullptr);
    closeVisualizerWindow();
    apvts.removeParameterListener("optTimingMode", this);
}

void SlotMachineAudioProcessorEditor::parentHierarchyChanged()
{
    juce::AudioProcessorEditor::parentHierarchyChanged();
    updateStandaloneWindowTitle();

    // Initialize power monitor for standalone builds only
    // This must be done here (not during static initialization) to avoid crashes
    #if JUCE_WINDOWS && JUCE_STANDALONE_APPLICATION
    // Forward declare the initialization function from StandaloneInit.cpp
    extern void initializeStandalonePowerMonitor(SlotMachineAudioProcessorEditor* editor);
    initializeStandalonePowerMonitor(this);
    #endif
}

void SlotMachineAudioProcessorEditor::mouseDown(const juce::MouseEvent& e)
{
    suppressNextFileBtnClick = false;
    juce::AudioProcessorEditor::mouseDown(e);

    auto* eventComponent = e.eventComponent;
    if (eventComponent == nullptr)
        return;

    const int timingMode = Opt::getInt(apvts, "optTimingMode", 1);

    for (int i = 0; i < kNumSlots; ++i)
    {
        if (auto* slot = slots[(size_t)i].get())
        {
            auto& beatsSlider = slot->count;
            const bool hitBeatsControl = (eventComponent == &beatsSlider) || beatsSlider.isParentOf(eventComponent);

            const auto isCountTextBoxComponent = [&beatsSlider](juce::Component* component)
            {
                if (component == nullptr || component == &beatsSlider)
                    return false;

                if (!beatsSlider.isParentOf(component))
                    return false;

                for (auto* c = component; c != nullptr && c != &beatsSlider; c = c->getParentComponent())
                {
                    if (dynamic_cast<juce::Label*>(c) != nullptr || dynamic_cast<juce::TextEditor*>(c) != nullptr)
                        return true;
                }

                return false;
            };

            if (hitBeatsControl && isCountTextBoxComponent(eventComponent))
            {
                if (e.mods.isPopupMenu())
                {
                    if (timingMode == 1)
                    {
                        const int beats = juce::jlimit<int>(1, kMaxBeatsPerSlot, juce::roundToInt(beatsSlider.getValue()));

                        CountBeatMaskGrid::Options maskOptions;
                        maskOptions.beats = beats;
                        maskOptions.columns = juce::jlimit(1, 8, (int)std::ceil(std::sqrt((double)beats)));

                        const uint64_t activeMask = processor.getSlotCountMask(i) & SlotMachineAudioProcessor::maskForBeats(beats);

                        auto maskHandler = [this, slotIndex = i, beats](uint64_t updatedMask)
                        {
                            const uint64_t active = SlotMachineAudioProcessor::maskForBeats(beats);
                            const uint64_t preserved = processor.getSlotCountMask(slotIndex) & ~active;
                            processor.setSlotCountMask(slotIndex, preserved | (updatedMask & active));
                        };

                        auto highlightProvider = [this, slotIndex = i, beats]() -> int
                        {
                            if (!Opt::getBool(apvts, "masterRun", false))
                                return -1;

                            const int timingModeNow = Opt::getInt(apvts, "optTimingMode", 1);
                            if (timingModeNow != 1)
                                return -1;

                            const int beatIndex = processor.getSlotCurrentBeatIndex(slotIndex);
                            if (beatIndex < 0 || beats <= 0)
                                return -1;

                            return beatIndex % beats;
                        };

                        auto grid = std::make_unique<CountBeatMaskGrid>(maskOptions, activeMask, std::move(maskHandler), std::move(highlightProvider));

                        const auto screenPos = e.getScreenPosition().roundToInt();
                        const auto calloutBounds = juce::Rectangle<int>(screenPos.x, screenPos.y, 1, 1);
                        juce::CallOutBox::launchAsynchronously(std::move(grid), calloutBounds, nullptr);
                    }
                    return;
                }

                if (e.mods.isLeftButtonDown())
                {
                    const int currentValue = juce::roundToInt(beatsSlider.getValue());

                    BeatsQuickPickGrid::Options opts;
                    opts.maxBeat = slot->beatsQuickPickExpanded ? kMaxBeatsPerSlot : kBeatsQuickPickDefaultMax;
                    if (currentValue > kBeatsQuickPickDefaultMax)
                        opts.maxBeat = kMaxBeatsPerSlot;

                    slot->beatsQuickPickExpanded = opts.maxBeat > kBeatsQuickPickDefaultMax;

                    auto pickHandler = [slot](int picked)
                    {
                        slot->beatsQuickPickExpanded = picked > kBeatsQuickPickDefaultMax;
                        slot->count.setValue(picked, juce::sendNotificationSync);
                    };

                    auto grid = std::make_unique<BeatsQuickPickGrid>(opts, std::move(pickHandler), currentValue);

                    const auto screenPos = e.getScreenPosition().roundToInt();
                    const auto calloutBounds = juce::Rectangle<int>(screenPos.x, screenPos.y, 1, 1);
                    juce::CallOutBox::launchAsynchronously(std::move(grid), calloutBounds, nullptr);
                    return;
                }
            }
        }
    }
}

void SlotMachineAudioProcessorEditor::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    auto* eventComponent = e.eventComponent;
    if (eventComponent == nullptr)
    {
        juce::AudioProcessorEditor::mouseWheelMove(e, wheel);
        return;
    }

    for (int i = 0; i < kNumSlots; ++i)
    {
        if (auto* slot = slots[(size_t)i].get())
        {
            auto& beatsSlider = slot->count;
            const bool hitBeatsControl = (eventComponent == &beatsSlider) || beatsSlider.isParentOf(eventComponent);

            if (hitBeatsControl)
            {
                if (wheel.deltaY == 0.0f)
                    return;

                const bool accelerated = e.mods.isCtrlDown() || e.mods.isCommandDown();
                const int step = accelerated ? 4 : 1;

                int value = juce::roundToInt(beatsSlider.getValue());
                if (wheel.deltaY > 0.0f)
                    value += step;
                else if (wheel.deltaY < 0.0f)
                    value -= step;

                const int limit = slot->beatsQuickPickExpanded ? kMaxBeatsPerSlot : kBeatsQuickPickDefaultMax;
                value = juce::jlimit(1, limit, value);

                if (value != juce::roundToInt(beatsSlider.getValue()))
                    beatsSlider.setValue(value, juce::sendNotificationSync);

                slot->beatsQuickPickExpanded = value > kBeatsQuickPickDefaultMax;
                return;
            }
        }
    }

    juce::AudioProcessorEditor::mouseWheelMove(e, wheel);
}

void SlotMachineAudioProcessorEditor::updateStandaloneWindowTitle()
{
#if JUCE_STANDALONE_APPLICATION
    if (auto* window = findParentComponentOfClass<juce::DocumentWindow>())
        window->setName(kStandaloneWindowTitle);
#endif
}

void SlotMachineAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black);

    if (logoImage.isValid() && !logoBounds.isEmpty())
        g.drawImage(logoImage,
                    (float)logoBounds.getX(),
                    (float)logoBounds.getY(),
                    (float)logoBounds.getWidth(),
                    (float)logoBounds.getHeight(),
                    0.0f,
                    0.0f,
                    (float)logoImage.getWidth(),
                    (float)logoImage.getHeight());

    // VST3: Draw BETA watermark to the right of the logo
    if (isRunningAsVST3() && !logoBounds.isEmpty())
    {
        g.setFont(createBoldFont(16.0f));
        g.setColour(juce::Colours::orange.withAlpha(0.9f));

        const int betaX = logoBounds.getRight() + 8;
        const int betaY = logoBounds.getCentreY() - 8;
        g.drawText("BETA", betaX, betaY, 50, 20, juce::Justification::centredLeft, false);
    }

    // Options
    const bool showMasterBar = Opt::getBool(apvts, "optShowMasterBar", true);
    const bool showSlotBars = Opt::getBool(apvts, "optShowSlotBars", true);

    const float glowAlpha = Opt::getFloat(apvts, "optGlowAlpha", 0.431f);
    const float glowWidthPx = Opt::getFloat(apvts, "optGlowWidth", 1.34f);
    const float pulseAlpha = Opt::getFloat(apvts, "optPulseAlpha", 1.0f);
    const float pulseWidthPx = Opt::getFloat(apvts, "optPulseWidth", 4.0f);

    const juce::Colour glowColour = Opt::rgbParam(apvts, "optGlowColor", 0x6994FC, glowAlpha);
    const juce::Colour pulseColour = Opt::rgbParam(apvts, "optPulseColor", 0xD5CFEE, pulseAlpha);

    const auto barBack = juce::Colours::white.withAlpha(0.18f);
    const auto barFill = pulseColour.withAlpha(0.92f);

    // Master progress bar
    if (showMasterBar)
    {
        float masterButtonCornerRadius = 6.0f;
        if (auto* lf = dynamic_cast<AppLookAndFeel*>(&getLookAndFeel()))
            masterButtonCornerRadius = lf->getCornerRadius();

        g.setColour(barBack);
        g.fillRoundedRectangle(masterBarBounds.toFloat(), masterButtonCornerRadius);

        paintMasterWaveform(g, masterBarBounds);

        const float clampedPhase = juce::jlimit(0.0f, 1.0f, masterPhase);
        const float w = masterBarBounds.getWidth() * clampedPhase;
        if (w > 1.0f)
        {
            auto filled = juce::Rectangle<float>((float)masterBarBounds.getX(),
                (float)masterBarBounds.getY(),
                w,
                (float)masterBarBounds.getHeight());
            g.setColour(barFill.withAlpha(0.7f));
            g.fillRoundedRectangle(filled, masterButtonCornerRadius);
        }

        if (masterBarBounds.getWidth() > 0)
        {
            const float widthMinusOne = (float) juce::jmax(1, masterBarBounds.getWidth() - 1);
            const float playheadX = (float) masterBarBounds.getX() + clampedPhase * widthMinusOne;
            g.setColour(juce::Colours::white.withAlpha(0.85f));
            g.drawLine(playheadX, (float) masterBarBounds.getY(), playheadX, (float) masterBarBounds.getBottom(), 2.0f);
        }

        // Flash overlay on cycle wrap, using the selected pulse colour/alpha
        if (cycleFlash > 0.001f)
        {
            // use the same 'pulseColour' and its alpha you already read from Options
            const float flashA = juce::jlimit(0.0f, 1.0f, cycleFlash);
            auto flashCol = pulseColour.withAlpha(pulseColour.getFloatAlpha() * flashA);

            // subtle full-bar glow
            g.setColour(flashCol);
            g.fillRoundedRectangle(masterBarBounds.toFloat(), masterButtonCornerRadius);

            // crisp highlight line at bar start (downbeat tick)
            g.setColour(juce::Colours::white.withAlpha(0.25f * flashA));
            auto tick = masterBarBounds.toFloat().removeFromLeft(3.0f);
            g.fillRoundedRectangle(tick, juce::jmin(masterButtonCornerRadius, 2.0f));
        }
    }

    // Slots
    for (int i = 0; i < kNumSlots; ++i)
    {
        auto* ui = slots[(size_t)i].get();
        if (!ui) continue;

        const auto boundsF = ui->group.getBounds().toFloat();

        // 1) Glow + pulse frame
        {
            auto frame = boundsF.reduced(1.5f, 1.5f);
            const bool  selected = ui->hasFile;
            const float pulse = ui->glow;
            const int   layers = 5;
            const float baseThick = glowWidthPx;

            const juce::Colour selColour = selected ? glowColour : glowColour.withAlpha(0.0f);
            drawNeonFrame(g, frame, 10.0f,
                selColour, layers, baseThick,
                pulseColour, pulseWidthPx, pulse);
        }

        // 2) Per-slot progress bar
        if (showSlotBars)
        {
            const float barH = 8.0f;
            auto inner = boundsF.reduced(8.0f, 8.0f);
            auto bar = juce::Rectangle<float>(inner.getX(), inner.getBottom() - barH,
                inner.getWidth(), barH);

            g.setColour(barBack);
            g.fillRoundedRectangle(bar, 3.0f);

            if (ui->hasFile)
            {
                const float w = bar.getWidth() * juce::jlimit(0.0f, 1.0f, ui->phase);
                if (w > 1.0f)
                {
                    g.setColour(pulseColour);
                    g.fillRoundedRectangle(juce::Rectangle<float>(bar.getX(), bar.getY(), w, barH), 3.0f);
                }
            }
        }

        // 3) Knob labels
        auto drawKnobLabel = [&g](juce::Slider& slider, const juce::String& text)
            {
                auto layout = slider.getLookAndFeel().getSliderLayout(slider);
                auto knobBounds = layout.sliderBounds.toFloat();

                if (!knobBounds.isEmpty())
                    knobBounds += slider.getPosition().toFloat();
                else
                    knobBounds = slider.getBounds().toFloat();

                const auto centre = knobBounds.getCentre();
                const auto size = juce::jmin(knobBounds.getWidth(), knobBounds.getHeight());
                auto square = juce::Rectangle<float>(size, size).withCentre(centre);
                auto labelArea = square.reduced(size * 0.28f, size * 0.28f);

                g.setFont(createBoldFont(13.0f));
                const bool enabled = slider.isEnabled();
                const auto textColour = enabled ? juce::Colours::white.withAlpha(0.90f)
                                                : juce::Colours::lightgrey.withAlpha(0.65f);
                const auto shadowColour = enabled ? juce::Colours::black.withAlpha(0.55f)
                                                  : juce::Colours::black.withAlpha(0.25f);
                g.setColour(shadowColour);
                g.drawFittedText(text, labelArea.translated(0, 1).toNearestInt(), juce::Justification::centred, 1);

                g.setColour(textColour);
                g.drawFittedText(text, labelArea.toNearestInt(), juce::Justification::centred, 1);
            };

        if (ui->showCountLabel)
            drawKnobLabel(ui->count, "COUNT");
        if (ui->showRateLabel)
            drawKnobLabel(ui->rate, "RATE");
        drawKnobLabel(ui->gain, "VOL");
        drawKnobLabel(ui->decay, "DECAY");
    }
}

void SlotMachineAudioProcessorEditor::resetUiToDefaultStateForStandalone()
{
    doResetAll(false);
}

void SlotMachineAudioProcessorEditor::resized()
{
    auto slotScaled = [this](int value) { return scaleDimension(value); };

    const int margin = 12;
    auto bounds = getLocalBounds();
    auto area = bounds.reduced(margin);

    if (logoImage.isValid())
    {
        const int imageWidth = logoImage.getWidth();
        const int imageHeight = logoImage.getHeight();

        if (imageWidth > 0 && imageHeight > 0)
        {
            const float logoScaleFactor = 1.3f;
            const float maxWidth = 160.0f * logoScaleFactor;
            const float maxHeight = 32.0f * logoScaleFactor;
            const float baseScale = juce::jmin(maxWidth / (float)imageWidth,
                                               maxHeight / (float)imageHeight,
                                               1.0f);
            const float scale = juce::jmax(0.0f, baseScale * kBannerScaleMultiplier);
            const int width = juce::jmax(1, juce::roundToInt((float)imageWidth * scale));
            const int height = juce::jmax(1, juce::roundToInt((float)imageHeight * scale));

            logoBounds = { bounds.getX() + margin + 15, bounds.getY() + 4 + 17, width, height };
        }
        else
        {
            logoBounds = {};
        }
    }
    else
    {
        logoBounds = {};
    }

    int masterButtonsBottom = 0;

    // Master row
    {
        const int sliderHeight = 32;
        const int sliderGap = 12;
        const int buttonHeight = 36;
        const int buttonInsetY = 8;
        const int bottomMargin = 6 + kMasterControlsYOffset;
        const int buttonRowGap = 4;
        const int buttonRowsHeight = buttonInsetY * 2 + buttonHeight * 3 + buttonRowGap * 2;
        const int masterHeight = juce::jmax(sliderHeight, buttonRowsHeight) + bottomMargin;

        auto top = area.removeFromTop(masterHeight);

        auto labelArea = top.removeFromLeft(170);
        auto sliderArea = top.removeFromLeft(420);

        auto buttonArea = top.reduced(10, buttonInsetY);
        const int numButtons = 10; // Start/Save/Load/Reset Loop/Reset UI/Initialize/Options/Export MIDI/Export Audio/Visualizer (Tutorial, User Manual, About, Lock, and Unlock below)
        const int bw = buttonArea.getWidth() / numButtons;
        const int bh = buttonHeight;
        const int firstRowY = buttonArea.getY();
        const int secondRowY = firstRowY + bh + buttonRowGap;
        const int thirdRowY = secondRowY + bh + buttonRowGap;
        const int buttonBottom = thirdRowY + bh;
        masterButtonsBottom = buttonBottom;

        auto labelBounds = labelArea.reduced(8, 0);

        auto sliderBounds = sliderArea.withTrimmedRight(10).withHeight(sliderHeight);
        sliderBounds.setBottom(buttonBottom - sliderGap + kMasterControlsYOffset);
        sliderBounds.translate(0, -20);
        sliderBounds.setLeft(labelBounds.getRight());
        sliderBounds.translate(-35, 0);
        sliderBounds.setWidth(juce::jmax(0, sliderBounds.getWidth() - 55));
        masterBPM.setBounds(sliderBounds);

        const auto textBoxBottom = sliderBounds.getY() + masterBPM.getTextBoxHeight();
        const auto labelHeight = (int)std::ceil(masterLabel.getFont().getHeight());
        const auto labelOffset = 20;

        labelBounds.setHeight(labelHeight);
        labelBounds.setBottom(textBoxBottom + labelOffset);
        labelBounds.translate(0, kMasterLabelExtraYOffset + 20);
        masterLabel.setBounds(labelBounds);

        const int barLeft = buttonArea.getX();
        const int tutorialLeft = buttonArea.getX() + 7 * bw;
        const int barRight = tutorialLeft;
        const int barWidth = juce::jmax(0, barRight - barLeft);
        masterBarBounds = juce::Rectangle<int>(barLeft,
                                               secondRowY,
                                               barWidth,
                                               bh);
        const int binWidth = juce::jmax(1, masterBarBounds.getWidth());
        minCols.resize((size_t) binWidth);
        maxCols.resize((size_t) binWidth);

        auto firstRowBounds = [&](int index)
        {
            return juce::Rectangle<int>(buttonArea.getX() + index * bw, firstRowY, bw, bh);
        };

        auto secondRowBounds = [&](int index)
        {
            return juce::Rectangle<int>(buttonArea.getX() + index * bw, secondRowY, bw, bh);
        };

        auto thirdRowBounds = [&](int index)
        {
            return juce::Rectangle<int>(buttonArea.getX() + index * bw, thirdRowY, bw, bh);
        };

        btnStart.setBounds(firstRowBounds(0));
        btnSave.setBounds(firstRowBounds(1));
        btnLoad.setBounds(firstRowBounds(2));
        btnResetLoop.setBounds(firstRowBounds(3));
        btnReset.setBounds(firstRowBounds(4));
        btnInitialize.setBounds(firstRowBounds(5));
        btnOptions.setBounds(firstRowBounds(6));
        btnExportMidi.setBounds(firstRowBounds(7));
        btnExportAudio.setBounds(firstRowBounds(8));
        btnVisualizer.setBounds(firstRowBounds(9));
        btnTutorial.setBounds(secondRowBounds(7));
        btnUserManual.setBounds(secondRowBounds(8));
        btnAbout.setBounds(secondRowBounds(9));
        btnLock.setBounds(thirdRowBounds(8));
        btnUnlock.setBounds(thirdRowBounds(9));

        const int refH = btnTutorial.getHeight();
        if (refH > 0)
            appLF.setCornerRadius(juce::jlimit(2.0f, 12.0f, 0.25f * (float) refH));
        else
            appLF.setCornerRadius(6.0f);
    }

    const int areaTopAfterMaster = area.getY();
    int masterSectionBottom = masterButtonsBottom;
    masterSectionBottom = juce::jmax(masterSectionBottom, masterLabel.getBottom());
    masterSectionBottom = juce::jmax(masterSectionBottom, masterBPM.getBottom());
    const int tabsLift = 110;

    auto tabsRow = area.removeFromTop(36);
    tabsRow.translate(0, -tabsLift);
    auto warningArea = tabsRow.removeFromRight(220).reduced(10, 4);
    patternWarningLabel.setBounds(warningArea);
    auto patternTabsBounds = tabsRow.reduced(0, 4);
    patternTabs.setBounds(patternTabsBounds);

    {
        auto lockBounds = btnLock.getBounds();
        lockBounds.setY(patternTabsBounds.getY());
        btnLock.setBounds(lockBounds);

        auto unlockBounds = btnUnlock.getBounds();
        unlockBounds.setY(patternTabsBounds.getY());
        btnUnlock.setBounds(unlockBounds);

        updateLockIconPositions();
    }

    area.translate(0, -tabsLift);
    area.setBottom(bounds.getBottom() - margin);

    // Grid layout (4 columns by as many rows as needed)
    const int columns = 4;
    const int rows = juce::jmax(1, (kNumSlots + columns - 1) / columns);
    const int gridX = area.getX(), gridY = area.getY();
    const int gridW = area.getWidth(), gridH = area.getHeight();
    const int cellW = gridW / columns, cellH = gridH / rows;
    const int pad = slotScaled(6), innerPad = slotScaled(12);

    for (int i = 0; i < kNumSlots; ++i)
    {
        if (!slots[(size_t)i]) continue;

        const int row = i / columns, col = i % columns;
        const int x = gridX + col * cellW + pad;
        const int y = gridY + row * cellH + pad;
        const int w = cellW - 2 * pad;
        const int h = cellH - 2 * pad;

        auto& ui = *slots[(size_t)i];
        ui.group.setBounds(x, y, w, h);

        const int raiseAmount = juce::jmax(1, scaleDimension(4));
        int contentYOffset = 0;

        // JUCE 7 exposes the group title label, but older builds do not.  Use the
        // accessor when it's available and fall back to pushing the slot contents
        // down slightly when it isn't so the visual spacing still improves.
        if (auto* titleLabel = getSlotTitleLabelIfAvailable(ui.group))
        {
            auto labelBounds = titleLabel->getBounds();
            labelBounds.translate(0, ui.titleLabelRaiseOffset);
            labelBounds.translate(0, -raiseAmount);
            titleLabel->setBounds(labelBounds);

            ui.titleLabelRaiseOffset = raiseAmount;
        }
        else
        {
            ui.titleLabelRaiseOffset = 0;
            contentYOffset = raiseAmount;
        }

        const int ix = x + innerPad;
        const int iy = y + innerPad + contentYOffset;
        const int iw = w - 2 * innerPad;

        const int fileRowH = slotScaled(28);
        const int loadW = scaleDimension(110);  // your existing Load width
        const int clearW = scaleDimension(24);   // tiny X button
        const int gap = slotScaled(4);

        ui.fileBtn.setBounds(ix, iy, loadW, fileRowH);
        ui.clearBtn.setBounds(ix + loadW + gap, iy, clearW, fileRowH);

        // filename label fills the rest
        const int labelX = ix + loadW + gap + clearW + gap;
        const int labelW = juce::jmax(0, iw - (labelX - ix));
        ui.fileLabel.setBounds(labelX, iy, labelW, fileRowH);


        const int knobsY = iy + fileRowH + slotScaled(4);
        const int knobsH = slotScaled(112);
        const int knobSpacing = slotScaled(12);
        const int knobCount = 3;
        const int knobW = juce::jmax(8, (iw - knobSpacing * (knobCount - 1)) / knobCount);
        const int totalKnobWidth = knobW * knobCount + knobSpacing * (knobCount - 1);
        const int knobStartX = ix + juce::jmax(0, (iw - totalKnobWidth) / 2);

        const juce::Rectangle<int> rateBounds(knobStartX, knobsY, knobW, knobsH);
        const juce::Rectangle<int> gainBounds(rateBounds.withX(rateBounds.getRight() + knobSpacing));
        const juce::Rectangle<int> decayBounds(gainBounds.withX(gainBounds.getRight() + knobSpacing));

        ui.count.setBounds(rateBounds);
        ui.rate.setBounds(rateBounds);
        ui.gain.setBounds(gainBounds);
        ui.decay.setBounds(decayBounds);

        const int buttonW = scaleDimension(60);
        const int buttonH = slotScaled(22);
        const int labelHeight = slotScaled(16);
        const int labelGapY = slotScaled(2);
        const int midiComboW = scaleDimensionWithMax(80, 0.95f);
        const int midiComboH = scaleDimensionWithMax(22, 0.95f);
        const int controlBlockHeight = juce::jmax(midiComboH, buttonH + labelGapY + labelHeight);

        const int knobsBottom = knobsY + knobsH;
        const int progressInset = juce::roundToInt(8.0f * slotScale);
        const int progressHeight = juce::roundToInt(8.0f * slotScale);
        const int progressTop = ui.group.getBottom() - progressInset - progressHeight;
        const int availableSpace = juce::jmax(0, progressTop - knobsBottom);
        int togglesY = knobsBottom + juce::jmax(0, (availableSpace - controlBlockHeight) / 2);

        const int absoluteMaxToggleY = progressTop - controlBlockHeight;
        const int minToggleY = knobsBottom;

        if (absoluteMaxToggleY >= minToggleY)
        {
            const int safetyMargin = juce::roundToInt(4.0f * slotScale);
            const int usableMaxToggleY = juce::jmax(minToggleY, absoluteMaxToggleY - safetyMargin);
            togglesY = juce::jlimit(minToggleY, usableMaxToggleY, togglesY);
        }
        else
        {
            togglesY = absoluteMaxToggleY;
        }

        const int timingCentreX = ui.rate.getBounds().getCentreX();
        const int gainCentreX = ui.gain.getBounds().getCentreX();
        const int decayCentreX = ui.decay.getBounds().getCentreX();

        const int midiY = togglesY + (controlBlockHeight - midiComboH) / 2;
        ui.midiChannel.setBounds(timingCentreX - midiComboW / 2, midiY, midiComboW, midiComboH);

        const int buttonY = togglesY;
        const int labelY = buttonY + buttonH + labelGapY;
        ui.muteBtn.setBounds(gainCentreX - buttonW / 2, buttonY, buttonW, buttonH);
        ui.muteLabel.setBounds(gainCentreX - buttonW / 2, labelY, buttonW, labelHeight);

        ui.soloBtn.setBounds(decayCentreX - buttonW / 2, buttonY, buttonW, buttonH);
        ui.soloLabel.setBounds(decayCentreX - buttonW / 2, labelY, buttonW, labelHeight);
    }

    // VST3: Ensure lock overlay covers entire editor if visible
    if (vst3LockOverlay && vst3LockOverlay->isVisible())
    {
        vst3LockOverlay->setBounds(getLocalBounds());
        vst3LockOverlay->toFront(false);
    }
}

int SlotMachineAudioProcessorEditor::scaleDimension(int base) const
{
    if (base == 0)
        return 0;

    const float scaled = (float)base * slotScale;
    if (base > 0)
        return juce::jmax(1, juce::roundToInt(scaled));

    return juce::jmin(-1, juce::roundToInt(scaled));
}

int SlotMachineAudioProcessorEditor::scaleDimensionWithMax(int base, float maxScale) const
{
    if (base == 0)
        return 0;

    const float appliedScale = juce::jmax(0.0f, juce::jmin(slotScale, maxScale));
    const float scaled = (float)base * appliedScale;
    if (base > 0)
        return juce::jmax(1, juce::roundToInt(scaled));

    return juce::jmin(-1, juce::roundToInt(scaled));
}

void SlotMachineAudioProcessorEditor::refreshSizeForSlotScale()
{
    constexpr int slotColumns = 4;
    const int slotRows = juce::jmax(1, (kNumSlots + slotColumns - 1) / slotColumns);
    const int slotRowHeight = scaleDimension(220);
    const int chromeHeight = 200 + kMasterControlsYOffset;
    const int newHeight = chromeHeight + slotRows * slotRowHeight;
    const int currentWidth = juce::jmax(1, getWidth());
    setSize(currentWidth, newHeight);
}

void SlotMachineAudioProcessorEditor::applySlotScale(float newScale)
{
    const float clamped = juce::jlimit(0.75f, 1.0f, newScale);
    if (std::abs(clamped - slotScale) < 0.0001f)
        return;

    slotScale = clamped;
    refreshSizeForSlotScale();
    resized();
    repaint();
}

void SlotMachineAudioProcessorEditor::handleSlotFileSelection(int slotIndex, const juce::File& file)
{
    if (!file.existsAsFile())
        return;

    embeddedSlotResourceNames[(size_t)slotIndex].clear();

    const bool loaded = processor.loadSampleForSlot(slotIndex, file, startToggle.getToggleState());

    juce::Array<int> failed;
    if (!loaded)
        failed.add(slotIndex);

    refreshSlotFileLabels(failed);
    showPatternWarning(failed);
    saveCurrentPattern();
    repaint();
}

juce::String SlotMachineAudioProcessorEditor::defaultPatternNameForIndex(int index) const
{
    juce::String result;
    int value = index;

    do
    {
        const int remainder = value % 26;
        result = juce::String::charToString((juce::juce_wchar)('A' + remainder)) + result;
        value = value / 26 - 1;
    }
    while (value >= 0);

    return result;
}

void SlotMachineAudioProcessorEditor::initialisePatterns()
{
    patternsTree = processor.getPatternsTree();
    const int count = patternsTree.getNumChildren();
    if (count <= 0)
        return;

    currentPatternIndex = juce::jlimit(0, count - 1, processor.getCurrentPatternIndex());
    processor.setCurrentPatternIndex(currentPatternIndex);
    activePatternIndex = currentPatternIndex;

    refreshPatternTabs();
    applyPattern(currentPatternIndex, true, false);
}

void SlotMachineAudioProcessorEditor::saveCurrentPattern()
{
    if (!patternsTree.isValid())
        patternsTree = processor.getPatternsTree();

    const int count = patternsTree.getNumChildren();
    if (count <= 0)
        return;

    currentPatternIndex = juce::jlimit(0, count - 1, currentPatternIndex);
    activePatternIndex = juce::jlimit(0, count - 1, activePatternIndex);

    if (auto pattern = patternsTree.getChild(activePatternIndex); pattern.isValid())
        processor.storeCurrentStateInPattern(pattern);
}

void SlotMachineAudioProcessorEditor::refreshPatternTabs()
{
    if (!patternsTree.isValid())
        patternsTree = processor.getPatternsTree();

    if (patternsTree.getNumChildren() == 0)
    {
        auto pattern = processor.createDefaultPatternTree(defaultPatternNameForIndex(0));
        patternsTree.addChild(pattern, -1, nullptr);
        currentPatternIndex = 0;
        processor.setCurrentPatternIndex(currentPatternIndex);
        activePatternIndex = currentPatternIndex;
    }

    juce::StringArray names;
    const int count = patternsTree.getNumChildren();
    currentPatternIndex = juce::jlimit(0, juce::jmax(0, count - 1), currentPatternIndex);
    activePatternIndex = juce::jlimit(0, juce::jmax(0, count - 1), activePatternIndex);
    names.ensureStorageAllocated(count);

    for (int i = 0; i < count; ++i)
    {
        auto child = patternsTree.getChild(i);
        ensurePatternRepeatProperty(child);
        juce::String name = child.getProperty(kPatternNameProperty).toString();
        if (name.isEmpty())
        {
            name = defaultPatternNameForIndex(i);
            child.setProperty(kPatternNameProperty, name, nullptr);
        }
        names.add(name);
    }

    if (names.isEmpty())
        names.add(defaultPatternNameForIndex(0));

    patternTabs.setTabs(names);
    patternTabs.setCurrentIndex(currentPatternIndex);
}

void SlotMachineAudioProcessorEditor::applyPatternTreeNow(const juce::ValueTree& pattern, bool allowTailRelease)
{
    juce::Array<int> failedSlots;
    processor.applyPatternTree(pattern, &failedSlots, allowTailRelease);

    refreshSlotFileLabels(failedSlots);
    showPatternWarning(failedSlots);
    repaint();

    if (patternsTree.isValid())
    {
        const int count = patternsTree.getNumChildren();
        activePatternIndex = juce::jlimit(0, juce::jmax(0, count - 1), currentPatternIndex);
    }
    else
    {
        activePatternIndex = currentPatternIndex;
    }
}

void SlotMachineAudioProcessorEditor::applyPattern(int index, bool updateTabs, bool saveExisting, bool deferIfRunning)
{
    if (!patternsTree.isValid())
        patternsTree = processor.getPatternsTree();

    const int count = patternsTree.getNumChildren();
    if (count <= 0)
        return;

    index = juce::jlimit(0, count - 1, index);

    if (saveExisting)
        saveCurrentPattern();

    auto pattern = patternsTree.getChild(index);
    if (!pattern.isValid())
        return;

    const bool isRunning = startToggle.getToggleState();
    const bool shouldDefer = deferIfRunning && isRunning;

    currentPatternIndex = index;
    processor.setCurrentPatternIndex(currentPatternIndex);

    if (shouldDefer)
    {
        pendingPatternTree = pattern;
        patternSwitchPending = true;
#if JUCE_DEBUG
        DBG("ED: APPLY_PATTERN_DEFER index=" << index
            << " isRunning=" << (int)isRunning
            << " playActive=" << (int)playThroughActive
            << " playPat=" << playThroughCurrentPattern
            << " cyclesRem=" << playThroughCyclesRemaining);
#endif
        return;
    }

    if (updateTabs)
        patternTabs.setCurrentIndex(currentPatternIndex);

    patternSwitchPending = false;
    pendingPatternTree = {};
    applyPatternTreeNow(pattern, isRunning);
}

void SlotMachineAudioProcessorEditor::showPatternWarning(const juce::Array<int>& failedSlots)
{
    // Hide error messages for sample loading failures
    patternWarningLabel.setVisible(false);
    patternWarningCounter = 0;
}

void SlotMachineAudioProcessorEditor::refreshSlotFileLabels(const juce::Array<int>& failedSlots)
{
    auto getFilePropertyId = [](int slotIndex)
    {
        return juce::String("slot") + juce::String(slotIndex + 1) + "_File";
    };

    juce::ValueTree activePattern;
    if (patternsTree.isValid() && juce::isPositiveAndBelow(currentPatternIndex, patternsTree.getNumChildren()))
        activePattern = patternsTree.getChild(currentPatternIndex);

    for (int i = 0; i < kNumSlots; ++i)
    {
        auto* ui = slots[(size_t)i].get();
        if (!ui)
            continue;

        const bool hasSample = processor.slotHasSample(i);
        juce::String path = processor.getSlotFilePath(i);
        const bool hadProcessorPath = path.isNotEmpty(); // Track if processor had a path
        if (embeddedSlotResourceNames[(size_t)i].isNotEmpty()
            && path.isNotEmpty()
            && path != embeddedSlotResourceNames[(size_t)i])
        {
            embeddedSlotResourceNames[(size_t)i].clear();
        }

        if (!hasSample)
            embeddedSlotResourceNames[(size_t)i].clear();

        juce::String label = "No file";
        juce::String embeddedResource = embeddedSlotResourceNames[(size_t)i];

        if (embeddedResource.isEmpty() && path.isNotEmpty())
        {
            int resourceSize = 0;
            if (const void* data = BinaryData::getNamedResource(path.toRawUTF8(), resourceSize))
            {
                if (resourceSize > 0)
                {
                    embeddedResource = path;
                    embeddedSlotResourceNames[(size_t)i] = embeddedResource;
                }
            }
        }

        bool isFailed = false;

        if (embeddedResource.isNotEmpty())
        {
            juce::String display = getEmbeddedSampleDisplay(embeddedResource);
            if (display.isEmpty())
                display = embeddedResource;

            const bool failed = failedSlots.contains(i) || !hasSample;
            isFailed = failed;
            label = failed ? display + " (missing)" : display + " (embedded)";
        }
        else
        {
            if (path.isEmpty())
            {
                const auto propertyId = getFilePropertyId(i);

                if (activePattern.isValid())
                    path = activePattern.getProperty(propertyId).toString();

                if (path.isEmpty())
                {
                    const auto stateValue = apvts.state.getProperty(propertyId);
                    if (!stateValue.isVoid())
                        path = stateValue.toString();
                }
            }

            if (path.isNotEmpty())
            {
                const bool failed = failedSlots.contains(i);
                juce::File f(path);
                const bool exists = f.existsAsFile();
                // Only mark as failed if in failedSlots OR (processor had path AND file doesn't exist)
                isFailed = failed || (!exists && hadProcessorPath);
                const juce::String fileName = f.getFileName().isNotEmpty() ? f.getFileName() : path;

                if (failed || !exists)
                    label = fileName + " (missing)";
                else
                    label = fileName;
            }
        }

        ui->hasFile = hasSample;
        ui->fileLabel.setText(label, juce::dontSendNotification);

        // Set color to red for failed samples, default color otherwise
        if (isFailed)
            ui->fileLabel.setColour(juce::Label::textColourId, juce::Colours::red);
        else
            ui->fileLabel.setColour(juce::Label::textColourId, juce::Label().findColour(juce::Label::textColourId));
    }
}

void SlotMachineAudioProcessorEditor::handlePatternContextMenu(const juce::MouseEvent& e)
{
    if (fileDialogActive)
        return;

    if (!patternsTree.isValid())
        patternsTree = processor.getPatternsTree();

    juce::PopupMenu menu;
    const int patternCount = patternsTree.getNumChildren();

    menu.addItem(1, "New Pattern");
    menu.addItem(2, "Duplicate Pattern", patternCount > 0);
    menu.addItem(3, "Rename Pattern", patternCount > 0);
    menu.addItem(4, "Delete Pattern", patternCount > 1);
    menu.addItem(5, "Import Saved Pattern", patternCount > 0);

    menu.addSeparator();
    menu.addItem(7, "Play Through", patternCount > 0 && !playThroughActive);
    menu.addItem(9, "Play Through from current Tab", patternCount > 0 && !playThroughActive);

    int repeatValue = 0;
    if (patternCount > 0 && juce::isPositiveAndBelow(currentPatternIndex, patternCount))
    {
        auto pattern = patternsTree.getChild(currentPatternIndex);
        repeatValue = ensurePatternRepeatProperty(pattern);
    }

    menu.addItem(6, "Repeat = " + juce::String(repeatValue), patternCount > 0);
    menu.addItem(8, "Loop Playthrough = " + juce::String(loopPlaythroughEnabled ? "True" : "False"));

    auto options = juce::PopupMenu::Options().withTargetComponent(&patternTabs);

    auto targetArea = patternTabs.getScreenBounds();
    targetArea.setX(e.getScreenX());
    targetArea.setWidth(1);

    options = options.withTargetScreenArea(targetArea);

    menu.showMenuAsync(options,
        [this](int result)
        {
            switch (result)
            {
            case 1: createNewPattern(); break;
            case 2: duplicateCurrentPattern(); break;
            case 3: renameCurrentPattern(); break;
            case 4: deleteCurrentPattern(); break;
            case 5: importPatternFromFile(); break;
            case 6: editCurrentPatternRepeat(); break;
            case 7: beginPlayThrough(); break;
            case 9: beginPlayThroughFromCurrentTab(); break;
            case 8: showLoopPlaythroughDialog(); break;
            default: break;
            }
        });
}

void SlotMachineAudioProcessorEditor::setSlotControlsFrozen(bool shouldFreeze)
{
    const bool enable = !shouldFreeze;
    const int timingMode = Opt::getInt(apvts, "optTimingMode", 1);
    const bool beatsPerCycleMode = (timingMode == 1);
    const bool countEnabled = enable && beatsPerCycleMode;
    const bool rateEnabled = enable && !beatsPerCycleMode;

    const float otherAlpha = enable ? 1.0f : 0.35f;

    for (auto& slot : slots)
    {
        if (!slot)
            continue;

        slot->fileBtn.setEnabled(enable);
        slot->clearBtn.setEnabled(enable);
        slot->muteBtn.setEnabled(enable);
        slot->soloBtn.setEnabled(enable);
        slot->muteLabel.setEnabled(enable);
        slot->soloLabel.setEnabled(enable);
        slot->gain.setEnabled(enable);
        slot->decay.setEnabled(enable);
        slot->midiChannel.setEnabled(enable);

        if (slot->count.isEnabled() != countEnabled)
            slot->count.setEnabled(countEnabled);
        if (slot->rate.isEnabled() != rateEnabled)
            slot->rate.setEnabled(rateEnabled);

        slot->count.setAlpha(countEnabled ? 1.0f : 0.35f);
        slot->rate.setAlpha(rateEnabled ? 1.0f : 0.35f);

        slot->fileBtn.setAlpha(otherAlpha);
        slot->clearBtn.setAlpha(otherAlpha);
        slot->muteBtn.setAlpha(otherAlpha);
        slot->soloBtn.setAlpha(otherAlpha);
        slot->gain.setAlpha(otherAlpha);
        slot->decay.setAlpha(otherAlpha);
        slot->midiChannel.setAlpha(otherAlpha);
        slot->muteLabel.setAlpha(otherAlpha);
        slot->soloLabel.setAlpha(otherAlpha);
    }
}

void SlotMachineAudioProcessorEditor::beginPlayThrough()
{
    beginPlayThroughAtIndex(0);
}

void SlotMachineAudioProcessorEditor::beginPlayThroughFromCurrentTab()
{
    beginPlayThroughAtIndex(currentPatternIndex);
}

void SlotMachineAudioProcessorEditor::beginPlayThroughAtIndex(int startIndex)
{
    if (playThroughActive)
        return;

    if (!patternsTree.isValid())
        patternsTree = processor.getPatternsTree();

    int patternCount = patternsTree.getNumChildren();
    if (patternCount <= 0)
        return;

    saveCurrentPattern();
    patternsTree = processor.getPatternsTree();
    patternCount = patternsTree.getNumChildren();
    if (patternCount <= 0)
        return;

    if (startToggle.getToggleState())
        setMasterRun(false);

    playThroughActive = true;
    playThroughInitialPattern = currentPatternIndex;
    playThroughCurrentPattern = juce::jlimit(0, patternCount - 1, startIndex);
    playThroughNextPatternPreloaded = false; // Option 5: Initialize pre-load flag

    setSlotControlsFrozen(true);

    auto pattern = patternsTree.getChild(playThroughCurrentPattern);
    playThroughCyclesRemaining = computePatternPlayThroughCycles(pattern);
    playThroughSkipNextWrap = true;
    playThroughWrapGuardPhase = lastPhase;

    processor.resetAllPhases(true);
    applyPattern(playThroughCurrentPattern, true, false, false);
    setMasterRun(true);
}

void SlotMachineAudioProcessorEditor::advancePlayThrough(bool applyImmediately)
{
    if (!playThroughActive)
        return;

    if (!patternsTree.isValid())
        patternsTree = processor.getPatternsTree();

    const int patternCount = patternsTree.getNumChildren();
    if (patternCount <= 0)
    {
        finishPlayThrough(true, true);
        return;
    }

#if JUCE_DEBUG
    DBG("ED: ADVANCE_PLAYTHROUGH fromWrap=" << (int)applyImmediately
        << " cyclesRem(before)=" << playThroughCyclesRemaining
        << " currentPat=" << playThroughCurrentPattern
        << " patternCount=" << patternCount
        << " preloaded=" << (int)playThroughNextPatternPreloaded);
#endif

    if (playThroughCurrentPattern < 0 || playThroughCurrentPattern >= patternCount)
        playThroughCurrentPattern = juce::jlimit(0, patternCount - 1, playThroughCurrentPattern);

#if JUCE_DEBUG
    const int cyclesBeforeDecrement = playThroughCyclesRemaining;
#endif
    if (playThroughCyclesRemaining > 0)
        --playThroughCyclesRemaining;

#if JUCE_DEBUG
    if (cyclesBeforeDecrement > 0)
    {
        DBG("ED: ADVANCE_PLAYTHROUGH cyclesRem(afterDecrement)=" << playThroughCyclesRemaining);
    }
#endif

    if (playThroughCyclesRemaining > 0)
        return;

    const bool isRunning = startToggle.getToggleState();
    const bool deferIfRunning = isRunning && !applyImmediately;

    const int nextIndex = playThroughCurrentPattern + 1;
    if (nextIndex < patternCount)
    {
        const int oldPattern = playThroughCurrentPattern;
        playThroughCurrentPattern = nextIndex;
        auto nextPattern = patternsTree.getChild(playThroughCurrentPattern);
        playThroughCyclesRemaining = computePatternPlayThroughCycles(nextPattern);
#if JUCE_DEBUG
        DBG("ED: ADVANCE_PLAYTHROUGH NEXT oldPat=" << oldPattern
            << " newPat=" << playThroughCurrentPattern
            << " cyclesRem=" << playThroughCyclesRemaining
            << " defer=" << (int)deferIfRunning
            << " fromWrap=" << (int)applyImmediately);
#endif
        applyPattern(playThroughCurrentPattern, true, false, deferIfRunning);
    }
    else
    {
        if (loopPlaythroughEnabled)
        {
            const int oldPattern = playThroughCurrentPattern;
            playThroughCurrentPattern = 0;
            auto nextPattern = patternsTree.getChild(playThroughCurrentPattern);
            playThroughCyclesRemaining = computePatternPlayThroughCycles(nextPattern);
#if JUCE_DEBUG
            DBG("ED: ADVANCE_PLAYTHROUGH LOOP oldPat=" << oldPattern
                << " newPat=" << playThroughCurrentPattern
                << " cyclesRem=" << playThroughCyclesRemaining
                << " defer=" << (int)deferIfRunning
                << " fromWrap=" << (int)applyImmediately);
#endif
            applyPattern(playThroughCurrentPattern, true, false, deferIfRunning);
        }
        else
        {
#if JUCE_DEBUG
            DBG("ED: ADVANCE_PLAYTHROUGH STOP fromWrap=" << (int)applyImmediately
                << " cyclesRem=" << playThroughCyclesRemaining
                << " currentPat=" << playThroughCurrentPattern);
#endif
            finishPlayThrough(true, true);
        }
    }
}

void SlotMachineAudioProcessorEditor::finishPlayThrough(bool restorePattern, bool stopTransport)
{
    if (!playThroughActive)
        return;

    playThroughActive = false;
    setSlotControlsFrozen(false);

    const bool isRunning = startToggle.getToggleState();

    const int restoreIndex = playThroughInitialPattern;
    playThroughInitialPattern = -1;
    playThroughCurrentPattern = -1;
    playThroughCyclesRemaining = 0;
    playThroughSkipNextWrap = false;
    playThroughWrapGuardPhase = 0.0f;
    playThroughNextPatternPreloaded = false; // Option 5: Reset pre-load flag

    // --- NEW ORDER: stop transport first (no more hits) ---
    if (stopTransport && startToggle.getToggleState())
        setMasterRun(false);

    // --- DEFER the restore so the last tails aren’t muted by the start tab’s solo/mute ---
    if (restorePattern)
    {
        if (!patternsTree.isValid())
            patternsTree = processor.getPatternsTree();

        const int patternCount = patternsTree.getNumChildren();
        if (patternCount > 0 && restoreIndex >= 0)
        {
            const int targetIndex = juce::jlimit(0, patternCount - 1, restoreIndex);

            auto safe = juce::Component::SafePointer<SlotMachineAudioProcessorEditor>(this);
            constexpr int kTailGraceMs = 200; // let the tail finish audibly
            juce::Timer::callAfterDelay(kTailGraceMs, [safe, targetIndex]()
                {
                    if (safe != nullptr)
                        safe->applyPattern(targetIndex, true, false, false);
                });
        }
    }
}

void SlotMachineAudioProcessorEditor::reorderPatterns(int fromIndex, int toIndex)
{
    if (fromIndex == toIndex)
        return;

    if (!patternsTree.isValid())
        patternsTree = processor.getPatternsTree();

    const int count = patternsTree.getNumChildren();
    if (count <= 0)
        return;

    if (!juce::isPositiveAndBelow(fromIndex, count) || !juce::isPositiveAndBelow(toIndex, count))
        return;

    saveCurrentPattern();

    auto child = patternsTree.getChild(fromIndex);
    if (!child.isValid())
        return;

    patternsTree.removeChild(fromIndex, nullptr);

    const int insertIndex = juce::jlimit(0, patternsTree.getNumChildren(), toIndex);
    if (insertIndex >= patternsTree.getNumChildren())
        patternsTree.addChild(child, -1, nullptr);
    else
        patternsTree.addChild(child, insertIndex, nullptr);

    const int newIndex = patternsTree.indexOf(child);

    auto remapIndex = [fromIndex, toIndex, newIndex](int index)
    {
        if (index == fromIndex)
            return newIndex;

        if (fromIndex < toIndex)
        {
            if (index > fromIndex && index <= toIndex)
                return index - 1;
        }
        else if (fromIndex > toIndex)
        {
            if (index < fromIndex && index >= toIndex)
                return index + 1;
        }

        return index;
    };

    currentPatternIndex = remapIndex(currentPatternIndex);
    activePatternIndex = remapIndex(activePatternIndex);

    const int maxIndex = juce::jmax(0, patternsTree.getNumChildren() - 1);
    currentPatternIndex = juce::jlimit(0, maxIndex, currentPatternIndex);
    activePatternIndex = juce::jlimit(0, maxIndex, activePatternIndex);

    processor.setCurrentPatternIndex(currentPatternIndex);
    refreshPatternTabs();
}

void SlotMachineAudioProcessorEditor::createNewPattern()
{
    if (!patternsTree.isValid())
        patternsTree = processor.getPatternsTree();

    saveCurrentPattern();

    const int newIndex = patternsTree.getNumChildren();
    auto pattern = processor.createDefaultPatternTree(defaultPatternNameForIndex(newIndex));
    patternsTree.addChild(pattern, -1, nullptr);

    currentPatternIndex = newIndex;
    processor.setCurrentPatternIndex(currentPatternIndex);
    refreshPatternTabs();
    applyPattern(currentPatternIndex, true, false);
}

void SlotMachineAudioProcessorEditor::duplicateCurrentPattern()
{
    if (!patternsTree.isValid())
        patternsTree = processor.getPatternsTree();

    const int count = patternsTree.getNumChildren();
    if (count <= 0)
        return;

    saveCurrentPattern();

    const int newIndex = count;
    auto copy = patternsTree.getChild(currentPatternIndex).createCopy();
    copy.setProperty(kPatternNameProperty, defaultPatternNameForIndex(newIndex), nullptr);
    patternsTree.addChild(copy, -1, nullptr);

    currentPatternIndex = newIndex;
    processor.setCurrentPatternIndex(currentPatternIndex);
    activePatternIndex = currentPatternIndex;
    refreshPatternTabs();
    patternTabs.setCurrentIndex(currentPatternIndex);
    juce::Array<int> none;
    refreshSlotFileLabels(none);
    showPatternWarning(none);
}

void SlotMachineAudioProcessorEditor::renameCurrentPattern()
{
    if (!patternsTree.isValid())
        patternsTree = processor.getPatternsTree();

    const int count = patternsTree.getNumChildren();
    if (count <= 0)
        return;

    auto pattern = patternsTree.getChild(currentPatternIndex);
    juce::String currentName = pattern.getProperty(kPatternNameProperty).toString();
    if (currentName.isEmpty())
        currentName = defaultPatternNameForIndex(currentPatternIndex);

    auto component = std::make_unique<RenamePatternComponent>(currentName,
        [this, pattern, patternIndex = currentPatternIndex](bool accepted, juce::String newName) mutable
        {
            if (!accepted)
                return;

            newName = newName.trim();
            if (newName.isEmpty())
                newName = defaultPatternNameForIndex(patternIndex);

            pattern.setProperty(kPatternNameProperty, newName, nullptr);
            refreshPatternTabs();
            patternTabs.setCurrentIndex(patternIndex);
        });

    auto* componentPtr = component.get();
    componentPtr->setSize(260, 110);

    auto tabBounds = patternTabs.getTabBoundsInParent(currentPatternIndex);
    juce::Rectangle<int> anchorArea(0, 0, 1, 1);
    anchorArea.setCentre(tabBounds.getCentreX(), tabBounds.getBottom());

    auto& callout = juce::CallOutBox::launchAsynchronously(std::move(component),
        anchorArea, this);
    componentPtr->setCallOutBox(callout);
    componentPtr->focusEditor();
}

void SlotMachineAudioProcessorEditor::editCurrentPatternRepeat()
{
    if (!patternsTree.isValid())
        patternsTree = processor.getPatternsTree();

    const int count = patternsTree.getNumChildren();
    if (count <= 0)
        return;

    auto pattern = patternsTree.getChild(currentPatternIndex);
    if (!pattern.isValid())
        return;

    const int currentRepeat = ensurePatternRepeatProperty(pattern);

    auto component = std::make_unique<EditPatternRepeatComponent>(currentRepeat,
        [pattern](bool accepted, int newRepeat) mutable
        {
            if (!accepted)
                return;

            newRepeat = juce::jmax(0, newRepeat);
            pattern.setProperty(kPatternRepeatProperty, newRepeat, nullptr);
        });

    auto* componentPtr = component.get();
    componentPtr->setSize(260, 110);

    auto tabBounds = patternTabs.getTabBoundsInParent(currentPatternIndex);
    juce::Rectangle<int> anchorArea(0, 0, 1, 1);
    anchorArea.setCentre(tabBounds.getCentreX(), tabBounds.getBottom());

    auto& callout = juce::CallOutBox::launchAsynchronously(std::move(component),
        anchorArea, this);
    componentPtr->setCallOutBox(callout);
    componentPtr->focusEditor();
}

void SlotMachineAudioProcessorEditor::showLoopPlaythroughDialog()
{
    if (auto* existing = loopPlaythroughDialog.getComponent())
    {
        if (auto* peer = existing->getPeer())
            peer->toFront(true);
        else
            existing->grabKeyboardFocus();
        return;
    }

    auto editorSafe = juce::Component::SafePointer<SlotMachineAudioProcessorEditor>(this);
    auto dialogContent = std::make_unique<LoopPlaythroughDialog>(
        loopPlaythroughEnabled,
        [editorSafe](bool shouldLoop)
        {
            if (editorSafe != nullptr)
            {
                editorSafe->loopPlaythroughDialog = nullptr;
                editorSafe->setLoopPlaythroughEnabled(shouldLoop);
            }
        },
        [editorSafe]()
        {
            if (editorSafe != nullptr)
                editorSafe->loopPlaythroughDialog = nullptr;
        });

    dialogContent->setSize(360, 220);

    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = "Loop Playthrough";
    options.dialogBackgroundColour = getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId);
    options.content.setOwned(dialogContent.release());
    options.componentToCentreAround = this;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;

    if (auto* window = options.launchAsync())
    {
        loopPlaythroughDialog = window;
        window->centreAroundComponent(this, window->getWidth(), window->getHeight());
    }
}

void SlotMachineAudioProcessorEditor::setLoopPlaythroughEnabled(bool shouldLoop)
{
    loopPlaythroughEnabled = shouldLoop;
}

void SlotMachineAudioProcessorEditor::importPatternFromFile()
{
    if (fileDialogActive)
        return;

    auto chooser = std::make_shared<juce::FileChooser>("Import saved pattern", juce::File(), "*.xml");

    fileDialogActive = true;
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this, chooser](const juce::FileChooser& fc) mutable
        {
            juce::ignoreUnused(chooser);
            fileDialogActive = false;

            auto file = fc.getResult();
            if (!file.existsAsFile())
                return;

            handlePatternImportFile(file);
        });
}

void SlotMachineAudioProcessorEditor::handlePatternImportFile(const juce::File& file)
{
    std::unique_ptr<juce::XmlElement> xml(juce::XmlDocument::parse(file));
    if (xml == nullptr)
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
            "Import Pattern",
            "Unable to read the selected file.");
        return;
    }

    auto state = juce::ValueTree::fromXml(*xml);
    if (!state.isValid())
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
            "Import Pattern",
            "The selected file does not contain a valid pattern.");
        return;
    }

    static const juce::Identifier kPatternsNodeId("patterns");
    static const juce::Identifier kPatternNodeType("pattern");

    juce::Array<juce::ValueTree> importedPatterns;

    if (auto patternsNode = state.getChildWithName(kPatternsNodeId); patternsNode.isValid())
    {
        for (int i = 0; i < patternsNode.getNumChildren(); ++i)
        {
            auto child = patternsNode.getChild(i);
            if (child.hasType(kPatternNodeType))
                importedPatterns.add(child);
        }
    }
    else if (state.hasType(kPatternsNodeId))
    {
        for (int i = 0; i < state.getNumChildren(); ++i)
        {
            auto child = state.getChild(i);
            if (child.hasType(kPatternNodeType))
                importedPatterns.add(child);
        }
    }
    else if (state.hasType(kPatternNodeType))
    {
        importedPatterns.add(state);
    }

    if (importedPatterns.isEmpty())
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
            "Import Pattern",
            "No saved patterns were found in the selected file.");
        return;
    }

    if (importedPatterns.size() == 1)
    {
        importPatternIntoCurrentTab(importedPatterns.getReference(0));
        return;
    }

    auto options = std::make_shared<std::vector<juce::ValueTree>>();
    options->reserve((size_t)importedPatterns.size());

    juce::PopupMenu selectionMenu;

    for (int i = 0; i < importedPatterns.size(); ++i)
    {
        auto pattern = importedPatterns.getReference(i);
        options->push_back(pattern);

        juce::String name = pattern.getProperty(kPatternNameProperty).toString();
        if (name.isEmpty())
            name = "Pattern " + juce::String(i + 1);

        selectionMenu.addItem(i + 1, name);
    }

    auto menuOptions = juce::PopupMenu::Options();

    auto tabBounds = patternTabs.getTabBoundsInParent(currentPatternIndex);
    auto screenArea = tabBounds;
    screenArea.setPosition(localPointToGlobal(tabBounds.getPosition()));

    if (screenArea.getWidth() > 0 && screenArea.getHeight() > 0)
        menuOptions = menuOptions.withTargetScreenArea(screenArea);
    else
        menuOptions = menuOptions.withTargetComponent(&patternTabs);

    selectionMenu.showMenuAsync(menuOptions,
        [this, options](int result)
        {
            if (result <= 0)
                return;

            const int index = result - 1;
            if (!juce::isPositiveAndBelow(index, (int)options->size()))
                return;

            importPatternIntoCurrentTab((*options)[(size_t)index]);
        });
}

void SlotMachineAudioProcessorEditor::importPatternIntoCurrentTab(const juce::ValueTree& patternTree)
{
    if (!patternTree.isValid())
        return;

    if (!patternsTree.isValid())
        patternsTree = processor.getPatternsTree();

    if (!patternsTree.isValid())
        return;

    if (patternsTree.getNumChildren() == 0)
        refreshPatternTabs();

    const int patternCount = patternsTree.getNumChildren();
    if (patternCount <= 0)
        return;

    currentPatternIndex = juce::jlimit(0, patternCount - 1, currentPatternIndex);

    saveCurrentPattern();

    auto currentPattern = patternsTree.getChild(currentPatternIndex);
    if (!currentPattern.isValid())
        return;

    juce::String currentName = currentPattern.getProperty(kPatternNameProperty).toString();
    if (currentName.isEmpty())
        currentName = defaultPatternNameForIndex(currentPatternIndex);

    auto importedCopy = patternTree.createCopy();
    ensurePatternRepeatProperty(importedCopy);
    importedCopy.setProperty(kPatternNameProperty, currentName, nullptr);

    patternsTree.removeChild(currentPatternIndex, nullptr);
    patternsTree.addChild(importedCopy, currentPatternIndex, nullptr);

    processor.setCurrentPatternIndex(currentPatternIndex);
    refreshPatternTabs();
    patternTabs.setCurrentIndex(currentPatternIndex);

    applyPatternTreeNow(importedCopy, startToggle.getToggleState());
    saveCurrentPattern();
}

void SlotMachineAudioProcessorEditor::clearExtraPatternsBeforeLoad()
{
    if (!patternsTree.isValid())
        patternsTree = processor.getPatternsTree();

    while (patternsTree.getNumChildren() > 1)
        patternsTree.removeChild(patternsTree.getNumChildren() - 1, nullptr);

    const int patternCount = patternsTree.getNumChildren();
    if (patternCount > 0)
    {
        currentPatternIndex = juce::jlimit(0, patternCount - 1, currentPatternIndex);
        processor.setCurrentPatternIndex(currentPatternIndex);
        activePatternIndex = currentPatternIndex;
    }
    else
    {
        currentPatternIndex = 0;
        processor.setCurrentPatternIndex(currentPatternIndex);
        activePatternIndex = currentPatternIndex;
    }

    refreshPatternTabs();
    patternTabs.setCurrentIndex(currentPatternIndex);
    juce::Array<int> none;
    refreshSlotFileLabels(none);
    showPatternWarning(none);
}

void SlotMachineAudioProcessorEditor::resetPatternsToSingleDefault()
{
    if (!patternsTree.isValid())
        patternsTree = processor.getPatternsTree();

    if (!patternsTree.isValid())
        return;

    while (patternsTree.getNumChildren() > 1)
        patternsTree.removeChild(patternsTree.getNumChildren() - 1, nullptr);

    if (patternsTree.getNumChildren() == 0)
    {
        auto pattern = processor.createDefaultPatternTree("A");
        patternsTree.addChild(pattern, -1, nullptr);
    }

    if (auto pattern = patternsTree.getChild(0); pattern.isValid())
        pattern.setProperty(kPatternNameProperty, "A", nullptr);

    currentPatternIndex = 0;
    processor.setCurrentPatternIndex(currentPatternIndex);
    activePatternIndex = currentPatternIndex;

    refreshPatternTabs();
    patternTabs.setCurrentIndex(currentPatternIndex);
}

void SlotMachineAudioProcessorEditor::buttonClicked(juce::Button* b)
{
    if (b == &btnStart)
    {
        const bool nextState = !startToggle.getToggleState();
        setMasterRun(nextState);
        return;
    }

    if (b == &btnUnlock)
    {
        showUnlockDialog();
        return;
    }

    if (b == &btnLock)
    {
#if JUCE_WINDOWS
        if (!clearLicenseFromRegistry())
        {
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                "Lock Slot Machine",
                "Unable to remove the saved license information from the registry.");
        }
#else
        clearLicenseFromRegistry();
#endif

        storedFirstName.clear();
        storedLastName.clear();
        storedEmail.clear();
        storedLicenseKey.clear();

        setUnlocked(false);
        return;
    }

    if (!isUnlocked && (b == &btnLoad || b == &btnExportAudio || b == &btnExportMidi))
    {
        showTrialModeDialog();
        return;
    }

    // >>> Load sequence: Stop -> Load (initialization happens after confirming the preset)
    if (b == &btnLoad)
    {
        setMasterRun(false);
        doLoadPreset();
        return;
    }

    if (b == &btnSave) { doSavePreset();       return; }
    if (b == &btnResetLoop) { resetLoopTransport(); return; }
    if (b == &btnInitialize)
    {
        auto safeThis = juce::Component::SafePointer<SlotMachineAudioProcessorEditor>(this);

        confirmWarningWithContinue(this,
            "Initialize",
            "Initializing will clear all slots for the selected Tab. Would you like to Continue?",
            [safeThis]()
            {
                if (auto* editor = safeThis.getComponent())
                    editor->doResetAll();
            });

        return;
    }
    if (b == &btnReset)
    {
        auto safeThis = juce::Component::SafePointer<SlotMachineAudioProcessorEditor>(this);

        confirmWarningWithContinue(this,
            "Reset UI",
            "Resetting UI will delete all but the main Tab, and clear all slots. Would you like to Continue?",
            [safeThis]()
            {
                if (auto* editor = safeThis.getComponent())
                {
                    editor->resetPatternsToSingleDefault();
                    editor->doResetAll();
                }
            });

        return;
    }
    if (b == &btnOptions) { showOptionsDialog();  return; }

    if (b == &btnVisualizer)
    {
        setShowVisualizerParam(true);
        if (!vizWindow)
        {
            openVisualizerWindow();
            lastShowVisualizer = true;
        }
        return;
    }

    if (b == &btnTutorial)
    {
        openTutorialVideo();
        return;
    }

    if (b == &btnUserManual)
    {
        openUserManual();
        return;
    }

    if (b == &btnAbout)
    {
        if (auto* existing = aboutDialog.getComponent())
        {
            if (auto* peer = existing->getPeer())
                peer->toFront(true);
            else
                existing->grabKeyboardFocus();
            return;
        }

        auto deactivateCallback = [this]()
        {
            // Confirm deactivation
            bool shouldDeactivate = juce::NativeMessageBox::showOkCancelBox(
                juce::AlertWindow::WarningIcon,
                "Deactivate License",
                "Are you sure you want to deactivate this license on this computer?\n\n"
                "This will free up an activation slot for use on another computer.",
                nullptr,
                nullptr);

            if (!shouldDeactivate)
                return;

            // Get current license info
            juce::String licenseKey = storedLicenseKey;
            juce::String instanceId = InstanceIdentifier::getOrCreateInstanceID();

            // Call API to deactivate
            bool apiSuccess = LemonSqueezyAPI::deactivateLicense(licenseKey, instanceId);

            // Clear local cache regardless of API result
#if JUCE_WINDOWS
            if (!clearLicenseFromRegistry())
            {
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                    "Deactivate License",
                    "Unable to remove the saved license information from the registry.");
            }
#else
            clearLicenseFromRegistry();
#endif

            // Clear instance ID
            InstanceIdentifier::clearInstanceID();

            // Clear stored credentials
            storedFirstName.clear();
            storedLastName.clear();
            storedEmail.clear();
            storedLicenseKey.clear();

            // Update UI
            setUnlocked(false);

            // Close about dialog
            if (auto* dialog = aboutDialog.getComponent())
            {
                if (auto* window = dynamic_cast<juce::DialogWindow*>(dialog))
                    window->exitModalState(0);
            }

            // Show result message
            if (apiSuccess)
            {
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon,
                    "Deactivate License",
                    "License successfully deactivated on this computer.");
            }
            else
            {
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                    "Deactivate License",
                    "License deactivated locally, but there was an error communicating with the server.\n\n"
                    "The license has been removed from this computer, but you may need to contact support "
                    "to free up the activation slot.");
            }
        };

        auto checkForUpdatesCallback = [this]()
        {
            // Use a weak reference to safely access 'this' in the callback
            juce::Component::SafePointer<SlotMachineAudioProcessorEditor> safeThis(this);

            // Force check ignores "declined recently" state
            updateChecker.checkForUpdatesAsync([safeThis](UpdateChecker::CheckResult result,
                                                          const UpdateChecker::VersionInfo& latestVersion)
            {
                if (safeThis == nullptr)
                    return;

                switch (result)
                {
                    case UpdateChecker::CheckResult::UpdateAvailable:
                    {
                        UpdateChecker::showUpdateDialog(
                            safeThis.getComponent(),
                            latestVersion,
                            []()
                            {
                                UpdateChecker::launchUpdaterAndTerminate();
                            },
                            []()
                            {
                                UpdateChecker::recordUpdateDeclined();
                            });
                        break;
                    }

                    case UpdateChecker::CheckResult::UpToDate:
                        juce::AlertWindow::showMessageBoxAsync(
                            juce::AlertWindow::InfoIcon,
                            "Check for Updates",
                            "You're running the latest version of S.L.O.T. Machine.",
                            "OK");
                        break;

                    case UpdateChecker::CheckResult::NetworkError:
                        juce::AlertWindow::showMessageBoxAsync(
                            juce::AlertWindow::WarningIcon,
                            "Check for Updates",
                            "Could not check for updates. Please check your internet connection and try again.",
                            "OK");
                        break;

                    case UpdateChecker::CheckResult::ParseError:
                        juce::AlertWindow::showMessageBoxAsync(
                            juce::AlertWindow::WarningIcon,
                            "Check for Updates",
                            "Could not check for updates. Please try again later.",
                            "OK");
                        break;

                    case UpdateChecker::CheckResult::DeclinedRecently:
                        // This shouldn't happen with forceCheck=true, but handle it anyway
                        break;
                }
            }, true);  // forceCheck = true
        };

        auto aboutContent = std::make_unique<AboutComponent>(getRegistrationDisplayName(), deactivateCallback, isUnlocked, checkForUpdatesCallback);
        aboutContent->setSize(420, 460);

        juce::DialogWindow::LaunchOptions options;
        options.dialogTitle = "About Slot Machine";
        options.dialogBackgroundColour = getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId);
        options.content.setOwned(aboutContent.release());
        options.componentToCentreAround = this;
        options.escapeKeyTriggersCloseButton = true;
        options.useNativeTitleBar = true;
        options.resizable = false;

        if (auto* window = options.launchAsync())
        {
            aboutDialog = window;
            window->centreAroundComponent(this, window->getWidth(), window->getHeight());
        }

        return;
    }

    // ===== Export Audio (user-selected cycles) =====
    if (b == &btnExportAudio)
    {
        promptForExportCycles("Export Audio", 1,
            [this](int cycles, bool exportPlaythrough)
            {
                beginAudioExportWithCycles(cycles, exportPlaythrough);
            },
            true,
            lastAudioExportPlaythrough);
        return;
    }

    // ===== Export MIDI (user-selected cycles) =====
    if (b == &btnExportMidi)
    {
        promptForExportCycles("Export MIDI", 1,
            [this](int cycles, bool exportPlaythrough)
            {
                beginMidiExportWithCycles(cycles, exportPlaythrough);
            },
            true,
            lastMidiExportPlaythrough);
        return;
    }

    // Slot events (file load / clear / solo exclusivity)
    for (int i = 0; i < kNumSlots; ++i)
    {
        auto* ui = slots[(size_t)i].get();
        if (!ui) continue;

        if (b == &ui->clearBtn)
        {
            processor.clearSlot(i, startToggle.getToggleState());
            embeddedSlotResourceNames[(size_t)i].clear();
            ui->hasFile = false;
            ui->fileLabel.setText("No file", juce::dontSendNotification);
            ui->fileLabel.setColour(juce::Label::textColourId, juce::Label().findColour(juce::Label::textColourId));
            ui->glow = 0.0f;
            ui->phase = 0.0f;
            ui->lastHitCounter = 0;
            repaint();
            return;
        }

        // Per-slot file load
        if (b == &ui->fileBtn)
        {
            if (suppressNextFileBtnClick)
            {
                suppressNextFileBtnClick = false;
                return;
            }

            auto chooser = std::make_shared<juce::FileChooser>(
                "Select audio file", juce::File(), "*.wav;*.aiff;*.aif;*.flac");

            fileDialogActive = true;
            chooser->launchAsync(juce::FileBrowserComponent::openMode
                | juce::FileBrowserComponent::canSelectFiles,
                [this, i, chooser](const juce::FileChooser& fc) mutable
                {
                    juce::ignoreUnused(chooser);
                    fileDialogActive = false;
                    handleSlotFileSelection(i, fc.getResult());
                });
            return;
        }

        // Mute/Solo interactions
        if (b == &ui->muteBtn)
        {
            const bool nowOn = ui->muteBtn.getToggleState();
            if (nowOn)
            {
                if (auto* soloParam = dynamic_cast<juce::AudioParameterBool*>(
                        apvts.getParameter("slot" + juce::String(i + 1) + "_Solo")))
                {
                    soloParam->beginChangeGesture();
                    *soloParam = false;
                    soloParam->endChangeGesture();
                }
            }
            return;
        }

        if (b == &ui->soloBtn)
        {
            const bool nowOn = ui->soloBtn.getToggleState();
            if (nowOn)
            {
                if (auto* muteParam = dynamic_cast<juce::AudioParameterBool*>(
                        apvts.getParameter("slot" + juce::String(i + 1) + "_Mute")))
                {
                    muteParam->beginChangeGesture();
                    *muteParam = false;
                    muteParam->endChangeGesture();
                }

                for (int j = 0; j < kNumSlots; ++j)
                {
                    if (j == i) continue;
                    if (auto* soloParam = dynamic_cast<juce::AudioParameterBool*>(
                            apvts.getParameter("slot" + juce::String(j + 1) + "_Solo")))
                    {
                        soloParam->beginChangeGesture();
                        *soloParam = false;
                        soloParam->endChangeGesture();
                    }
                }
            }
            return;
        }
    }
}

void SlotMachineAudioProcessorEditor::showOptionsDialog()
{
    auto content = std::make_unique<OptionsComponent>(apvts, [this](float newScale)
        {
            applySlotScale(newScale);
        });
    content->setSize(640, 720);

    juce::DialogWindow::LaunchOptions opt;
    opt.dialogTitle = "Options";
    opt.content.setOwned(content.release());
    opt.escapeKeyTriggersCloseButton = true;
    opt.useNativeTitleBar = true;
    opt.componentToCentreAround = this;
    opt.resizable = true;
    opt.dialogBackgroundColour = juce::Colours::black;

    if (auto* dlg = opt.launchAsync())
        dlg->setResizeLimits(480, 720, 2000, 1368);
}

void SlotMachineAudioProcessorEditor::promptForExportCycles(const juce::String& dialogTitle,
    int defaultCycles,
    std::function<void(int, bool)> onConfirm,
    bool includePlaythroughOptions,
    bool initialPlaythroughSelection)
{
    if (auto* existing = exportCyclesPromptWindow.getComponent())
    {
        if (auto* peer = existing->getPeer())
            peer->toFront(true);
        else
            existing->grabKeyboardFocus();
        return;
    }

    auto editorSafe = juce::Component::SafePointer<SlotMachineAudioProcessorEditor>(this);
    auto dialogContent = std::make_unique<ExportCyclesDialog>(
        defaultCycles,
        includePlaythroughOptions,
        initialPlaythroughSelection,
        [editorSafe, handler = std::move(onConfirm)](int cycles, bool exportPlaythrough) mutable
        {
            if (editorSafe != nullptr)
            {
                editorSafe->exportCyclesPromptWindow = nullptr;

                if (handler)
                    handler(cycles, exportPlaythrough);
            }
        },
        [editorSafe]()
        {
            if (editorSafe != nullptr)
                editorSafe->exportCyclesPromptWindow = nullptr;
        });

    const int dialogHeight = includePlaythroughOptions ? 240 : 180;
    dialogContent->setSize(360, dialogHeight);

    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = dialogTitle;
    options.dialogBackgroundColour = getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId);
    options.content.setOwned(dialogContent.release());
    options.componentToCentreAround = this;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;

    if (auto* window = options.launchAsync())
    {
        exportCyclesPromptWindow = window;
        window->centreAroundComponent(this, window->getWidth(), window->getHeight());
    }
}

void SlotMachineAudioProcessorEditor::beginAudioExportWithCycles(int cyclesRequested, bool exportPlaythrough)
{
    if (cyclesRequested <= 0)
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon,
            "Export Audio",
            "Please enter a positive whole number of cycles.");
        return;
    }

    lastAudioExportPlaythrough = exportPlaythrough;
    apvts.state.setProperty(kLastAudioExportPlaythroughProperty, exportPlaythrough, nullptr);

    juce::String chooserTitle;
    if (exportPlaythrough)
    {
        const juce::String cycleLabel = juce::String(cyclesRequested) + (cyclesRequested == 1 ? " cycle" : " cycles");
        chooserTitle = "Export Tab Play Through (" + cycleLabel + ") audio file";
    }
    else
    {
        chooserTitle = "Export " + juce::String(cyclesRequested) + "-cycle audio file";
    }

    auto chooser = std::make_shared<juce::FileChooser>(
        chooserTitle,
        juce::File(),
        "*.wav");

    fileDialogActive = true;
    chooser->launchAsync(juce::FileBrowserComponent::saveMode
                             | juce::FileBrowserComponent::canSelectFiles,
        [this, chooser, cyclesRequested, exportPlaythrough](const juce::FileChooser& fc) mutable
        {
            juce::ignoreUnused(chooser);
            fileDialogActive = false;

            auto file = fc.getResult();
            if (file.getFullPathName().isEmpty())
                return;

            if (!file.hasFileExtension(".wav"))
                file = file.withFileExtension(".wav");

            // Lambda to perform the actual export
            auto performExport = [this, file, cyclesRequested, exportPlaythrough]()
            {
                juce::String error;
                const bool ok = exportPlaythrough
                    ? processor.exportAudioPlaythroughCycles(file, cyclesRequested, error)
                    : processor.exportAudioCycles(file, cyclesRequested, error);

                if (ok)
                {
                    juce::AlertWindow::showMessageBoxAsync(
                        juce::AlertWindow::InfoIcon,
                        "Export Audio",
                        "Saved: " + file.getFullPathName());
                }
                else
                {
                    if (error.isEmpty())
                        error = "Unable to export audio.";

                    juce::AlertWindow::showMessageBoxAsync(
                        juce::AlertWindow::WarningIcon,
                        "Export Audio",
                        error);
                }
            };

            // Check if file exists and ask user if they want to overwrite
            if (file.exists())
            {
                auto options = juce::MessageBoxOptions()
                    .withIconType(juce::MessageBoxIconType::QuestionIcon)
                    .withTitle("File Already Exists")
                    .withMessage("The file \"" + file.getFileName() + "\" already exists. Do you want to overwrite it?")
                    .withButton("Overwrite")
                    .withButton("Cancel");

                juce::AlertWindow::showAsync(options,
                    juce::ModalCallbackFunction::create([performExport](int result)
                    {
                        if (result == 1) // User clicked "Overwrite"
                            performExport();
                        // Otherwise user clicked "Cancel" or closed dialog - do nothing
                    }));
            }
            else
            {
                // File doesn't exist, proceed with export
                performExport();
            }
        });
}

void SlotMachineAudioProcessorEditor::beginMidiExportWithCycles(int cyclesRequested, bool exportPlaythrough)
{
    if (cyclesRequested <= 0)
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon,
            "Export MIDI",
            "Please enter a positive whole number of cycles.");
        return;
    }

    lastMidiExportPlaythrough = exportPlaythrough;
    apvts.state.setProperty(kLastMidiExportPlaythroughProperty, exportPlaythrough, nullptr);

    juce::String chooserTitle;
    if (exportPlaythrough)
    {
        const juce::String cycleLabel = juce::String(cyclesRequested) + (cyclesRequested == 1 ? " cycle" : " cycles");
        chooserTitle = "Export Tab Play Through (" + cycleLabel + ") MIDI file";
    }
    else
    {
        chooserTitle = "Export " + juce::String(cyclesRequested) + "-cycle MIDI file";
    }

    auto chooser = std::make_shared<juce::FileChooser>(
        chooserTitle,
        juce::File(),
        "*.mid");

    fileDialogActive = true;
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
        [this, chooser, cyclesRequested, exportPlaythrough](const juce::FileChooser& fc) mutable
        {
            juce::ignoreUnused(chooser);
            fileDialogActive = false;

            auto file = fc.getResult();
            if (file.getFullPathName().isEmpty())
                return;

            if (!file.hasFileExtension(".mid"))
                file = file.withFileExtension(".mid");

            // Lambda to perform the actual export
            auto performExport = [this, file, cyclesRequested, exportPlaythrough]()
            {
                juce::String error;
                const bool ok = exportPlaythrough
                    ? processor.exportMidiPlaythroughCycles(file, cyclesRequested, error)
                    : processor.exportMidiCycles(file, cyclesRequested, error);

                if (ok)
                {
                    juce::AlertWindow::showMessageBoxAsync(
                        juce::AlertWindow::InfoIcon,
                        "Export MIDI",
                        "Saved: " + file.getFullPathName());
                }
                else
                {
                    if (error.isEmpty())
                        error = "Unable to export MIDI.";

                    juce::AlertWindow::showMessageBoxAsync(
                        juce::AlertWindow::WarningIcon,
                        "Export MIDI",
                        error);
                }
            };

            // Check if file exists and ask user if they want to overwrite
            if (file.exists())
            {
                auto options = juce::MessageBoxOptions()
                    .withIconType(juce::MessageBoxIconType::QuestionIcon)
                    .withTitle("File Already Exists")
                    .withMessage("The file \"" + file.getFileName() + "\" already exists. Do you want to overwrite it?")
                    .withButton("Overwrite")
                    .withButton("Cancel");

                juce::AlertWindow::showAsync(options,
                    juce::ModalCallbackFunction::create([performExport](int result)
                    {
                        if (result == 1) // User clicked "Overwrite"
                            performExport();
                        // Otherwise user clicked "Cancel" or closed dialog - do nothing
                    }));
            }
            else
            {
                // File doesn't exist, proceed with export
                performExport();
            }
        });
}

void SlotMachineAudioProcessorEditor::setShowVisualizerParam(bool shouldShow)
{
    if (auto* param = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter("optShowVisualizer")))
    {
        if (param->get() == shouldShow)
            return;

        param->beginChangeGesture();
        *param = shouldShow;
        param->endChangeGesture();
        saveOptionsToDisk(apvts);
    }
}

void SlotMachineAudioProcessorEditor::openVisualizerWindow()
{
    if (vizWindow)
    {
        vizWindow->setAlwaysOnTop(true);
        if (auto* peer = vizWindow->getPeer())
            peer->toFront(true);
        else
            vizWindow->toFront(true);
        return;
    }

    vizComponent = std::make_unique<PolyrhythmVizComponent>(processor, apvts);
    auto* componentPtr = vizComponent.release();

    auto window = std::make_unique<VisualizerWindow>(*this);
    window->setContentOwned(componentPtr, true);

    // Connect right-click handler to show context menu
    componentPtr->onRightClick = [windowPtr = window.get()]()
    {
        windowPtr->showContextMenu();
    };

    window->centreWithSize(640, 640);
    window->setAlwaysOnTop(true);
    window->setVisible(true);
    window->toFront(true);

    vizWindow = std::move(window);
}

void SlotMachineAudioProcessorEditor::closeVisualizerWindow()
{
    if (vizWindow)
    {
        vizWindow->setVisible(false);
        vizWindow.reset();
    }

    vizComponent.reset();
}

void SlotMachineAudioProcessorEditor::handleVisualizerWindowCloseRequest()
{
    closeVisualizerWindow();
    lastShowVisualizer = false;
    setShowVisualizerParam(false);
}

void SlotMachineAudioProcessorEditor::openTutorialVideo()
{
    const auto executable = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
    auto tutorialFile = executable.getParentDirectory().getChildFile("tutorialslotmachine.mp4");

    if (!tutorialFile.existsAsFile())
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon,
            "Tutorial",
            "Unable to find tutorialslotmachine.mp4 next to the application.");
        return;
    }

    if (!tutorialFile.startAsProcess())
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon,
            "Tutorial",
            "Unable to open tutorialslotmachine.mp4 with the system default player.");
    }
}

void SlotMachineAudioProcessorEditor::openUserManual()
{
    auto manualFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getChildFile("SlotMachine-UserManual.html");

    manualFile.getParentDirectory().createDirectory();

    if (!manualFile.replaceWithData(BinaryData::SlotMachineUserManual_html,
                                    static_cast<size_t>(BinaryData::SlotMachineUserManual_htmlSize)))
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon,
            "User Manual",
            "Unable to access the embedded User Manual.");
        return;
    }

#if JUCE_WEB_BROWSER
    if (juce::URL(manualFile).launchInDefaultBrowser())
        return;
#endif

    if (!manualFile.startAsProcess())
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon,
            "User Manual",
            "Unable to open the embedded User Manual in a browser.");
    }
}

void SlotMachineAudioProcessorEditor::setMasterRun(bool shouldRun)
{
    if (auto* runParam = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter("masterRun")))
    {
        if (runParam->get() != shouldRun)
        {
            runParam->beginChangeGesture();
            *runParam = shouldRun;
            runParam->endChangeGesture();
        }
    }

    startToggle.setToggleState(shouldRun, juce::dontSendNotification);
    patternTabs.setReorderingEnabled(!shouldRun);

    const auto glowColour = Opt::rgbParam(apvts, "optGlowColor", 0x6994FC, 1.0f);
    const auto pulseColour = Opt::rgbParam(apvts, "optPulseColor", 0xD5CFEE, 1.0f);
    const float glowAlpha = Opt::getFloat(apvts, "optGlowAlpha", 0.431f);
    const float glowWidth = Opt::getFloat(apvts, "optGlowWidth", 1.34f);

    updateStartButtonVisuals(shouldRun, glowColour, pulseColour, glowAlpha, glowWidth);
    cachedStartGlowColour = glowColour;
    cachedStartPulseColour = pulseColour;
    cachedStartGlowAlpha = glowAlpha;
    cachedStartGlowWidth = glowWidth;
    lastStartToggleState = shouldRun;

    if (!shouldRun && playThroughActive)
        finishPlayThrough(true, false);

    if (shouldRun)
    {
        animateStartButton(glowColour, pulseColour);
    }
    else
    {
        startButtonAnimPhase = 0.0f;
    }
}

void SlotMachineAudioProcessorEditor::updateStartButtonVisuals(bool shouldRun,
    juce::Colour glowColour,
    juce::Colour pulseColour,
    float glowAlpha,
    float glowWidth)
{
    juce::ignoreUnused(pulseColour);

    if (shouldRun)
    {
        if (btnStart.getButtonText() != "Stop")
            btnStart.setButtonText("Stop");

        if (startButtonGlowEnabled)
        {
            btnStart.setComponentEffect(nullptr);
            startButtonGlowEnabled = false;
        }

        auto baseColour = glowColour.withAlpha(juce::jlimit(0.4f, 1.0f, glowAlpha + 0.45f));
        btnStart.setColour(juce::TextButton::textColourOffId, baseColour);
        btnStart.setColour(juce::TextButton::textColourOnId, baseColour);
    }
    else
    {
        if (btnStart.getButtonText() != "Start")
            btnStart.setButtonText("Start");

        const float glowRadius = juce::jlimit(6.0f, 42.0f, glowWidth * 3.0f);
        const float glowIntensity = juce::jlimit(0.2f, 0.95f, glowAlpha + 0.35f);
        startButtonGlow.setGlowProperties(glowRadius, glowColour.withAlpha(glowIntensity));

        if (!startButtonGlowEnabled)
        {
            btnStart.setComponentEffect(&startButtonGlow);
            startButtonGlowEnabled = true;
        }

        auto textColour = glowColour.withAlpha(juce::jlimit(0.6f, 1.0f, glowAlpha + 0.55f));
        btnStart.setColour(juce::TextButton::textColourOffId, textColour);
        btnStart.setColour(juce::TextButton::textColourOnId, textColour);
    }

    btnStart.repaint();
}

void SlotMachineAudioProcessorEditor::animateStartButton(juce::Colour glowColour, juce::Colour pulseColour)
{
    startButtonAnimPhase += 0.04f;
    if (startButtonAnimPhase > juce::MathConstants<float>::twoPi)
        startButtonAnimPhase -= juce::MathConstants<float>::twoPi;

    const float mix = 0.5f * (1.0f + std::sin(startButtonAnimPhase));
    auto blended = glowColour.interpolatedWith(pulseColour, mix);

    const float brightness = 0.55f + 0.45f * (0.5f * (1.0f + std::sin(startButtonAnimPhase * 0.75f + juce::MathConstants<float>::halfPi)));
    blended = blended.withAlpha(juce::jlimit(0.35f, 1.0f, brightness));

    btnStart.setColour(juce::TextButton::textColourOffId, blended);
    btnStart.setColour(juce::TextButton::textColourOnId, blended);
    btnStart.repaint();
}

void SlotMachineAudioProcessorEditor::updateSliderKnobColours(juce::Colour pulseColour)
{
    if (pulseColour == cachedKnobPulseColour)
        return;

    masterBPM.setColour(juce::Slider::thumbColourId, pulseColour);

    for (auto& slot : slots)
    {
        if (!slot)
            continue;

        slot->count.setColour(juce::Slider::thumbColourId, pulseColour);
        slot->rate.setColour(juce::Slider::thumbColourId, pulseColour);
        slot->gain.setColour(juce::Slider::thumbColourId, pulseColour);
        slot->decay.setColour(juce::Slider::thumbColourId, pulseColour);
    }

    cachedKnobPulseColour = pulseColour;
}

// ===== Preset Save / Load / Initialize =====
void SlotMachineAudioProcessorEditor::doSavePreset()
{
    saveCurrentPattern();
    auto chooser = std::make_shared<juce::FileChooser>("Save preset", juce::File(), "*.xml");
    fileDialogActive = true;
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
        [this, chooser](const juce::FileChooser& fc) mutable
        {
            juce::ignoreUnused(chooser);
            fileDialogActive = false;
            auto f = fc.getResult();
            if (f.getFullPathName().isEmpty())
                return;

            if (!f.hasFileExtension(".xml"))
                f = f.withFileExtension(".xml");

            // Lambda to perform the actual save
            auto performSave = [this, f]()
            {
                auto state = processor.copyStateWithVersion();
                if (auto xml = state.createXml())
                    xml->writeTo(f);
            };

            // Check if file exists and ask user if they want to overwrite
            if (f.exists())
            {
                auto options = juce::MessageBoxOptions()
                    .withIconType(juce::MessageBoxIconType::QuestionIcon)
                    .withTitle("File Already Exists")
                    .withMessage("The file \"" + f.getFileName() + "\" already exists. Do you want to overwrite it?")
                    .withButton("Overwrite")
                    .withButton("Cancel");

                juce::AlertWindow::showAsync(options,
                    juce::ModalCallbackFunction::create([performSave](int result)
                    {
                        if (result == 1) // User clicked "Overwrite"
                            performSave();
                        // Otherwise user clicked "Cancel" or closed dialog - do nothing
                    }));
            }
            else
            {
                // File doesn't exist, proceed with save
                performSave();
            }
        });
}

void SlotMachineAudioProcessorEditor::doLoadPreset()
{
    saveCurrentPattern();
    auto chooser = std::make_shared<juce::FileChooser>("Load preset", juce::File(), "*.xml");
    fileDialogActive = true;
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this, chooser](const juce::FileChooser& fc) mutable
        {
            juce::ignoreUnused(chooser);
            fileDialogActive = false;
            auto f = fc.getResult();
            if (!f.existsAsFile()) return;

            std::unique_ptr<juce::XmlElement> xml(juce::XmlDocument::parse(f));
            if (xml == nullptr)
            {
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                    "Load Preset",
                    "The file is corrupt or unable to be read.");
                return;
            }

            auto newState = juce::ValueTree::fromXml(*xml);
            if (!newState.isValid())
            {
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                    "Load Preset",
                    "The selected file does not contain a valid preset configuration.");
                return;
            }

            doResetAll();

            clearExtraPatternsBeforeLoad();

            const float previousSlotScaleParam = Opt::getFloat(apvts, "optSlotScale", slotScale);
            const bool presetHasSlotScale = newState.hasProperty("optSlotScale");

            // Load all parameters + properties into the APVTS
            apvts.replaceState(newState);
            processor.upgradeLegacySlotParameters();

            if (!presetHasSlotScale)
            {
                if (auto* slotScaleParam = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("optSlotScale")))
                {
                    slotScaleParam->beginChangeGesture();
                    slotScaleParam->setValueNotifyingHost(slotScaleParam->range.convertTo0to1(previousSlotScaleParam));
                    slotScaleParam->endChangeGesture();
                    applySlotScale(previousSlotScaleParam);
                }
            }
            else
            {
                applySlotScale(Opt::getFloat(apvts, "optSlotScale", slotScale));
            }

            patternsTree = processor.getPatternsTree();
            const int patternCount = patternsTree.getNumChildren();
            if (patternCount > 0)
            {
                currentPatternIndex = juce::jlimit(0, patternCount - 1, processor.getCurrentPatternIndex());
                processor.setCurrentPatternIndex(currentPatternIndex);
                refreshPatternTabs();
                applyPattern(currentPatternIndex, true, false);
            }
            else
            {
                currentPatternIndex = 0;
                refreshPatternTabs();
                juce::Array<int> none;
                refreshSlotFileLabels(none);
                showPatternWarning(none);
            }

            // Persist options immediately too (Standalone)
            saveOptionsToDisk(apvts);
        });
}

void SlotMachineAudioProcessorEditor::deleteCurrentPattern()
{
    if (!patternsTree.isValid())
        patternsTree = processor.getPatternsTree();

    const int count = patternsTree.getNumChildren();
    if (count <= 1)
        return;

    saveCurrentPattern();

    const int indexToRemove = juce::jlimit(0, count - 1, currentPatternIndex);
    patternsTree.removeChild(indexToRemove, nullptr);

    const int remaining = patternsTree.getNumChildren();
    currentPatternIndex = juce::jlimit(0, remaining - 1, indexToRemove);

    refreshPatternTabs();
    applyPattern(currentPatternIndex, true, false);
}

void SlotMachineAudioProcessorEditor::doResetAll(bool persistOptions)
{
    // Reset every parameter to its default via the processor’s parameter list
    const auto& params = processor.getParameters();
    for (auto* p : params)
    {
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(p))
        {
            if (isOptionParameter(rp->getParameterID()))
                continue;

            rp->beginChangeGesture();
            rp->setValueNotifyingHost(rp->getDefaultValue()); // normalized 0..1 default
            rp->endChangeGesture();
        }
    }

    // Clear all samples & file path properties
    processor.clearAllSlots();

    for (int slotIndex = 0; slotIndex < kNumSlots; ++slotIndex)
    {
        const juce::String countParamId = "slot" + juce::String(slotIndex + 1) + "_Count";
        const int countValue = juce::jmax(1, Opt::getInt(apvts, countParamId, 4));
        const uint64_t fullMask = SlotMachineAudioProcessor::maskForBeats(countValue);
        processor.setSlotCountMask(slotIndex, fullMask);
    }

    for (int i = 0; i < kNumSlots; ++i)
    {
        if (slots[(size_t)i])
        {
            slots[(size_t)i]->hasFile = false;
            slots[(size_t)i]->fileLabel.setText("No file", juce::dontSendNotification);
            slots[(size_t)i]->glow = 0.0f;
            slots[(size_t)i]->phase = 0.0f;
            slots[(size_t)i]->lastHitCounter = 0;
        }
    }

    // Reset phases (soft)
    processor.resetAllPhases(false);

    resetProgressVisuals();

    // Reset Tab A's Repeat value to 0
    if (patternsTree.isValid() && patternsTree.getNumChildren() > 0)
    {
        auto tabA = patternsTree.getChild(0);
        if (tabA.isValid())
            tabA.setProperty(kPatternRepeatProperty, 0, nullptr);
    }

    // Persist options (Standalone fallback)
    if (persistOptions)
        saveOptionsToDisk(apvts);

    saveCurrentPattern();
}

void SlotMachineAudioProcessorEditor::resetLoopTransport()
{
    processor.resetAllPhases(true);

    for (int i = 0; i < kNumSlots; ++i)
    {
        if (slots[(size_t)i])
            slots[(size_t)i]->lastHitCounter = processor.getSlotHitCounter(i);
    }

    resetProgressVisuals();
}

void SlotMachineAudioProcessorEditor::resetProgressVisuals()
{
    masterPhase = 0.0f;
    lastPhase = 0.0f;
    cycleFlash = 0.0f;
    startButtonAnimPhase = 0.0f;
    barWritePos = 0;
    barFilledOnce = false;
    if (samplesPerBar > 0 && barVisual.getNumSamples() >= samplesPerBar)
        barVisual.clear();

    for (auto& slot : slots)
    {
        if (!slot)
            continue;

        slot->phase = 0.0f;
        slot->glow = 0.0f;
    }

    if (patternWarningCounter > 0)
    {
        --patternWarningCounter;
        if (patternWarningCounter == 0)
            patternWarningLabel.setVisible(false);
    }

    repaint();
}


void SlotMachineAudioProcessorEditor::refreshSamplesPerBar()
{
    const double sr  = processor.getSampleRate();
    const double bpm = processor.getBpm();
    const int    beatsPerBar = processor.getBeatsPerBar();

    lastSampleRate = sr;
    lastBpmForSizing = bpm;
    lastBeatsPerBar = beatsPerBar;

    if (sr <= 0.0 || bpm <= 0.0 || beatsPerBar <= 0)
    {
        samplesPerBar = 0;
        barVisual.setSize(0, 0);
        barScratch.clear();
        barWritePos = 0;
        barFilledOnce = false;
        return;
    }

    const double secondsPerBeat = 60.0 / bpm;
    const int newSamplesPerBar = juce::jmax(1, (int) std::round(sr * secondsPerBeat * (double) beatsPerBar));

    if (newSamplesPerBar != samplesPerBar)
    {
        samplesPerBar = newSamplesPerBar;
        barVisual.setSize(1, samplesPerBar);
        barVisual.clear();
        barScratch.resize((size_t) samplesPerBar);
        barWritePos = 0;
        barFilledOnce = false;
    }
    else if ((int) barScratch.size() != samplesPerBar)
    {
        barScratch.resize((size_t) samplesPerBar);
    }
}

void SlotMachineAudioProcessorEditor::consumeScopeBlocks()
{
    if (samplesPerBar <= 0)
        return;

    const int capacity = SlotMachineAudioProcessor::kScopeBlockSize * SlotMachineAudioProcessor::kScopeBlocks;
    if (scopeTemp.getNumSamples() < capacity)
        scopeTemp.setSize(1, capacity);

    scopeTemp.clear(0, 0, scopeTemp.getNumSamples());

    auto& queue = processor.getScopeQueue();
    const int blocksCopied = queue.popTo(scopeTemp, 0);
    if (blocksCopied <= 0)
        return;

    const int samplesCopied = blocksCopied * SlotMachineAudioProcessor::kScopeBlockSize;
    const float* src = scopeTemp.getReadPointer(0);

    int remaining = samplesCopied;
    int offset = 0;

    while (remaining > 0)
    {
        int space = samplesPerBar - barWritePos;
        if (space <= 0)
        {
            barWritePos = 0;
            barFilledOnce = true;
            space = samplesPerBar;
        }

        const int toCopy = juce::jmin(remaining, space);
        if (toCopy <= 0)
            break;

        barVisual.copyFrom(0, barWritePos, src + offset, toCopy);
        barWritePos += toCopy;
        offset += toCopy;
        remaining -= toCopy;

        if (barWritePos >= samplesPerBar)
        {
            barWritePos = 0;
            barFilledOnce = true;
        }
    }
}

void SlotMachineAudioProcessorEditor::paintMasterWaveform(juce::Graphics& g, juce::Rectangle<int> bounds)
{
    if (samplesPerBar <= 0 || bounds.isEmpty())
        return;

    const int width = bounds.getWidth();
    if (width <= 0)
        return;

    if ((int) minCols.size() != width)
        minCols.resize((size_t) width);
    if ((int) maxCols.size() != width)
        maxCols.resize((size_t) width);

    const int availableSamples = barFilledOnce ? samplesPerBar : barWritePos;
    if (availableSamples <= 0)
        return;

    const int scratchNeeded = barFilledOnce ? samplesPerBar : availableSamples;
    if ((int) barScratch.size() < scratchNeeded)
        barScratch.resize((size_t) juce::jmax(1, scratchNeeded));

    const float* src = barVisual.getReadPointer(0);

    if (barFilledOnce)
    {
        const int tail = samplesPerBar - barWritePos;
        if (tail > 0)
            std::memcpy(barScratch.data(), src + barWritePos, (size_t) tail * sizeof(float));
        if (barWritePos > 0)
            std::memcpy(barScratch.data() + tail, src, (size_t) barWritePos * sizeof(float));
    }
    else if (availableSamples > 0)
    {
        std::memcpy(barScratch.data(), src, (size_t) availableSamples * sizeof(float));
    }

    const int samplesToBin = barFilledOnce ? samplesPerBar : availableSamples;
    MinMaxBinner::compute(barScratch.data(), samplesToBin, minCols.data(), maxCols.data(), width);

    juce::Graphics::ScopedSaveState state(g);
    g.setOpacity(0.9f);

    g.setColour(juce::Colours::white.withAlpha(0.12f));
    auto centreLine = bounds.withHeight(1).withCentre(bounds.getCentre());
    g.fillRect(centreLine);

    g.setColour(juce::Colours::white.withAlpha(0.35f));
    for (int x = 0; x < width; ++x)
    {
        const float mn = juce::jlimit(-1.0f, 1.0f, minCols[(size_t) x]);
        const float mx = juce::jlimit(-1.0f, 1.0f, maxCols[(size_t) x]);

        const float yMax = juce::jmap(mx, -1.0f, 1.0f, (float) bounds.getBottom(), (float) bounds.getY());
        const float yMin = juce::jmap(mn, -1.0f, 1.0f, (float) bounds.getBottom(), (float) bounds.getY());

        const int xPos = bounds.getX() + x;
        g.drawVerticalLine(xPos, yMax, yMin);
    }
}


void SlotMachineAudioProcessorEditor::timerCallback()
{
    const float currentScaleParam = Opt::getFloat(apvts, "optSlotScale", slotScale);
    if (std::abs(currentScaleParam - slotScale) > 0.0001f)
        applySlotScale(currentScaleParam);

    // Visualizer should only open when user explicitly clicks the Visualize button,
    // not automatically when loading saved state

    const double sampleRateNow = processor.getSampleRate();
    const double bpmNow = processor.getBpm();
    const int beatsNow = processor.getBeatsPerBar();
    if (std::abs(sampleRateNow - lastSampleRate) > 0.5
        || std::abs(bpmNow - lastBpmForSizing) > 1.0e-6
        || beatsNow != lastBeatsPerBar)
    {
        refreshSamplesPerBar();
    }

    const bool isRunning = startToggle.getToggleState();
    const auto glowColour = Opt::rgbParam(apvts, "optGlowColor", 0x6994FC, 1.0f);
    const auto pulseColour = Opt::rgbParam(apvts, "optPulseColor", 0xD5CFEE, 1.0f);
    const float glowAlpha = Opt::getFloat(apvts, "optGlowAlpha", 0.431f);
    const float glowWidth = Opt::getFloat(apvts, "optGlowWidth", 1.34f);

    updateSliderKnobColours(pulseColour);

    if (isRunning != lastStartToggleState
        || glowColour != cachedStartGlowColour
        || pulseColour != cachedStartPulseColour
        || std::abs(glowAlpha - cachedStartGlowAlpha) > 0.0001f
        || std::abs(glowWidth - cachedStartGlowWidth) > 0.0001f)
    {
        updateStartButtonVisuals(isRunning, glowColour, pulseColour, glowAlpha, glowWidth);
        if (isRunning != lastStartToggleState)
            patternTabs.setReorderingEnabled(!isRunning);
        lastStartToggleState = isRunning;
        cachedStartGlowColour = glowColour;
        cachedStartPulseColour = pulseColour;
        cachedStartGlowAlpha = glowAlpha;
        cachedStartGlowWidth = glowWidth;

        if (!isRunning)
            startButtonAnimPhase = 0.0f;
    }

    if (isRunning)
        animateStartButton(glowColour, pulseColour);

    // 0..1 over full polyrhythmic cycle
    const float p = juce::jlimit(0.0f, 1.0f, (float)processor.getMasterPhase());

    // === Apply pending manual tab switches early (before cycle wraps) ===
    // This gives the audio thread time to apply the switch at the upcoming downbeat,
    // preventing the extra cycle delay that occurs when scheduling at wrap time.
    constexpr float kPreLoadThreshold = 0.95f; // Load when 95% through cycle
    if (patternSwitchPending && isRunning && p >= kPreLoadThreshold)
    {
#if JUCE_DEBUG
        DBG("ED: PATTERN_SWITCH_PRELOAD phase=" << p
            << " currentTab=" << currentPatternIndex
            << " playActive=" << (int)playThroughActive);
#endif
        // Schedule tab switch for the upcoming downbeat (at wrap)
        processor.scheduleTabSwitchOnNextDownbeat(currentPatternIndex);

        // Pre-load samples immediately (on message thread, safe)
        if (pendingPatternTree.isValid())
        {
            applyPatternTreeNow(pendingPatternTree, isRunning);
            patternTabs.setCurrentIndex(currentPatternIndex);
        }

        // Clear pending flags
        patternSwitchPending = false;
        pendingPatternTree = {};
    }

    // === Option 5: Pre-load next pattern BEFORE cycle wraps ===
    // This gives samples time to load before Beat 0 triggers
    if (playThroughActive && isRunning && !playThroughNextPatternPreloaded && p >= kPreLoadThreshold)
    {
        // Check if we'll advance on the next wrap
        if (playThroughCyclesRemaining <= 1) // Will decrement to 0 or less
        {
            if (!patternsTree.isValid())
                patternsTree = processor.getPatternsTree();

            const int patternCount = patternsTree.getNumChildren();
            if (patternCount > 0)
            {
                const int nextIndex = playThroughCurrentPattern + 1;
                const bool hasNextPattern = (nextIndex < patternCount) || loopPlaythroughEnabled;

                if (hasNextPattern)
                {
                    const int targetIndex = (nextIndex < patternCount) ? nextIndex : 0;
                    auto nextPattern = patternsTree.getChild(targetIndex);

#if JUCE_DEBUG
                    DBG("ED: PRELOAD_NEXT_PATTERN phase=" << p
                        << " currentPat=" << playThroughCurrentPattern
                        << " nextPat=" << targetIndex
                        << " cyclesRem=" << playThroughCyclesRemaining);
#endif

                    // Pre-load samples immediately (on message thread, safe)
                    applyPatternTreeNow(nextPattern, true);
                    playThroughNextPatternPreloaded = true;

                    // Don't update tabs yet - that happens after wrap
                }
                else
                {
                    // No next pattern - finish playthrough NOW (before wrap)
                    // This prevents the extra beat at the cycle boundary
#if JUCE_DEBUG
                    DBG("ED: PRELOAD_FINISH_EARLY phase=" << p
                        << " currentPat=" << playThroughCurrentPattern
                        << " cyclesRem=" << playThroughCyclesRemaining
                        << " reason=NoNextPattern");
#endif
                    finishPlayThrough(true, true);
                }
            }
        }
    }

    // Detect wrap (phase jumped backwards a bit)
    const bool wrapped = (p + 0.02f) < lastPhase; // small hysteresis
    if (wrapped)
    {
#if JUCE_DEBUG
        DBG("ED: PHASE_WRAP p=" << p
            << " last=" << lastPhase
            << " playActive=" << (int)playThroughActive
            << " cyclesRem=" << playThroughCyclesRemaining
            << " playPat=" << playThroughCurrentPattern
            << " currentTab=" << currentPatternIndex
            << " skipNext=" << (int)playThroughSkipNextWrap);
#endif
        cycleFlash = 1.0f;                        // start flash
    }

    bool suppressPlayThroughAdvanceForWrap = false;
    if (patternSwitchPending && (!isRunning || wrapped))
    {
#if JUCE_DEBUG
        DBG("ED: PATTERN_SWITCH_PENDING isRunning=" << (int)isRunning
            << " wrapped=" << (int)wrapped
            << " currentTab=" << currentPatternIndex
            << " playActive=" << (int)playThroughActive
            << " playPat=" << playThroughCurrentPattern);
#endif
        if (isRunning && wrapped)
            suppressPlayThroughAdvanceForWrap = true;

        if (isRunning)
        {
#if JUCE_DEBUG
            DBG("ED: SCHEDULE_TAB_SWITCH_ON_NEXT_DOWNBEAT tab=" << currentPatternIndex);
#endif
            processor.scheduleTabSwitchOnNextDownbeat(currentPatternIndex);
        }

        if (pendingPatternTree.isValid())
        {
            applyPatternTreeNow(pendingPatternTree, isRunning);
            patternTabs.setCurrentIndex(currentPatternIndex);
        }

        patternSwitchPending = false;
        pendingPatternTree = {};
    }

    if (playThroughActive && wrapped)
    {
        // Option 5: Reset pre-load flag after wrap completes
        playThroughNextPatternPreloaded = false;

#if JUCE_DEBUG
        DBG("ED: PLAYTHROUGH_WRAP active=" << (int)playThroughActive
            << " skipNext=" << (int)playThroughSkipNextWrap
            << " suppress=" << (int)suppressPlayThroughAdvanceForWrap
            << " cyclesRem=" << playThroughCyclesRemaining
            << " playPat=" << playThroughCurrentPattern);
#endif
        if (playThroughSkipNextWrap)
        {
            const bool shouldIgnoreWrap = playThroughWrapGuardPhase > kPlayThroughWrapGuardThreshold;
            playThroughSkipNextWrap = false;
            playThroughWrapGuardPhase = 0.0f;

            if (!shouldIgnoreWrap && !suppressPlayThroughAdvanceForWrap)
            {
#if JUCE_DEBUG
                DBG("ED: PLAYTHROUGH_ADVANCE_CALL fromWrap=1 skipNext=1 ignore=" << (int)shouldIgnoreWrap
                    << " suppress=" << (int)suppressPlayThroughAdvanceForWrap
                    << " cyclesRem=" << playThroughCyclesRemaining
                    << " playPat=" << playThroughCurrentPattern);
#endif
                advancePlayThrough(true);
#if JUCE_DEBUG
                DBG("ED: PLAYTHROUGH_ADVANCE_DONE cyclesRem=" << playThroughCyclesRemaining
                    << " playPat=" << playThroughCurrentPattern);
#endif
            }
        }
        else if (!suppressPlayThroughAdvanceForWrap)
        {
#if JUCE_DEBUG
            DBG("ED: PLAYTHROUGH_ADVANCE_CALL fromWrap=1 skipNext=0 suppress=" << (int)suppressPlayThroughAdvanceForWrap
                << " cyclesRem=" << playThroughCyclesRemaining
                << " playPat=" << playThroughCurrentPattern);
#endif
            advancePlayThrough(true);
#if JUCE_DEBUG
            DBG("ED: PLAYTHROUGH_ADVANCE_DONE cyclesRem=" << playThroughCyclesRemaining
                << " playPat=" << playThroughCurrentPattern);
#endif
        }
    }

    // Decay flash envelope @ ~60 Hz
    cycleFlash = juce::jmax(0.0f, cycleFlash * 0.88f - 0.01f);

    lastPhase = p;
    masterPhase = p; // used by paint() for the master bar

    // ---- per-slot UI polling ----
    const int timingMode = Opt::getInt(apvts, "optTimingMode", 1);

    if (timingMode == 0 && lastShowVisualizer)
    {
        closeVisualizerWindow();
        lastShowVisualizer = false;
        setShowVisualizerParam(false);
    }

    if (timingMode != lastTimingMode)
    {
        const bool rateMode = (timingMode == 0);
        btnVisualizer.setEnabled(!rateMode);
        btnVisualizer.setAlpha(rateMode ? 0.35f : 1.0f);

        refreshSlotTimingModeUI(timingMode);

        for (int i = 0; i < kNumSlots; ++i)
            if (auto* slot = slots[(size_t)i].get())
                initialiseSlotTimingPair(i, *slot);

        lastTimingMode = timingMode;
    }

    for (int i = 0; i < kNumSlots; ++i)
    {
        auto* ui = slots[(size_t)i].get();
        if (!ui) continue;

        ui->phase = (float)processor.getSlotPhase(i);

        const uint32_t hits = processor.getSlotHitCounter(i);
        if (hits != ui->lastHitCounter)
        {
            ui->lastHitCounter = hits;
            ui->glow = 1.0f; // pulse on hit
        }

        // simple glow decay
        ui->glow = juce::jmax(0.0f, ui->glow - 0.06f);
        ui->hasFile = processor.slotHasSample(i);

        const bool beatsPerCycleMode = (timingMode == 1);
        const bool countEnabled = beatsPerCycleMode && !playThroughActive;
        const bool rateEnabled = !beatsPerCycleMode && !playThroughActive;

        if (ui->count.isEnabled() != countEnabled)
            ui->count.setEnabled(countEnabled);
        if (ui->rate.isEnabled() != rateEnabled)
            ui->rate.setEnabled(rateEnabled);

        ui->count.setAlpha(countEnabled ? 1.0f : 0.35f);
        ui->rate.setAlpha(rateEnabled ? 1.0f : 0.35f);
    }

    consumeScopeBlocks();

    repaint();
}

void SlotMachineAudioProcessorEditor::handleSlotRateChanged(int slotIndex, SlotUI& ui)
{
    if (ui.syncingFromCount) return;

    const float rateValue   = (float) ui.rate.getValue();
    const int   desiredCount = convertRateToCount(rateValue);
    const juce::String paramId = "slot" + juce::String(slotIndex + 1) + "_Count";

    if (auto* p = apvts.getParameter(paramId)) // juce::RangedAudioParameter*
    {
        // Prevent feedback loop into the other handler
        juce::ScopedValueSetter<bool> guard(ui.syncingFromRate, true);

        // Normalise real value -> 0..1 using the parameter's own mapping
        const float normalised = p->convertTo0to1((float) desiredCount);

        p->beginChangeGesture();
        p->setValueNotifyingHost(normalised);
        p->endChangeGesture();
    }
}

void SlotMachineAudioProcessorEditor::handleSlotCountChanged(int slotIndex, SlotUI& ui)
{
    if (ui.syncingFromRate) return;

    const int   countValue   = (int) std::round(ui.count.getValue());
    const float desiredRate  = convertCountToRate(countValue);
    const juce::String paramId = "slot" + juce::String(slotIndex + 1) + "_Rate";

    if (Opt::getInt(apvts, "optTimingMode", 1) == 1)
    {
        const uint64_t fullMask = SlotMachineAudioProcessor::maskForBeats(countValue);
        processor.setSlotCountMask(slotIndex, fullMask);
    }

    if (auto* p = apvts.getParameter(paramId)) // juce::RangedAudioParameter*
    {
        juce::ScopedValueSetter<bool> guard(ui.syncingFromCount, true);

        const float normalised = p->convertTo0to1(desiredRate);

        p->beginChangeGesture();
        p->setValueNotifyingHost(normalised);
        p->endChangeGesture();
    }
}

void SlotMachineAudioProcessorEditor::initialiseSlotTimingPair(int slotIndex, SlotUI& ui)
{
    const int timingMode = Opt::getInt(apvts, "optTimingMode", 1);

    if (timingMode == 1)
    {
        handleSlotCountChanged(slotIndex, ui);
    }
    else
    {
        handleSlotRateChanged(slotIndex, ui);
    }
}

void SlotMachineAudioProcessorEditor::refreshSlotTimingModeUI()
{
    refreshSlotTimingModeUI(Opt::getInt(apvts, "optTimingMode", 1));
}

void SlotMachineAudioProcessorEditor::refreshSlotTimingModeUI(int timingMode)
{
    for (auto& slot : slots)
        if (slot)
            slot->updateTimingModeVisibility(timingMode);

    repaint();
}

void SlotMachineAudioProcessorEditor::parameterChanged(const juce::String& parameterID, float)
{
    if (parameterID == "optTimingMode")
    {
        juce::MessageManager::callAsync([safe = juce::Component::SafePointer<SlotMachineAudioProcessorEditor>(this)]()
        {
            if (safe != nullptr)
                safe->refreshSlotTimingModeUI();
        });
    }
}

void SlotMachineAudioProcessorEditor::handleMasterTap()
{
    constexpr double tapWindowSeconds = 6.0;
    constexpr double minimumSpanSeconds = 3.0;

    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    masterTapTimes.push_back(now);

    while (!masterTapTimes.empty() && (now - masterTapTimes.front()) > tapWindowSeconds)
        masterTapTimes.pop_front();

    if (masterTapTimes.size() < 3)
        return;

    const double span = masterTapTimes.back() - masterTapTimes.front();
    if (span < minimumSpanSeconds)
        return;

    const double minInterval = 60.0 / (double)masterBPM.getMaximum();
    const double maxInterval = 60.0 / (double)masterBPM.getMinimum();

    double intervalSum = 0.0;
    int validIntervals = 0;

    for (size_t i = 1; i < masterTapTimes.size(); ++i)
    {
        const double diff = masterTapTimes[i] - masterTapTimes[i - 1];
        if (diff < minInterval || diff > maxInterval)
            continue;

        intervalSum += diff;
        ++validIntervals;
    }

    if (validIntervals == 0)
        return;

    const double averageInterval = intervalSum / (double)validIntervals;
    double bpm = 60.0 / averageInterval;
    bpm = juce::jlimit(masterBPM.getMinimum(), masterBPM.getMaximum(), bpm);

    if (auto* param = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("masterBPM")))
    {
        param->beginChangeGesture();
        param->setValueNotifyingHost(param->convertTo0to1((float)bpm));
        param->endChangeGesture();
    }
}

namespace
{
    bool componentContainsScreenPoint(const juce::Component& component, juce::Point<int> screenPoint)
    {
        if (!component.isShowing())
            return false;

        return component.getScreenBounds().contains(screenPoint);
    }
}

void SlotMachineAudioProcessorEditor::mouseUp(const juce::MouseEvent& e)
{
    juce::AudioProcessorEditor::mouseUp(e);

    const juce::Point<int> screenPos(e.getScreenX(), e.getScreenY());

    if (componentContainsScreenPoint(masterLabel, screenPos))
    {
        if (e.mouseWasClicked())
            handleMasterTap();
        return;
    }

    int clickedIndex = -1;
    for (int i = 0; i < kNumSlots; ++i)
    {
        auto* ui = slots[(size_t)i].get();
        if (!ui)
            continue;

        if (componentContainsScreenPoint(ui->group, screenPos))
        {
            clickedIndex = i;
            break;
        }
    }

    if (clickedIndex < 0)
        return;

    auto& U = *slots[(size_t)clickedIndex];

    auto isInteractiveHit = [screenPos](juce::Component& c)
    {
        return componentContainsScreenPoint(c, screenPos);
    };

    if (isInteractiveHit(U.fileBtn) || isInteractiveHit(U.clearBtn) || isInteractiveHit(U.fileLabel)
        || isInteractiveHit(U.muteBtn) || isInteractiveHit(U.soloBtn)
        || isInteractiveHit(U.muteLabel) || isInteractiveHit(U.soloLabel)
        || isInteractiveHit(U.rate) || isInteractiveHit(U.gain) || isInteractiveHit(U.decay))
        return;

    if (!processor.slotHasSample(clickedIndex))
        return;

    processor.requestManualTrigger(clickedIndex);
}
