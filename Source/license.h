#pragma once

#include <string>

namespace license {
    std::string makeLicense(const std::string& first,
                            const std::string& last,
                            const std::string& email);

    bool verifyLicense(const std::string& licenseStr,
                       const std::string& first,
                       const std::string& last,
                       const std::string& email);
} // namespace license
