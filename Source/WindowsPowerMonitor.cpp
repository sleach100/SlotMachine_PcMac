#include "WindowsPowerMonitor.h"

#if JUCE_WINDOWS && JUCE_STANDALONE_APPLICATION

// Need to include this to access StandalonePluginHolder
#include <map>
#include <iostream>
#include <juce_audio_plugin_client/juce_audio_plugin_client.h>

// Map of HWND to WindowsPowerMonitor instance for the static callback
static std::map<HWND, WindowsPowerMonitor*> windowMonitorMap;

WindowsPowerMonitor::WindowsPowerMonitor()
{
    logMessage("WindowsPowerMonitor created");
}

WindowsPowerMonitor::~WindowsPowerMonitor()
{
    stopTimer();

    // Restore original window proc if we hooked it
    if (windowHandle != nullptr && originalWindowProc != nullptr)
    {
        SetWindowLongPtr(windowHandle, GWLP_WNDPROC, (LONG_PTR)originalWindowProc);
        windowMonitorMap.erase(windowHandle);
    }

    logMessage("WindowsPowerMonitor destroyed");
}

void WindowsPowerMonitor::attachToWindow(juce::Component* editorComponent)
{
    if (editorComponent == nullptr)
        return;

    // Find the top-level window
    auto* topLevel = editorComponent->getTopLevelComponent();
    if (topLevel == nullptr)
        return;

    // Get the native window handle
    windowHandle = (HWND)topLevel->getWindowHandle();
    if (windowHandle == nullptr)
    {
        logMessage("Failed to get window handle");
        return;
    }

    // Store this instance in the map so the static callback can find it
    windowMonitorMap[windowHandle] = this;

    // Hook the window procedure to intercept WM_POWERBROADCAST messages
    originalWindowProc = (WNDPROC)SetWindowLongPtr(windowHandle, GWLP_WNDPROC, (LONG_PTR)windowProc);

    if (originalWindowProc == nullptr)
    {
        logMessage("Failed to hook window procedure");
        windowMonitorMap.erase(windowHandle);
        windowHandle = nullptr;
        return;
    }

    logMessage("Successfully attached to window and hooked message handler");
}

void WindowsPowerMonitor::setReconnectDelayMs(int delayMs)
{
    reconnectDelayMs = juce::jmax(0, delayMs);
    logMessage("Reconnect delay set to " + juce::String(reconnectDelayMs) + "ms");
}

LRESULT CALLBACK WindowsPowerMonitor::windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    // Find the monitor instance for this window
    auto it = windowMonitorMap.find(hwnd);
    if (it == windowMonitorMap.end())
    {
        // This shouldn't happen, but call the default proc just in case
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }

    WindowsPowerMonitor* monitor = it->second;
    WNDPROC originalProc = monitor->originalWindowProc;

    // Handle power broadcast messages
    if (msg == WM_POWERBROADCAST)
    {
        switch (wParam)
        {
            case PBT_APMSUSPEND:
                monitor->logMessage("System is suspending (sleep)");
                monitor->handleSystemSuspend();
                break;

            case PBT_APMRESUMEAUTOMATIC:
                monitor->logMessage("System is resuming (wake)");
                monitor->handleSystemResume();
                break;

            case PBT_APMRESUMESUSPEND:
                monitor->logMessage("System resumed due to user input");
                // Also handle this case
                if (!monitor->isTimerRunning())
                    monitor->handleSystemResume();
                break;

            default:
                break;
        }
    }

    // Call the original window procedure
    if (originalProc != nullptr)
        return CallWindowProc(originalProc, hwnd, msg, wParam, lParam);
    else
        return DefWindowProc(hwnd, msg, wParam, lParam);
}

void WindowsPowerMonitor::handleSystemSuspend()
{
    // Save the current audio device setup before the system goes to sleep
    auto* deviceManager = getAudioDeviceManager();
    if (deviceManager != nullptr)
    {
        deviceManager->getAudioDeviceSetup(savedSetup);
        hasStoredSetup = true;
        logMessage("Saved audio device setup: " + savedSetup.outputDeviceName);

        // Close the audio device to avoid issues during sleep
        logMessage("Closing audio device before sleep");
        deviceManager->closeAudioDevice();
    }
    else
    {
        logMessage("Warning: Could not access AudioDeviceManager during suspend");
    }
}

void WindowsPowerMonitor::handleSystemResume()
{
    // Start a timer to reconnect after the specified delay
    // This gives Windows time to fully reinitialize the audio subsystem
    logMessage("Scheduling audio device reconnection in " + juce::String(reconnectDelayMs) + "ms");
    startTimer(reconnectDelayMs);
}

void WindowsPowerMonitor::timerCallback()
{
    stopTimer();
    reconnectAudioDevice();
}

void WindowsPowerMonitor::reconnectAudioDevice()
{
    auto* deviceManager = getAudioDeviceManager();
    if (deviceManager == nullptr)
    {
        logMessage("Error: Could not access AudioDeviceManager for reconnection");
        return;
    }

    if (!hasStoredSetup)
    {
        logMessage("Warning: No stored audio setup to restore");
        return;
    }

    logMessage("Reconnecting audio device: " + savedSetup.outputDeviceName);

    // Attempt to restore the previous audio device setup
    juce::String error = deviceManager->setAudioDeviceSetup(savedSetup, true);

    if (error.isEmpty())
    {
        logMessage("Successfully reconnected audio device");

        // Verify the device is actually working
        if (auto* currentDevice = deviceManager->getCurrentAudioDevice())
        {
            logMessage("Current device: " + currentDevice->getName() +
                      " (" + juce::String(currentDevice->getCurrentSampleRate()) + " Hz, " +
                      juce::String(currentDevice->getCurrentBufferSizeSamples()) + " samples)");
        }
    }
    else
    {
        logMessage("Error reconnecting audio device: " + error);

        // Try to initialize with default device as fallback
        logMessage("Attempting to use default audio device as fallback");

        juce::AudioDeviceManager::AudioDeviceSetup fallbackSetup;
        fallbackSetup.outputDeviceName = juce::String();  // Use default
        fallbackSetup.inputDeviceName = juce::String();
        fallbackSetup.sampleRate = savedSetup.sampleRate;
        fallbackSetup.bufferSize = savedSetup.bufferSize;

        juce::String fallbackError = deviceManager->initialiseWithDefaultDevices(2, 2);
        if (fallbackError.isEmpty())
            logMessage("Successfully initialized with default device");
        else
            logMessage("Failed to initialize with default device: " + fallbackError);
    }
}

juce::AudioDeviceManager* WindowsPowerMonitor::getAudioDeviceManager()
{
    // Access the StandalonePluginHolder's AudioDeviceManager
    // This is the recommended way to access it in JUCE standalone apps
    if (auto* holder = juce::StandalonePluginHolder::getInstance())
    {
        return &holder->deviceManager;
    }
    return nullptr;
}

void WindowsPowerMonitor::logMessage(const juce::String& message)
{
    // Log with timestamp for debugging
    juce::String logMsg = "[WindowsPowerMonitor] " + message;
    DBG(logMsg);

    // Also log to console in debug builds
    #if JUCE_DEBUG
    std::cout << logMsg << std::endl;
    #endif
}

#endif // JUCE_WINDOWS && JUCE_STANDALONE_APPLICATION
