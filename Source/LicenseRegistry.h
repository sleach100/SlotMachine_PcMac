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

bool loadLicenseFromRegistry(std::string& first, std::string& last,
                             std::string& email, std::string& licenseStr);
bool saveLicenseToRegistry(const std::string& first, const std::string& last,
                           const std::string& email, const std::string& licenseStr);
bool clearLicenseFromRegistry();
