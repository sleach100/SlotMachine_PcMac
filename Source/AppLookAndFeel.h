#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class AppLookAndFeel : public juce::LookAndFeel_V4
{
public:
    AppLookAndFeel()
    {
        loadSkinDefinition();
        loadKnobFilmstrip();
        loadBackgroundImages();
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

    void drawGroupComponentOutline (juce::Graphics& g, int width, int height,
                                    const juce::String& text,
                                    const juce::Justification& position,
                                    juce::GroupComponent& group) override
    {
        // Try to get flash state from SlotGroupComponent
        float flashState = 0.0f;
        bool hasFlashState = false;

        // Check if this is a custom slot group component with flash state
        // Use component name as a way to identify slot groups
        if (group.getName().startsWith("SLOT"))
        {
            // Try to get flash state via component properties
            auto flashVar = group.getProperties()["flashState"];
            if (!flashVar.isVoid())
            {
                flashState = (float)flashVar;
                hasFlashState = true;
            }
        }

        // Determine which background image to use
        bool useFlashImage = hasFlashState && flashState > 0.01f && backgroundFlashImage.isValid();
        const juce::Image* bgImage = useFlashImage ? &backgroundFlashImage : &backgroundImage;

        // Draw background image if available, otherwise use default rendering
        if (bgImage->isValid())
        {
            // Draw the background image stretched to fit the group bounds
            g.drawImage(*bgImage, 0, 0, width, height,
                       0, 0, bgImage->getWidth(), bgImage->getHeight());

            // Draw the title text on top of the background image
            auto textColour = group.findColour(juce::GroupComponent::textColourId);
            g.setColour(textColour);

            // Use standard font for group component title
            juce::Font font(15.0f, juce::Font::bold);
            g.setFont(font);

            g.drawText(text, 4, 0, width - 8, (int)font.getHeight(),
                      position, false);
        }
        else
        {
            // Fall back to default JUCE group component outline
            juce::LookAndFeel_V4::drawGroupComponentOutline(g, width, height, text, position, group);
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
        backgroundFilename = "";
        backgroundFlashFilename = "";

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
                else if (key.equalsIgnoreCase("Background"))
                {
                    backgroundFilename = value;
                    DBG("Skin: Background = " + backgroundFilename);
                }
                else if (key.equalsIgnoreCase("BackgroundFlash"))
                {
                    backgroundFlashFilename = value;
                    DBG("Skin: BackgroundFlash = " + backgroundFlashFilename);
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

    void loadBackgroundImages()
    {
        // Load normal background image
        if (backgroundFilename.isNotEmpty())
        {
            juce::File bgFile = juce::File::getCurrentWorkingDirectory()
                                   .getChildFile("Skins")
                                   .getChildFile("Default")
                                   .getChildFile(backgroundFilename);

            if (bgFile.existsAsFile())
            {
                backgroundImage = juce::ImageFileFormat::loadFrom(bgFile);
                if (backgroundImage.isValid())
                    DBG("Successfully loaded background: " + bgFile.getFullPathName());
                else
                    DBG("Failed to load background: " + bgFile.getFullPathName());
            }
            else
            {
                DBG("Background file not found: " + bgFile.getFullPathName());
            }
        }

        // Load flash background image
        if (backgroundFlashFilename.isNotEmpty())
        {
            juce::File bgFlashFile = juce::File::getCurrentWorkingDirectory()
                                         .getChildFile("Skins")
                                         .getChildFile("Default")
                                         .getChildFile(backgroundFlashFilename);

            if (bgFlashFile.existsAsFile())
            {
                backgroundFlashImage = juce::ImageFileFormat::loadFrom(bgFlashFile);
                if (backgroundFlashImage.isValid())
                    DBG("Successfully loaded background flash: " + bgFlashFile.getFullPathName());
                else
                    DBG("Failed to load background flash: " + bgFlashFile.getFullPathName());
            }
            else
            {
                DBG("Background flash file not found: " + bgFlashFile.getFullPathName());
            }
        }
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
    juce::String backgroundFilename;
    juce::String backgroundFlashFilename;

    // Loaded skin images
    juce::Image backgroundImage;
    juce::Image backgroundFlashImage;
};
