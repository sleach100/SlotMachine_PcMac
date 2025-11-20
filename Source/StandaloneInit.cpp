// Standalone-specific initialization code
// This file is ONLY compiled for standalone builds, not for VST3/AU/AAX

#include "WindowsPowerMonitor.h"
#include "PluginEditor.h"
#include "../JuceLibraryCode/JuceHeader.h"

#if JUCE_WINDOWS && JUCE_STANDALONE_APPLICATION

using namespace juce;

// Check if we can include the JUCE standalone header
#if __has_include(<juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>)
    #include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
    #define JUCE_STANDALONE_HEADER_FOUND 1
#else
    // Fall back to forward declaration if header not found
    #define JUCE_STANDALONE_HEADER_FOUND 0

    // Forward declaration approach
    class StandalonePluginHolder
    {
    public:
        static StandalonePluginHolder* getInstance();
        juce::AudioDeviceManager deviceManager;
        std::unique_ptr<juce::AudioProcessor> processor;
    };
#endif

// Global power monitor instance
static std::unique_ptr<WindowsPowerMonitor> g_powerMonitor;

// Timer that polls for components to be ready
// This is created AFTER JUCE is initialized, not during static initialization
struct StandalonePowerMonitorInitializer : public Timer
{
    SlotMachineAudioProcessorEditor* editor;
    int checkCount = 0;

    StandalonePowerMonitorInitializer(SlotMachineAudioProcessorEditor* ed)
        : editor(ed)
    {
        // Safe to start timer here - we're called from parentHierarchyChanged, not static init
        DBG("StandalonePowerMonitor: Initializer created, starting timer...");
        startTimer(250);  // Check every 250ms
    }

    void timerCallback() override
    {
        checkCount++;

        // Stop checking after 40 attempts (10 seconds at 250ms intervals)
        if (checkCount > 40)
        {
            DBG("StandalonePowerMonitor: Failed to initialize after 10 seconds, giving up");
            stopTimer();
            delete this;  // Clean up
            return;
        }

        // Get the standalone plugin holder instance
        auto* holder = StandalonePluginHolder::getInstance();
        if (!holder)
        {
            if (checkCount % 4 == 0)  // Log every 1 second
                DBG("StandalonePowerMonitor: Waiting for StandalonePluginHolder... (attempt " + String(checkCount) + ")");
            return;
        }

        auto* topLevel = editor->getTopLevelComponent();
        if (!topLevel)
        {
            if (checkCount % 4 == 0)
                DBG("StandalonePowerMonitor: Waiting for top level component... (attempt " + String(checkCount) + ")");
            return;
        }

        // Initialize power monitor if not already done and editor is ready
        if (g_powerMonitor == nullptr)
        {
            DBG("StandalonePowerMonitor: All components ready, initializing power monitor...");
            g_powerMonitor = std::make_unique<WindowsPowerMonitor>();
            g_powerMonitor->setReconnectDelayMs(5000);  // 5-second delay after wake
            g_powerMonitor->attachToWindow(editor, &holder->deviceManager);
            DBG("WindowsPowerMonitor successfully initialized and attached to editor");
            stopTimer();
            delete this;  // Clean up - we're done
        }
    }
};

// Initialization function called from PluginEditor
// This is called from parentHierarchyChanged(), not during static initialization
void initializeStandalonePowerMonitor(SlotMachineAudioProcessorEditor* editor)
{
    static bool initialized = false;
    if (initialized || !editor)
        return;

    initialized = true;
    DBG("StandalonePowerMonitor: initializeStandalonePowerMonitor called");

    // Create the initializer - it will delete itself when done
    new StandalonePowerMonitorInitializer(editor);
}

// Cleanup function called from PluginEditor destructor
// This ensures cleanup happens before JUCE shuts down, avoiding leaks
void shutdownStandalonePowerMonitor()
{
    if (g_powerMonitor != nullptr)
    {
        DBG("StandalonePowerMonitor: Shutting down power monitor");
        g_powerMonitor.reset();  // Explicitly destroy before JUCE shuts down
    }
}

#endif // JUCE_WINDOWS && JUCE_STANDALONE_APPLICATION
