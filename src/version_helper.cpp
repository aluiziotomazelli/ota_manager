#include "version_helper.hpp"
#include <regex>

std::optional<OtaVersion> VersionHelper::parse(const std::string& version_str)
{
    // Regex para encontrar o padrão X.Y.Z na string
    std::regex version_regex(R"((\d+)\.(\d+)\.(\d+))");
    std::smatch match;

    if (std::regex_search(version_str, match, version_regex)) {
        unsigned long major = std::stoul(match[1].str());
        unsigned long minor = std::stoul(match[2].str());
        unsigned long patch = std::stoul(match[3].str());

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
