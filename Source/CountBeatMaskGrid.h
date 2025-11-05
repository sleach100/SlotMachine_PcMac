#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <cstdint>

class CountBeatMaskGrid : public juce::Component,
                          private juce::Button::Listener,
                          private juce::Timer
{
public:
    struct Options
    {
        int beats = 1;
        int columns = 4;
        int buttonW = 32;
        int buttonH = 28;
        int gap = 6;
    };

    CountBeatMaskGrid(Options options,
                      uint64_t initialMask,
                      std::function<void(uint64_t)> onMaskChanged,
                      std::function<int()> beatHighlightProvider = {});
    ~CountBeatMaskGrid() override = default;

    void resized() override;
    void visibilityChanged() override;
    bool keyPressed(const juce::KeyPress& key) override;
    void paintOverChildren(juce::Graphics& g) override;

private:
    void buildButtons();
    void buttonClicked(juce::Button* button) override;
    void setMask(uint64_t newMask, bool sendNotification);
    void updateButtonStatesFromMask();
    void setHighlightedBeat(int beatIndex);
    void updateButtonColours();
    void timerCallback() override;

    static uint64_t limitMaskToBeats(uint64_t mask, int beats);

    Options options;
    uint64_t mask = 0;
    std::function<void(uint64_t)> maskChanged;
    juce::OwnedArray<juce::TextButton> buttons;
    std::function<int()> highlightProvider;
    int highlightedBeat = -1;
    juce::Colour buttonOffColour;
    juce::Colour buttonOnColour;
};
