#pragma once

#include "interfaces/i_manifest_parser.hpp"

/**
 * @brief Concrete implementation of the manifest parser interface.
 *
 * Parses OTA manifest information from JSON content, extracting device type,
 * version, firmware URL, size, and SHA256 hash.
 */
class ManifestParser : public IManifestParser
{
public:
    /**
     * @copydoc IManifestParser::parse()
     */
    std::optional<OtaManifest> parse(const std::string& json_content) const override;
};
