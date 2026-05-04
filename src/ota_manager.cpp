// components/ota_manager/src/ota_manager.cpp
#include "ota_manager.hpp"
#include "esp_log.h"
#include "manifest_parser.hpp"
#include "version_helper.hpp"
#include <iomanip>
#include <sstream>

static const char* TAG = "OtaManager";

namespace {
static constexpr uint32_t OTA_START_BIT = 0x01;
static constexpr uint32_t OTA_STOP_BIT = 0x02;
static constexpr uint32_t OTA_CANCEL_BIT = 0x04;

bool is_version_newer(const OtaVersion& current, const OtaVersion& manifest, bool allow_same)
{
    if (manifest.major > current.major)
        return true;
    if (manifest.major < current.major)
        return false;
    if (manifest.minor > current.minor)
        return true;
    if (manifest.minor < current.minor)
        return false;
    if (manifest.patch > current.patch)
        return true;
    if (manifest.patch < current.patch)
        return false;
    return allow_same;
}

std::string bytes_to_hex(const uint8_t* bytes, size_t len)
{
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    for (size_t i = 0; i < len; ++i) {
        ss << std::setw(2) << static_cast<int>(bytes[i]);
    }
    return ss.str();
}
} // namespace

OtaManager::OtaManager(const OtaDependencies& deps, const OtaConfig& config)
    : deps_(deps)
    , config_(config)
    , status_(OtaStatus::IDLE)
{
}

OtaManager::~OtaManager()
{
    // deinit();
}

bool OtaManager::init(const OtaConfig& config)
{
    config_ = config;

    if (state_mutex_ == nullptr) {
        state_mutex_ = deps_.task_scheduler.mutex_create();
        if (state_mutex_ == nullptr) {
            ESP_LOGE(TAG, "Failed to create state mutex");
            return false;
        }
    }

    if (shutdown_done_ == nullptr) {
        shutdown_done_ = deps_.task_scheduler.semaphore_binary_create();
        if (shutdown_done_ == nullptr) {
            ESP_LOGE(TAG, "Failed to create shutdown semaphore");
            deps_.task_scheduler.semaphore_delete(state_mutex_);
            state_mutex_ = nullptr;
            return false;
        }
    }

    set_status(OtaStatus::IDLE);
    return true;
}

void OtaManager::deinit()
{
    deps_.ota_session.abort();

    TaskHandle_t worker_handle = nullptr;
    if (state_mutex_ != nullptr && deps_.task_scheduler.semaphore_take(state_mutex_, portMAX_DELAY) == pdPASS) {
        worker_handle = ota_task_handle_;
        deps_.task_scheduler.semaphore_give(state_mutex_);
    }

    if (worker_handle != nullptr) {
        deps_.task_scheduler.notify_task(worker_handle, OTA_STOP_BIT, eSetBits);

        if (shutdown_done_ != nullptr) {
            if (deps_.task_scheduler.semaphore_take(shutdown_done_, pdMS_TO_TICKS(1000)) != pdPASS) {
                ESP_LOGW(TAG, "OTA worker did not stop in time");
            }
        }
    }

    if (state_mutex_ != nullptr && deps_.task_scheduler.semaphore_take(state_mutex_, portMAX_DELAY) == pdPASS) {
        if (ota_task_handle_ != nullptr) {
            deps_.task_scheduler.delete_task(ota_task_handle_);
            ota_task_handle_ = nullptr;
        }
        deps_.task_scheduler.semaphore_give(state_mutex_);
    }

    if (shutdown_done_ != nullptr) {
        deps_.task_scheduler.semaphore_delete(shutdown_done_);
        shutdown_done_ = nullptr;
    }

    if (state_mutex_ != nullptr) {
        deps_.task_scheduler.semaphore_delete(state_mutex_);
        state_mutex_ = nullptr;
    }

    status_ = OtaStatus::IDLE;
}

bool OtaManager::start_ota()
{
    if (get_status() != OtaStatus::IDLE && get_status() != OtaStatus::FAILED) {
        return false;
    }

    TaskHandle_t worker_handle = nullptr;
    if (state_mutex_ != nullptr && deps_.task_scheduler.semaphore_take(state_mutex_, portMAX_DELAY) == pdPASS) {
        worker_handle = ota_task_handle_;
        deps_.task_scheduler.semaphore_give(state_mutex_);
    }

    if (worker_handle == nullptr) {
        BaseType_t ret = deps_.task_scheduler.create_task(
            ota_task_func, "ota_worker", config_.task_stack_size, this, config_.task_priority, &ota_task_handle_);

        if (ret != pdPASS) {
            ESP_LOGE(TAG, "Failed to create OTA task");
            return false;
        }
    }

    deps_.task_scheduler.notify_task(ota_task_handle_, OTA_START_BIT, eSetBits);
    return true;
}

void OtaManager::cancel_ota()
{
    deps_.ota_session.abort();

    TaskHandle_t worker_handle = nullptr;
    if (state_mutex_ != nullptr && deps_.task_scheduler.semaphore_take(state_mutex_, portMAX_DELAY) == pdPASS) {
        worker_handle = ota_task_handle_;
        deps_.task_scheduler.semaphore_give(state_mutex_);
    }

    if (worker_handle != nullptr) {
        deps_.task_scheduler.notify_task(worker_handle, OTA_CANCEL_BIT, eSetBits);
    }
}

OtaStatus OtaManager::get_status() const
{
    if (state_mutex_ == nullptr) {
        return status_;
    }

    if (deps_.task_scheduler.semaphore_take(state_mutex_, portMAX_DELAY) != pdPASS) {
        return status_;
    }

    OtaStatus current_status = status_;
    deps_.task_scheduler.semaphore_give(state_mutex_);
    return current_status;
}

bool OtaManager::check_pending_verify() const
{
    return deps_.rollback_manager.is_pending_verify();
}

bool OtaManager::confirm_app_valid()
{
    return deps_.rollback_manager.mark_app_valid() == ESP_OK;
}

void OtaManager::rollback_and_reboot()
{
    deps_.rollback_manager.rollback_and_reboot();
}

void OtaManager::ota_task_func(void* pvParameters)
{
    OtaManager* self = static_cast<OtaManager*>(pvParameters);
    self->ota_task();
}

void OtaManager::ota_task()
{
    uint32_t notifications = 0;
    bool should_exit = false;

    while (!should_exit) {
        // Variable blocking: wait forever if idle/failed/pending, otherwise poll
        TickType_t wait_time =
            (status_ == OtaStatus::IDLE || status_ == OtaStatus::FAILED || status_ == OtaStatus::PENDING_VERIFY)
                ? portMAX_DELAY
                : 0;

        // Peek at notifications
        if (deps_.task_scheduler.task_notify_wait(0, 0, &notifications, wait_time) == pdPASS) {
            if ((notifications & OTA_STOP_BIT) == OTA_STOP_BIT) {
                deps_.ota_session.abort(); // Ensure session is closed if task stops
                should_exit = true;
                continue;
            }

            if ((notifications & OTA_CANCEL_BIT) == OTA_CANCEL_BIT) {
                ESP_LOGI(TAG, "OTA cancellation requested");
                deps_.ota_session.abort();
                set_status(OtaStatus::IDLE);
                // Clear the bits we just processed
                deps_.task_scheduler.task_notify_wait(0, (OTA_CANCEL_BIT | OTA_START_BIT), &notifications, 0);
                continue;
            }

            if ((notifications & OTA_START_BIT) == OTA_START_BIT) {
                if (status_ == OtaStatus::IDLE || status_ == OtaStatus::FAILED) {
                    set_status(OtaStatus::MANIFEST_FETCH);
                }
                // Clear the start bit
                deps_.task_scheduler.task_notify_wait(0, OTA_START_BIT, &notifications, 0);
            }
        }

        OtaStepResult step_res = OtaStepResult::IN_PROGRESS;
        switch (status_) {
        case OtaStatus::MANIFEST_FETCH:
            step_res = handle_manifest_state();
            if (step_res == OtaStepResult::SUCCESS) {
                set_status(OtaStatus::VERSION_CHECK);
            }
            else if (step_res == OtaStepResult::FAILED) {
                set_status(OtaStatus::FAILED);
            }
            break;

        case OtaStatus::VERSION_CHECK:
            step_res = handle_version_state();
            if (step_res == OtaStepResult::SUCCESS) {
                set_status(OtaStatus::DOWNLOADING);
            }
            else if (step_res == OtaStepResult::FAILED) {
                set_status(OtaStatus::FAILED);
            }
            break;

        case OtaStatus::DOWNLOADING:
            step_res = handle_download_state();
            if (step_res == OtaStepResult::SUCCESS) {
                set_status(OtaStatus::VERIFYING);
            }
            else if (step_res == OtaStepResult::FAILED) {
                set_status(OtaStatus::FAILED);
            }
            break;

        case OtaStatus::VERIFYING:
            step_res = handle_verification_state();
            if (step_res == OtaStepResult::SUCCESS) {
                set_status(OtaStatus::READY_TO_RESTART);
            }
            else if (step_res == OtaStepResult::FAILED) {
                set_status(OtaStatus::FAILED);
            }
            break;

        case OtaStatus::READY_TO_RESTART:
            if (config_.restart_on_success) {
                ESP_LOGI(TAG, "OTA Successful. Restarting...");
                deps_.system.restart();
                should_exit = true;
            }
            else {
                set_status(OtaStatus::IDLE);
            }
            break;

        default:
            break;
        }
    }

    ESP_LOGI(TAG, "OTA Task exiting.");

    if (state_mutex_ != nullptr && deps_.task_scheduler.semaphore_take(state_mutex_, portMAX_DELAY) == pdPASS) {
        ota_task_handle_ = nullptr;
        deps_.task_scheduler.semaphore_give(state_mutex_);
    }

    signal_shutdown_done();
    deps_.task_scheduler.delete_task(nullptr);
}

// ==================================================================================
// Private methods
// ==================================================================================

OtaStepResult OtaManager::handle_manifest_state()
{
    std::string manifest_content;
    if (deps_.http_client.fetch(config_.manifest_url, manifest_content) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to fetch manifest from %s", config_.manifest_url.c_str());
        return OtaStepResult::FAILED;
    }

    auto manifest_opt = deps_.manifest_parser.parse(manifest_content);
    if (!manifest_opt.has_value()) {
        ESP_LOGE(TAG, "Failed to parse manifest JSON");
        return OtaStepResult::FAILED;
    }

    manifest_ = manifest_opt.value();
    ESP_LOGI(
        TAG,
        "Manifest fetched: version %u.%u.%u for %s",
        manifest_.version.major,
        manifest_.version.minor,
        manifest_.version.patch,
        manifest_.device_type.c_str());
    return OtaStepResult::SUCCESS;
}

OtaStepResult OtaManager::handle_version_state()
{
    // 1. Validate Device Type
    if (manifest_.device_type != config_.device_type) {
        ESP_LOGE(
            TAG,
            "Device type mismatch: manifest=%s, config=%s",
            manifest_.device_type.c_str(),
            config_.device_type.c_str());
        return OtaStepResult::FAILED;
    }

    // 2. Fetch current version
    const esp_app_desc_t* running_app = deps_.system.get_running_app_desc();
    auto current_v_opt = VersionHelper::parse(running_app->version);
    if (!current_v_opt.has_value()) {
        ESP_LOGE(TAG, "Failed to parse current version string: %s", running_app->version);
        return OtaStepResult::FAILED;
    }

    // 3. Compare versions
    if (!is_version_newer(current_v_opt.value(), manifest_.version, config_.allow_same_version)) {
        ESP_LOGW(
            TAG,
            "Version is not newer. Current: %s, Manifest: %u.%u.%u",
            running_app->version,
            manifest_.version.major,
            manifest_.version.minor,
            manifest_.version.patch);
        return OtaStepResult::FAILED;
    }

    ESP_LOGI(TAG, "Version check passed. Proceeding with download.");
    return OtaStepResult::SUCCESS;
}

OtaStepResult OtaManager::handle_download_state()
{
    // 1. Setup session if not active
    if (!deps_.ota_session.is_active()) {
        esp_http_client_config_t http_config = {};
        http_config.url = manifest_.firmware_url.c_str();
        http_config.timeout_ms = config_.http_timeout_ms;

        if (deps_.ota_session.begin(&http_config) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to begin OTA session");
            deps_.ota_session.abort();
            return OtaStepResult::FAILED;
        }

        // Verify image descriptor (extra safety)
        esp_app_desc_t new_app_info;
        if (deps_.ota_session.get_img_desc(&new_app_info) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to get image descriptor");
            deps_.ota_session.abort();
            return OtaStepResult::FAILED;
        }

        auto current_v_opt = VersionHelper::parse(deps_.system.get_running_app_desc()->version);
        auto new_v_opt = VersionHelper::parse(new_app_info.version);
        if (!new_v_opt.has_value() ||
            !is_version_newer(current_v_opt.value(), new_v_opt.value(), config_.allow_same_version)) {
            ESP_LOGE(TAG, "Image version validation failed: %s", new_app_info.version);
            deps_.ota_session.abort();
            return OtaStepResult::FAILED;
        }
        ESP_LOGI(TAG, "OTA session initialized, starting download...");
    }

    // 2. Perform one iteration of download
    esp_err_t ret = deps_.ota_session.perform();

    if (ret == ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
        return OtaStepResult::IN_PROGRESS;
    }

    if (ret != ESP_OK || !deps_.ota_session.is_complete()) {
        ESP_LOGE(TAG, "Download failed: %s", esp_err_to_name(ret));
        deps_.ota_session.abort();
        return OtaStepResult::FAILED;
    }

    // 3. Cleanup session on success
    if (deps_.ota_session.finish() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to finish OTA session");
        return OtaStepResult::FAILED;
    }

    ESP_LOGI(TAG, "Download completed successfully");
    return OtaStepResult::SUCCESS;
}

OtaStepResult OtaManager::handle_verification_state()
{
    uint8_t sha256[32];
    const esp_partition_t* update_partition = deps_.system.get_update_partition();

    if (update_partition == nullptr) {
        ESP_LOGE(TAG, "Failed to get update partition");
        return OtaStepResult::FAILED;
    }

    if (deps_.system.get_partition_sha256(update_partition, sha256) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to calculate SHA256 for partition");
        return OtaStepResult::FAILED;
    }

    std::string calculated_hash = bytes_to_hex(sha256, 32);
    if (calculated_hash != manifest_.sha256_hex) {
        ESP_LOGE(
            TAG, "Hash mismatch! Manifest: %s, Calculated: %s", manifest_.sha256_hex.c_str(), calculated_hash.c_str());
        return OtaStepResult::FAILED;
    }

    ESP_LOGI(TAG, "SHA256 verification passed: %s", calculated_hash.c_str());
    return OtaStepResult::SUCCESS;
}

void OtaManager::set_status(OtaStatus status)
{
    if (state_mutex_ == nullptr) {
        status_ = status;
        return;
    }

    if (deps_.task_scheduler.semaphore_take(state_mutex_, portMAX_DELAY) == pdPASS) {
        status_ = status;
        deps_.task_scheduler.semaphore_give(state_mutex_);
    }
}

void OtaManager::signal_shutdown_done()
{
    if (shutdown_done_ != nullptr) {
        deps_.task_scheduler.semaphore_give(shutdown_done_);
    }
}
