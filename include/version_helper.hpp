#pragma once

#include "ota_types.hpp"
#include <optional>
#include <string>

/**
 * @brief Helper class for version string parsing and validation.
 *
 * Provides static methods to parse version strings into structured OtaVersion objects.
 */
class VersionHelper
{
public:
    /**
     * @brief Parses a version string into an OtaVersion structure.
     *
     * @param version_str The version string to parse (e.g., "1.2.3")
     * @return std::optional<OtaVersion> Parsed version structure if successful,
     *         std::nullopt if parsing fails
     */
    static std::optional<OtaVersion> parse(const std::string& version_str);
};
