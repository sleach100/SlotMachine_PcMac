#include "InstanceIdentifier.h"
#include "LicenseRegistry.h"

#if JUCE_WINDOWS
 #include <windows.h>
 #include <vector>
#endif

namespace InstanceIdentifier
{

juce::String getOrCreateInstanceID()
{
#if JUCE_WINDOWS
    // First, try to read existing instance ID from registry
    std::wstring instanceIDW;
    if (reg::readString(reg::kRegistrySubkey, L"InstanceID", instanceIDW))
    {
        // Convert to narrow string
        if (!instanceIDW.empty())
        {
            const int requiredBytes = WideCharToMultiByte(CP_UTF8, 0, instanceIDW.c_str(), -1, nullptr, 0, nullptr, nullptr);
            if (requiredBytes > 0)
            {
                std::string instanceID(static_cast<size_t>(requiredBytes - 1), '\0');
                WideCharToMultiByte(CP_UTF8, 0, instanceIDW.c_str(), -1, instanceID.data(), requiredBytes, nullptr, nullptr);
                return juce::String(instanceID);
            }
        }
    }

    // If no instance ID exists, try to get the Windows Machine GUID
    HKEY key = nullptr;
    LONG status = RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                                  L"SOFTWARE\\Microsoft\\Cryptography",
                                  0,
                                  KEY_READ | KEY_WOW64_64KEY,
                                  &key);

    juce::String machineGuid;
    if (status == ERROR_SUCCESS && key != nullptr)
    {
        DWORD dataSize = 0;
        LONG result = RegGetValueW(key, nullptr, L"MachineGuid", RRF_RT_REG_SZ, nullptr, nullptr, &dataSize);

        if (result == ERROR_SUCCESS && dataSize > 0)
        {
            std::vector<wchar_t> buffer(dataSize / sizeof(wchar_t));
            result = RegGetValueW(key, nullptr, L"MachineGuid", RRF_RT_REG_SZ, nullptr, buffer.data(), &dataSize);

            if (result == ERROR_SUCCESS)
            {
                size_t length = dataSize / sizeof(wchar_t);
                if (length > 0 && buffer[length - 1] == L'\0')
                    --length;

                std::wstring guidW(buffer.data(), length);

                // Convert to narrow string
                const int requiredBytes = WideCharToMultiByte(CP_UTF8, 0, guidW.c_str(), -1, nullptr, 0, nullptr, nullptr);
                if (requiredBytes > 0)
                {
                    std::string guidNarrow(static_cast<size_t>(requiredBytes - 1), '\0');
                    WideCharToMultiByte(CP_UTF8, 0, guidW.c_str(), -1, guidNarrow.data(), requiredBytes, nullptr, nullptr);
                    machineGuid = juce::String(guidNarrow);
                }
            }
        }
        RegCloseKey(key);
    }

    // If we couldn't get the machine GUID, generate a random UUID
    if (machineGuid.isEmpty())
    {
        juce::Uuid uuid;
        machineGuid = uuid.toDashedString();
    }

    // Store the instance ID in the registry for future use
    const int requiredChars = MultiByteToWideChar(CP_UTF8, 0, machineGuid.toRawUTF8(), -1, nullptr, 0);
    if (requiredChars > 0)
    {
        std::wstring storedInstanceIDW(static_cast<size_t>(requiredChars - 1), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, machineGuid.toRawUTF8(), -1, storedInstanceIDW.data(), requiredChars);
        reg::writeString(reg::kRegistrySubkey, L"InstanceID", storedInstanceIDW);
    }

    return machineGuid;
#else
    // For non-Windows platforms (future support)
    juce::Uuid uuid;
    return uuid.toDashedString();
#endif
}

void clearInstanceID()
{
#if JUCE_WINDOWS
    reg::deleteValue(reg::kRegistrySubkey, L"InstanceID");
#endif
}

} // namespace InstanceIdentifier
