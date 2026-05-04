#pragma once

#include "ota_types.hpp"
#include <optional>
#include <string>

class VersionHelper
{
public:
    static std::optional<OtaVersion> parse(const std::string& version_str);
};
