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
