#include "LicenseRegistry.h"

#include <juce_core/juce_core.h>

#if JUCE_WINDOWS
 #include <windows.h>
 #include <vector>
#endif

namespace
{
#if JUCE_WINDOWS
    std::wstring toWide(const std::string& text)
    {
        if (text.empty())
            return {};

        const int requiredChars = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
        if (requiredChars <= 0)
            return {};

        std::wstring result(static_cast<size_t>(requiredChars - 1), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, result.data(), requiredChars);
        return result;
    }

    std::string toNarrow(const std::wstring& text)
    {
        if (text.empty())
            return {};

        const int requiredBytes = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (requiredBytes <= 0)
            return {};

        std::string result(static_cast<size_t>(requiredBytes - 1), '\0');
        WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, result.data(), requiredBytes, nullptr, nullptr);
        return result;
    }
#endif
}

namespace reg
{

bool readQWORD(const wchar_t* subkey, const wchar_t* name, int64_t& out)
{
#if JUCE_WINDOWS
    if (subkey == nullptr || name == nullptr)
        return false;

    HKEY key = nullptr;
    const LONG status = RegOpenKeyExW(HKEY_CURRENT_USER, subkey, 0, KEY_READ, &key);
    if (status != ERROR_SUCCESS)
        return false;

    DWORD dataSize = sizeof(DWORD64);
    DWORD64 value = 0;
    LONG result = RegGetValueW(key, nullptr, name, RRF_RT_QWORD, nullptr, &value, &dataSize);
    RegCloseKey(key);

    if (result != ERROR_SUCCESS)
        return false;

    out = static_cast<int64_t>(value);
    return true;
#else
    juce::ignoreUnused(subkey, name, out);
    return false;
#endif
}

bool writeQWORD(const wchar_t* subkey, const wchar_t* name, int64_t value)
{
#if JUCE_WINDOWS
    if (subkey == nullptr || name == nullptr)
        return false;

    HKEY key = nullptr;
    DWORD disposition = 0;
    const LONG status = RegCreateKeyExW(HKEY_CURRENT_USER, subkey, 0, nullptr,
        REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &key, &disposition);

    if (status != ERROR_SUCCESS)
        return false;

    DWORD64 qwordValue = static_cast<DWORD64>(value);
    const LONG result = RegSetValueExW(key, name, 0, REG_QWORD,
        reinterpret_cast<const BYTE*>(&qwordValue), sizeof(DWORD64));

    RegCloseKey(key);
    return result == ERROR_SUCCESS;
#else
    juce::ignoreUnused(subkey, name, value);
    return false;
#endif
}

bool readString(const wchar_t* subkey, const wchar_t* name, std::wstring& out)
{
#if JUCE_WINDOWS
    if (subkey == nullptr || name == nullptr)
        return false;

    HKEY key = nullptr;
    const LONG status = RegOpenKeyExW(HKEY_CURRENT_USER, subkey, 0, KEY_READ, &key);
    if (status != ERROR_SUCCESS)
        return false;

    DWORD dataSize = 0;
    LONG result = RegGetValueW(key, nullptr, name, RRF_RT_REG_SZ, nullptr, nullptr, &dataSize);
    if (result != ERROR_SUCCESS || dataSize == 0)
    {
        RegCloseKey(key);
        return false;
    }

    std::vector<wchar_t> buffer(dataSize / sizeof(wchar_t));
    result = RegGetValueW(key, nullptr, name, RRF_RT_REG_SZ, nullptr, buffer.data(), &dataSize);
    RegCloseKey(key);

    if (result != ERROR_SUCCESS)
        return false;

    size_t length = dataSize / sizeof(wchar_t);
    if (length > 0 && buffer[length - 1] == L'\0')
        --length;

    out.assign(buffer.data(), length);
    return true;
#else
    juce::ignoreUnused(subkey, name, out);
    return false;
#endif
}

bool writeString(const wchar_t* subkey, const wchar_t* name, const std::wstring& value)
{
#if JUCE_WINDOWS
    if (subkey == nullptr || name == nullptr)
        return false;

    HKEY key = nullptr;
    DWORD disposition = 0;
    const LONG status = RegCreateKeyExW(HKEY_CURRENT_USER, subkey, 0, nullptr,
        REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &key, &disposition);

    if (status != ERROR_SUCCESS)
        return false;

    const DWORD bytes = static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t));
    const LONG result = RegSetValueExW(key, name, 0, REG_SZ,
        reinterpret_cast<const BYTE*>(value.c_str()), bytes);

    RegCloseKey(key);
    return result == ERROR_SUCCESS;
#else
    juce::ignoreUnused(subkey, name, value);
    return false;
#endif
}

bool keyExists(const wchar_t* subkey)
{
#if JUCE_WINDOWS
    if (subkey == nullptr)
        return false;

    HKEY key = nullptr;
    const LONG status = RegOpenKeyExW(HKEY_CURRENT_USER, subkey, 0, KEY_READ, &key);
    if (status == ERROR_SUCCESS && key != nullptr)
    {
        RegCloseKey(key);
        return true;
    }

    return false;
#else
    juce::ignoreUnused(subkey);
    return false;
#endif
}

bool deleteValue(const wchar_t* subkey, const wchar_t* name)
{
#if JUCE_WINDOWS
    if (subkey == nullptr || name == nullptr)
        return false;

    HKEY key = nullptr;
    const LONG status = RegOpenKeyExW(HKEY_CURRENT_USER, subkey, 0, KEY_SET_VALUE, &key);
    if (status != ERROR_SUCCESS)
        return status == ERROR_FILE_NOT_FOUND;

    const LONG result = RegDeleteValueW(key, name);
    RegCloseKey(key);

    return result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND;
#else
    juce::ignoreUnused(subkey, name);
    return false;
#endif
}

} // namespace reg

bool loadLicenseFromRegistry(std::string& first, std::string& last,
                             std::string& email, std::string& licenseStr)
{
#if JUCE_WINDOWS
    std::wstring firstNameW, lastNameW, emailW, licenseW;

    if (!reg::readString(reg::kRegistrySubkey, L"FirstName", firstNameW))
        return false;
    if (!reg::readString(reg::kRegistrySubkey, L"LastName", lastNameW))
        return false;
    if (!reg::readString(reg::kRegistrySubkey, L"Email", emailW))
        return false;
    if (!reg::readString(reg::kRegistrySubkey, L"License", licenseW))
        return false;

    first = toNarrow(firstNameW);
    last = toNarrow(lastNameW);
    email = toNarrow(emailW);
    licenseStr = toNarrow(licenseW);
    return true;
#else
    juce::ignoreUnused(first, last, email, licenseStr);
    return false;
#endif
}

bool saveLicenseToRegistry(const std::string& first, const std::string& last,
                           const std::string& email, const std::string& licenseStr)
{
#if JUCE_WINDOWS
    const std::wstring firstW = toWide(first);
    const std::wstring lastW = toWide(last);
    const std::wstring emailW = toWide(email);
    const std::wstring licenseW = toWide(licenseStr);

    if (!reg::writeString(reg::kRegistrySubkey, L"FirstName", firstW))
        return false;
    if (!reg::writeString(reg::kRegistrySubkey, L"LastName", lastW))
        return false;
    if (!reg::writeString(reg::kRegistrySubkey, L"Email", emailW))
        return false;
    if (!reg::writeString(reg::kRegistrySubkey, L"License", licenseW))
        return false;

    return true;
#else
    juce::ignoreUnused(first, last, email, licenseStr);
    return false;
#endif
}

bool clearLicenseFromRegistry()
{
#if JUCE_WINDOWS
    const bool firstRemoved = reg::deleteValue(reg::kRegistrySubkey, L"FirstName");
    const bool lastRemoved = reg::deleteValue(reg::kRegistrySubkey, L"LastName");
    const bool emailRemoved = reg::deleteValue(reg::kRegistrySubkey, L"Email");
    const bool licenseRemoved = reg::deleteValue(reg::kRegistrySubkey, L"License");

    return firstRemoved && lastRemoved && emailRemoved && licenseRemoved;
#else
    return false;
#endif
}

// =============================================================================
// Lemon Squeezy License Cache Implementation
// =============================================================================

namespace LemonSqueezyCache
{

bool saveLicenseCache(const std::string& licenseKey,
                      const std::string& instanceId,
                      const std::string& cachedJson,
                      const std::string& licenseeName,
                      const std::string& licenseeEmail,
                      int64_t validationTimestamp)
{
#if JUCE_WINDOWS
    const std::wstring licenseKeyW = toWide(licenseKey);
    const std::wstring instanceIdW = toWide(instanceId);
    const std::wstring cachedJsonW = toWide(cachedJson);
    const std::wstring licenseeNameW = toWide(licenseeName);
    const std::wstring licenseeEmailW = toWide(licenseeEmail);

    if (!reg::writeString(reg::kRegistrySubkey, L"LS_LicenseKey", licenseKeyW))
        return false;
    if (!reg::writeString(reg::kRegistrySubkey, L"LS_InstanceID", instanceIdW))
        return false;
    if (!reg::writeString(reg::kRegistrySubkey, L"LS_CachedJson", cachedJsonW))
        return false;
    if (!reg::writeString(reg::kRegistrySubkey, L"LS_LicenseeName", licenseeNameW))
        return false;
    if (!reg::writeString(reg::kRegistrySubkey, L"LS_LicenseeEmail", licenseeEmailW))
        return false;
    if (!reg::writeQWORD(reg::kRegistrySubkey, L"LS_ValidationTimestamp", validationTimestamp))
        return false;

    return true;
#else
    juce::ignoreUnused(licenseKey, instanceId, cachedJson, licenseeName, licenseeEmail, validationTimestamp);
    return false;
#endif
}

bool loadLicenseCache(std::string& licenseKey,
                      std::string& instanceId,
                      std::string& cachedJson,
                      std::string& licenseeName,
                      std::string& licenseeEmail,
                      int64_t& validationTimestamp)
{
#if JUCE_WINDOWS
    std::wstring licenseKeyW, instanceIdW, cachedJsonW, licenseeNameW, licenseeEmailW;

    if (!reg::readString(reg::kRegistrySubkey, L"LS_LicenseKey", licenseKeyW))
        return false;
    if (!reg::readString(reg::kRegistrySubkey, L"LS_InstanceID", instanceIdW))
        return false;
    if (!reg::readString(reg::kRegistrySubkey, L"LS_CachedJson", cachedJsonW))
        return false;
    if (!reg::readString(reg::kRegistrySubkey, L"LS_LicenseeName", licenseeNameW))
        return false;
    if (!reg::readString(reg::kRegistrySubkey, L"LS_LicenseeEmail", licenseeEmailW))
        return false;
    if (!reg::readQWORD(reg::kRegistrySubkey, L"LS_ValidationTimestamp", validationTimestamp))
        return false;

    licenseKey = toNarrow(licenseKeyW);
    instanceId = toNarrow(instanceIdW);
    cachedJson = toNarrow(cachedJsonW);
    licenseeName = toNarrow(licenseeNameW);
    licenseeEmail = toNarrow(licenseeEmailW);

    return true;
#else
    juce::ignoreUnused(licenseKey, instanceId, cachedJson, licenseeName, licenseeEmail, validationTimestamp);
    return false;
#endif
}

bool clearLicenseCache()
{
#if JUCE_WINDOWS
    const bool keyRemoved = reg::deleteValue(reg::kRegistrySubkey, L"LS_LicenseKey");
    const bool instanceRemoved = reg::deleteValue(reg::kRegistrySubkey, L"LS_InstanceID");
    const bool jsonRemoved = reg::deleteValue(reg::kRegistrySubkey, L"LS_CachedJson");
    const bool nameRemoved = reg::deleteValue(reg::kRegistrySubkey, L"LS_LicenseeName");
    const bool emailRemoved = reg::deleteValue(reg::kRegistrySubkey, L"LS_LicenseeEmail");
    const bool timestampRemoved = reg::deleteValue(reg::kRegistrySubkey, L"LS_ValidationTimestamp");

    return keyRemoved && instanceRemoved && jsonRemoved && nameRemoved && emailRemoved && timestampRemoved;
#else
    return false;
#endif
}

bool hasCachedLicense()
{
#if JUCE_WINDOWS
    std::wstring licenseKeyW;
    return reg::readString(reg::kRegistrySubkey, L"LS_LicenseKey", licenseKeyW);
#else
    return false;
#endif
}

} // namespace LemonSqueezyCache
