#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class AppLookAndFeel : public juce::LookAndFeel_V4
{
public:
    AppLookAndFeel()
    {
        loadSkinDefinition();
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
    void loadSkinDefinition()
    {
        // Set defaults
        knobFilename = "knob.png";
        knobFrameCount = 50;
        knobOrientation = "Vertical";

        juce::File skinDefFile = juce::File::getCurrentWorkingDirectory()
                                    .getChildFile("Skins")
                                    .getChildFile("Default")
                                    .getChildFile("SkinDef.txt");

        if (!skinDefFile.existsAsFile())
        {
            DBG("SkinDef.txt not found at: " + skinDefFile.getFullPathName() + " - using defaults");
            return;
        }

        juce::StringArray lines;
        skinDefFile.readLines(lines);

        for (const auto& line : lines)
        {
            // Skip empty lines and comments (lines starting with ;)
            auto trimmed = line.trim();
            if (trimmed.isEmpty() || trimmed.startsWith(";"))
                continue;

            // Parse key=value pairs
            int equalsPos = trimmed.indexOf("=");
            if (equalsPos > 0)
            {
                juce::String key = trimmed.substring(0, equalsPos).trim();
                juce::String value = trimmed.substring(equalsPos + 1).trim();

                if (key.equalsIgnoreCase("Knobs"))
                {
                    knobFilename = value;
                    DBG("Skin: Knobs = " + knobFilename);
                }
                else if (key.equalsIgnoreCase("KnobFrames"))
                {
                    knobFrameCount = value.getIntValue();
                    DBG("Skin: KnobFrames = " + juce::String(knobFrameCount));
                }
                else if (key.equalsIgnoreCase("KnobFilmstripOrientation"))
                {
                    knobOrientation = value;
                    DBG("Skin: KnobFilmstripOrientation = " + knobOrientation);
                }
            }
        }

        DBG("SkinDef.txt loaded successfully");
    }

    void loadKnobFilmstrip()
    {
        juce::File knobFile = juce::File::getCurrentWorkingDirectory()
                                .getChildFile("Skins")
                                .getChildFile("Default")
                                .getChildFile(knobFilename);

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
        if (!knobFilmstrip.isValid() || knobFrameCount <= 0)
            return;

        const int numFrames = knobFrameCount;
        const bool isVertical = knobOrientation.equalsIgnoreCase("Vertical");

        // Calculate frame dimensions based on orientation
        int frameWidth, frameHeight;
        if (isVertical)
        {
            frameWidth = knobFilmstrip.getWidth();
            frameHeight = knobFilmstrip.getHeight() / numFrames;
        }
        else // Horizontal
        {
            frameWidth = knobFilmstrip.getWidth() / numFrames;
            frameHeight = knobFilmstrip.getHeight();
        }

        // Calculate which frame to display
        int frameIndex = juce::jlimit(0, numFrames - 1,
                                     (int)(sliderPosProportional * (numFrames - 1) + 0.5f));

        // Calculate source rectangle for this frame based on orientation
        int sourceX, sourceY;
        if (isVertical)
        {
            sourceX = 0;
            sourceY = frameIndex * frameHeight;
        }
        else // Horizontal
        {
            sourceX = frameIndex * frameWidth;
            sourceY = 0;
        }
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

    // Skin definition parameters
    juce::String knobFilename;
    int knobFrameCount = 50;
    juce::String knobOrientation = "Vertical";
};
