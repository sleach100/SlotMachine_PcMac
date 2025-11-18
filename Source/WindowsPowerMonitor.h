#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_events/juce_events.h>
#include <juce_gui_basics/juce_gui_basics.h>

#if JUCE_WINDOWS && JUCE_STANDALONE_APPLICATION

// Prevent Windows macro pollution before including windows.h
#ifndef NOMINMAX
 #define NOMINMAX
#endif

#ifndef WIN32_LEAN_AND_MEAN
 #define WIN32_LEAN_AND_MEAN
#endif

// Include Windows headers for power management
#include <windows.h>

// Undefine problematic Windows macros that conflict with C++ code
#ifdef max
 #undef max
#endif

#ifdef min
 #undef min
#endif

/**
 * WindowsPowerMonitor
 *
 * Monitors Windows power events (sleep/wake) and automatically handles
 * audio device reconnection after the system wakes from sleep.
 *
 * This solves the common issue on Windows 11 where audio devices become
 * invalidated after sleep, causing audio to stop working.
 *
 * Usage:
 * - Create an instance in your editor when running standalone on Windows
 * - Call attachToWindow() with the top-level DocumentWindow
 * - The monitor will automatically handle sleep/wake events
 */
class WindowsPowerMonitor : public juce::Timer
{
public:
    WindowsPowerMonitor();
    ~WindowsPowerMonitor() override;

    /**
     * Attaches the power monitor to the standalone application window.
     * This must be called after the editor is added to the window hierarchy.
     */
    void attachToWindow(juce::Component* editorComponent);

    /**
     * Sets the delay (in milliseconds) to wait after system wake before
     * attempting to reconnect the audio device.
     * Default is 5000ms (5 seconds).
     */
    void setReconnectDelayMs(int delayMs);

private:
    // Windows message handler
    static LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    // Timer callback for delayed audio device reconnection
    void timerCallback() override;

    // Audio device reconnection
    void handleSystemSuspend();
    void handleSystemResume();
    void reconnectAudioDevice();

    juce::AudioDeviceManager* getAudioDeviceManager();

    // Stored audio device setup
    juce::AudioDeviceManager::AudioDeviceSetup savedSetup;
    bool hasStoredSetup = false;

    // Reconnection delay in milliseconds (default 5 seconds)
    int reconnectDelayMs = 5000;

    // Windows-specific
    HWND windowHandle = nullptr;
    WNDPROC originalWindowProc = nullptr;

    // Debug logging
    void logMessage(const juce::String& message);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WindowsPowerMonitor)
};

#endif // JUCE_WINDOWS && JUCE_STANDALONE_APPLICATION
