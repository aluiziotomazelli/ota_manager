#pragma once

#include <optional>

#include "ota_types.hpp"

/**
 * @brief Public interface for the OTA Manager component.
 *
 * Provides a passive, dependency-injected API for managing the OTA update flow.
 */
class IOtaManager
{
public:
    virtual ~IOtaManager() = default;

    /** @brief Initializes the OTA manager with the provided configuration. */
    virtual bool init(const OtaConfig& config) = 0;

    /**
     * @brief Stops the worker task and cleans up resources used by the OTA manager.
     *
     * @return true if shutdown completed cleanly, false otherwise.
     */
    virtual bool deinit() = 0;

    /** @brief Initiates the OTA process in the background. */
    virtual bool start_ota() = 0;

    /**
     * @brief Cancels the current OTA operation and returns the manager to IDLE.
     *
     * This preserves the worker infrastructure for future OTA runs.
     */
    virtual void cancel_ota() = 0;

    /** @brief Returns the current status of the OTA manager. */
    virtual OtaStatus get_status() const = 0;

    /**
     * @brief Returns the specific failure reason when status is FAILED.
     *
     * @return OtaFailReason code, or OtaFailReason::NONE if no failure has occurred.
     * @note Only meaningful when get_status() == OtaStatus::FAILED.
     */
    virtual OtaFailReason get_last_error() const = 0;

    /** @brief Returns the version of the currently running firmware, if valid. */
    virtual std::optional<OtaVersion> get_running_version() const = 0;

    /** @brief Checks if a newly downloaded image is pending verification. */
    virtual bool check_pending_verify() const = 0;

    /** @brief Confirms that the current application image is valid and cancels any pending rollback. */
    virtual bool confirm_app_valid() = 0;

    /** @brief Marks the current image as invalid and requests a rollback and reboot. */
    virtual void rollback_and_reboot() = 0;
};
