#pragma once

#include "ota_manager.hpp"

class OtaManagerTestable : public OtaManager
{
public:
    using OtaManager::get_manifest_ref;
    using OtaManager::handle_download_state;
    using OtaManager::handle_manifest_state;
    using OtaManager::handle_verification_state;
    using OtaManager::handle_version_state;
    using OtaManager::OtaManager;
    using OtaManager::set_status;
};
