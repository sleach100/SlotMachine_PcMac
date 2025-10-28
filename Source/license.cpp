/*
    License generation and verification helpers.

    IMPORTANT: Replace the SECRET array below with your own random bytes
    before shipping. The license format is VERSION-DATE-XXXX-XXXX-XXXX,
    where VERSION is currently "V1", DATE is UTC YYYYMMDD, and the
    suffix is derived from the first 18 Base32 characters of the
    HMAC-SHA256 signature of the payload "first|last|email|version|date".
    The UI displays up to the first 12 characters (three 4-character
    groups) for readability.
*/

#include "crypto_small.h"
#include "base32.h"
#include "license.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <vector>

namespace license {
    using crypto_small::hmac_sha256;

    namespace {
        static const char* kVersion = "V1";

        // TODO: REPLACE SECRET with 32+ random bytes before release.
        static const uint8_t SECRET[] = {
            0x4f, 0x92, 0x7b, 0x61, 0x33, 0xa8, 0xde, 0x5c,
            0x11, 0xfe, 0x76, 0x2a, 0x9d, 0x44, 0x3b, 0x50,
            0x8c, 0xe7, 0x17, 0xd4, 0x6a, 0x0b, 0x2c, 0x95,
            0x38, 0xf1, 0xaa, 0x66, 0xcd, 0x12, 0x7e, 0xb4
        };

        inline std::string trim(const std::string& s)
        {
            size_t start = 0;
            while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start])))
                ++start;
            size_t end = s.size();
            while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1])))
                --end;
            return s.substr(start, end - start);
        }

        bool isDigits(const std::string& s)
        {
            return std::all_of(s.begin(), s.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; });
        }

        bool constTimeEquals(const std::string& a, const std::string& b)
        {
            if (a.size() != b.size())
                return false;
            unsigned char result = 0;
            for (size_t i = 0; i < a.size(); ++i)
            {
                result |= static_cast<unsigned char>(a[i] ^ b[i]);
            }
            return result == 0;
        }

        std::string normalizeField(const std::string& s)
        {
            std::string trimmed = trim(s);
            std::string out;
            out.reserve(trimmed.size());
            bool inSpace = false;
            for (char ch : trimmed)
            {
                unsigned char uch = static_cast<unsigned char>(ch);
                const bool isSpace = std::isspace(uch) != 0;
                if (isSpace)
                {
                    if (! inSpace && ! out.empty())
                    {
                        out.push_back(' ');
                        inSpace = true;
                    }
                }
                else
                {
                    out.push_back(static_cast<char>(std::tolower(uch)));
                    inSpace = false;
                }
            }
            return out;
        }

        std::string makePayload(const std::string& first,
                                const std::string& last,
                                const std::string& email,
                                const std::string& version,
                                const std::string& yyyymmdd)
        {
            std::ostringstream oss;
            oss << normalizeField(first) << '|'
                << normalizeField(last) << '|'
                << normalizeField(email) << '|'
                << version << '|'
                << yyyymmdd;
            return oss.str();
        }

        std::string utcDateYYYYMMDD()
        {
            std::time_t now = std::time(nullptr);
            std::tm tm{};
#if defined(_WIN32)
            gmtime_s(&tm, &now);
#else
            gmtime_r(&now, &tm);
#endif
            std::ostringstream oss;
            oss << std::setfill('0')
                << std::setw(4) << (tm.tm_year + 1900)
                << std::setw(2) << (tm.tm_mon + 1)
                << std::setw(2) << tm.tm_mday;
            return oss.str();
        }
    }

    std::string makeLicense(const std::string& first,
                            const std::string& last,
                            const std::string& email)
    {
        const std::string date = utcDateYYYYMMDD();
        const std::string payload = makePayload(first, last, email, kVersion, date);
        const auto digest = hmac_sha256(SECRET, sizeof(SECRET),
                                        reinterpret_cast<const uint8_t*>(payload.data()), payload.size());
        const std::string encoded = base32::base32_encode(digest.data(), digest.size());
        const std::string signatureFull = encoded.substr(0, std::min<size_t>(18, encoded.size()));
        if (signatureFull.size() < 12)
            return {};
        const std::string signature = signatureFull.substr(0, 12);

        std::string formatted;
        const size_t versionLen = std::strlen(kVersion);
        formatted.reserve(versionLen + 1 + date.size() + 1 + 4 + 1 + 4 + 1 + 4);
        formatted.append(kVersion);
        formatted.push_back('-');
        formatted.append(date);
        formatted.push_back('-');
        formatted.append(signature.substr(0, 4));
        formatted.push_back('-');
        formatted.append(signature.substr(4, 4));
        formatted.push_back('-');
        formatted.append(signature.substr(8, 4));

        return formatted;
    }

    bool verifyLicense(const std::string& licenseStr,
                       const std::string& first,
                       const std::string& last,
                       const std::string& email)
    {
        if (licenseStr.empty())
            return false;

        std::string cleaned;
        cleaned.reserve(licenseStr.size());
        for (char c : licenseStr)
        {
            if (std::isspace(static_cast<unsigned char>(c)))
                continue;
            cleaned.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
        }

        std::vector<std::string> parts;
        size_t start = 0;
        while (start <= cleaned.size())
        {
            size_t pos = cleaned.find('-', start);
            if (pos == std::string::npos)
            {
                parts.push_back(cleaned.substr(start));
                break;
            }
            parts.push_back(cleaned.substr(start, pos - start));
            start = pos + 1;
        }

        if (parts.size() != 5)
            return false;

        const std::string& version = parts[0];
        const std::string& date = parts[1];
        if (version != kVersion || date.size() != 8 || ! isDigits(date))
            return false;

        for (size_t i = 2; i < parts.size(); ++i)
        {
            if (parts[i].empty())
                return false;
        }

        std::string signature = parts[2] + parts[3] + parts[4];
        if (signature.size() != 12)
            return false;

        if (! std::all_of(signature.begin(), signature.end(), [](char c)
        {
            return (c >= 'A' && c <= 'Z') || (c >= '2' && c <= '7');
        }))
            return false;

        const std::string payload = makePayload(first, last, email, version, date);
        const auto digest = hmac_sha256(SECRET, sizeof(SECRET),
                                        reinterpret_cast<const uint8_t*>(payload.data()), payload.size());
        const std::string encoded = base32::base32_encode(digest.data(), digest.size());
        const std::string expected = encoded.substr(0, 12);

        if (expected.size() != signature.size())
            return false;

        return constTimeEquals(signature, expected);
    }
} // namespace license
