#pragma once

#include "esp_http_client.h"
#include "esp_app_desc.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const esp_http_client_config_t *http_config;
    // Add other fields as needed
} esp_https_ota_config_t;

typedef void* esp_https_ota_handle_t;

#define ESP_ERR_HTTPS_OTA_BASE       0x9000
#define ESP_ERR_HTTPS_OTA_IN_PROGRESS (ESP_ERR_HTTPS_OTA_BASE + 1)

esp_err_t esp_https_ota_begin(const esp_https_ota_config_t *ota_config, esp_https_ota_handle_t *handle);
esp_err_t esp_https_ota_get_img_desc(esp_https_ota_handle_t ota_handle, esp_app_desc_t *new_app_info);
esp_err_t esp_https_ota_perform(esp_https_ota_handle_t ota_handle);
esp_err_t esp_https_ota_finish(esp_https_ota_handle_t ota_handle);
esp_err_t esp_https_ota_abort(esp_https_ota_handle_t ota_handle);

#ifdef __cplusplus
}
#endif
