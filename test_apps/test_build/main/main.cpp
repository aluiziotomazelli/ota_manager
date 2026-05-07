#include "esp_log.h"
// #include "i_ota_manager.hpp"
#include "ota_manager.hpp"

extern "C" void app_main(void)
{
    ESP_LOGI("main", "Testing OtaManager component compilation");
    // Just a basic usage to ensure linking and headers work

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
        .device_type = "test",
        .manifest_url = "http://localhost:8080/manifest.json",
        .task_stack_size = 4096,
        .task_priority = 5,
        .http_timeout_ms = 30000,
        .allow_same_version = true,
        .restart_on_success = true};

    OtaManager ota_manager(deps);
    ota_manager.init(config);
}
