#pragma once

#include <string>

namespace reg
{
#if JUCE_WINDOWS
    inline constexpr const wchar_t* kRegistrySubkey = L"Software\\LonePearLogic\\SlotMachine";
#else
    inline constexpr const wchar_t* kRegistrySubkey = L"";
#endif

    bool readString(const wchar_t* subkey, const wchar_t* name, std::wstring& out);
    bool writeString(const wchar_t* subkey, const wchar_t* name, const std::wstring& value);
    bool keyExists(const wchar_t* subkey);
    bool deleteValue(const wchar_t* subkey, const wchar_t* name);
}

// Old V1 license functions (archived - kept for reference)
bool loadLicenseFromRegistry(std::string& first, std::string& last,
                             std::string& email, std::string& licenseStr);
bool saveLicenseToRegistry(const std::string& first, const std::string& last,
                           const std::string& email, const std::string& licenseStr);
bool clearLicenseFromRegistry();

// New Lemon Squeezy license cache functions
namespace LemonSqueezyCache
{
    /**
     * Save a validated license to the registry cache.
     * This allows offline validation on subsequent starts.
     */
    bool saveLicenseCache(const std::string& licenseKey,
                          const std::string& instanceId,
                          const std::string& cachedJson,
                          const std::string& licenseeName,
                          const std::string& licenseeEmail,
                          int64_t validationTimestamp);

    /**
     * Load a cached license from the registry.
     * Returns true if a cache exists, false otherwise.
     */
    bool loadLicenseCache(std::string& licenseKey,
                          std::string& instanceId,
                          std::string& cachedJson,
                          std::string& licenseeName,
                          std::string& licenseeEmail,
                          int64_t& validationTimestamp);

    /**
     * Clear the Lemon Squeezy license cache (used during deactivation).
     */
    bool clearLicenseCache();

    /**
     * Check if a Lemon Squeezy license cache exists.
     */
    bool hasCachedLicense();
}
