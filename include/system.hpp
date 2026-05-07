#pragma once

#include "interfaces/i_system.hpp"

/**
 * @brief Concrete implementation of ISystem for ESP32.
 */
class System : public ISystem
{
public:
    /** @copydoc ISystem::restart */
    void restart() override;
    /** @copydoc ISystem::get_running_app_desc */
    const esp_app_desc_t* get_running_app_desc() const override;
    esp_err_t get_partition_sha256(const esp_partition_t* partition, size_t size, uint8_t* sha_256) override;
    const esp_partition_t* get_update_partition() override;
};
