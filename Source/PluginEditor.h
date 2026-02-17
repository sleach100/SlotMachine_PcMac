#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <array>
#include <memory>
#include <functional>
#include <deque>
#include <vector>
#include <utility>
#include <map>
#include <string>
#include "PluginProcessor.h"
#include "WaveformUtils.h"
#include "AppLookAndFeel.h"
#include "UpdateChecker.h"

class PolyrhythmVizComponent;

class SlotMachineAudioProcessorEditor
    : public juce::AudioProcessorEditor
    , private juce::Button::Listener
    , private juce::Timer
    , private juce::AudioProcessorValueTreeState::Listener
{
public:

    using APVTS = juce::AudioProcessorValueTreeState;

    SlotMachineAudioProcessorEditor(SlotMachineAudioProcessor& processor,
        APVTS& state);
    ~SlotMachineAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void paintOverChildren(juce::Graphics& g) override;
    void resized() override;
    void parentHierarchyChanged() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    bool keyPressed(const juce::KeyPress& key) override;

    // Standalone helper used to ensure the UI starts in a clean state.
    void resetUiToDefaultStateForStandalone();

    // Reload skin with a specific folder name
    void reloadSkinWithFolder(const juce::String& skinFolderName);

private:
    // Apply button font color from skin to all relevant components
    void updateButtonFontColors();

    // Get pulse color - uses ButtonFontColor if available, otherwise uses option setting
    juce::Colour getPulseColour() const;

    // VST3-specific behavior
    bool isRunningAsVST3() const;
    void showVST3LockOverlayIfNeeded();
    class VST3LockOverlay;
    std::unique_ptr<VST3LockOverlay> vst3LockOverlay;

    void checkForUpdatesOnStartup();
    void checkForUpdateCompletedMessage();
    void initialiseLicenseState();
    void setUnlocked(bool unlocked);
    void showUnlockDialog();
    void handleUnlockDialogResult(bool accepted,
        const juce::String& firstName,
        const juce::String& lastName,
        const juce::String& email,
        const juce::String& licenseKey);
    void showTrialModeDialog();
    void updateLockIconPositions();
    juce::String getRegistrationDisplayName() const;

    AppLookAndFeel appLF;

    struct EmbeddedSample
    {
        juce::String category;
        juce::String display;
        juce::String resourceName;
    };

    using EmbeddedCatalog = std::map<juce::String, juce::Array<EmbeddedSample>>;

    class VisualizerWindow;
    class EmbeddedSampleSelector;

    class PatternTabs : public juce::Component,
                        private juce::Button::Listener
    {
    public:
        PatternTabs();

        void setTabs(const juce::StringArray& names);
        void setCurrentIndex(int index, bool notify = false);
        int  getCurrentIndex() const { return currentIndex; }
        juce::Rectangle<int> getTabBoundsInParent(int index) const;

        void setReorderingEnabled(bool shouldEnable);

        void onTabSelected(std::function<void(int)> handler) { tabSelected = std::move(handler); }
        void onTabBarRightClick(std::function<void(const juce::MouseEvent&)> handler) { rightClick = std::move(handler); }
        void onTabReordered(std::function<void(int, int)> handler) { tabReordered = std::move(handler); }

        void updateTabColors(juce::Colour textColorOff, juce::Colour textColorOn);

        void paint(juce::Graphics& g) override;
        void resized() override;
        void mouseUp(const juce::MouseEvent& e) override;
    private:
        struct TabButton : juce::TextButton
        {
            explicit TabButton(PatternTabs& ownerRef)
                : owner(ownerRef)
            {
            }

            void mouseDown(const juce::MouseEvent& e) override
            {
                owner.handleTabMouseDown(*this, e);
                juce::TextButton::mouseDown(e);
            }

            void mouseDrag(const juce::MouseEvent& e) override
            {
                owner.handleTabMouseDrag(*this, e);
                juce::TextButton::mouseDrag(e);
            }

            void mouseUp(const juce::MouseEvent& e) override
            {
                owner.handleTabMouseUp(*this, e);
                juce::TextButton::mouseUp(e);
            }

            PatternTabs& owner;
            int index = -1;
        };

        void buttonClicked(juce::Button* b) override;
        void updateToggleStates();
        void reorderTab(int fromIndex, int toIndex, bool notify);
        int  getDropIndexForPosition(int x) const;
        void handleTabMouseDown(TabButton& button, const juce::MouseEvent& e);
        void handleTabMouseDrag(TabButton& button, const juce::MouseEvent& e);
        void handleTabMouseUp(TabButton& button, const juce::MouseEvent& e);
        void resetDragState(bool clearSuppressed = true);

        juce::OwnedArray<TabButton> buttons;
        int currentIndex = 0;
        std::function<void(int)> tabSelected;
        std::function<void(const juce::MouseEvent&)> rightClick;
        std::function<void(int, int)> tabReordered;

        void animateTabToPosition(TabButton* tab, int targetX);
        void updateDraggedTabPosition(int mouseScreenX);
        void layoutTabsWithAnimation(int excludeIndex);
        void snapDraggedTabToSlot();

        TabButton* dragButton = nullptr;
        int dragStartIndex = -1;
        int dragCurrentIndex = -1;
        int dragStartScreenX = 0;
        int dragButtonGrabOffsetX = 0;  // Offset from button left edge where mouse grabbed
        bool dragging = false;
        bool suppressNextClick = false;
        bool allowReordering = true;

        juce::ComponentAnimator tabAnimator;
        static constexpr int kTabAnimationDurationMs = 150;
    };

    class RenamePatternComponent : public juce::Component,
                                   private juce::Button::Listener,
                                   private juce::TextEditor::Listener
    {
    public:
        using ResultHandler = std::function<void(bool accepted, juce::String newName)>;

        RenamePatternComponent(const juce::String& currentName, ResultHandler handler);
        ~RenamePatternComponent() override;

        void setCallOutBox(juce::CallOutBox& box);
        void focusEditor();

        void resized() override;

    private:
        void buttonClicked(juce::Button* button) override;
        void textEditorReturnKeyPressed(juce::TextEditor&) override;
        void textEditorEscapeKeyPressed(juce::TextEditor&) override;

        void commit(bool accepted);

        juce::Label prompt;
        juce::TextEditor editor;
        juce::TextButton okButton{ "OK" };
        juce::TextButton cancelButton{ "Cancel" };

        ResultHandler onResult;
        juce::CallOutBox* owner = nullptr;
        bool hasCommitted = false;
    };

    class EditPatternRepeatComponent : public juce::Component,
                                       private juce::Button::Listener,
                                       private juce::TextEditor::Listener
    {
    public:
        using ResultHandler = std::function<void(bool accepted, int newRepeat)>;

        EditPatternRepeatComponent(int currentRepeat, ResultHandler handler);
        ~EditPatternRepeatComponent() override;

        void setCallOutBox(juce::CallOutBox& box);
        void focusEditor();

        void resized() override;
        void mouseWheelMove(const juce::MouseEvent& event,
                            const juce::MouseWheelDetails& wheel) override;

    private:
        void buttonClicked(juce::Button* button) override;
        void textEditorReturnKeyPressed(juce::TextEditor&) override;
        void textEditorEscapeKeyPressed(juce::TextEditor&) override;

        void commit(bool accepted);
        int  parseEditorValue() const;

        juce::Label prompt;
        juce::TextEditor editor;
        juce::TextButton okButton{ "OK" };
        juce::TextButton cancelButton{ "Cancel" };

        ResultHandler onResult;
        juce::CallOutBox* owner = nullptr;
        bool hasCommitted = false;
    };

    // ===== Master UI =====
    void handleMasterTap();
    juce::TooltipWindow tooltipWindow;  // must live as long as the editor
    juce::Image        logoImage;

    juce::Label  masterLabel{ {}, "Master BPM" };
    juce::Slider masterBPM;
    juce::ToggleButton startToggle; // hidden, just for attachment

    juce::TextButton btnStart{ "Start" };
    juce::TextButton btnSave{ "Save" };
    juce::TextButton btnLoad{ "Load" };
    juce::TextButton btnInitialize{ "Initialize" };
    juce::TextButton btnResetLoop{ "Reset Loop" };
    juce::TextButton btnReset{ "Reset UI" };
    juce::TextButton btnOptions{ "Options" };
    juce::TextButton btnExportMidi{ "Export MIDI" };
    juce::TextButton btnExportAudio{ "Export Audio" };
    juce::TextButton btnVisualizer{ "Visualize" };
    juce::TextButton btnTutorial{ "Tutorial" };
    juce::TextButton btnUserManual{ "User Manual" };
    juce::TextButton btnAbout{ "About" };
    juce::TextButton btnLock{ "Lock" };
    juce::TextButton btnUnlock{ "Unlock Trial" };

    bool isUnlocked = false;
    juce::Image lockIconImage;
    juce::ImageComponent lockIconLoad;
    juce::ImageComponent lockIconExportAudio;
    juce::ImageComponent lockIconExportMidi;

    std::string storedFirstName;
    std::string storedLastName;
    std::string storedEmail;
    std::string storedLicenseKey;

    std::unique_ptr<APVTS::SliderAttachment> masterBPMA;
    std::unique_ptr<APVTS::ButtonAttachment> masterRunA;

    juce::Rectangle<int> masterBarBounds;
    juce::Rectangle<int> logoBounds;
    float masterPhase = 0.0f;

    std::deque<double> masterTapTimes;

    float lastPhase = 0.0f;      // previous cycle phase (0..1)
    float cycleFlash = 0.0f;     // 0..1 envelope that decays after wrap
    bool lastShowVisualizer = false;

    std::unique_ptr<juce::DocumentWindow> vizWindow;
    std::unique_ptr<PolyrhythmVizComponent> vizComponent;

    // --- Master waveform ---
    juce::AudioBuffer<float> barVisual;
    juce::AudioBuffer<float> scopeTemp;
    std::vector<float> barScratch;
    std::vector<float> minCols, maxCols;
    int barWritePos = 0;
    bool barFilledOnce = false;
    double lastBpmForSizing = 0.0;
    int lastBeatsPerBar = 0;
    double lastSampleRate = 0.0;
    int samplesPerBar = 0;

    // ===== Slot UI =====
    struct SlotUI
    {
        // Custom GroupComponent that tracks flash state for skinning
        struct SlotGroupComponent : juce::GroupComponent
        {
            void setFlashState(float flashAmount)
            {
                if (flashState != flashAmount)
                {
                    flashState = flashAmount;
                    repaint();
                }
            }

            float getFlashState() const { return flashState; }

        private:
            float flashState = 0.0f;
        };

        struct ToggleLabel : juce::Label
        {
            ToggleLabel()
            {
                setInterceptsMouseClicks(true, false);
            }

            void mouseUp(const juce::MouseEvent& e) override
            {
                juce::Label::mouseUp(e);

                if (target != nullptr && e.mouseWasClicked())
                    target->triggerClick();
            }

            bool hitTest(int x, int y) override
            {
                // Reduce hit area to center 50% of bounds
                auto bounds = getLocalBounds().toFloat();
                auto reduced = bounds.reduced(bounds.getWidth() * 0.25f, bounds.getHeight() * 0.2f);
                return reduced.contains(static_cast<float>(x), static_cast<float>(y));
            }

            juce::Button* target = nullptr;
        };

        // Custom ImageButton with reduced hit area for mute/solo buttons
        struct SlotToggleButton : juce::ImageButton
        {
            bool hitTest(int x, int y) override
            {
                // Reduce hit area to center portion of bounds
                auto bounds = getLocalBounds().toFloat();
                auto reduced = bounds.reduced(bounds.getWidth() * 0.3f, bounds.getHeight() * 0.25f);
                return reduced.contains(static_cast<float>(x), static_cast<float>(y));
            }
        };

        struct FileButton : juce::TextButton, juce::FileDragAndDropTarget
        {
            FileButton();

            std::function<void(const juce::File&)> onFileDropped;
            std::function<void(const juce::MouseEvent&)> onRightClick;

            bool isInterestedInFileDrag(const juce::StringArray& files) override;
            void fileDragEnter(const juce::StringArray& files, int, int) override;
            void fileDragExit(const juce::StringArray& files) override;
            void filesDropped(const juce::StringArray& files, int, int) override;
            void mouseUp(const juce::MouseEvent& e) override;

        private:
            void paintButton(juce::Graphics& g, bool isMouseOverButton, bool isButtonDown) override;
            static bool containsSupportedFile(const juce::StringArray& files);
            static bool isSupportedFile(const juce::File& file);
            void updateDragHighlight(bool shouldHighlight);

            bool dragActive = false;
        };

        // Custom slider with reduced hit-test area to allow clicks to pass through
        // to the parent for audio sampling and drag-drop operations
        struct SlotKnobSlider : juce::Slider
        {
            bool hitTest(int x, int y) override
            {
                // Always allow clicks in the text box area
                auto layout = getLookAndFeel().getSliderLayout(*this);
                if (layout.textBoxBounds.contains(x, y))
                    return true;

                // Get the knob bounds
                auto knobBounds = layout.sliderBounds.toFloat();
                if (knobBounds.isEmpty())
                    knobBounds = getLocalBounds().toFloat();

                // Calculate a tighter knob circle (reduce radius to match visual arc)
                const auto centre = knobBounds.getCentre();
                const auto diameter = juce::jmin(knobBounds.getWidth(), knobBounds.getHeight());
                // Use 0.38 of diameter for tighter hit area around the visual arc
                const auto radius = diameter * 0.38f;

                // Check if point is within the knob circle
                const auto point = juce::Point<float>(static_cast<float>(x), static_cast<float>(y));
                const auto distance = centre.getDistanceFrom(point);

                return distance <= radius;
            }
        };

        SlotGroupComponent group;
        FileButton        fileBtn;
        juce::TextButton  clearBtn{ "X" };      // NEW: Clear sample
        juce::Label       fileLabel;

        SlotToggleButton  muteBtn;
        SlotToggleButton  soloBtn;
        ToggleLabel       muteLabel;
        ToggleLabel       soloLabel;
        juce::ComboBox    midiChannel;

        SlotKnobSlider count, rate, gain, decay;

        void updateTimingModeVisibility(int timingMode);

        std::unique_ptr<APVTS::ButtonAttachment> muteA, soloA;
        std::unique_ptr<APVTS::SliderAttachment> countA, rateA, gainA, decayA;
        std::unique_ptr<APVTS::ComboBoxAttachment> midiChannelA;

        bool     hasFile = false;
        float    glow = 0.0f;
        float    phase = 0.0f;
        uint32_t lastHitCounter = 0;

        int titleLabelRaiseOffset = 0;

        bool syncingFromRate = false;
        bool syncingFromCount = false;
        bool beatsQuickPickExpanded = false;

        bool showRateLabel = true;
        bool showCountLabel = false;
    };

    static constexpr int kNumSlots = SlotMachineAudioProcessor::kNumSlots;
    static constexpr int kMaxBeatsPerSlot = 64;
    std::array<std::unique_ptr<SlotUI>, kNumSlots> slots;
    std::array<juce::String, kNumSlots> embeddedSlotResourceNames{};

    PatternTabs patternTabs;
    juce::Label patternWarningLabel;

    // ===== Helpers =====
    void buttonClicked(juce::Button*) override;
    void openTutorialVideo();
    void openUserManual();
    void openEmbeddedSampleSelectorForSlot(int slotIndex, const juce::MouseEvent& e);
    void timerCallback() override;
    void refreshSamplesPerBar();
    void consumeScopeBlocks();
    void paintMasterWaveform(juce::Graphics& g, juce::Rectangle<int> bounds);
    void updateStandaloneWindowTitle();

    void setMasterRun(bool shouldRun);
    void updateStartButtonVisuals(bool shouldRun,
        juce::Colour glowColour,
        juce::Colour pulseColour,
        float glowAlpha,
        float glowWidth);
    void animateStartButton(juce::Colour glowColour, juce::Colour pulseColour, float dt);
    void updateSliderKnobColours(juce::Colour pulseColour);
    void doSavePreset();
    void doLoadPreset();
    void doResetAll(bool persistOptions = true);
    void updateMuteSoloButtonImages();
    void resetLoopTransport();
    void resetProgressVisuals();
    void showOptionsDialog();
    void promptForExportCycles(const juce::String& dialogTitle,
        int defaultCycles,
        std::function<void(int, bool, bool)> onConfirm,
        bool includePlaythroughOptions,
        bool initialPlaythroughSelection,
        bool includeMidiNoteLengthOptions);
    void beginAudioExportWithCycles(int cyclesRequested, bool exportPlaythrough);
    void beginMidiExportWithCycles(int cyclesRequested, bool exportPlaythrough, bool useFixedNoteLength);
    void openVisualizerWindow();
    void closeVisualizerWindow();
    void handleVisualizerWindowCloseRequest();
    void setShowVisualizerParam(bool shouldShow);
    void handleSlotRateChanged(int slotIndex, SlotUI& ui);
    void handleSlotCountChanged(int slotIndex, SlotUI& ui);
    void initialiseSlotTimingPair(int slotIndex, SlotUI& ui);
    void setSlotControlsFrozen(bool shouldFreeze);
    static int convertRateToCount(float rateValue)
    {
        const float clampedRate = juce::jlimit(0.0625f, 4.0f, rateValue);
        const int scaled = juce::jmax(1, juce::roundToInt(clampedRate * (float)SlotMachineAudioProcessor::kCountModeBaseBeats));
        return juce::jlimit<int>(1, kMaxBeatsPerSlot, scaled);
    }

    static float convertCountToRate(int countValue)
    {
        const int clampedCount = juce::jlimit<int>(1, kMaxBeatsPerSlot, countValue);
        const float rate = (float)clampedCount / (float)SlotMachineAudioProcessor::kCountModeBaseBeats;
        return juce::jlimit(0.0625f, 4.0f, rate);
    }
    void handleSlotFileSelection(int slotIndex, const juce::File& file);
    void copySlotData(int fromSlot, int toSlot);
    void initialisePatterns();
    void saveCurrentPattern();
    void refreshPatternTabs();
    void applyPattern(int index, bool updateTabs = true, bool saveExisting = true, bool deferIfRunning = false);
    void applyPatternTreeNow(const juce::ValueTree& pattern, bool allowTailRelease = false);
    void showPatternWarning(const juce::Array<int>& failedSlots);
    void refreshSlotFileLabels(const juce::Array<int>& failedSlots);
    juce::String defaultPatternNameForIndex(int index) const;
    void handlePatternContextMenu(const juce::MouseEvent& e);
    void reorderPatterns(int fromIndex, int toIndex);
    void createNewPattern();
    void duplicateCurrentPattern();
    void deleteCurrentPattern();
    void renameCurrentPattern();
    void editCurrentPatternRepeat();
    void showLoopPlaythroughDialog();
    void setLoopPlaythroughEnabled(bool shouldLoop);
    void beginPlayThrough();
    void beginPlayThroughFromCurrentTab();
    void beginPlayThroughAtIndex(int startIndex);
    void advancePlayThrough(bool applyImmediately);
    void finishPlayThrough(bool restorePattern, bool stopTransport);
    void importPatternFromFile();
    void handlePatternImportFile(const juce::File& file);
    void importPatternIntoCurrentTab(const juce::ValueTree& patternTree);
    void clearExtraPatternsBeforeLoad();
    void resetPatternsToSingleDefault();

    void applySlotScale(float newScale);
    void refreshSizeForSlotScale();
    int scaleDimension(int base) const;
    int scaleDimensionWithMax(int base, float maxScale) const;
    void refreshSlotTimingModeUI();
    void refreshSlotTimingModeUI(int timingMode);
    void parameterChanged(const juce::String& parameterID, float newValue) override;
    void buildEmbeddedSampleCatalog();
    juce::String getEmbeddedSampleDisplay(const juce::String& resourceName) const;

    // Refs
    SlotMachineAudioProcessor& processor;
    APVTS& apvts;

    EmbeddedCatalog embeddedCatalog;
    std::map<juce::String, EmbeddedSample> embeddedSampleLookup;
    bool embeddedCatalogBuilt = false;

    float slotScale = 1.0f;
    int   lastTimingMode = 0;

    bool lastStartToggleState = false;
    float startButtonAnimPhase = 0.0f;
    juce::GlowEffect startButtonGlow;
    bool startButtonGlowEnabled = false;
    juce::Colour cachedStartGlowColour{ juce::Colours::transparentBlack };
    juce::Colour cachedStartPulseColour{ juce::Colours::transparentBlack };
    float cachedStartGlowAlpha = -1.0f;
    float cachedStartGlowWidth = -1.0f;
    juce::Colour cachedKnobPulseColour{ juce::Colours::transparentBlack };

    juce::ValueTree patternsTree;
    int currentPatternIndex = 0;
    int activePatternIndex = 0;

    bool playThroughActive = false;
    int  playThroughInitialPattern = -1;
    int  playThroughCurrentPattern = -1;
    int  playThroughCyclesRemaining = 0;
    bool  playThroughSkipNextWrap = false;
    float playThroughWrapGuardPhase = 0.0f;
    bool  playThroughNextPatternPreloaded = false;  // Option 5: Track if next pattern is pre-loaded
    bool loopPlaythroughEnabled = false;

    bool patternSwitchPending = false;
    juce::ValueTree pendingPatternTree;
    bool fileDialogActive = false;
    bool suppressNextFileBtnClick = false;
    juce::File lastLoadedPresetFile;  // Remembers file path for save dialog (per-session only)
    juce::Component::SafePointer<juce::DialogWindow> exportCyclesPromptWindow;
    juce::Component::SafePointer<juce::DialogWindow> loopPlaythroughDialog;
    juce::Component::SafePointer<juce::DialogWindow> aboutDialog;
    int patternWarningCounter = 0;
    bool lastAudioExportPlaythrough = false;
    bool lastMidiExportPlaythrough = false;

    // Display-sync repaint (macOS): VBlankAttachment fires at hardware vsync rate via
    // CVDisplayLink, eliminating timer-to-display phase jitter.
    // Windows uses startTimerHz() instead (see constructor).
#if JUCE_MAC
    std::unique_ptr<juce::VBlankAttachment> vblankAttachment;
#endif
    // Timestamp of the previous timerCallback() invocation, used to compute dt
    // (elapsed time normalised to one 60 Hz frame) for frame-rate-independent decay.
    int64_t lastCallbackMs = 0;

    // Update checker (standalone only)
    UpdateChecker updateChecker;

    // Slot drag-and-drop state
    int slotDragSourceIndex = -1;
    bool slotDragActive = false;
    juce::Point<int> slotDragStartPosition;
    juce::MouseCursor preDragCursor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SlotMachineAudioProcessorEditor)
};
