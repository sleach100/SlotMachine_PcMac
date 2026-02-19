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

    juce::Font getTextButtonFont (juce::TextButton& button, int buttonHeight) override
    {
        auto var = button.getProperties()["smallFont"];
        if (!var.isVoid() && (bool) var)
            return juce::Font{ juce::FontOptions((float) juce::jmin(12, buttonHeight - 1)) };
        return juce::LookAndFeel_V4::getTextButtonFont(button, buttonHeight);
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b,
                               const juce::Colour& bg, bool isOver, bool isDown) override
    {
        // Check if this is a tab button (TextButton with toggle state)
        bool isTab = dynamic_cast<juce::TextButton*>(&b) != nullptr && b.getClickingTogglesState();
        bool isTabSelected = isTab && b.getToggleState();

        // Count the number of tab siblings (only show TabSelected if there are multiple tabs)
        int tabCount = 0;
        if (isTab && b.getParentComponent() != nullptr)
        {
            for (auto* child : b.getParentComponent()->getChildren())
            {
                if (auto* btn = dynamic_cast<juce::TextButton*>(child))
                {
                    if (btn->getClickingTogglesState())
                        tabCount++;
                }
            }
        }

        // If it's a selected tab and we have a tab selected image, use that
        // But only if there are multiple tabs (if only 1 tab, use default button image)
        if (isTabSelected && tabCount > 1 && tabSelectedImage.isValid())
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
        // If we have a custom thumb image for horizontal sliders, draw track only then custom thumb
        if (sliderThumbImage.isValid() && style == juce::Slider::LinearHorizontal)
        {
            // Draw the slider background/track - custom image if available, otherwise JUCE default
            if (sliderTrackImage.isValid())
            {

                // Get the proper slider layout from JUCE to know where the track should be drawn
                auto layout = getSliderLayout(slider);
                auto trackBounds = layout.sliderBounds;

                // Draw the custom slider track image
                const int imgW = sliderTrackImage.getWidth();
                const int imgH = sliderTrackImage.getHeight();

                const int trackWidth = trackBounds.getWidth();
                const int trackHeight = trackBounds.getHeight();

                int scaledW, scaledH;
                int drawX, drawY;

                // If image is smaller than available width, stretch horizontally to fill
                if (imgW < trackWidth)
                {
                    scaledW = trackWidth;
                    // Maintain aspect ratio for height
                    scaledH = juce::roundToInt(imgH * ((float)trackWidth / (float)imgW));
                    drawX = trackBounds.getX();
                    drawY = trackBounds.getY() + (trackHeight - scaledH) / 2;
                }
                else
                {
                    // Image is larger than or equal to width - scale to fit while maintaining aspect ratio
                    const float scaleW = (float)trackWidth / (float)imgW;
                    const float scaleH = (float)trackHeight / (float)imgH;
                    const float scale = juce::jmin(scaleW, scaleH);

                    scaledW = juce::roundToInt(imgW * scale);
                    scaledH = juce::roundToInt(imgH * scale);

                    // Center the image within the track bounds
                    drawX = trackBounds.getX() + (trackWidth - scaledW) / 2;
                    drawY = trackBounds.getY() + (trackHeight - scaledH) / 2;
                }

                g.drawImage(sliderTrackImage,
                           drawX, drawY, scaledW, scaledH,
                           0, 0, imgW, imgH,
                           false);
            }
            else
            {
                // Draw the slider background/track using JUCE's method (without the thumb)
                drawLinearSliderBackground(g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
            }

            const int imgW = sliderThumbImage.getWidth();
            const int imgH = sliderThumbImage.getHeight();

            // Apply scale percentage
            const float scale = sliderThumbScale / 100.0f;
            const int scaledW = (int)(imgW * scale);
            const int scaledH = (int)(imgH * scale);

            // Calculate thumb position - sliderPos is the center X position of the thumb
            auto thumbX = juce::roundToInt(sliderPos - scaledW * 0.5f);
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
        // Use custom slider thumb image if available, otherwise use default rendering
        if (sliderThumbImage.isValid())
        {

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
        }

        // Determine which background image to use
        // Priority: Flash > Sample Loaded > Normal Background
        const juce::Image* bgImage = nullptr;
        if (hasFlashState && flashState > 0.01f && backgroundFlashImage.isValid())
        {
            bgImage = &backgroundFlashImage;
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
        // Skip TextBox image for Mute, Solo, Master BPM labels, file labels, and ComboBox labels
        auto labelText = label.getText();
        auto labelName = label.getName();
        bool isExcluded = labelText == "Mute" || labelText == "Solo" || labelText == "Master BPM" || labelName == "SlotFileLabel";

        // Check if this label is inside a ComboBox - if so, exclude it from TextBox background
        // The ComboBox already draws its own TextBox background via drawComboBox
        if (!isExcluded && label.getParentComponent() != nullptr)
        {
            if (dynamic_cast<juce::ComboBox*>(label.getParentComponent()) != nullptr)
            {
                isExcluded = true;
            }
        }

        // Check if this is a numeric label (count, volume, decay, Master BPM slider)
        // Numeric labels contain only digits, decimal points, minus signs, and spaces
        bool isNumeric = false;
        if (!isExcluded && labelText.isNotEmpty())
        {
            isNumeric = true;
            for (int i = 0; i < labelText.length(); ++i)
            {
                juce::juce_wchar c = labelText[i];
                if (!juce::CharacterFunctions::isDigit(c) && c != '.' && c != '-' && c != ' ')
                {
                    isNumeric = false;
                    break;
                }
            }
        }

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
            // Use custom font and color for numeric labels if available
            if (isNumeric && textboxFontFilename.isNotEmpty())
            {
                g.setFont(textboxCustomFont);
            }
            else
            {
                g.setFont(label.getFont());
            }

            if (isNumeric && textboxFontColor.isNotEmpty())
            {
                g.setColour(textboxCustomColor);
            }
            else
            {
                g.setColour(label.findColour(juce::Label::textColourId));
            }

            auto textArea = bounds.reduced(2, 0); // Small padding
            g.drawText(label.getText(), textArea,
                      label.getJustificationType(),
                      label.getText().length() > 0);
        }
        else
        {
            // No textbox image, but still apply custom font/color to numeric labels if available
            if (isNumeric && (textboxFontFilename.isNotEmpty() || textboxFontColor.isNotEmpty()))
            {
                auto bounds = label.getLocalBounds();

                if (textboxFontFilename.isNotEmpty())
                    g.setFont(textboxCustomFont);
                else
                    g.setFont(label.getFont());

                if (textboxFontColor.isNotEmpty())
                    g.setColour(textboxCustomColor);
                else
                    g.setColour(label.findColour(juce::Label::textColourId));

                g.drawText(label.getText(), bounds,
                          label.getJustificationType(),
                          label.getText().length() > 0);
            }
            else
            {
                // Fall back to default JUCE label rendering
                juce::LookAndFeel_V4::drawLabel(g, label);
            }
        }
    }

    juce::Font getLabelFont (juce::Label& label) override
    {
        // Check if this is a numeric label (count, volume, decay, Master BPM slider)
        auto labelText = label.getText();
        auto labelName = label.getName();
        bool isExcluded = labelText == "Mute" || labelText == "Solo" || labelText == "Master BPM" || labelName == "SlotFileLabel";

        // Check if this is a numeric label
        bool isNumeric = false;
        if (!isExcluded && labelText.isNotEmpty())
        {
            isNumeric = true;
            for (int i = 0; i < labelText.length(); ++i)
            {
                juce::juce_wchar c = labelText[i];
                if (!juce::CharacterFunctions::isDigit(c) && c != '.' && c != '-' && c != ' ')
                {
                    isNumeric = false;
                    break;
                }
            }
        }

        // Apply custom font and color for numeric labels if available
        if (isNumeric)
        {
            // Set the label's text color for both display and editing
            if (textboxFontColor.isNotEmpty())
            {
                label.setColour(juce::Label::textColourId, textboxCustomColor);
                label.setColour(juce::Label::textWhenEditingColourId, textboxCustomColor);
            }

            // Return custom font if available
            if (textboxFontFilename.isNotEmpty())
            {
                return textboxCustomFont;
            }
        }

        // Fall back to default font
        return juce::LookAndFeel_V4::getLabelFont(label);
    }

    void drawComboBox (juce::Graphics& g, int width, int height, bool,
                      int, int, int, int, juce::ComboBox& box) override
    {
        // Draw custom textbox image if available, otherwise use default rendering
        if (textboxImage.isValid())
        {
            // First, fill with transparent to ensure no default background shows through
            g.fillAll(juce::Colours::transparentBlack);

            // Draw the textbox image stretched to fit the combobox bounds
            // This creates a single background that both the text and arrow will appear over
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
        // Draw custom MIDI list panel background image if available
        // This is used for the MIDI Channel Selector dropdown list
        // Falls back to default JUCE rendering (not TextBox) to avoid vertical distortion
        if (midiListPanelBackgroundImage.isValid())
        {
            // First, fill the entire background with transparent to enable window transparency
            g.fillAll(juce::Colours::transparentBlack);

            // Then draw the custom image on top, scaled to fit
            // The image's opaque pixels will show, transparent pixels will show desktop
            g.setOpacity(1.0f);
            g.drawImage(midiListPanelBackgroundImage,
                       0, 0, width, height,
                       0, 0, midiListPanelBackgroundImage.getWidth(), midiListPanelBackgroundImage.getHeight(),
                       false);
        }
        else
        {
            // Fall back to default JUCE popup menu background (not TextBox)
            juce::LookAndFeel_V4::drawPopupMenuBackground(g, width, height);
        }
    }


    void drawPopupMenuItem(juce::Graphics& g, const juce::Rectangle<int>& area,
                          bool isSeparator, bool isActive, bool isHighlighted,
                          bool isTicked, bool hasSubMenu,
                          const juce::String& text,
                          const juce::String& shortcutKeyText,
                          const juce::Drawable* icon,
                          const juce::Colour* textColourToUse) override
    {
        // Priority: MidiSelectorColor > TextBoxFontColor > default
        juce::Colour textColour;
        if (textColourToUse != nullptr)
        {
            textColour = *textColourToUse;
        }
        else if (midiSelectorFontColor.isNotEmpty())
        {
            textColour = midiSelectorCustomColor;
        }
        else if (textboxFontColor.isNotEmpty())
        {
            textColour = textboxCustomColor;
        }
        else
        {
            textColour = findColour(juce::PopupMenu::textColourId);
        }

        // Pass the custom color to the base implementation
        juce::LookAndFeel_V4::drawPopupMenuItem(g, area, isSeparator, isActive, isHighlighted,
                                                isTicked, hasSubMenu, text, shortcutKeyText,
                                                icon, &textColour);
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

    // Get progress bar width from skin (0 = use default full width)
    int getProgressBarWidth() const { return progressBarWidth; }

    // Check if progress bar should be hidden (true = always hide)
    bool shouldHideProgressBar() const { return hideProgressBar.equalsIgnoreCase("True"); }

    // Get MIDI list width from skin (0 = use default width)
    int getMidiListWidth() const { return midiListWidth; }

    // Set the font scale factor applied when reloading skin fonts
    void setFontScale(float scale) { fontScale = juce::jlimit(0.5f, 2.0f, scale); }

    // Get mute/solo button images from skin
    const juce::Image& getMuteOnImage() const { return muteOnImage; }
    const juce::Image& getMuteOffImage() const { return muteOffImage; }
    const juce::Image& getSoloOnImage() const { return soloOnImage; }
    const juce::Image& getSoloOffImage() const { return soloOffImage; }

    // Get logo image from skin
    const juce::Image& getLogoImage() const { return logoImage; }

    // Get Master BPM label image from skin
    const juce::Image& getMasterBpmImage() const { return masterBpmImage; }

    // Get button font color from skin
    juce::Colour getButtonFontColor() const { return buttonCustomColor; }
    bool hasButtonFontColor() const { return buttonFontColor.isNotEmpty(); }

    // Get textbox font color from skin
    juce::Colour getTextBoxFontColor() const { return textboxCustomColor; }
    bool hasTextBoxFontColor() const { return textboxFontColor.isNotEmpty(); }

    // Get MIDI selector font color from skin (overrides TextBoxFontColor if present)
    juce::Colour getMidiSelectorFontColor() const { return midiSelectorCustomColor; }
    bool hasMidiSelectorFontColor() const { return midiSelectorFontColor.isNotEmpty(); }

    // Set skin folder name (default is "Classic")
    void setSkinFolder(const juce::String& folderName)
    {
        skinFolderName = folderName.isEmpty() ? "Classic" : folderName;
    }

    // Get current skin folder name
    juce::String getSkinFolder() const { return skinFolderName; }

    // Reload skin from SkinDef.txt
    void reloadSkin()
    {
        loadSkinDefinition();
        loadKnobFilmstrip();
        loadBackgroundImages();
    }

private:
    // Helper function to get the Skins base directory from user's Documents folder
    juce::File getSkinsDirectory() const
    {
        return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                   .getChildFile("SlotMachine")
                   .getChildFile("Skins");
    }

    void loadSkinDefinition()
    {
        // Set defaults
        knobFilename = "knob.png";
        knobFrameCount = 50;
        knobOrientation = "Vertical";
        staticKnob = false;
        knobShadow.enabled = false;  // No shadow by default
        backgroundFilename = "";
        backgroundFlashFilename = "";
        backgroundSampleLoadedFilename = "";
        textboxFilename = "";
        midiListPanelBackgroundFilename = "";
        fileNameWidth = 0; // 0 = auto/dynamic width
        loadButtonWidth = 0; // 0 = auto/default width (110px scaled)
        buttonFilename = "";
        buttonHoverFilename = "";
        buttonClickedFilename = "";
        sliderThumbFilename = "";
        sliderThumbScale = 100; // Default to 100% (actual size)
        sliderTrackFilename = "";
        tabSelectedFilename = "";
        muteOnFilename = "";
        muteOffFilename = "";
        soloOnFilename = "";
        soloOffFilename = "";
        logoFilename = "";
        masterBpmImageFilename = "";
        textboxFontFilename = "";
        textboxFontSize = 0; // 0 = use default
        textboxFontColor = ""; // Empty = use default
        midiSelectorFontColor = ""; // Empty = use TextBoxFontColor or default
        progressBarWidth = 0; // 0 = use default full width
        hideProgressBar = ""; // Empty = use default behavior
        midiListWidth = 0; // 0 = use default width
        buttonFontColor = ""; // Empty = use default

        juce::File skinDefFile = getSkinsDirectory()
                                    .getChildFile(skinFolderName)
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

            // Remove inline comments (everything after first ;)
            int commentPos = trimmed.indexOf(";");
            if (commentPos >= 0)
                trimmed = trimmed.substring(0, commentPos).trim();

            // Skip if nothing left after removing comment
            if (trimmed.isEmpty())
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
                else if (key.equalsIgnoreCase("StaticKnob"))
                {
                    staticKnob = value.equalsIgnoreCase("True");
                    DBG("Skin: StaticKnob = " + juce::String(staticKnob ? "True" : "False"));
                }
                else if (key.equalsIgnoreCase("KnobShadow"))
                {
                    // Parse KnobShadow=radiusPercent,outerEdgeOpacity,verticalOffsetPercent,horizontalOffsetPercent
                    // Example: KnobShadow=110,10,15,15
                    juce::StringArray parts;
                    parts.addTokens(value, ",", "");

                    if (parts.size() == 4)
                    {
                        knobShadow.radiusPercent = parts[0].trim().getFloatValue();
                        knobShadow.outerEdgeOpacity = parts[1].trim().getFloatValue();
                        knobShadow.verticalOffsetPercent = parts[2].trim().getFloatValue();
                        knobShadow.horizontalOffsetPercent = parts[3].trim().getFloatValue();
                        knobShadow.enabled = true;

                        DBG("Skin: KnobShadow = " + juce::String(knobShadow.radiusPercent) + "%, "
                            + juce::String(knobShadow.outerEdgeOpacity) + "%, "
                            + juce::String(knobShadow.verticalOffsetPercent) + "%, "
                            + juce::String(knobShadow.horizontalOffsetPercent) + "%");
                    }
                    else
                    {
                        DBG("Skin: KnobShadow format invalid (expected 4 comma-separated values), shadow disabled");
                        knobShadow.enabled = false;
                    }
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
                else if (key.equalsIgnoreCase("MIDI_ListPanelBackground"))
                {
                    midiListPanelBackgroundFilename = value;
                    DBG("Skin: MIDI_ListPanelBackground = " + midiListPanelBackgroundFilename);
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
                else if (key.equalsIgnoreCase("SliderTrack"))
                {
                    sliderTrackFilename = value;
                    DBG("Skin: SliderTrack = " + sliderTrackFilename);
                }
                else if (key.equalsIgnoreCase("TabSelected"))
                {
                    tabSelectedFilename = value;
                    DBG("Skin: TabSelected = " + tabSelectedFilename);
                }
                else if (key.equalsIgnoreCase("MuteON"))
                {
                    muteOnFilename = value;
                    DBG("Skin: MuteON = " + muteOnFilename);
                }
                else if (key.equalsIgnoreCase("MuteOFF"))
                {
                    muteOffFilename = value;
                    DBG("Skin: MuteOFF = " + muteOffFilename);
                }
                else if (key.equalsIgnoreCase("SoloON"))
                {
                    soloOnFilename = value;
                    DBG("Skin: SoloON = " + soloOnFilename);
                }
                else if (key.equalsIgnoreCase("SoloOFF"))
                {
                    soloOffFilename = value;
                    DBG("Skin: SoloOFF = " + soloOffFilename);
                }
                else if (key.equalsIgnoreCase("Logo"))
                {
                    logoFilename = value;
                    DBG("Skin: Logo = " + logoFilename);
                }
                else if (key.equalsIgnoreCase("MasterBPM_Image"))
                {
                    masterBpmImageFilename = value;
                    DBG("Skin: MasterBPM_Image = " + masterBpmImageFilename);
                }
                else if (key.equalsIgnoreCase("TextBoxFont"))
                {
                    textboxFontFilename = value;
                    DBG("Skin: TextBoxFont = " + textboxFontFilename);
                }
                else if (key.equalsIgnoreCase("TextBoxFontSize"))
                {
                    textboxFontSize = value.getIntValue();
                    DBG("Skin: TextBoxFontSize = " + juce::String(textboxFontSize));
                }
                else if (key.equalsIgnoreCase("TextBoxFontColor"))
                {
                    textboxFontColor = value;
                    DBG("Skin: TextBoxFontColor = " + textboxFontColor);
                }
                else if (key.equalsIgnoreCase("MidiSelectorColor"))
                {
                    midiSelectorFontColor = value;
                    DBG("Skin: MidiSelectorColor = " + midiSelectorFontColor);
                }
                else if (key.equalsIgnoreCase("ProgressBarWidth"))
                {
                    progressBarWidth = value.getIntValue();
                    DBG("Skin: ProgressBarWidth = " + juce::String(progressBarWidth));
                }
                else if (key.equalsIgnoreCase("HideProgressBar"))
                {
                    hideProgressBar = value;
                    DBG("Skin: HideProgressBar = " + hideProgressBar);
                }
                else if (key.equalsIgnoreCase("MIDI_ListWidth"))
                {
                    midiListWidth = value.getIntValue();
                    DBG("Skin: MIDI_ListWidth = " + juce::String(midiListWidth));
                }
                else if (key.equalsIgnoreCase("ButtonFontColor"))
                {
                    buttonFontColor = value;
                    DBG("Skin: ButtonFontColor = " + buttonFontColor);
                }
            }
        }

        DBG("SkinDef.txt loaded successfully");
    }

    void loadKnobFilmstrip()
    {
        // Clear filmstrip first to ensure proper reset
        knobFilmstrip = juce::Image();

        juce::File knobFile = getSkinsDirectory()
                                .getChildFile(skinFolderName)
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
        // Clear all images first to ensure proper reset when SkinDef.txt is removed or parameters are missing
        backgroundImage = juce::Image();
        backgroundFlashImage = juce::Image();
        backgroundSampleLoadedImage = juce::Image();
        textboxImage = juce::Image();
        midiListPanelBackgroundImage = juce::Image();
        buttonImage = juce::Image();
        buttonHoverImage = juce::Image();
        buttonClickedImage = juce::Image();
        sliderThumbImage = juce::Image();
        sliderTrackImage = juce::Image();
        tabSelectedImage = juce::Image();
        muteOnImage = juce::Image();
        muteOffImage = juce::Image();
        soloOnImage = juce::Image();
        soloOffImage = juce::Image();
        logoImage = juce::Image();
        masterBpmImage = juce::Image();
        textboxCustomFont = juce::Font();
        textboxCustomColor = juce::Colour();
        midiSelectorCustomColor = juce::Colour();

        // Load normal background image
        if (backgroundFilename.isNotEmpty())
        {
            juce::File bgFile = getSkinsDirectory()
                                   .getChildFile(skinFolderName)
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
            juce::File bgFlashFile = getSkinsDirectory()
                                         .getChildFile(skinFolderName)
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
            juce::File bgSampleLoadedFile = getSkinsDirectory()
                                                .getChildFile(skinFolderName)
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
            juce::File textboxFile = getSkinsDirectory()
                                         .getChildFile(skinFolderName)
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

        // Load MIDI list panel background image
        if (midiListPanelBackgroundFilename.isNotEmpty())
        {
            juce::File midiListPanelBgFile = getSkinsDirectory()
                                                 .getChildFile(skinFolderName)
                                                 .getChildFile(midiListPanelBackgroundFilename);

            if (midiListPanelBgFile.existsAsFile())
            {
                midiListPanelBackgroundImage = juce::ImageFileFormat::loadFrom(midiListPanelBgFile);
                if (midiListPanelBackgroundImage.isValid())
                {
                    DBG("Successfully loaded MIDI list panel background: " + midiListPanelBgFile.getFullPathName());
                    // Set popup menu background to transparent so desktop shows through transparent image corners
                    setColour(juce::PopupMenu::backgroundColourId, juce::Colours::transparentBlack);
                }
                else
                    DBG("Failed to load MIDI list panel background: " + midiListPanelBgFile.getFullPathName());
            }
            else
            {
                DBG("MIDI list panel background file not found: " + midiListPanelBgFile.getFullPathName());
            }
        }
        else
        {
            // No custom MIDI list panel background - restore default popup menu background colour
            setColour(juce::PopupMenu::backgroundColourId, juce::Colour(0xff1a1a1a));
        }

        // Load button images
        if (buttonFilename.isNotEmpty())
        {
            juce::File buttonFile = getSkinsDirectory()
                                        .getChildFile(skinFolderName)
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
            juce::File buttonHoverFile = getSkinsDirectory()
                                             .getChildFile(skinFolderName)
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
            juce::File buttonClickedFile = getSkinsDirectory()
                                               .getChildFile(skinFolderName)
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
            juce::File sliderThumbFile = getSkinsDirectory()
                                             .getChildFile(skinFolderName)
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

        // Load slider track image
        if (sliderTrackFilename.isNotEmpty())
        {
            juce::File sliderTrackFile = getSkinsDirectory()
                                             .getChildFile(skinFolderName)
                                             .getChildFile(sliderTrackFilename);

            if (sliderTrackFile.existsAsFile())
            {
                sliderTrackImage = juce::ImageFileFormat::loadFrom(sliderTrackFile);
                if (sliderTrackImage.isValid())
                    DBG("Successfully loaded slider track: " + sliderTrackFile.getFullPathName());
                else
                    DBG("Failed to load slider track: " + sliderTrackFile.getFullPathName());
            }
            else
            {
                DBG("Slider track file not found: " + sliderTrackFile.getFullPathName());
            }
        }

        // Load tab selected image
        if (tabSelectedFilename.isNotEmpty())
        {
            juce::File tabSelectedFile = getSkinsDirectory()
                                             .getChildFile(skinFolderName)
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

        // Load mute/solo button images
        if (muteOnFilename.isNotEmpty())
        {
            juce::File muteOnFile = getSkinsDirectory()
                                        .getChildFile(skinFolderName)
                                        .getChildFile(muteOnFilename);
            if (muteOnFile.existsAsFile())
            {
                muteOnImage = juce::ImageFileFormat::loadFrom(muteOnFile);
                if (muteOnImage.isValid())
                    DBG("Successfully loaded mute ON: " + muteOnFile.getFullPathName());
                else
                    DBG("Failed to load mute ON: " + muteOnFile.getFullPathName());
            }
            else
                DBG("Mute ON file not found: " + muteOnFile.getFullPathName());
        }

        if (muteOffFilename.isNotEmpty())
        {
            juce::File muteOffFile = getSkinsDirectory()
                                         .getChildFile(skinFolderName)
                                         .getChildFile(muteOffFilename);
            if (muteOffFile.existsAsFile())
            {
                muteOffImage = juce::ImageFileFormat::loadFrom(muteOffFile);
                if (muteOffImage.isValid())
                    DBG("Successfully loaded mute OFF: " + muteOffFile.getFullPathName());
                else
                    DBG("Failed to load mute OFF: " + muteOffFile.getFullPathName());
            }
            else
                DBG("Mute OFF file not found: " + muteOffFile.getFullPathName());
        }

        if (soloOnFilename.isNotEmpty())
        {
            juce::File soloOnFile = getSkinsDirectory()
                                        .getChildFile(skinFolderName)
                                        .getChildFile(soloOnFilename);
            if (soloOnFile.existsAsFile())
            {
                soloOnImage = juce::ImageFileFormat::loadFrom(soloOnFile);
                if (soloOnImage.isValid())
                    DBG("Successfully loaded solo ON: " + soloOnFile.getFullPathName());
                else
                    DBG("Failed to load solo ON: " + soloOnFile.getFullPathName());
            }
            else
                DBG("Solo ON file not found: " + soloOnFile.getFullPathName());
        }

        if (soloOffFilename.isNotEmpty())
        {
            juce::File soloOffFile = getSkinsDirectory()
                                         .getChildFile(skinFolderName)
                                         .getChildFile(soloOffFilename);
            if (soloOffFile.existsAsFile())
            {
                soloOffImage = juce::ImageFileFormat::loadFrom(soloOffFile);
                if (soloOffImage.isValid())
                    DBG("Successfully loaded solo OFF: " + soloOffFile.getFullPathName());
                else
                    DBG("Failed to load solo OFF: " + soloOffFile.getFullPathName());
            }
            else
                DBG("Solo OFF file not found: " + soloOffFile.getFullPathName());
        }

        // Load logo image
        if (logoFilename.isNotEmpty())
        {
            juce::File logoFile = getSkinsDirectory()
                                      .getChildFile(skinFolderName)
                                      .getChildFile(logoFilename);
            if (logoFile.existsAsFile())
            {
                logoImage = juce::ImageFileFormat::loadFrom(logoFile);
                if (logoImage.isValid())
                    DBG("Successfully loaded logo: " + logoFile.getFullPathName());
                else
                    DBG("Failed to load logo: " + logoFile.getFullPathName());
            }
            else
                DBG("Logo file not found: " + logoFile.getFullPathName());
        }

        // Load Master BPM label image
        if (masterBpmImageFilename.isNotEmpty())
        {
            juce::File masterBpmFile = getSkinsDirectory()
                                           .getChildFile(skinFolderName)
                                           .getChildFile(masterBpmImageFilename);
            if (masterBpmFile.existsAsFile())
            {
                masterBpmImage = juce::ImageFileFormat::loadFrom(masterBpmFile);
                if (masterBpmImage.isValid())
                    DBG("Successfully loaded Master BPM image: " + masterBpmFile.getFullPathName());
                else
                    DBG("Failed to load Master BPM image: " + masterBpmFile.getFullPathName());
            }
            else
                DBG("Master BPM image file not found: " + masterBpmFile.getFullPathName());
        }

        // Load custom font for numeric textboxes
        if (textboxFontFilename.isNotEmpty())
        {
            juce::File fontFile = getSkinsDirectory()
                                      .getChildFile(skinFolderName)
                                      .getChildFile(textboxFontFilename);
            if (fontFile.existsAsFile())
            {
                juce::MemoryBlock fontData;
                if (fontFile.loadFileAsData(fontData))
                {
                    juce::Typeface::Ptr typeface = juce::Typeface::createSystemTypefaceFor(fontData.getData(), fontData.getSize());
                    if (typeface != nullptr)
                    {
                        float fontSize = (textboxFontSize > 0 ? (float)textboxFontSize : 14.0f) * fontScale;
                        textboxCustomFont = juce::Font(typeface).withHeight(fontSize);
                        DBG("Successfully loaded custom font: " + fontFile.getFullPathName());
                    }
                    else
                    {
                        DBG("Failed to create typeface from font file: " + fontFile.getFullPathName());
                    }
                }
                else
                {
                    DBG("Failed to load font file data: " + fontFile.getFullPathName());
                }
            }
            else
            {
                DBG("Custom font file not found: " + fontFile.getFullPathName());
            }
        }

        // Parse custom font color
        if (textboxFontColor.isNotEmpty())
        {
            juce::String colorStr = textboxFontColor.trim();

            // Remove '#' prefix if present
            if (colorStr.startsWithChar('#'))
                colorStr = colorStr.substring(1);

            // Parse hex color (RGB or RRGGBB format)
            if (colorStr.length() == 6 || colorStr.length() == 3)
            {
                // Convert to full RRGGBB if using shorthand RGB
                if (colorStr.length() == 3)
                {
                    juce::String r = colorStr.substring(0, 1);
                    juce::String g = colorStr.substring(1, 2);
                    juce::String b = colorStr.substring(2, 3);
                    colorStr = r + r + g + g + b + b;
                }

                // Parse the hex string as RRGGBB
                int rgb = colorStr.getHexValue32();
                textboxCustomColor = juce::Colour((juce::uint8)((rgb >> 16) & 0xFF),
                                                   (juce::uint8)((rgb >> 8) & 0xFF),
                                                   (juce::uint8)(rgb & 0xFF));
                DBG("Custom font color set to: #" + colorStr + " = " + textboxCustomColor.toString());
            }
            else
            {
                DBG("Invalid color format: " + textboxFontColor + " (expected #RRGGBB)");
            }
        }

        // Parse button font color
        if (buttonFontColor.isNotEmpty())
        {
            juce::String colorStr = buttonFontColor.trim();

            // Remove '#' prefix if present
            if (colorStr.startsWithChar('#'))
                colorStr = colorStr.substring(1);

            // Parse hex color (RGB or RRGGBB format)
            if (colorStr.length() == 6 || colorStr.length() == 3)
            {
                // Convert to full RRGGBB if using shorthand RGB
                if (colorStr.length() == 3)
                {
                    juce::String r = colorStr.substring(0, 1);
                    juce::String g = colorStr.substring(1, 2);
                    juce::String b = colorStr.substring(2, 3);
                    colorStr = r + r + g + g + b + b;
                }

                // Parse the hex string as RRGGBB
                int rgb = colorStr.getHexValue32();
                buttonCustomColor = juce::Colour((juce::uint8)((rgb >> 16) & 0xFF),
                                                  (juce::uint8)((rgb >> 8) & 0xFF),
                                                  (juce::uint8)(rgb & 0xFF));
                DBG("Button font color set to: #" + colorStr + " = " + buttonCustomColor.toString());
            }
            else
            {
                DBG("Invalid color format: " + buttonFontColor + " (expected #RRGGBB)");
            }
        }

        // Parse MIDI selector font color
        if (midiSelectorFontColor.isNotEmpty())
        {
            juce::String colorStr = midiSelectorFontColor.trim();

            // Remove '#' prefix if present
            if (colorStr.startsWithChar('#'))
                colorStr = colorStr.substring(1);

            // Parse hex color (RGB or RRGGBB format)
            if (colorStr.length() == 6 || colorStr.length() == 3)
            {
                // Convert to full RRGGBB if using shorthand RGB
                if (colorStr.length() == 3)
                {
                    juce::String r = colorStr.substring(0, 1);
                    juce::String g = colorStr.substring(1, 2);
                    juce::String b = colorStr.substring(2, 3);
                    colorStr = r + r + g + g + b + b;
                }

                // Parse the hex string as RRGGBB
                int rgb = colorStr.getHexValue32();
                midiSelectorCustomColor = juce::Colour((juce::uint8)((rgb >> 16) & 0xFF),
                                                       (juce::uint8)((rgb >> 8) & 0xFF),
                                                       (juce::uint8)(rgb & 0xFF));
                DBG("MIDI selector font color set to: #" + colorStr + " = " + midiSelectorCustomColor.toString());
            }
            else
            {
                DBG("Invalid color format: " + midiSelectorFontColor + " (expected #RRGGBB)");
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
        if (!knobFilmstrip.isValid())
            return;

        // For filmstrip mode, we need frames; for static mode we don't
        if (!staticKnob && knobFrameCount <= 0)
            return;

        // If StaticKnob is enabled, rotate the image instead of using filmstrip frames
        if (staticKnob)
        {
            // Calculate destination rectangle (center the knob in the available space)
            const int knobSize = juce::jmin(width, height);
            const float destX = x + (width - knobSize) / 2.0f;
            const float destY = y + (height - knobSize) / 2.0f;

            // Calculate rotation angle
            // Standard rotary slider range is 270 degrees (-135° to +135°)
            // Convert sliderPosProportional (0.0 to 1.0) to angle in radians
            const float rotaryRange = 1.5f * juce::MathConstants<float>::pi; // 270 degrees in radians
            const float startAngle = juce::MathConstants<float>::pi * 1.25f; // Start at -135 degrees (bottom left)
            const float angle = startAngle + sliderPosProportional * rotaryRange;

            // Calculate the center point where we want the knob
            const float centerX = destX + knobSize * 0.5f;
            const float centerY = destY + knobSize * 0.5f;

            // Get the image dimensions
            const float imageWidth = (float)knobFilmstrip.getWidth();
            const float imageHeight = (float)knobFilmstrip.getHeight();

            // Calculate scale to fit the knob into the destination size
            const float scale = (float)knobSize / juce::jmax(imageWidth, imageHeight);

            // Create the transform:
            // 1. Translate image so its center is at origin
            // 2. Scale it
            // 3. Rotate it
            // 4. Translate to final position
            juce::AffineTransform transform =
                juce::AffineTransform::translation(-imageWidth * 0.5f, -imageHeight * 0.5f)
                .scaled(scale)
                .rotated(angle)
                .translated(centerX, centerY);

            // Draw shadow FIRST - positioned completely behind/below the knob
            // Only draw shadow if KnobShadow parameter is enabled
            if (knobShadow.enabled)
            {
                const float knobRadius = knobSize * 0.5f;

                // Use KnobShadow parameters
                const float shadowRadius = knobRadius * (knobShadow.radiusPercent / 100.0f);
                const float shadowOffsetX = knobSize * (knobShadow.horizontalOffsetPercent / 100.0f);
                const float shadowOffsetY = knobSize * (knobShadow.verticalOffsetPercent / 100.0f);
                const float shadowCenterX = centerX + shadowOffsetX;
                const float shadowCenterY = centerY + shadowOffsetY;
                const float outerEdgeAlpha = knobShadow.outerEdgeOpacity / 100.0f;

                // Create radial gradient for soft shadow effect
                juce::ColourGradient shadowGradient(
                    juce::Colours::black.withAlpha(1.0f),           // Center: 100% black
                    shadowCenterX, shadowCenterY,
                    juce::Colours::black.withAlpha(outerEdgeAlpha), // Edges: configurable opacity
                    shadowCenterX + shadowRadius, shadowCenterY,
                    true  // isRadial
                );

                g.setGradientFill(shadowGradient);
                g.fillEllipse(shadowCenterX - shadowRadius,
                             shadowCenterY - shadowRadius,
                             shadowRadius * 2.0f,
                             shadowRadius * 2.0f);
            }

            // Draw the rotated knob image on top (nothing will be drawn over this)
            g.drawImageTransformed(knobFilmstrip, transform);
        }
        else
        {
            // Original filmstrip logic
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
    }

    float cornerRadius = 6.0f;
    juce::Image knobFilmstrip;
    juce::String filmstripErrorMessage;

    // Skin definition parameters
    juce::String skinFolderName = "Classic";  // Default skin folder
    juce::String knobFilename;
    int knobFrameCount = 50;
    juce::String knobOrientation = "Vertical";
    bool staticKnob = false;

    // KnobShadow parameters (parsed from "KnobShadow=radiusPercent,outerEdgeOpacity,verticalOffsetPercent,horizontalOffsetPercent")
    struct KnobShadowSettings
    {
        bool enabled = false;
        float radiusPercent = 110.0f;      // Percentage of knob radius (e.g., 110 = 110%)
        float outerEdgeOpacity = 10.0f;    // Opacity at outer edge (0-100)
        float verticalOffsetPercent = 15.0f;   // Vertical offset as percentage of knob size
        float horizontalOffsetPercent = 15.0f; // Horizontal offset as percentage of knob size
    } knobShadow;

    juce::String backgroundFilename;
    juce::String backgroundFlashFilename;
    juce::String backgroundSampleLoadedFilename;
    juce::String textboxFilename;
    juce::String midiListPanelBackgroundFilename;
    int fileNameWidth = 0;
    int loadButtonWidth = 0;
    juce::String buttonFilename;
    juce::String buttonHoverFilename;
    juce::String buttonClickedFilename;
    juce::String sliderThumbFilename;
    int sliderThumbScale = 100; // Scale percentage (1-100)
    juce::String sliderTrackFilename;
    juce::String tabSelectedFilename;
    juce::String muteOnFilename;
    juce::String muteOffFilename;
    juce::String soloOnFilename;
    juce::String soloOffFilename;
    juce::String logoFilename;
    juce::String masterBpmImageFilename;
    juce::String textboxFontFilename;
    int textboxFontSize = 0; // 0 = use default
    float fontScale = 1.0f; // applied on top of textboxFontSize when reloadSkin is called
    juce::String textboxFontColor; // Hex RGB color (e.g., "#D0D0D0")
    juce::String midiSelectorFontColor; // Hex RGB color for MIDI selector (overrides TextBoxFontColor)
    int progressBarWidth = 0; // 0 = use default full width
    juce::String hideProgressBar; // "True" = always hide, empty = use default behavior
    int midiListWidth = 0; // 0 = use default width
    juce::String buttonFontColor; // Hex RGB color for buttons, tabs, and labels (e.g., "#FFFFFF")

    // Loaded skin images
    juce::Image backgroundImage;
    juce::Image backgroundFlashImage;
    juce::Image backgroundSampleLoadedImage;
    juce::Image textboxImage;
    juce::Image midiListPanelBackgroundImage;
    juce::Image buttonImage;
    juce::Image buttonHoverImage;
    juce::Image buttonClickedImage;
    juce::Image sliderThumbImage;
    juce::Image sliderTrackImage;
    juce::Image tabSelectedImage;
    juce::Image muteOnImage;
    juce::Image muteOffImage;
    juce::Image soloOnImage;
    juce::Image soloOffImage;
    juce::Image logoImage;
    juce::Image masterBpmImage;

    // Custom font for numeric textboxes
    juce::Font textboxCustomFont;
    juce::Colour textboxCustomColor;

    // Custom color for MIDI selector (overrides textboxCustomColor if present)
    juce::Colour midiSelectorCustomColor;

    // Custom color for buttons, tabs, and labels
    juce::Colour buttonCustomColor;
};
