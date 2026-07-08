# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).


## [1.0.0] - 2026-05-05

### Added
- Initial release of the OTA manager component.
- Domain-oriented HAL abstraction for network, system, and OTA session management.
- Pure iterative Finite State Machine (FSM) for robust and reactive OTA orchestration.
- Support for manifest JSON parsing and firmware version validation.
- Secure hash verification for downloaded firmware images (manual mbedtls SHA-256).
- Host-based testing support with comprehensive mock suite (98% coverage).
- On-target integration test scenarios (v1.0.0_base, v1.1.0_success, v1.2.0_failure, v1.3.0_security_failure).
- OTA test server with management CLI (`ota_server/manage.py`).

### Changed
- **Security policy enforcement**: Added `OtaSecurityConfig` with `allow_http_during_development` flag; URL validation applied at both manifest and firmware download gates.
- **Transport configuration**: Replaced single `http_timeout_ms` with `TransportConfig` containing separate `manifest_timeout_ms` and `firmware_timeout_ms`.
- **Transport ownership**: Consolidated configuration ownership in `OtaManager` — dependencies receive parameters at call time rather than construction time.
- **Session abstraction**: `IOtaSession::begin()` now accepts domain-level `OtaDownloadRequest` instead of leaking `esp_http_client_config_t`.
- **Worker lifecycle**: Refactored shutdown coordination — `deinit()` returns `bool`, worker self-deletes after signaling completion, no forced cleanup on timeout.
- **FSM thread safety**: Fixed race condition in `start_ota()` (task creation and notification now protected by mutex).
- **Version validation**: Fixed unchecked `std::optional::value()` access; version parser hardened against git metadata suffixes.
- **Const-correctness**: `IRollbackManager::is_pending_verify()` and `ISystem::get_running_app_desc()` now const.
- **OtaManager constructor**: Removed redundant config parameter; config is passed exclusively via `init()`.
- **Code organization**: Moved `OtaStepResult` to `ota_types.hpp`; centralized `OtaManagerTestable` in common header.
- **SHA-256 integrity**: Fixed mbedTLS return value checks; partial hash no longer produced on partition read errors.

### Fixed
- Race condition in `start_ota()` — `ota_task_handle_` written and read without mutex protection.
- Unchecked `mbedtls_sha256_finish` return value could produce invalid hash silently.
- Partial SHA-256 hash produced when `esp_partition_read` fails mid-loop.
- `status_` read without mutex in the worker task's wait-time calculation.
- Unchecked `std::optional::value()` call in `handle_download_state()` could crash on malformed version strings.
- Section numbering duplicates in `README.md` and `DESIGN.md`.

### Documentation
- `HTTPS_UPGRADE_PLAN.md` — complete migration plan for secure-by-default HTTPS.
- `COMPONENT_ISSUES_AND_PRE_HTTPS_FIXES.md` — tracking of all resolved pre-migration issues.
- `FIX_HISTORY.md` — consolidated record of all resolved issues and their implementation plans.
- `API.md` — full API reference with data types, enums, lifecycle methods, and usage example.
- `examples/scenarios/README.md` — detailed instructions for the four on-target test scenarios.
- All code documentation (interfaces, headers, implementation) updated to reflect current architecture.

[1.0.0]: https://github.com/aluiziotomazelli/ota_manager/releases/tag/v1.0.0