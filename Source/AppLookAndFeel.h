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
        // Check if this is a tab button (TextButton with toggle state)
        bool isTab = dynamic_cast<juce::TextButton*>(&b) != nullptr && b.getClickingTogglesState();
        bool isTabSelected = isTab && b.getToggleState();

        // If it's a selected tab and we have a tab selected image, use that
        if (isTabSelected && tabSelectedImage.isValid())
        {
            auto bounds = b.getLocalBounds();
            g.drawImage(tabSelectedImage,
                       0, 0, bounds.getWidth(), bounds.getHeight(),
                       0, 0, tabSelectedImage.getWidth(), tabSelectedImage.getHeight(),
                       false);
        }
        // Otherwise use custom button images if available, based on button state
        else if (buttonImage.isValid())
        {
            auto bounds = b.getLocalBounds();
            const juce::Image* imageToUse = &buttonImage;

            // Select image based on state
            if (isDown && buttonClickedImage.isValid())
                imageToUse = &buttonClickedImage;
            else if (isOver && buttonHoverImage.isValid())
                imageToUse = &buttonHoverImage;

            // Draw the button image stretched to fit bounds
            g.drawImage(*imageToUse,
                       0, 0, bounds.getWidth(), bounds.getHeight(),
                       0, 0, imageToUse->getWidth(), imageToUse->getHeight(),
                       false);
        }
        else
        {
            // Fall back to default JUCE button rendering
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

    void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, float minSliderPos, float maxSliderPos,
                          const juce::Slider::SliderStyle style, juce::Slider& slider) override
    {
        DBG("drawLinearSlider called for: " + slider.getName() + " (style: " + juce::String((int)style) + ")");

        // If we have a custom thumb image for horizontal sliders, draw track only then custom thumb
        if (sliderThumbImage.isValid() && style == juce::Slider::LinearHorizontal)
        {
            // Draw the slider background/track using JUCE's method (without the thumb)
            drawLinearSliderBackground(g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);

            DBG("Drawing custom slider thumb for: " + slider.getName()
                + " (scale: " + juce::String(sliderThumbScale) + "%)");

            const int imgW = sliderThumbImage.getWidth();
            const int imgH = sliderThumbImage.getHeight();

            // Apply scale percentage
            const float scale = sliderThumbScale / 100.0f;
            const int scaledW = (int)(imgW * scale);
            const int scaledH = (int)(imgH * scale);

            // Calculate thumb position - sliderPos is the center X position of the thumb
            auto thumbX = (int)(sliderPos - scaledW * 0.5f);
            auto thumbY = y + (height - scaledH) / 2;

            // Draw the thumb image at scaled size
            g.drawImage(sliderThumbImage,
                       thumbX, thumbY, scaledW, scaledH,
                       0, 0, imgW, imgH,
                       false);
        }
        else
        {
            // No custom thumb - use default JUCE rendering
            juce::LookAndFeel_V4::drawLinearSlider(g, x, y, width, height,
                                                  sliderPos, minSliderPos, maxSliderPos,
                                                  style, slider);
        }
    }

    void drawLinearSliderThumb (juce::Graphics& g, int x, int y, int width, int height,
                               float sliderPos, float minSliderPos, float maxSliderPos,
                               const juce::Slider::SliderStyle style, juce::Slider& slider) override
    {
        DBG("drawLinearSliderThumb called for: " + slider.getName() + " (style: " + juce::String((int)style) + ")");

        // Use custom slider thumb image if available, otherwise use default rendering
        if (sliderThumbImage.isValid())
        {
            DBG("drawLinearSliderThumb: Using custom thumb image for slider: " + slider.getName()
                + " (bounds: " + juce::String(width) + "x" + juce::String(height) + ")");

            // Draw the static slider thumb image maintaining aspect ratio, centered in bounds
            const int imgW = sliderThumbImage.getWidth();
            const int imgH = sliderThumbImage.getHeight();

            // Calculate scaling to fit within bounds while maintaining aspect ratio
            const float scale = juce::jmin((float)width / (float)imgW, (float)height / (float)imgH);
            const int scaledW = (int)(imgW * scale);
            const int scaledH = (int)(imgH * scale);

            // Center the image within the thumb bounds
            const int drawX = x + (width - scaledW) / 2;
            const int drawY = y + (height - scaledH) / 2;

            g.drawImage(sliderThumbImage,
                       drawX, drawY, scaledW, scaledH,
                       0, 0, imgW, imgH,
                       false);
        }
        else
        {
            DBG("drawLinearSliderThumb: No custom thumb image, using default for: " + slider.getName());
            // Fall back to default JUCE slider thumb rendering
            juce::LookAndFeel_V4::drawLinearSliderThumb(g, x, y, width, height,
                                                        sliderPos, minSliderPos, maxSliderPos,
                                                        style, slider);
        }
    }

    void drawGroupComponentOutline (juce::Graphics& g, int width, int height,
                                    const juce::String& text,
                                    const juce::Justification& position,
                                    juce::GroupComponent& group) override
    {
        // Try to get flash state and hasFile state from SlotGroupComponent
        float flashState = 0.0f;
        bool hasFlashState = false;
        bool hasFile = false;

        // Check if this is a custom slot group component with flash state
        // Use the text parameter (not getName) to identify slot groups
        if (text.startsWith("SLOT"))
        {
            // Try to get flash state via component properties
            auto flashVar = group.getProperties()["flashState"];
            if (!flashVar.isVoid())
            {
                flashState = (float)flashVar;
                hasFlashState = true;
            }

            // Try to get hasFile state via component properties
            auto hasFileVar = group.getProperties()["hasFile"];
            if (!hasFileVar.isVoid())
            {
                hasFile = (bool)hasFileVar;
            }

            // Debug: log flash state when it's significant
            if (flashState > 0.01f)
            {
                DBG("Flash state for " + text + ": " + juce::String(flashState));
                DBG("  backgroundFlashImage.isValid(): " + juce::String(backgroundFlashImage.isValid() ? "true" : "false"));
            }
        }

        // Determine which background image to use
        // Priority: Flash > Sample Loaded > Normal Background
        const juce::Image* bgImage = nullptr;
        if (hasFlashState && flashState > 0.01f && backgroundFlashImage.isValid())
        {
            bgImage = &backgroundFlashImage;
            DBG("Using FLASH image for " + text);
        }
        else if (hasFile && backgroundSampleLoadedImage.isValid())
        {
            bgImage = &backgroundSampleLoadedImage;
        }
        else if (backgroundImage.isValid())
        {
            bgImage = &backgroundImage;
        }

        // Draw background image if available, otherwise use default rendering
        if (bgImage != nullptr)
        {
            // Draw the background image stretched to fit the group bounds
            g.drawImage(*bgImage, 0, 0, width, height,
                       0, 0, bgImage->getWidth(), bgImage->getHeight());

            // Don't draw slot number text when using custom background images
            // The background image may include the slot number or shouldn't be obscured
        }
        else
        {
            // Fall back to default JUCE group component outline
            juce::LookAndFeel_V4::drawGroupComponentOutline(g, width, height, text, position, group);
        }
    }

    void drawLabel (juce::Graphics& g, juce::Label& label) override
    {
        // Skip TextBox image for Mute, Solo, and Master BPM labels - leave them unaltered
        auto labelText = label.getText();
        bool isExcluded = labelText == "Mute" || labelText == "Solo" || labelText == "Master BPM";

        // Draw custom textbox image if available and not excluded label
        if (textboxImage.isValid() && !isExcluded)
        {
            auto bounds = label.getLocalBounds();

            // Draw the textbox image stretched to fit the label bounds
            g.drawImage(textboxImage,
                       0, 0, bounds.getWidth(), bounds.getHeight(),
                       0, 0, textboxImage.getWidth(), textboxImage.getHeight(),
                       false);

            // Draw the label text on top of the textbox image
            g.setColour(label.findColour(juce::Label::textColourId));
            g.setFont(label.getFont());

            auto textArea = bounds.reduced(2, 0); // Small padding
            g.drawText(label.getText(), textArea,
                      label.getJustificationType(),
                      label.getText().length() > 0);
        }
        else
        {
            // Fall back to default JUCE label rendering
            juce::LookAndFeel_V4::drawLabel(g, label);
        }
    }

    void drawComboBox (juce::Graphics& g, int width, int height, bool,
                      int, int, int, int, juce::ComboBox& box) override
    {
        // Draw custom textbox image if available, otherwise use default rendering
        if (textboxImage.isValid())
        {
            auto bounds = juce::Rectangle<int>(0, 0, width, height);

            // Draw the textbox image stretched to fit the combobox bounds
            g.drawImage(textboxImage,
                       0, 0, width, height,
                       0, 0, textboxImage.getWidth(), textboxImage.getHeight(),
                       false);

            // Draw the dropdown arrow on the right side
            auto arrowZone = juce::Rectangle<int>(width - 20, 0, 20, height);
            juce::Path path;
            path.startNewSubPath(arrowZone.getX() + 3.0f, arrowZone.getCentreY() - 2.0f);
            path.lineTo(arrowZone.getCentreX(), arrowZone.getCentreY() + 3.0f);
            path.lineTo(arrowZone.getRight() - 3.0f, arrowZone.getCentreY() - 2.0f);

            g.setColour(box.findColour(juce::ComboBox::arrowColourId).withAlpha(box.isEnabled() ? 0.9f : 0.2f));
            g.strokePath(path, juce::PathStrokeType(2.0f));
        }
        else
        {
            // Fall back to default JUCE combobox rendering
            juce::LookAndFeel_V4::drawComboBox(g, width, height, false, 0, 0, 0, 0, box);
        }
    }

    void drawPopupMenuBackground (juce::Graphics& g, int width, int height) override
    {
        // Draw custom textbox image if available, otherwise use default rendering
        if (textboxImage.isValid())
        {
            // Draw the textbox image stretched to fit the popup menu bounds
            g.drawImage(textboxImage,
                       0, 0, width, height,
                       0, 0, textboxImage.getWidth(), textboxImage.getHeight(),
                       false);
        }
        else
        {
            // Fall back to default JUCE popup menu background
            juce::LookAndFeel_V4::drawPopupMenuBackground(g, width, height);
        }
    }

    bool hasKnobFilmstrip() const { return knobFilmstrip.isValid(); }
    juce::String getKnobFilmstripError() const { return filmstripErrorMessage; }

    // Check if slot background images are loaded from skin
    bool hasSlotBackgroundImages() const
    {
        return backgroundImage.isValid() || backgroundFlashImage.isValid();
    }

    // Get filename label width from skin (0 = auto/dynamic)
    int getFileNameWidth() const { return fileNameWidth; }

    // Get load button width from skin (0 = auto/default 110px scaled)
    int getLoadButtonWidth() const { return loadButtonWidth; }

private:
    void loadSkinDefinition()
    {
        // Set defaults
        knobFilename = "knob.png";
        knobFrameCount = 50;
        knobOrientation = "Vertical";
        backgroundFilename = "";
        backgroundFlashFilename = "";
        backgroundSampleLoadedFilename = "";
        textboxFilename = "";
        fileNameWidth = 0; // 0 = auto/dynamic width
        loadButtonWidth = 0; // 0 = auto/default width (110px scaled)
        buttonFilename = "";
        buttonHoverFilename = "";
        buttonClickedFilename = "";
        sliderThumbFilename = "";
        sliderThumbScale = 100; // Default to 100% (actual size)
        tabSelectedFilename = "";

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
                else if (key.equalsIgnoreCase("BackgroundSampleLoaded"))
                {
                    backgroundSampleLoadedFilename = value;
                    DBG("Skin: BackgroundSampleLoaded = " + backgroundSampleLoadedFilename);
                }
                else if (key.equalsIgnoreCase("TextBox"))
                {
                    textboxFilename = value;
                    DBG("Skin: TextBox = " + textboxFilename);
                }
                else if (key.equalsIgnoreCase("FileNameWidth"))
                {
                    fileNameWidth = value.getIntValue();
                    DBG("Skin: FileNameWidth = " + juce::String(fileNameWidth));
                }
                else if (key.equalsIgnoreCase("LoadButtonWidth"))
                {
                    loadButtonWidth = value.getIntValue();
                    DBG("Skin: LoadButtonWidth = " + juce::String(loadButtonWidth));
                }
                else if (key.equalsIgnoreCase("Button"))
                {
                    buttonFilename = value;
                    DBG("Skin: Button = " + buttonFilename);
                }
                else if (key.equalsIgnoreCase("ButtonHover"))
                {
                    buttonHoverFilename = value;
                    DBG("Skin: ButtonHover = " + buttonHoverFilename);
                }
                else if (key.equalsIgnoreCase("ButtonClicked"))
                {
                    buttonClickedFilename = value;
                    DBG("Skin: ButtonClicked = " + buttonClickedFilename);
                }
                else if (key.equalsIgnoreCase("SliderThumb"))
                {
                    sliderThumbFilename = value;
                    DBG("Skin: SliderThumb = " + sliderThumbFilename);
                }
                else if (key.equalsIgnoreCase("SliderThumbScale"))
                {
                    sliderThumbScale = juce::jlimit(1, 100, value.getIntValue());
                    DBG("Skin: SliderThumbScale = " + juce::String(sliderThumbScale));
                }
                else if (key.equalsIgnoreCase("TabSelected"))
                {
                    tabSelectedFilename = value;
                    DBG("Skin: TabSelected = " + tabSelectedFilename);
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

        // Load sample loaded background image
        if (backgroundSampleLoadedFilename.isNotEmpty())
        {
            juce::File bgSampleLoadedFile = juce::File::getCurrentWorkingDirectory()
                                                .getChildFile("Skins")
                                                .getChildFile("Default")
                                                .getChildFile(backgroundSampleLoadedFilename);

            if (bgSampleLoadedFile.existsAsFile())
            {
                backgroundSampleLoadedImage = juce::ImageFileFormat::loadFrom(bgSampleLoadedFile);
                if (backgroundSampleLoadedImage.isValid())
                    DBG("Successfully loaded background sample loaded: " + bgSampleLoadedFile.getFullPathName());
                else
                    DBG("Failed to load background sample loaded: " + bgSampleLoadedFile.getFullPathName());
            }
            else
            {
                DBG("Background sample loaded file not found: " + bgSampleLoadedFile.getFullPathName());
            }
        }

        // Load textbox image
        if (textboxFilename.isNotEmpty())
        {
            juce::File textboxFile = juce::File::getCurrentWorkingDirectory()
                                         .getChildFile("Skins")
                                         .getChildFile("Default")
                                         .getChildFile(textboxFilename);

            if (textboxFile.existsAsFile())
            {
                textboxImage = juce::ImageFileFormat::loadFrom(textboxFile);
                if (textboxImage.isValid())
                    DBG("Successfully loaded textbox: " + textboxFile.getFullPathName());
                else
                    DBG("Failed to load textbox: " + textboxFile.getFullPathName());
            }
            else
            {
                DBG("Textbox file not found: " + textboxFile.getFullPathName());
            }
        }

        // Load button images
        if (buttonFilename.isNotEmpty())
        {
            juce::File buttonFile = juce::File::getCurrentWorkingDirectory()
                                        .getChildFile("Skins")
                                        .getChildFile("Default")
                                        .getChildFile(buttonFilename);

            if (buttonFile.existsAsFile())
            {
                buttonImage = juce::ImageFileFormat::loadFrom(buttonFile);
                if (buttonImage.isValid())
                    DBG("Successfully loaded button: " + buttonFile.getFullPathName());
                else
                    DBG("Failed to load button: " + buttonFile.getFullPathName());
            }
            else
            {
                DBG("Button file not found: " + buttonFile.getFullPathName());
            }
        }

        if (buttonHoverFilename.isNotEmpty())
        {
            juce::File buttonHoverFile = juce::File::getCurrentWorkingDirectory()
                                             .getChildFile("Skins")
                                             .getChildFile("Default")
                                             .getChildFile(buttonHoverFilename);

            if (buttonHoverFile.existsAsFile())
            {
                buttonHoverImage = juce::ImageFileFormat::loadFrom(buttonHoverFile);
                if (buttonHoverImage.isValid())
                    DBG("Successfully loaded button hover: " + buttonHoverFile.getFullPathName());
                else
                    DBG("Failed to load button hover: " + buttonHoverFile.getFullPathName());
            }
            else
            {
                DBG("Button hover file not found: " + buttonHoverFile.getFullPathName());
            }
        }

        if (buttonClickedFilename.isNotEmpty())
        {
            juce::File buttonClickedFile = juce::File::getCurrentWorkingDirectory()
                                               .getChildFile("Skins")
                                               .getChildFile("Default")
                                               .getChildFile(buttonClickedFilename);

            if (buttonClickedFile.existsAsFile())
            {
                buttonClickedImage = juce::ImageFileFormat::loadFrom(buttonClickedFile);
                if (buttonClickedImage.isValid())
                    DBG("Successfully loaded button clicked: " + buttonClickedFile.getFullPathName());
                else
                    DBG("Failed to load button clicked: " + buttonClickedFile.getFullPathName());
            }
            else
            {
                DBG("Button clicked file not found: " + buttonClickedFile.getFullPathName());
            }
        }

        // Load slider thumb image
        if (sliderThumbFilename.isNotEmpty())
        {
            juce::File sliderThumbFile = juce::File::getCurrentWorkingDirectory()
                                             .getChildFile("Skins")
                                             .getChildFile("Default")
                                             .getChildFile(sliderThumbFilename);

            if (sliderThumbFile.existsAsFile())
            {
                sliderThumbImage = juce::ImageFileFormat::loadFrom(sliderThumbFile);
                if (sliderThumbImage.isValid())
                    DBG("Successfully loaded slider thumb: " + sliderThumbFile.getFullPathName());
                else
                    DBG("Failed to load slider thumb: " + sliderThumbFile.getFullPathName());
            }
            else
            {
                DBG("Slider thumb file not found: " + sliderThumbFile.getFullPathName());
            }
        }

        // Load tab selected image
        if (tabSelectedFilename.isNotEmpty())
        {
            juce::File tabSelectedFile = juce::File::getCurrentWorkingDirectory()
                                             .getChildFile("Skins")
                                             .getChildFile("Default")
                                             .getChildFile(tabSelectedFilename);

            if (tabSelectedFile.existsAsFile())
            {
                tabSelectedImage = juce::ImageFileFormat::loadFrom(tabSelectedFile);
                if (tabSelectedImage.isValid())
                    DBG("Successfully loaded tab selected: " + tabSelectedFile.getFullPathName());
                else
                    DBG("Failed to load tab selected: " + tabSelectedFile.getFullPathName());
            }
            else
            {
                DBG("Tab selected file not found: " + tabSelectedFile.getFullPathName());
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
    juce::String backgroundSampleLoadedFilename;
    juce::String textboxFilename;
    int fileNameWidth = 0;
    int loadButtonWidth = 0;
    juce::String buttonFilename;
    juce::String buttonHoverFilename;
    juce::String buttonClickedFilename;
    juce::String sliderThumbFilename;
    int sliderThumbScale = 100; // Scale percentage (1-100)
    juce::String tabSelectedFilename;

    // Loaded skin images
    juce::Image backgroundImage;
    juce::Image backgroundFlashImage;
    juce::Image backgroundSampleLoadedImage;
    juce::Image textboxImage;
    juce::Image buttonImage;
    juce::Image buttonHoverImage;
    juce::Image buttonClickedImage;
    juce::Image sliderThumbImage;
    juce::Image tabSelectedImage;
};
