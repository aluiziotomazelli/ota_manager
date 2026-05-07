#pragma once

#include "interfaces/i_http_client.hpp"
#include "interfaces/i_manifest_parser.hpp"
#include "interfaces/i_ota_manager.hpp"
#include "interfaces/i_ota_session.hpp"
#include "interfaces/i_rollback_manager.hpp"
#include "interfaces/i_system.hpp"
#include "interfaces/i_task_scheduler.hpp"

#include "http_client.hpp"
#include "manifest_parser.hpp"
#include "ota_session.hpp"
#include "rollback_manager.hpp"
#include "system.hpp"
#include "task_scheduler.hpp"

/**
 * @brief Dependency injection structure for OTA Manager.
 * 
 * Bundles all external interface dependencies required by OtaManager to enable
 * unit testing and loose coupling.
 */
struct OtaDependencies
{
    IHttpClient& http_client;           /**< HTTP client for network operations */
    IManifestParser& manifest_parser;   /**< Parser for OTA manifest files */
    IOtaSession& ota_session;           /**< Session manager for OTA downloads */
    ISystem& system;                    /**< System-level operations interface */
    ITaskScheduler& task_scheduler;     /**< Task scheduling and management interface */
    IRollbackManager& rollback_manager; /**< Rollback management interface */
};

/**
 * @brief Concrete implementation of the OTA Manager interface.
 * 
 * Manages the complete OTA update flow including manifest fetching, version checking,
 * firmware downloading, verification, and application. Uses a state machine pattern
 * with dependency injection for testability.
 * 
 * @see IOtaManager
 */
class OtaManager : public IOtaManager
{
public:
    /**
     * @brief Constructs an OtaManager with the specified dependencies and configuration.
     * 
     * @param deps Dependency injection structure containing all required interfaces
     */
    OtaManager(const OtaDependencies& deps);
    
    /**
     * @brief Destructor for OtaManager.
     * @note Does NOT call deinit() automatically to avoid side effects in 
     * Dependency Injection scenarios. The caller is responsible for calling deinit().
     */
    ~OtaManager() override;

    /**
     * @brief Initializes the OTA manager with the provided configuration.
     * @param config Configuration settings
     * @return true if initialization succeeded, false otherwise
     */
    bool init(const OtaConfig& config) override;
    
    /**
     * @brief Stops the worker task and cleans up resources used by the OTA manager.
     *
     * @return true if shutdown completed cleanly, false otherwise.
     * @note This method MUST be called manually before the object is destroyed
     * to ensure all background tasks are stopped and mutexes are released.
     */
    bool deinit() override;
    
    /** @copydoc IOtaManager::start_ota() */
    bool start_ota() override;
    
    /** @copydoc IOtaManager::cancel_ota() */
    void cancel_ota() override;
    
    /** @copydoc IOtaManager::get_status() */
    OtaStatus get_status() const override;
    
    /** @copydoc IOtaManager::check_pending_verify() */
    bool check_pending_verify() const override;
    
    /** @copydoc IOtaManager::confirm_app_valid() */
    bool confirm_app_valid() override;
    
    /** @copydoc IOtaManager::rollback_and_reboot() */
    void rollback_and_reboot() override;

protected:
    /**
     * @brief Handles the manifest fetch state.
     * 
     * Downloads and parses the OTA manifest from the configured URL.
     * 
     * @return OtaStepResult Result of the manifest fetch operation
     */
    OtaStepResult handle_manifest_state();
    
    /**
     * @brief Handles the version check state.
     * 
     * Compares the manifest version against the current running version.
     * 
     * @return OtaStepResult Result of the version check operation
     */
    OtaStepResult handle_version_state();
    
    /**
     * @brief Handles the firmware download state.
     * 
     * Downloads the firmware image from the URL specified in the manifest.
     * 
     * @return OtaStepResult Result of the download operation
     */
    OtaStepResult handle_download_state();
    
    /**
     * @brief Handles the firmware verification state.
     * 
     * Verifies the integrity of the downloaded firmware image.
     * 
     * @return OtaStepResult Result of the verification operation
     */
    OtaStepResult handle_verification_state();

    /**
     * @brief Sets the current OTA status.
     * 
     * @param status The new status to set
     */
    void set_status(OtaStatus status);

    /**
     * @brief Returns a reference to the current manifest.
     * 
     * @return Reference to the parsed OtaManifest
     */
    OtaManifest& get_manifest_ref() { return manifest_; }

private:
    OtaDependencies deps_;          /**< Injected dependencies */
    OtaConfig config_;              /**< Configuration settings */
    OtaStatus status_;              /**< Current OTA status */
    TaskHandle_t ota_task_handle_ = nullptr;    /**< Handle to the OTA FreeRTOS task */

    OtaManifest manifest_;          /**< Parsed manifest data */
    SemaphoreHandle_t state_mutex_ = nullptr;   /**< Mutex for state protection */
    SemaphoreHandle_t shutdown_done_ = nullptr; /**< Semaphore signaling shutdown completion */

    /**
     * @brief Static FreeRTOS task entry point.
     * 
     * @param pvParameters Pointer to the OtaManager instance
     */
    static void ota_task_func(void* pvParameters);
    
    /**
     * @brief Main OTA task loop implementation.
     * 
     * Executes the OTA state machine in a dedicated FreeRTOS task.
     */
    void ota_task();

    /**
     * @brief Signals that the OTA task has completed shutdown.
     */
    void signal_shutdown_done();

    /**
     * @brief Finalizes worker-owned shutdown state before self-deletion.
     */
    void finalize_worker_shutdown();
};
