#include "system.hpp"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "mbedtls/sha256.h"
#include <algorithm>
#include <cstdlib>

void System::restart()
{
    esp_restart();
}

const esp_app_desc_t* System::get_running_app_desc()
{
    return esp_app_get_description();
}

esp_err_t System::get_partition_sha256(const esp_partition_t* partition, size_t size, uint8_t* sha_256)
{
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0); // Removido _ret

    const size_t chunk_size = 4096;
    uint8_t* buffer = (uint8_t*)std::malloc(chunk_size);
    if (!buffer) {
        mbedtls_sha256_free(&ctx);
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = ESP_OK;
    size_t offset = 0;
    while (offset < size) {
        size_t read_len = std::min(chunk_size, size - offset);
        err = esp_partition_read(partition, offset, buffer, read_len);
        if (err != ESP_OK) {
            break;
        }
        mbedtls_sha256_update(&ctx, buffer, read_len); // Removido _ret
        offset += read_len;
    }

    mbedtls_sha256_finish(&ctx, sha_256); // Removido _ret
    mbedtls_sha256_free(&ctx);
    std::free(buffer);
    return err;
}

const esp_partition_t* System::get_update_partition()
{
    return esp_ota_get_next_update_partition(NULL);
}
