// Standalone-specific initialization code
// This file is ONLY compiled for standalone builds, not for VST3/AU/AAX

#include "WindowsPowerMonitor.h"
#include "PluginEditor.h"
#include "../JuceLibraryCode/JuceHeader.h"

#if JUCE_WINDOWS && JUCE_STANDALONE_APPLICATION

using namespace juce;

// Check if we can include the JUCE standalone header
// If this fails, please add "C:\JUCE\modules\juce_audio_plugin_client" to your include path
#if defined(_MSC_VER)
    #pragma message("Attempting to locate juce_audio_plugin_client module...")
#endif

// Try including with different possible paths
#if __has_include(<juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>)
    #include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
    #define JUCE_STANDALONE_HEADER_FOUND 1
#else
    // Fall back to forward declaration if header not found
    #define JUCE_STANDALONE_HEADER_FOUND 0
    #if defined(_MSC_VER)
        #pragma message("Warning: Could not find JUCE standalone header, using forward declaration")
        #pragma message("This may cause linker errors. Consider adding the JUCE module path to include directories.")
    #endif

    // Forward declaration approach (may not link correctly)
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

// Simple timer-based initializer that doesn't require message thread to be running during construction
struct StandalonePowerMonitorInitializer : public Timer
{
    int checkCount = 0;
    bool started = false;

    StandalonePowerMonitorInitializer()
    {
        // Constructor is safe - doesn't start timer yet
    }

    // Call this from anywhere to begin initialization attempts
    void startIfNeeded()
    {
        if (!started && MessageManager::getInstance()->isThisTheMessageThread())
        {
            DBG("StandalonePowerMonitor: Starting initialization checks from message thread");
            started = true;
            startTimer(100);  // Check every 100ms
        }
    }

    void timerCallback() override
    {
        checkCount++;

        // Stop checking after 100 attempts (10 seconds)
        if (checkCount > 100)
        {
            DBG("StandalonePowerMonitor: Failed to initialize after 10 seconds, giving up");
            stopTimer();
            return;
        }

        // Get the standalone plugin holder instance
        auto* holder = StandalonePluginHolder::getInstance();
        if (!holder)
        {
            if (checkCount % 10 == 0)  // Log every 1 second
                DBG("StandalonePowerMonitor: Waiting for StandalonePluginHolder... (attempt " + String(checkCount) + ")");
            return;
        }

        // Try to get the editor from the processor
        auto* processor = holder->processor.get();
        if (!processor)
        {
            if (checkCount % 10 == 0)
                DBG("StandalonePowerMonitor: Waiting for processor... (attempt " + String(checkCount) + ")");
            return;
        }

        auto* editor = dynamic_cast<SlotMachineAudioProcessorEditor*>(processor->getActiveEditor());
        if (!editor)
        {
            if (checkCount % 10 == 0)
                DBG("StandalonePowerMonitor: Waiting for editor... (attempt " + String(checkCount) + ")");
            return;
        }

        auto* topLevel = editor->getTopLevelComponent();
        if (!topLevel)
        {
            if (checkCount % 10 == 0)
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
            stopTimer();  // Stop checking once initialized
        }
    }
};

// Global instance
static StandalonePowerMonitorInitializer g_initializer;

// This timer runs once to kick off initialization after message thread is definitely running
struct StarterTimer : public Timer
{
    StarterTimer()
    {
        // Start a one-shot timer
        startTimer(50);
    }

    void timerCallback() override
    {
        DBG("StandalonePowerMonitor: Starter timer fired, beginning initialization");
        g_initializer.startIfNeeded();
        stopTimer();
        delete this;  // One-shot, delete ourselves
    }
};

// Use a JUCE initialization callback to start our initialization
// This runs after JUCE is fully initialized
struct InitCallback
{
    InitCallback()
    {
        // Schedule initialization to happen on the message thread
        MessageManager::callAsync([]()
        {
            DBG("StandalonePowerMonitor: Message thread callback executing, creating starter");
            new StarterTimer();  // Will delete itself after firing
        });
    }
};

// Global launcher
static InitCallback g_initCallback;

#endif // JUCE_WINDOWS && JUCE_STANDALONE_APPLICATION
