#pragma once

#include <juce_core/juce_core.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

/**
 * UpdateChecker - Handles checking for application updates on startup.
 *
 * Fetches updates.txt from lonepearlogic.com, compares versions,
 * and prompts the user to update if a newer version is available.
 */
class UpdateChecker
{
public:
    /**
     * Version information parsed from updates.txt
     */
    struct VersionInfo
    {
        int major = 1;
        int minor = 0;
        int patch = 0;
        juce::String filename;  // e.g., "SlotMachineSetup-01.02.03.exe"

        bool isNewerThan(const VersionInfo& other) const;
        juce::String toString() const;

        static VersionInfo fromString(const juce::String& versionStr);
        static VersionInfo fromFilename(const juce::String& filename);
    };

    /**
     * Result of the update check
     */
    enum class CheckResult
    {
        UpToDate,           // Current version is latest
        UpdateAvailable,    // Newer version available
        DeclinedRecently,   // User declined within last 7 days
        NetworkError,       // Could not fetch updates.txt
        ParseError          // Could not parse updates.txt
    };

    /**
     * Callback for when update check completes.
     * Called on the message thread.
     */
    using CheckCallback = std::function<void(CheckResult result, const VersionInfo& latestVersion)>;

    UpdateChecker() = default;
    ~UpdateChecker() = default;

    /**
     * Check for updates asynchronously.
     * The callback will be invoked on the message thread when complete.
     *
     * @param callback Function to call with the result
     * @param forceCheck If true, ignores the "declined recently" state and
     *                   returns UpdateAvailable instead of DeclinedRecently
     */
    void checkForUpdatesAsync(CheckCallback callback, bool forceCheck = false);

    /**
     * Show the update prompt dialog.
     *
     * @param parent Parent component for the dialog
     * @param latestVersion Version info for the available update
     * @param onAccept Callback when user accepts the update
     * @param onDecline Callback when user declines the update
     */
    static void showUpdateDialog(juce::Component* parent,
                                  const VersionInfo& latestVersion,
                                  std::function<void()> onAccept,
                                  std::function<void()> onDecline);

    /**
     * Launch the updater application and terminate this app.
     *
     * @return true if updater was launched successfully
     */
    static bool launchUpdaterAndTerminate();

    /**
     * Get the currently installed version from version.txt in the executable directory.
     * Reads line 1 in format: installedVersion="X.Y.Z"
     * If not found, returns version 1.0.0 as default.
     */
    static VersionInfo getInstalledVersion();

    /**
     * Save the installed version to options.xml.
     * Called by the updater after successful update.
     */
    static void saveInstalledVersion(const VersionInfo& version);

    /**
     * Record that the user declined an update.
     * Saves the current date to options.xml.
     */
    static void recordUpdateDeclined();

    /**
     * Check if user declined an update within the last 7 days.
     */
    static bool wasUpdateDeclinedRecently();

    /**
     * Record that the user deferred the update reminder.
     * Calculates and saves the date when user can be asked again.
     * @param daysToDefer Number of days to wait before asking again (0 = next launch)
     */
    static void recordUpdateDeferred(int daysToDefer);

    /**
     * Check if user has deferred the update and the deferral period hasn't passed.
     * @return true if user should not be prompted for update yet
     */
    static bool isUpdateDeferred();

    /**
     * Clear the deferred update date.
     * Called when user accepts an update.
     */
    static void clearDeferredUpdate();

    /**
     * Get the path to the options.xml file.
     */
    static juce::File getOptionsFile();

    /**
     * URL for the updates.txt file
     */
    // Use:  static constexpr const char* UPDATES_URL = "https://www.lonepearlogic.com/Update.txt";

    static constexpr const char* UPDATES_URL = "https://www.lonepearlogic.com/Update.txt";

    /**
     * Base URL for downloading installer files
     */
    static constexpr const char* DOWNLOAD_BASE_URL = "https://lonepearlogic.com/";

    /**
     * Number of days before prompting again after user declines
     */
    static constexpr int DECLINE_REMINDER_DAYS = 7;

    /**
     * HTTP timeout in milliseconds
     */
    static constexpr int TIMEOUT_MS = 10000;

private:
    /**
     * Fetch and parse updates.txt synchronously.
     * Should be called from a background thread.
     */
    static std::pair<CheckResult, VersionInfo> fetchLatestVersion();

    /**
     * Parse the last line of updates.txt to get the latest version.
     */
    static VersionInfo parseUpdatesFile(const juce::String& content);

    /**
     * Load update-related properties from options.xml
     */
    static juce::ValueTree loadUpdateOptions();

    /**
     * Save update-related properties to options.xml
     */
    static void saveUpdateOptions(const juce::ValueTree& updateOptions);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UpdateChecker)
};
