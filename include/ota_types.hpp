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
 * @brief Result of an individual OTA step operation.
 */
enum class OtaStepResult
{
    SUCCESS,      /**< Step completed successfully, proceed to next state */
    FAILED,       /**< Step encountered an error, transition to FAILED */
    IN_PROGRESS   /**< Step is ongoing (only used by handle_download_state) */
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

struct TransportConfig
{
    uint32_t manifest_timeout_ms; /**< Timeout for manifest fetching (ms) */
    uint32_t firmware_timeout_ms; /**< Timeout for firmware download (ms) */
};

struct OtaSecurityConfig
{
    bool allow_http_during_development = false; /**< Whether to allow insecure HTTP connections */
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
    TransportConfig transport;    /**< Transport-specific settings */
    OtaSecurityConfig security;   /**< Security settings */
    bool allow_same_version;  /**< Whether to allow updates to the same version */
    bool restart_on_success;  /**< Whether to automatically restart after successful update */
};
