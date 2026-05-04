#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t major;
    uint16_t minor;
    uint16_t patch;
    char version[32];
    char project_name[32];
} esp_app_desc_t;

const esp_app_desc_t *esp_app_get_description(void);

#ifdef __cplusplus
}
#endif
