#pragma once

#include <juce_core/juce_core.h>

/**
 * Result structure for license validation operations.
 */
struct LicenseValidationResult
{
    bool valid = false;               // Whether the license is valid
    bool hasError = false;            // Whether an error occurred (network, parsing, etc.)
    juce::String errorMessage;        // Error message if hasError is true
    juce::String errorCode;           // Error code from API (e.g., "activation_limit_reached")

    // License information (populated if valid == true)
    juce::String licenseKey;          // The license key
    juce::String licenseeEmail;       // Customer email
    juce::String licenseeName;        // Customer name
    juce::String licenseStatus;       // Status: "active", "inactive", etc.

    // Activation tracking
    int activationLimit = 0;          // Maximum allowed activations
    int activationUsage = 0;          // Current number of activations
    juce::String instanceId;          // The instance ID for this activation

    // Timestamp
    juce::Time validatedAt;           // When this validation occurred

    // Raw API response (for caching)
    juce::String rawJsonResponse;     // Complete JSON response from API
};

/**
 * Lemon Squeezy API integration for license validation.
 * Uses JUCE's built-in HTTP capabilities to communicate with the Lemon Squeezy API.
 */
class LemonSqueezyAPI
{
public:
    /**
     * Validate a license key with the Lemon Squeezy API.
     * This performs an online validation and should be called from a background thread.
     *
     * @param licenseKey The license key to validate (format: XXXX-XXXX-XXXX-XXXX)
     * @param instanceId Unique identifier for this machine/installation
     * @return LicenseValidationResult with validation status and details
     */
    static LicenseValidationResult validateLicense(const juce::String& licenseKey,
                                                    const juce::String& instanceId);

    /**
     * Deactivate a license on this machine.
     * This decrements the activation count for the license.
     *
     * @param licenseKey The license key to deactivate
     * @param instanceId The instance ID to deactivate
     * @return true if deactivation was successful, false otherwise
     */
    static bool deactivateLicense(const juce::String& licenseKey,
                                  const juce::String& instanceId);

    /**
     * Parse a cached API response (for offline validation).
     *
     * @param jsonResponse The raw JSON response from a previous validation
     * @return LicenseValidationResult parsed from the cached response
     */
    static LicenseValidationResult parseCachedResponse(const juce::String& jsonResponse);

private:
    // API configuration
    static constexpr const char* API_BASE_URL = "https://api.lemonsqueezy.com/v1";
    static constexpr int TIMEOUT_MS = 10000; // 10 second timeout

    // Obfuscated API key (decoded at runtime)
    static juce::String getAPIKey();

    // Helper methods
    static LicenseValidationResult parseValidationResponse(const juce::String& jsonResponse);
    static juce::String buildValidationRequestBody(const juce::String& licenseKey,
                                                     const juce::String& instanceId);
};
