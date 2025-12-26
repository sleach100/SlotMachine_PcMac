#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>

class BeatsQuickPickGrid : public juce::Component,
                           private juce::Button::Listener
{
public:
    struct Options {
        int minBeat = 1;
        int maxBeat = 32; // default human range
        int columns = 8;  // 8x4 grid for 1–32
        int buttonW = 36;
        int buttonH = 28;
        int gap = 6;
        bool showExpandToggle = true;
    };

    BeatsQuickPickGrid(Options opts,
                       std::function<void(int)> onPick,
                       int currentValue,
                       juce::Colour textColor = juce::Colours::white);
    ~BeatsQuickPickGrid() override = default;

    void resized() override;

private:
    void buildButtons();
    void rebuildForRange(int newMax);
    void buttonClicked(juce::Button* b) override;

    Options options;
    std::function<void(int)> pickCallback;
    juce::OwnedArray<juce::TextButton> buttons;
    std::unique_ptr<juce::TextButton> expandToggle;
    int current = 1;
    bool expanded = false;
    juce::Colour customTextColor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BeatsQuickPickGrid)
};
