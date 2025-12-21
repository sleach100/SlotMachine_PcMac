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
        // Use filmstrip for slot knobs if filmstrip is valid, otherwise fall back to default
        if (useFilmstripForSlider(slider) && knobFilmstrip.isValid())
        {
            drawFilmstripKnob(g, x, y, width, height, sliderPosProportional, slider);
        }
        else
        {
            // Use default JUCE rendering for other sliders or if filmstrip failed to load
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
            filmstripErrorMessage = "Knob filmstrip not found at: " + knobFile.getFullPathName();
            DBG(filmstripErrorMessage + " - using default slider rendering");
            return;
        }

        knobFilmstrip = juce::ImageFileFormat::loadFrom(knobFile);

        if (!knobFilmstrip.isValid())
        {
            filmstripErrorMessage = "Failed to load knob filmstrip from: " + knobFile.getFullPathName();
            DBG(filmstripErrorMessage + " - using default slider rendering");
            return;
        }

        // Successfully loaded
        filmstripErrorMessage = "";
        DBG("Successfully loaded knob filmstrip: " + knobFile.getFullPathName());
    }

    bool useFilmstripForSlider(juce::Slider& slider) const
    {
        // Use filmstrip for all slot knobs (count/rate, gain, decay)
        juce::String name = slider.getName();
        return name.contains("Count") || name.contains("Rate") ||
               name.contains("Gain") || name.contains("Decay");
    }

    void drawFilmstripKnob(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPosProportional, juce::Slider& slider)
    {
        // This function should only be called if filmstrip is valid
        // (checked in drawRotarySlider), but double-check for safety
        if (!knobFilmstrip.isValid())
            return;

        const int numFrames = 50;
        const int frameWidth = knobFilmstrip.getWidth();
        const int frameHeight = knobFilmstrip.getHeight() / numFrames;

        // Calculate which frame to display (0-49)
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
