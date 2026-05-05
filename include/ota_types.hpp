#pragma once

#include <cstdint>
#include <string>

/**
 * @brief Enumeration of possible OTA operation states.
 */
enum class OtaStatus
{
    IDLE,             /**< OTA manager is idle, no operation in progress */
    MANIFEST_FETCH,   /**< Currently fetching the manifest file */
    VERSION_CHECK,    /**< Comparing version numbers */
    DOWNLOADING,      /**< Downloading firmware image */
    VERIFYING,        /**< Verifying downloaded firmware */
    READY_TO_RESTART, /**< Firmware downloaded and verified, ready to apply */
    FAILED,           /**< OTA operation failed */
    PENDING_VERIFY    /**< New firmware applied, waiting for confirmation */
};

/**
 * @brief Struct representing a semantic version number.
 *
 * Contains major, minor, and patch version components.
 */
struct OtaVersion
{
    uint16_t major; /**< Major version number */
    uint16_t minor; /**< Minor version number */
    uint16_t patch; /**< Patch version number */
};

/**
 * @brief Struct representing an OTA manifest file.
 *
 * Contains metadata about the available firmware update including device compatibility,
 * version, download URL, file size, and integrity hash.
 */
struct OtaManifest
{
    std::string device_type;  /**< Device type identifier for compatibility */
    OtaVersion version;       /**< Version of the firmware */
    std::string firmware_url; /**< URL to download the firmware image */
    uint32_t firmware_size;   /**< Size of the firmware image in bytes */
    std::string sha256_hex;   /**< SHA256 hash of the firmware for integrity verification */
};

/**
 * @brief Configuration structure for the OTA manager.
 *
 * Contains all settings needed to configure the OTA manager behavior including
 * network settings, task configuration, and update policies.
 */
struct OtaConfig
{
    std::string device_type;  /**< Device type to check in manifest compatibility */
    std::string manifest_url; /**< URL to fetch the OTA manifest from */
    uint32_t task_stack_size; /**< Stack size for the OTA task */
    uint8_t task_priority;    /**< Priority for the OTA task */
    uint32_t http_timeout_ms; /**< HTTP request timeout in milliseconds */
    bool allow_same_version;  /**< Whether to allow updates to the same version */
    bool restart_on_success;  /**< Whether to automatically restart after successful update */
};
