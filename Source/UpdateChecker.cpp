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

    // Parse filename like "SlotMachineSetup-01.02.03.exe"
    // Extract the version part between "-" and ".exe"
    int dashIndex = filename.lastIndexOf("-");
    int exeIndex = filename.lastIndexOf(".exe");

    if (dashIndex >= 0 && exeIndex > dashIndex)
    {
        juce::String versionPart = filename.substring(dashIndex + 1, exeIndex);
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

    // Save back to file
    if (auto xml = existingOptions.createXml())
        xml->writeTo(optionsFile);
}

//==============================================================================
// Version management
//==============================================================================

UpdateChecker::VersionInfo UpdateChecker::getInstalledVersion()
{
    auto options = loadUpdateOptions();

    if (options.hasProperty(kInstalledVersionProperty))
    {
        juce::String versionStr = options.getProperty(kInstalledVersionProperty).toString();
        if (versionStr.isNotEmpty())
            return VersionInfo::fromString(versionStr);
    }

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
        if (line.isNotEmpty() && line.startsWith("SlotMachineSetup-") && line.endsWith(".exe"))
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

void UpdateChecker::checkForUpdatesAsync(CheckCallback callback)
{
    // Run network check on background thread
    juce::Thread::launch([callback]()
    {
        // Check if user recently declined
        bool declinedRecently = wasUpdateDeclinedRecently();

        // Fetch latest version from server
        auto [fetchResult, latestVersion] = fetchLatestVersion();

        // Determine final result on message thread
        juce::MessageManager::callAsync([callback, fetchResult, latestVersion, declinedRecently]()
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
                if (declinedRecently)
                {
                    DBG("UpdateChecker: Update available but user declined recently");
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

    auto options = juce::MessageBoxOptions()
        .withIconType(juce::MessageBoxIconType::QuestionIcon)
        .withTitle("Update Available")
        .withMessage(message)
        .withButton("Install Update")
        .withButton("Not Now");

    if (parent != nullptr)
        options = options.withAssociatedComponent(parent);

    juce::AlertWindow::showAsync(options,
        juce::ModalCallbackFunction::create([onAccept, onDecline](int result)
        {
            if (result == 1)
            {
                // User clicked "Install Update"
                if (onAccept)
                    onAccept();
            }
            else
            {
                // User clicked "Not Now" or closed dialog
                if (onDecline)
                    onDecline();
            }
        }));
}

//==============================================================================
// Updater launch
//==============================================================================

bool UpdateChecker::launchUpdaterAndTerminate()
{
    // Get the path to the updater executable
    // It should be in the same directory as the main application
    juce::File appFile = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
    juce::File appDir = appFile.getParentDirectory();
    juce::File updaterFile = appDir.getChildFile("SlotMachineUpdater.exe");

    if (!updaterFile.existsAsFile())
    {
        DBG("UpdateChecker: Updater not found at " + updaterFile.getFullPathName());

        juce::AlertWindow::showMessageBoxAsync(
            juce::MessageBoxIconType::WarningIcon,
            "Update Error",
            "Could not find the updater application.\n\n"
            "Please download the latest version manually from lonepearlogic.com",
            "OK");

        return false;
    }

    DBG("UpdateChecker: Launching updater: " + updaterFile.getFullPathName());

    // Launch the updater
    if (!updaterFile.startAsProcess())
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
}
