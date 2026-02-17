#include "LicenseRegistry.h"

#include <juce_core/juce_core.h>
#include "crypto_small.h"

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

#if !JUCE_WINDOWS
//==============================================================================
// macOS / non-Windows file-based storage backend
//
// Data is stored in an encrypted flat file at:
//   ~/Library/Application Support/LonePearLogic/SlotMachine/license_cache.dat
//
// Format (plaintext before encryption):
//   key=base64(utf8value)\n   ...
//
// Values are base64-encoded so they are newline-safe (handles JSON blobs).
// Encryption: counter-mode XOR stream cipher keyed by HMAC-SHA256 blocks.
//==============================================================================
namespace
{
    static const uint8_t kFileKey[32] = {
        0x4C, 0x50, 0x4C, 0x53, 0x4D, 0x61, 0x63, 0x4F,
        0x53, 0x4C, 0x69, 0x63, 0x65, 0x6E, 0x73, 0x65,
        0x43, 0x61, 0x63, 0x68, 0x65, 0x4B, 0x65, 0x79,
        0x32, 0x30, 0x32, 0x34, 0x4C, 0x50, 0x4C, 0x21
    };

    static void xorCrypt (uint8_t* data, size_t length) noexcept
    {
        if (length == 0)
            return;

        constexpr size_t blockSize = 32;
        size_t offset = 0;
        uint64_t blockIndex = 0;

        while (offset < length)
        {
            uint8_t counter[8];
            uint64_t tmp = blockIndex++;
            for (int i = 7; i >= 0; --i)
            {
                counter[i] = static_cast<uint8_t> (tmp & 0xFFu);
                tmp >>= 8;
            }

            auto ks = crypto_small::hmac_sha256 (kFileKey, sizeof (kFileKey),
                                                  counter, sizeof (counter));
            const size_t chunk = juce::jmin (blockSize, length - offset);
            for (size_t i = 0; i < chunk; ++i)
                data[offset + i] ^= ks[i];

            offset += chunk;
        }
    }

    static juce::File getLicenseCacheFile()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("LonePearLogic")
                   .getChildFile ("SlotMachine")
                   .getChildFile ("license_cache.dat");
    }

    static juce::StringPairArray loadAllPairs()
    {
        juce::StringPairArray pairs;
        const juce::File f = getLicenseCacheFile();
        if (! f.existsAsFile())
            return pairs;

        juce::MemoryBlock rawBytes;
        if (! f.loadFileAsData (rawBytes) || rawBytes.getSize() == 0)
            return pairs;

        xorCrypt (static_cast<uint8_t*> (rawBytes.getData()), rawBytes.getSize());

        const juce::String content = juce::String::fromUTF8 (
            static_cast<const char*> (rawBytes.getData()),
            static_cast<int> (rawBytes.getSize()));

        juce::StringArray lines;
        lines.addTokens (content, "\n", "");

        for (const auto& line : lines)
        {
            const int eq = line.indexOf ("=");
            if (eq > 0)
            {
                const juce::String key         = line.substring (0, eq).trim();
                const juce::String encodedValue = line.substring (eq + 1).trim();
                if (key.isNotEmpty())
                {
                    juce::MemoryOutputStream mos;
                    juce::Base64::convertFromBase64 (mos, encodedValue);
                    pairs.set (key, juce::String::fromUTF8 (
                        static_cast<const char*> (mos.getData()),
                        static_cast<int> (mos.getDataSize())));
                }
            }
        }

        return pairs;
    }

    static bool saveAllPairs (const juce::StringPairArray& pairs)
    {
        const juce::File f = getLicenseCacheFile();
        f.getParentDirectory().createDirectory();

        juce::String content;
        const juce::StringArray& keys = pairs.getAllKeys();
        const juce::StringArray& vals = pairs.getAllValues();

        for (int i = 0; i < keys.size(); ++i)
        {
            content += keys[i];
            content += "=";
            content += juce::Base64::toBase64 (vals[i].toRawUTF8(),
                                               vals[i].getNumBytesAsUTF8());
            content += "\n";
        }

        juce::MemoryBlock rawBytes (content.toRawUTF8(), content.getNumBytesAsUTF8());
        xorCrypt (static_cast<uint8_t*> (rawBytes.getData()), rawBytes.getSize());
        return f.replaceWithData (rawBytes.getData(), rawBytes.getSize());
    }

    // Convert wchar_t* to juce::String.
    // On macOS wchar_t is 32-bit (UTF-32), matching juce::CharPointer_UTF32::CharType.
    static juce::String wcharToString (const wchar_t* w)
    {
        if (w == nullptr)
            return {};
        return juce::String (juce::CharPointer_UTF32 (
            reinterpret_cast<const juce::CharPointer_UTF32::CharType*> (w)));
    }

    // Convert std::wstring (UTF-32 on macOS) to std::string (UTF-8).
    static std::string wstringToStdString (const std::wstring& w)
    {
        return juce::String (juce::CharPointer_UTF32 (
            reinterpret_cast<const juce::CharPointer_UTF32::CharType*> (w.c_str())))
               .toStdString();
    }

    // Convert std::string (UTF-8) to std::wstring (UTF-32 on macOS).
    static std::wstring stdStringToWstring (const std::string& s)
    {
        juce::String j (s);
        std::wstring result;
        result.reserve (static_cast<size_t> (j.length()));
        for (auto cp = j.getCharPointer(); ! cp.isEmpty(); ++cp)
            result.push_back (static_cast<wchar_t> (*cp));
        return result;
    }

    //--------------------------------------------------------------------------
    // File-backed primitives — macOS equivalents of the reg:: Windows ops
    //--------------------------------------------------------------------------

    static bool readStringFile (const wchar_t* /*subkey*/, const wchar_t* name,
                                std::wstring& out)
    {
        if (name == nullptr)
            return false;

        auto pairs = loadAllPairs();
        const juce::String key = wcharToString (name);
        if (! pairs.getAllKeys().contains (key))
            return false;

        const juce::String value = pairs.getValue (key, {});
        out.clear();
        out.reserve (static_cast<size_t> (value.length()));
        for (auto cp = value.getCharPointer(); ! cp.isEmpty(); ++cp)
            out.push_back (static_cast<wchar_t> (*cp));
        return true;
    }

    static bool writeStringFile (const wchar_t* /*subkey*/, const wchar_t* name,
                                 const std::wstring& value)
    {
        if (name == nullptr)
            return false;

        auto pairs = loadAllPairs();
        pairs.set (wcharToString (name),
                   juce::String (juce::CharPointer_UTF32 (
                       reinterpret_cast<const juce::CharPointer_UTF32::CharType*> (value.c_str()))));
        return saveAllPairs (pairs);
    }

    static bool readQWORDFile (const wchar_t* /*subkey*/, const wchar_t* name,
                               int64_t& out)
    {
        if (name == nullptr)
            return false;

        auto pairs = loadAllPairs();
        const juce::String key = wcharToString (name);
        if (! pairs.getAllKeys().contains (key))
            return false;

        const juce::String val = pairs.getValue (key, {});
        if (val.isEmpty())
            return false;

        out = val.getLargeIntValue();
        return true;
    }

    static bool writeQWORDFile (const wchar_t* /*subkey*/, const wchar_t* name,
                                int64_t value)
    {
        if (name == nullptr)
            return false;

        auto pairs = loadAllPairs();
        pairs.set (wcharToString (name), juce::String (value));
        return saveAllPairs (pairs);
    }

    static bool keyExistsFile (const wchar_t* /*subkey*/)
    {
        return getLicenseCacheFile().existsAsFile();
    }

    static bool deleteValueFile (const wchar_t* /*subkey*/, const wchar_t* name)
    {
        if (name == nullptr)
            return false;

        auto pairs = loadAllPairs();
        const juce::String key = wcharToString (name);

        if (! pairs.getAllKeys().contains (key))
            return true;   // Already absent — mirrors Windows ERROR_FILE_NOT_FOUND success

        // Rebuild without the deleted key
        juce::StringPairArray newPairs;
        const juce::StringArray& keys = pairs.getAllKeys();
        const juce::StringArray& vals = pairs.getAllValues();
        for (int i = 0; i < keys.size(); ++i)
            if (keys[i] != key)
                newPairs.set (keys[i], vals[i]);

        return saveAllPairs (newPairs);
    }

} // anonymous namespace
#endif // !JUCE_WINDOWS

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
    return readQWORDFile(subkey, name, out);
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
    return writeQWORDFile(subkey, name, value);
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
    return readStringFile(subkey, name, out);
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
    return writeStringFile(subkey, name, value);
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
    return keyExistsFile(subkey);
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
    return deleteValueFile(subkey, name);
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
    std::wstring firstW, lastW, emailW, licenseW;
    if (!reg::readString(reg::kRegistrySubkey, L"FirstName", firstW))   return false;
    if (!reg::readString(reg::kRegistrySubkey, L"LastName",  lastW))    return false;
    if (!reg::readString(reg::kRegistrySubkey, L"Email",     emailW))   return false;
    if (!reg::readString(reg::kRegistrySubkey, L"License",   licenseW)) return false;
    first      = wstringToStdString(firstW);
    last       = wstringToStdString(lastW);
    email      = wstringToStdString(emailW);
    licenseStr = wstringToStdString(licenseW);
    return true;
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
    if (!reg::writeString(reg::kRegistrySubkey, L"FirstName", stdStringToWstring(first)))      return false;
    if (!reg::writeString(reg::kRegistrySubkey, L"LastName",  stdStringToWstring(last)))       return false;
    if (!reg::writeString(reg::kRegistrySubkey, L"Email",     stdStringToWstring(email)))      return false;
    if (!reg::writeString(reg::kRegistrySubkey, L"License",   stdStringToWstring(licenseStr))) return false;
    return true;
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
    const bool firstRemoved   = reg::deleteValue(reg::kRegistrySubkey, L"FirstName");
    const bool lastRemoved    = reg::deleteValue(reg::kRegistrySubkey, L"LastName");
    const bool emailRemoved   = reg::deleteValue(reg::kRegistrySubkey, L"Email");
    const bool licenseRemoved = reg::deleteValue(reg::kRegistrySubkey, L"License");
    return firstRemoved && lastRemoved && emailRemoved && licenseRemoved;
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
    if (!reg::writeString(reg::kRegistrySubkey, L"LS_LicenseKey",          stdStringToWstring(licenseKey)))    return false;
    if (!reg::writeString(reg::kRegistrySubkey, L"LS_InstanceID",          stdStringToWstring(instanceId)))    return false;
    if (!reg::writeString(reg::kRegistrySubkey, L"LS_CachedJson",          stdStringToWstring(cachedJson)))    return false;
    if (!reg::writeString(reg::kRegistrySubkey, L"LS_LicenseeName",        stdStringToWstring(licenseeName)))  return false;
    if (!reg::writeString(reg::kRegistrySubkey, L"LS_LicenseeEmail",       stdStringToWstring(licenseeEmail))) return false;
    if (!reg::writeQWORD (reg::kRegistrySubkey, L"LS_ValidationTimestamp", validationTimestamp))                return false;
    return true;
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
    std::wstring licenseKeyW, instanceIdW, cachedJsonW, licenseeNameW, licenseeEmailW;
    if (!reg::readString(reg::kRegistrySubkey, L"LS_LicenseKey",          licenseKeyW))    return false;
    if (!reg::readString(reg::kRegistrySubkey, L"LS_InstanceID",          instanceIdW))    return false;
    if (!reg::readString(reg::kRegistrySubkey, L"LS_CachedJson",          cachedJsonW))    return false;
    if (!reg::readString(reg::kRegistrySubkey, L"LS_LicenseeName",        licenseeNameW))  return false;
    if (!reg::readString(reg::kRegistrySubkey, L"LS_LicenseeEmail",       licenseeEmailW)) return false;
    if (!reg::readQWORD (reg::kRegistrySubkey, L"LS_ValidationTimestamp", validationTimestamp)) return false;
    licenseKey    = wstringToStdString(licenseKeyW);
    instanceId    = wstringToStdString(instanceIdW);
    cachedJson    = wstringToStdString(cachedJsonW);
    licenseeName  = wstringToStdString(licenseeNameW);
    licenseeEmail = wstringToStdString(licenseeEmailW);
    return true;
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
    const bool keyRemoved       = reg::deleteValue(reg::kRegistrySubkey, L"LS_LicenseKey");
    const bool instanceRemoved  = reg::deleteValue(reg::kRegistrySubkey, L"LS_InstanceID");
    const bool jsonRemoved      = reg::deleteValue(reg::kRegistrySubkey, L"LS_CachedJson");
    const bool nameRemoved      = reg::deleteValue(reg::kRegistrySubkey, L"LS_LicenseeName");
    const bool emailRemoved     = reg::deleteValue(reg::kRegistrySubkey, L"LS_LicenseeEmail");
    const bool timestampRemoved = reg::deleteValue(reg::kRegistrySubkey, L"LS_ValidationTimestamp");
    return keyRemoved && instanceRemoved && jsonRemoved && nameRemoved && emailRemoved && timestampRemoved;
#endif
}

bool hasCachedLicense()
{
#if JUCE_WINDOWS
    std::wstring licenseKeyW;
    return reg::readString(reg::kRegistrySubkey, L"LS_LicenseKey", licenseKeyW);
#else
    std::wstring licenseKeyW;
    return reg::readString(reg::kRegistrySubkey, L"LS_LicenseKey", licenseKeyW);
#endif
}

} // namespace LemonSqueezyCache
