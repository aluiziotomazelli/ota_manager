#pragma once

#include "interfaces/i_system.hpp"

/**
 * @brief Concrete implementation of ISystem for ESP32.
 */
class System : public ISystem
{
public:
    void restart() override;
    const esp_app_desc_t* get_running_app_desc() override;
    esp_err_t get_partition_sha256(const esp_partition_t* partition, size_t size, uint8_t* sha_256) override;
    const esp_partition_t* get_update_partition() override;
};
