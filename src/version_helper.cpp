#include "version_helper.hpp"
#include <regex>

std::optional<OtaVersion> VersionHelper::parse(const std::string& version_str)
{
    // Regex para encontrar o padrão X.Y.Z na string
    std::regex version_regex(R"((\d+)\.(\d+)\.(\d+))");
    std::smatch match;

    if (std::regex_search(version_str, match, version_regex)) {
        unsigned long major = std::strtoul(match[1].str().c_str(), nullptr, 10);
        unsigned long minor = std::strtoul(match[2].str().c_str(), nullptr, 10);
        unsigned long patch = std::strtoul(match[3].str().c_str(), nullptr, 10);

        if (major > UINT16_MAX || minor > UINT16_MAX || patch > UINT16_MAX) {
            return std::nullopt;
        }

        return OtaVersion{
            static_cast<uint16_t>(major),
            static_cast<uint16_t>(minor),
            static_cast<uint16_t>(patch)
        };
    }

    return std::nullopt;
}
