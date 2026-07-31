#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ESP_APP_DESC_MAGIC_WORD (0xABCD5432) /*!< The magic word for the esp_app_desc structure that is in DROM. */

typedef struct
{
    uint32_t magic_word; /*!< Magic word ESP_APP_DESC_MAGIC_WORD */
    uint16_t major;
    uint16_t minor;
    uint16_t patch;
    char version[32];
    char project_name[32];
} esp_app_desc_t;

const esp_app_desc_t* esp_app_get_description(void);

#ifdef __cplusplus
}
#endif
