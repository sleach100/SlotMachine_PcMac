#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class AppLookAndFeel : public juce::LookAndFeel_V4
{
public:
    AppLookAndFeel()
    {
        loadKnobFilmstrip();
    }

    void setCornerRadius (float r) noexcept { cornerRadius = r; }
    float getCornerRadius () const noexcept { return cornerRadius; }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b,
                               const juce::Colour& bg, bool isOver, bool isDown) override
    {
        auto bounds = b.getLocalBounds().toFloat();
        auto base = bg;

        // Handle transparent backgrounds - use an overlay for hover/press effects
        if (bg.getAlpha() < 10)
        {
            if (isDown)
                base = juce::Colours::white.withAlpha (0.35f);
            else if (isOver)
                base = juce::Colours::white.withAlpha (0.25f);
        }
        else
        {
            if (isDown)      base = base.darker (0.2f);
            else if (isOver) base = base.brighter (0.1f);
        }

        g.setColour (base);
        g.fillRoundedRectangle (bounds, cornerRadius);

        // Use original bg color for border to keep it consistent across states
        g.setColour (bg.contrasting (0.35f));
        g.drawRoundedRectangle (bounds, cornerRadius, 1.0f);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPosProportional, float rotaryStartAngle,
                          float rotaryEndAngle, juce::Slider& slider) override
    {
        // Use filmstrip for slot knobs, fall back to default for others
        if (useFilmstripForSlider(slider))
        {
            drawFilmstripKnob(g, x, y, width, height, sliderPosProportional, slider);
        }
        else
        {
            // Use default JUCE rendering for other sliders
            juce::LookAndFeel_V4::drawRotarySlider(g, x, y, width, height,
                                                   sliderPosProportional,
                                                   rotaryStartAngle, rotaryEndAngle, slider);
        }
    }

    bool hasKnobFilmstrip() const { return knobFilmstrip.isValid(); }
    juce::String getKnobFilmstripError() const { return filmstripErrorMessage; }

private:
    void loadKnobFilmstrip()
    {
        juce::File knobFile = juce::File::getCurrentWorkingDirectory()
                                .getChildFile("Skins")
                                .getChildFile("Default")
                                .getChildFile("knob.png");

        if (!knobFile.existsAsFile())
        {
            filmstripErrorMessage = "ERROR: Knob filmstrip not found at: " + knobFile.getFullPathName();
            DBG(filmstripErrorMessage);
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                   "Missing Skin File",
                                                   filmstripErrorMessage,
                                                   "OK");
            return;
        }

        knobFilmstrip = juce::ImageFileFormat::loadFrom(knobFile);

        if (!knobFilmstrip.isValid())
        {
            filmstripErrorMessage = "ERROR: Failed to load knob filmstrip from: " + knobFile.getFullPathName();
            DBG(filmstripErrorMessage);
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                   "Invalid Skin File",
                                                   filmstripErrorMessage,
                                                   "OK");
            return;
        }

        // Successfully loaded
        filmstripErrorMessage = "";
        DBG("Successfully loaded knob filmstrip: " + knobFile.getFullPathName());
    }

    bool useFilmstripForSlider(juce::Slider& slider) const
    {
        // Only use filmstrip for sliders named with slot-related names
        // Currently: count/rate slider as proof of concept
        juce::String name = slider.getName();
        return name.contains("Count") || name.contains("Rate");
    }

    void drawFilmstripKnob(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPosProportional, juce::Slider& slider)
    {
        if (!knobFilmstrip.isValid())
        {
            // Draw error indicator
            g.setColour(juce::Colours::red);
            g.drawRect(x, y, width, height, 2);
            g.drawText("X", x, y, width, height, juce::Justification::centred);
            return;
        }

        const int numFrames = 101;
        const int frameWidth = knobFilmstrip.getWidth();
        const int frameHeight = knobFilmstrip.getHeight() / numFrames;

        // Calculate which frame to display (0-100)
        int frameIndex = juce::jlimit(0, numFrames - 1,
                                     (int)(sliderPosProportional * (numFrames - 1) + 0.5f));

        // Calculate source rectangle for this frame
        const int sourceX = 0;
        const int sourceY = frameIndex * frameHeight;
        const int sourceWidth = frameWidth;
        const int sourceHeight = frameHeight;

        // Calculate destination rectangle (center the knob in the available space)
        const int knobSize = juce::jmin(width, height);
        const int destX = x + (width - knobSize) / 2;
        const int destY = y + (height - knobSize) / 2;
        const int destWidth = knobSize;
        const int destHeight = knobSize;

        // Draw the appropriate frame using JUCE's drawImage with explicit coordinates
        g.drawImage(knobFilmstrip,
                    destX, destY, destWidth, destHeight,
                    sourceX, sourceY, sourceWidth, sourceHeight,
                    false);
    }

    float cornerRadius = 6.0f;
    juce::Image knobFilmstrip;
    juce::String filmstripErrorMessage;
};
