#include "esp_log.h"
#include "ota_manager.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "secrets.h"

static OtaManager* g_ota_manager = nullptr;
static const gpio_num_t OTA_BUTTON_GPIO = GPIO_NUM_0;

static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI("wifi", "got ip");
    }
}

void connect_wifi(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, NULL));

    wifi_config_t wifi_config = {};
    snprintf((char*)wifi_config.sta.ssid, sizeof(wifi_config.sta.ssid), WIFI_SSID);
    snprintf((char*)wifi_config.sta.password, sizeof(wifi_config.sta.password), WIFI_PASS);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

void button_task(void* pvParameter)
{
    gpio_set_direction(OTA_BUTTON_GPIO, GPIO_MODE_INPUT);
    gpio_set_pull_mode(OTA_BUTTON_GPIO, GPIO_PULLUP_ONLY);

    while (true) {
        if (gpio_get_level(OTA_BUTTON_GPIO) == 0) {
            ESP_LOGI("button", "Button pressed, starting OTA...");
            if (g_ota_manager) {
                g_ota_manager->start_ota();
            }
            vTaskDelay(pdMS_TO_TICKS(2000));
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

extern "C" void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    connect_wifi();

    ESP_LOGI("main", "Running version: 1.0.0");

    HttpClient http_client;
    ManifestParser manifest_parser;
    OtaSession ota_session;
    System system;
    TaskScheduler task_scheduler;
    RollbackManager rollback_manager;

    OtaDependencies deps = {
        .http_client = http_client,
        .manifest_parser = manifest_parser,
        .ota_session = ota_session,
        .system = system,
        .task_scheduler = task_scheduler,
        .rollback_manager = rollback_manager};

    OtaConfig config{
        .device_type = "test_ota_manager",
        .manifest_url = SERVER_URL,
        .task_stack_size = 4096,
        .task_priority = 5,
        .http_timeout_ms = 30000,
        .allow_same_version = false,
        .restart_on_success = true};

    OtaManager ota_manager(deps);
    g_ota_manager = &ota_manager;
    ota_manager.init(config);

    if (ota_manager.check_pending_verify()) {
        ESP_LOGI("main", "Pending verify detected! Running sanity checks...");
        vTaskDelay(pdMS_TO_TICKS(10000));
        ota_manager.confirm_app_valid();
        ESP_LOGI("main", "Firmware confirmed!");
    }

    xTaskCreate(button_task, "button_task", 2048, NULL, 5, NULL);

    while (true) {
        ESP_LOGI("main", "Running version: 1.0.0");
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
