#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char label[16];
    uint32_t size;
    // Add other fields as needed
} esp_partition_t;

esp_err_t esp_partition_get_sha256(const esp_partition_t *partition, uint8_t *sha_256);

#ifdef __cplusplus
}
#endif
