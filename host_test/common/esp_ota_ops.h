#pragma once

#include "esp_err.h"
#include "esp_partition.h"
#include "esp_app_desc.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ESP_OTA_IMG_NEW,
    ESP_OTA_IMG_PENDING_VERIFY,
    ESP_OTA_IMG_VALID,
    ESP_OTA_IMG_INVALID,
    ESP_OTA_IMG_ABORTED,
    ESP_OTA_IMG_UNDEFINED,
} esp_ota_img_states_t;

esp_err_t esp_ota_get_state_partition(const esp_partition_t *partition, esp_ota_img_states_t *ota_state);
esp_err_t esp_ota_mark_app_valid_cancel_rollback(void);
esp_err_t esp_ota_mark_app_invalid_rollback_and_reboot(void);
const esp_partition_t* esp_ota_get_running_partition(void);
const esp_partition_t* esp_ota_get_next_update_partition(const esp_partition_t *start_from);
const esp_app_desc_t* esp_ota_get_app_description(void);

#ifdef __cplusplus
}
#endif
