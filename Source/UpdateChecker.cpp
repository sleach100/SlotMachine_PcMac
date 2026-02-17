#include "UpdateChecker.h"

// Include JucePlugin defines for manufacturer/name
#if __has_include("JucePluginDefines.h")
#include "JucePluginDefines.h"
#endif

#ifndef JucePlugin_Manufacturer
#define JucePlugin_Manufacturer "Lone Pear Logic"
#endif

#ifndef JucePlugin_Name
#define JucePlugin_Name "SlotMachine"
#endif

// Property identifiers for options.xml
static const juce::Identifier kUpdateOptionsType("UPDATE_OPTIONS");
static const juce::Identifier kInstalledVersionProperty("installedVersion");
static const juce::Identifier kLastDeclinedDateProperty("lastDeclinedUpdateDate");
static const juce::Identifier kDeferredUntilDateProperty("deferredUntilDate");

//==============================================================================
// VersionInfo implementation
//==============================================================================

bool UpdateChecker::VersionInfo::isNewerThan(const VersionInfo& other) const
{
    if (major != other.major)
        return major > other.major;
    if (minor != other.minor)
        return minor > other.minor;
    return patch > other.patch;
}

juce::String UpdateChecker::VersionInfo::toString() const
{
    return juce::String(major) + "." + juce::String(minor) + "." + juce::String(patch);
}

UpdateChecker::VersionInfo UpdateChecker::VersionInfo::fromString(const juce::String& versionStr)
{
    VersionInfo info;

    // Parse version string like "1.2.3" or "01.02.03"
    juce::StringArray parts;
    parts.addTokens(versionStr, ".", "");

    if (parts.size() >= 1)
        info.major = parts[0].getIntValue();
    if (parts.size() >= 2)
        info.minor = parts[1].getIntValue();
    if (parts.size() >= 3)
        info.patch = parts[2].getIntValue();

    return info;
}

UpdateChecker::VersionInfo UpdateChecker::VersionInfo::fromFilename(const juce::String& filename)
{
    VersionInfo info;
    info.filename = filename;

    // Parse filename like "SlotMachineSetup-01.02.03.exe" / ".dmg" / ".pkg"
    // Extract the version part between the last "-" and the file extension.
    int dashIndex = filename.lastIndexOf("-");
    int extIndex = -1;
    if (filename.endsWithIgnoreCase(".exe"))
        extIndex = filename.lastIndexOf(".exe");
    else if (filename.endsWithIgnoreCase(".dmg"))
        extIndex = filename.lastIndexOf(".dmg");
    else if (filename.endsWithIgnoreCase(".pkg"))
        extIndex = filename.lastIndexOf(".pkg");

    if (dashIndex >= 0 && extIndex > dashIndex)
    {
        juce::String versionPart = filename.substring(dashIndex + 1, extIndex);
        info = fromString(versionPart);
        info.filename = filename;
    }

    return info;
}

//==============================================================================
// File path helpers
//==============================================================================

juce::File UpdateChecker::getOptionsFile()
{
    auto dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile(JucePlugin_Manufacturer)
        .getChildFile(JucePlugin_Name);
    dir.createDirectory();
    return dir.getChildFile("options.xml");
}

//==============================================================================
// Options.xml persistence
//==============================================================================

juce::ValueTree UpdateChecker::loadUpdateOptions()
{
    auto optionsFile = getOptionsFile();
    if (!optionsFile.existsAsFile())
        return juce::ValueTree(kUpdateOptionsType);

    std::unique_ptr<juce::XmlElement> xml(juce::XmlDocument::parse(optionsFile));
    if (!xml)
        return juce::ValueTree(kUpdateOptionsType);

    juce::ValueTree vt = juce::ValueTree::fromXml(*xml);
    if (!vt.isValid())
        return juce::ValueTree(kUpdateOptionsType);

    return vt;
}

void UpdateChecker::saveUpdateOptions(const juce::ValueTree& updateOptions)
{
    auto optionsFile = getOptionsFile();

    // Load existing options to preserve other settings
    juce::ValueTree existingOptions;
    if (optionsFile.existsAsFile())
    {
        std::unique_ptr<juce::XmlElement> xml(juce::XmlDocument::parse(optionsFile));
        if (xml)
            existingOptions = juce::ValueTree::fromXml(*xml);
    }

    if (!existingOptions.isValid())
        existingOptions = juce::ValueTree("OPTIONS");

    // Copy update-related properties to existing options
    if (updateOptions.hasProperty(kInstalledVersionProperty))
        existingOptions.setProperty(kInstalledVersionProperty,
                                     updateOptions.getProperty(kInstalledVersionProperty), nullptr);
    if (updateOptions.hasProperty(kLastDeclinedDateProperty))
        existingOptions.setProperty(kLastDeclinedDateProperty,
                                     updateOptions.getProperty(kLastDeclinedDateProperty), nullptr);
    if (updateOptions.hasProperty(kDeferredUntilDateProperty))
        existingOptions.setProperty(kDeferredUntilDateProperty,
                                     updateOptions.getProperty(kDeferredUntilDateProperty), nullptr);

    // Save back to file
    if (auto xml = existingOptions.createXml())
        xml->writeTo(optionsFile);
}

//==============================================================================
// Version management
//==============================================================================

UpdateChecker::VersionInfo UpdateChecker::getInstalledVersion()
{
    // Read version from version.txt.
    // On macOS the executable lives at Contents/MacOS/<app>; version.txt is
    // bundled into Contents/Resources/ by the build system.
    // On Windows it sits next to the executable.
    juce::File appFile = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
#if JUCE_MAC
    juce::File versionFile = appFile.getParentDirectory()
                                     .getParentDirectory()
                                     .getChildFile("Resources/version.txt");
#else
    juce::File versionFile = appFile.getParentDirectory().getChildFile("version.txt");
#endif

    if (versionFile.existsAsFile())
    {
        juce::StringArray lines;
        versionFile.readLines(lines);

        if (lines.size() > 0)
        {
            juce::String firstLine = lines[0].trim();

            // Parse format: installedVersion="1.0.1"
            if (firstLine.startsWith("installedVersion=\"") && firstLine.endsWith("\""))
            {
                // Extract version string between quotes
                int startQuote = firstLine.indexOf("\"");
                int endQuote = firstLine.lastIndexOf("\"");

                if (startQuote >= 0 && endQuote > startQuote)
                {
                    juce::String versionStr = firstLine.substring(startQuote + 1, endQuote);
                    if (versionStr.isNotEmpty())
                    {
                        DBG("UpdateChecker: Read version " + versionStr + " from version.txt");
                        return VersionInfo::fromString(versionStr);
                    }
                }
            }
        }
    }

    DBG("UpdateChecker: Could not read version from version.txt, defaulting to 1.0.0");

    // Default to 1.0.0 if no version found
    VersionInfo defaultVersion;
    defaultVersion.major = 1;
    defaultVersion.minor = 0;
    defaultVersion.patch = 0;
    return defaultVersion;
}

void UpdateChecker::saveInstalledVersion(const VersionInfo& version)
{
    auto options = loadUpdateOptions();
    options.setProperty(kInstalledVersionProperty, version.toString(), nullptr);
    saveUpdateOptions(options);

    DBG("UpdateChecker: Saved installed version " + version.toString());
}

void UpdateChecker::recordUpdateDeclined()
{
    auto options = loadUpdateOptions();

    // Save current date as ISO string (YYYY-MM-DD)
    juce::Time now = juce::Time::getCurrentTime();
    juce::String dateStr = now.formatted("%Y-%m-%d");

    options.setProperty(kLastDeclinedDateProperty, dateStr, nullptr);
    saveUpdateOptions(options);

    DBG("UpdateChecker: Recorded update declined on " + dateStr);
}

bool UpdateChecker::wasUpdateDeclinedRecently()
{
    auto options = loadUpdateOptions();

    if (!options.hasProperty(kLastDeclinedDateProperty))
        return false;

    juce::String dateStr = options.getProperty(kLastDeclinedDateProperty).toString();
    if (dateStr.isEmpty())
        return false;

    // Parse date string (YYYY-MM-DD)
    juce::StringArray parts;
    parts.addTokens(dateStr, "-", "");
    if (parts.size() != 3)
        return false;

    int year = parts[0].getIntValue();
    int month = parts[1].getIntValue();
    int day = parts[2].getIntValue();

    juce::Time declinedDate(year, month - 1, day, 0, 0, 0, 0, false);
    juce::Time now = juce::Time::getCurrentTime();

    // Calculate days since declined
    juce::RelativeTime diff = now - declinedDate;
    int daysSinceDeclined = (int)(diff.inDays());

    DBG("UpdateChecker: Days since update declined: " + juce::String(daysSinceDeclined));

    return daysSinceDeclined < DECLINE_REMINDER_DAYS;
}

void UpdateChecker::recordUpdateDeferred(int daysToDefer)
{
    auto options = loadUpdateOptions();

    if (daysToDefer <= 0)
    {
        // "Remind me next launch" - remove the deferred date so it shows next time
        auto optionsFile = getOptionsFile();
        juce::ValueTree existingOptions;

        if (optionsFile.existsAsFile())
        {
            std::unique_ptr<juce::XmlElement> xml(juce::XmlDocument::parse(optionsFile));
            if (xml)
                existingOptions = juce::ValueTree::fromXml(*xml);
        }

        if (existingOptions.isValid() && existingOptions.hasProperty(kDeferredUntilDateProperty))
        {
            existingOptions.removeProperty(kDeferredUntilDateProperty, nullptr);
            if (auto xml = existingOptions.createXml())
                xml->writeTo(optionsFile);
            DBG("UpdateChecker: Removed deferredUntilDate from options.xml");
        }

        DBG("UpdateChecker: Update reminder set for next launch");
        return;
    }

    // Calculate the datetime when user can be asked again
    juce::Time now = juce::Time::getCurrentTime();
    juce::Time deferUntil = now + juce::RelativeTime::days(daysToDefer);

    // Store as full ISO datetime (YYYY-MM-DD HH:MM:SS) for accurate comparison
    juce::String dateTimeStr = deferUntil.formatted("%Y-%m-%d %H:%M:%S");

    options.setProperty(kDeferredUntilDateProperty, dateTimeStr, nullptr);
    saveUpdateOptions(options);

    DBG("UpdateChecker: Update deferred until " + dateTimeStr + " (" + juce::String(daysToDefer) + " days)");
}

bool UpdateChecker::isUpdateDeferred()
{
    auto options = loadUpdateOptions();

    if (!options.hasProperty(kDeferredUntilDateProperty))
        return false;

    juce::String dateTimeStr = options.getProperty(kDeferredUntilDateProperty).toString();
    if (dateTimeStr.isEmpty())
        return false;

    // Parse datetime string (YYYY-MM-DD HH:MM:SS or legacy YYYY-MM-DD)
    juce::StringArray dateTimeParts;
    dateTimeParts.addTokens(dateTimeStr, " ", "");

    juce::StringArray dateParts;
    dateParts.addTokens(dateTimeParts[0], "-", "");
    if (dateParts.size() != 3)
        return false;

    int year = dateParts[0].getIntValue();
    int month = dateParts[1].getIntValue();
    int day = dateParts[2].getIntValue();

    // Parse time if present, otherwise default to end of day for legacy date-only format
    int hour = 23, minute = 59, second = 59;
    if (dateTimeParts.size() >= 2)
    {
        juce::StringArray timeParts;
        timeParts.addTokens(dateTimeParts[1], ":", "");
        if (timeParts.size() >= 3)
        {
            hour = timeParts[0].getIntValue();
            minute = timeParts[1].getIntValue();
            second = timeParts[2].getIntValue();
        }
    }

    juce::Time deferUntilDate(year, month - 1, day, hour, minute, second, 0, false);
    juce::Time now = juce::Time::getCurrentTime();

    // Check if we're still before the deferred datetime
    bool isDeferred = now < deferUntilDate;

    if (isDeferred)
    {
        DBG("UpdateChecker: Update deferred until " + dateTimeStr + ", not showing yet");
    }
    else
    {
        DBG("UpdateChecker: Deferred period has passed (was until " + dateTimeStr + ")");
    }

    return isDeferred;
}

void UpdateChecker::clearDeferredUpdate()
{
    auto optionsFile = getOptionsFile();
    juce::ValueTree existingOptions;

    if (optionsFile.existsAsFile())
    {
        std::unique_ptr<juce::XmlElement> xml(juce::XmlDocument::parse(optionsFile));
        if (xml)
            existingOptions = juce::ValueTree::fromXml(*xml);
    }

    if (existingOptions.isValid() && existingOptions.hasProperty(kDeferredUntilDateProperty))
    {
        existingOptions.removeProperty(kDeferredUntilDateProperty, nullptr);
        if (auto xml = existingOptions.createXml())
            xml->writeTo(optionsFile);
        DBG("UpdateChecker: Cleared deferred update date");
    }
}

//==============================================================================
// Network operations
//==============================================================================

std::pair<UpdateChecker::CheckResult, UpdateChecker::VersionInfo> UpdateChecker::fetchLatestVersion()
{
    VersionInfo latestVersion;

    // Fetch updates.txt
    juce::URL url(UPDATES_URL);

    std::unique_ptr<juce::InputStream> stream = url.createInputStream(
        juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
            .withConnectionTimeoutMs(TIMEOUT_MS)
            .withNumRedirectsToFollow(5)
    );

    if (stream == nullptr)
    {
        DBG("UpdateChecker: Failed to connect to " + juce::String(UPDATES_URL));
        return { CheckResult::NetworkError, latestVersion };
    }

    juce::String content = stream->readEntireStreamAsString();

    if (content.isEmpty())
    {
        DBG("UpdateChecker: Received empty response from updates.txt");
        return { CheckResult::NetworkError, latestVersion };
    }

    DBG("UpdateChecker: Fetched updates.txt content:\n" + content);

    latestVersion = parseUpdatesFile(content);

    if (latestVersion.filename.isEmpty())
    {
        DBG("UpdateChecker: Failed to parse updates.txt");
        return { CheckResult::ParseError, latestVersion };
    }

    return { CheckResult::UpToDate, latestVersion };  // Result will be determined by caller
}

UpdateChecker::VersionInfo UpdateChecker::parseUpdatesFile(const juce::String& content)
{
    VersionInfo info;

    // Split content into lines and get the last non-empty line
    juce::StringArray lines;
    lines.addTokens(content, "\r\n", "");

    for (int i = lines.size() - 1; i >= 0; --i)
    {
        juce::String line = lines[i].trim();

        if (line.isNotEmpty() && line.startsWith("SlotMachineSetup-") &&
            (line.endsWith(".exe") || line.endsWith(".dmg") || line.endsWith(".pkg")))
        {
            info = VersionInfo::fromFilename(line);
            DBG("UpdateChecker: Parsed latest version: " + info.toString() + " from " + line);
            return info;
        }
    }

    DBG("UpdateChecker: No valid installer filename found in updates.txt");
    return info;
}

//==============================================================================
// Main update check logic
//==============================================================================

void UpdateChecker::checkForUpdatesAsync(CheckCallback callback, bool forceCheck)
{
    // Run network check on background thread
    juce::Thread::launch([callback, forceCheck]()
    {
        // Check if user recently declined or has deferred (only if not forcing)
        bool declinedRecently = forceCheck ? false : wasUpdateDeclinedRecently();
        bool updateDeferred = forceCheck ? false : isUpdateDeferred();

        // Fetch latest version from server
        auto fetchPair    = fetchLatestVersion();
        auto fetchResult  = fetchPair.first;
        auto latestVersion = fetchPair.second;

        // Determine final result on message thread
        juce::MessageManager::callAsync([callback, fetchResult, latestVersion, declinedRecently, updateDeferred]()
        {
            if (fetchResult == CheckResult::NetworkError ||
                fetchResult == CheckResult::ParseError)
            {
                callback(fetchResult, latestVersion);
                return;
            }

            // Get installed version
            VersionInfo installedVersion = getInstalledVersion();
            DBG("UpdateChecker: Installed version: " + installedVersion.toString());
            DBG("UpdateChecker: Latest version: " + latestVersion.toString());

            // Compare versions
            if (latestVersion.isNewerThan(installedVersion))
            {
                if (declinedRecently || updateDeferred)
                {
                    DBG("UpdateChecker: Update available but user " +
                        juce::String(updateDeferred ? "deferred" : "declined recently"));
                    callback(CheckResult::DeclinedRecently, latestVersion);
                }
                else
                {
                    DBG("UpdateChecker: Update available!");
                    callback(CheckResult::UpdateAvailable, latestVersion);
                }
            }
            else
            {
                DBG("UpdateChecker: Already up to date");
                callback(CheckResult::UpToDate, latestVersion);
            }
        });
    });
}

//==============================================================================
// UI
//==============================================================================

// Custom dialog component that includes a reminder picker
class UpdateDialogContent : public juce::Component
{
public:
    UpdateDialogContent()
    {
        reminderLabel.setText("Remind me:", juce::dontSendNotification);
        reminderLabel.setJustificationType(juce::Justification::centredRight);
        addAndMakeVisible(reminderLabel);

        reminderCombo.addItem("Next launch", 1);
        reminderCombo.addItem("1 day", 2);
        reminderCombo.addItem("3 days", 3);
        reminderCombo.addItem("7 days", 4);
        reminderCombo.addItem("30 days", 5);
        reminderCombo.setSelectedId(4);  // Default to 7 days
        addAndMakeVisible(reminderCombo);

        setSize(280, 30);
    }

    void resized() override
    {
        auto bounds = getLocalBounds();
        reminderLabel.setBounds(bounds.removeFromLeft(80));
        bounds.removeFromLeft(5);
        reminderCombo.setBounds(bounds);
    }

    int getDeferralDays() const
    {
        switch (reminderCombo.getSelectedId())
        {
            case 1: return 0;   // Next launch
            case 2: return 1;   // 1 day
            case 3: return 3;   // 3 days
            case 4: return 7;   // 7 days
            case 5: return 30;  // 30 days
            default: return 7;
        }
    }

private:
    juce::Label reminderLabel;
    juce::ComboBox reminderCombo;
};

void UpdateChecker::showUpdateDialog(juce::Component* parent,
                                      const VersionInfo& latestVersion,
                                      std::function<void()> onAccept,
                                      std::function<void()> onDecline)
{
    VersionInfo currentVersion = getInstalledVersion();

    juce::String message = "A new version of S.L.O.T. Machine is available!\n\n"
                           "Current version: " + currentVersion.toString() + "\n"
                           "New version: " + latestVersion.toString() + "\n\n"
                           "Would you like to install the update now?";

    // Create a custom AlertWindow with a reminder picker
    auto* alertWindow = new juce::AlertWindow("Update Available",
                                               message,
                                               juce::MessageBoxIconType::QuestionIcon,
                                               parent);

    // Add the custom reminder picker component
    auto* reminderContent = new UpdateDialogContent();
    alertWindow->addCustomComponent(reminderContent);

    // Add buttons
    alertWindow->addButton("Install Update", 1, juce::KeyPress(juce::KeyPress::returnKey));
    alertWindow->addButton("Not Now", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    // Show the dialog asynchronously
    alertWindow->enterModalState(true, juce::ModalCallbackFunction::create(
        [alertWindow, reminderContent, onAccept, onDecline](int result)
        {
            if (result == 1)
            {
                // User clicked "Install Update"
                clearDeferredUpdate();
                if (onAccept)
                    onAccept();
            }
            else
            {
                // User clicked "Not Now" or closed dialog
                int deferralDays = reminderContent->getDeferralDays();
                recordUpdateDeferred(deferralDays);
                if (onDecline)
                    onDecline();
            }

            // Clean up - note: addCustomComponent does NOT take ownership
            delete reminderContent;
            delete alertWindow;
        }), true);
}

//==============================================================================
// Updater launch
//==============================================================================

bool UpdateChecker::launchUpdaterAndTerminate()
{
#if JUCE_MAC
    // On macOS there is no bundled updater application.
    // Open the downloads page so the user can grab the latest .dmg / .pkg.
    DBG("UpdateChecker: macOS — opening download page in default browser");
    juce::URL("https://lonepearlogic.com/slotmachine").launchInDefaultBrowser();
    return true;
#else
    // Get the path to the updater executable
    // It should be in the same directory as the main application
    juce::File appFile = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
    juce::File appDir = appFile.getParentDirectory();
    juce::File updaterFile = appDir.getChildFile("SlotMachineUpdater.exe");

    if (!updaterFile.existsAsFile())
    {
        DBG("UpdateChecker: No updater found at " + updaterFile.getFullPathName());

        juce::AlertWindow::showMessageBoxAsync(
            juce::MessageBoxIconType::WarningIcon,
            "Update Error",
            "Could not find the updater application.\n\n"
            "Please download the latest version manually from lonepearlogic.com",
            "OK");

        return false;
    }

    // Copy the updater to a temp directory so we can run it from there.
    // This avoids permission issues when the app is installed in Program Files,
    // and allows the installer to overwrite the original updater file during update.
    juce::File tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                            .getChildFile("SlotMachineUpdater");
    tempDir.createDirectory();

    juce::File tempUpdaterFile = tempDir.getChildFile("SlotMachineUpdater.exe");

    // Delete any existing copy from a previous update attempt
    if (tempUpdaterFile.existsAsFile())
    {
        DBG("UpdateChecker: Deleting existing temp updater file");
        tempUpdaterFile.deleteFile();
    }

    // Copy updater to temp directory
    if (!updaterFile.copyFileTo(tempUpdaterFile))
    {
        DBG("UpdateChecker: Failed to copy updater to temp directory");

        juce::AlertWindow::showMessageBoxAsync(
            juce::MessageBoxIconType::WarningIcon,
            "Update Error",
            "Could not prepare the updater application.\n\n"
            "Please try again or download the latest version manually from lonepearlogic.com",
            "OK");

        return false;
    }

    // Launch the updater from temp directory, passing the original app directory
    // as a command line argument so it knows where to relaunch SlotMachine.exe from
    juce::String parameters = "\"" + appDir.getFullPathName() + "\"";

    DBG("UpdateChecker: Launching updater from temp: " + tempUpdaterFile.getFullPathName());
    DBG("UpdateChecker: With parameters: " + parameters);

    if (!tempUpdaterFile.startAsProcess(parameters))
    {
        DBG("UpdateChecker: Failed to launch updater");

        juce::AlertWindow::showMessageBoxAsync(
            juce::MessageBoxIconType::WarningIcon,
            "Update Error",
            "Could not start the updater application.\n\n"
            "Please try again or download the latest version manually from lonepearlogic.com",
            "OK");

        return false;
    }

    // Terminate this application
    DBG("UpdateChecker: Updater launched, terminating application");
    juce::JUCEApplication::getInstance()->systemRequestedQuit();

    return true;
#endif
}
