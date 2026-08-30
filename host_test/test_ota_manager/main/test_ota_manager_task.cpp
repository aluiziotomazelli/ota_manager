#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "ota_manager.hpp"

#include "mock_i_http_client.hpp"
#include "mock_i_manifest_parser.hpp"
#include "mock_i_ota_session.hpp"
#include "mock_i_rollback_manager.hpp"
#include "mock_i_system.hpp"
#include "real_task_scheduler.hpp"

using ::testing::_;
using ::testing::DoAll;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SetArgPointee;
using ::testing::SetArgReferee;
using ::testing::SetArrayArgument;
#include "ota_manager.hpp"
#include "testable_ota_manager.hpp"

class OtaManagerTaskTest : public ::testing::Test
{
protected:
    NiceMock<MockHttpClient> mock_http_client;
    NiceMock<MockManifestParser> mock_manifest_parser;
    NiceMock<MockOtaSession> mock_ota_session;
    NiceMock<MockRollbackManager> mock_rollback_manager;
    NiceMock<MockSystem> mock_system;
    RealTaskScheduler real_task_scheduler;

    OtaDependencies deps = {
        .http_client = mock_http_client,
        .manifest_parser = mock_manifest_parser,
        .ota_session = mock_ota_session,
        .system = mock_system,
        .task_scheduler = real_task_scheduler,
        .rollback_manager = mock_rollback_manager};

    OtaConfig config = {
        .device_type = "test",
        .manifest_url = "http://localhost:8080/manifest.json",
        .task_stack_size = 4096,
        .task_priority = 5,
        .transport = {
        .manifest_timeout_ms = 30000,
        .firmware_timeout_ms = 30000,
    },
        .security = {.allow_http_during_development = true},
        .allow_same_version = true,
        .restart_on_success = true};

    OtaManagerTestable sut = OtaManagerTestable(deps);

    SemaphoreHandle_t fake_mutex = reinterpret_cast<SemaphoreHandle_t>(0x1);
    SemaphoreHandle_t fake_shutdown_done = reinterpret_cast<SemaphoreHandle_t>(0x2);
    TaskHandle_t fake_task = reinterpret_cast<TaskHandle_t>(0x3);

    static constexpr uint32_t OTA_START_BIT = 0x01;
    static constexpr uint32_t OTA_STOP_BIT = 0x02;
    static constexpr uint32_t OTA_CANCEL_BIT = 0x04;

    static constexpr uint16_t delay = 10;

    void init_and_wait()
    {
        sut.init(config);
        vTaskDelay(pdMS_TO_TICKS(delay));
    }

    std::pair<std::string, OtaManifest> create_standard_manifest()
    {
        std::string json = R"({"version": "2.0.0", "device_type": "test", "firmware_url": "http://link.to/bin"})";
        OtaManifest manifest;
        manifest.version = {2, 0, 0};
        manifest.device_type = "test";
        manifest.firmware_url = "http://link.to/bin";
        return {json, manifest};
    }

    void setup_success_mocks(const std::string& manifest_json, const OtaManifest& manifest, const esp_app_desc_t& running_app)
    {
        EXPECT_CALL(mock_http_client, fetch(config.manifest_url, _, _))
            .WillRepeatedly(DoAll(SetArgReferee<1>(manifest_json), Return(ESP_OK)));

        EXPECT_CALL(mock_manifest_parser, parse(manifest_json)).WillRepeatedly(Return(manifest));
        EXPECT_CALL(mock_system, get_running_app_desc()).WillRepeatedly(Return(&running_app));
        EXPECT_CALL(mock_ota_session, is_active()).WillRepeatedly(Return(true));
        EXPECT_CALL(mock_ota_session, begin(::testing::_)).WillRepeatedly(Return(ESP_OK));
        EXPECT_CALL(mock_ota_session, perform()).WillRepeatedly(Return(ESP_OK));
        EXPECT_CALL(mock_ota_session, finish()).WillRepeatedly(Return(ESP_OK));
        
        const esp_partition_t fake_partition = {};
        EXPECT_CALL(mock_system, get_update_partition()).WillRepeatedly(Return(&fake_partition));
        EXPECT_CALL(mock_system, get_partition_sha256(&fake_partition, _, _)).WillRepeatedly(Return(ESP_OK));
    }
};

// ====================================================================
// INIT TESTS
// ====================================================================

TEST_F(OtaManagerTaskTest, InitCreateMutexAndSemaphoreSuccess)
{
    EXPECT_TRUE(sut.init(config));
    EXPECT_EQ(sut.get_status(), OtaStatus::IDLE);
    sut.start_ota();
}

TEST_F(OtaManagerTaskTest, FullOtaSuccessFlow)
{
    // 1. Prepare Data
    auto [manifest_json, manifest] = create_standard_manifest();
    manifest.sha256_hex = "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f";
    
    // Update JSON string to include sha256 for the test
    manifest_json = R"({"version": "2.0.0", "device_type": "test", "firmware_url": "http://link.to/bin", "sha256": "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f"})";

    esp_app_desc_t running_app = {};
    strncpy(running_app.version, "1.0.0", sizeof(running_app.version));

    esp_app_desc_t new_app_desc = {};
    strncpy(new_app_desc.version, "2.0.0", sizeof(new_app_desc.version));

    // 2. Setup Mocks
    EXPECT_CALL(mock_http_client, fetch(config.manifest_url, _, _))
        .WillOnce(DoAll(SetArgReferee<1>(manifest_json), Return(ESP_OK)));

    EXPECT_CALL(mock_manifest_parser, parse(manifest_json)).WillOnce(Return(manifest));

    EXPECT_CALL(mock_system, get_running_app_desc()).WillRepeatedly(Return(&running_app));

    EXPECT_CALL(mock_ota_session, is_active())
        .WillOnce(Return(false)) // First check in download state
        .WillRepeatedly(Return(true));

    EXPECT_CALL(mock_ota_session, begin(::testing::_)).WillOnce(Return(ESP_OK));

    EXPECT_CALL(mock_ota_session, get_img_desc(_)).WillOnce(DoAll(SetArgPointee<0>(new_app_desc), Return(ESP_OK)));

    EXPECT_CALL(mock_ota_session, perform()).WillOnce(Return(ESP_OK));


    EXPECT_CALL(mock_ota_session, finish()).WillOnce(Return(ESP_OK));

    // 2.1 Verification setup
    const esp_partition_t fake_partition = {};
    uint8_t expected_sha[32] = {0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15,
                                16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31};

    // Modify config for deterministic state checking
    config.restart_on_success = false;

    EXPECT_CALL(mock_system, get_update_partition()).WillOnce(Return(&fake_partition));
    EXPECT_CALL(mock_system, get_partition_sha256(&fake_partition, _, _))
        .WillOnce(DoAll(SetArrayArgument<2>(expected_sha, expected_sha + 32), Return(ESP_OK)));

    EXPECT_CALL(mock_system, restart()).Times(0);

    // 3. Execute
    ASSERT_TRUE(sut.init(config));
    ASSERT_TRUE(sut.start_ota());

    // 4. Wait for transitions
    int timeout_ms = 1000;
    int elapsed_ms = 0;
    while (sut.get_status() != OtaStatus::READY_TO_RESTART && elapsed_ms < timeout_ms) {
        vTaskDelay(pdMS_TO_TICKS(10));
        elapsed_ms += 10;
    }
    EXPECT_EQ(sut.get_status(), OtaStatus::READY_TO_RESTART);
}

// ====================================================================
// VERSION VALIDATION TEST
// ====================================================================

TEST_F(OtaManagerTaskTest, VersionCheckFailsForOlderVersion)
{
    // 1. Prepare Data
    std::string manifest_json = R"({"version": "0.9.0", "device_type": "test", "firmware_url": "http://link.to/bin"})";
    OtaManifest manifest;
    manifest.version = {0, 9, 0};
    manifest.device_type = "test";
    manifest.firmware_url = "http://link.to/bin";

    esp_app_desc_t running_app = {};
    strncpy(running_app.version, "1.0.0", sizeof(running_app.version));

    // 2. Setup Mocks
    EXPECT_CALL(mock_http_client, fetch(config.manifest_url, _, _))
        .WillOnce(DoAll(SetArgReferee<1>(manifest_json), Return(ESP_OK)));

    EXPECT_CALL(mock_manifest_parser, parse(manifest_json)).WillOnce(Return(manifest));

    EXPECT_CALL(mock_system, get_running_app_desc()).WillRepeatedly(Return(&running_app));

    // 3. Execute
    ASSERT_TRUE(sut.init(config));
    ASSERT_TRUE(sut.start_ota());

    // 4. Wait for transition
    int timeout_ms = 500;
    int elapsed_ms = 0;
    while (sut.get_status() != OtaStatus::FAILED && elapsed_ms < timeout_ms) {
        vTaskDelay(pdMS_TO_TICKS(10));
        elapsed_ms += 10;
    }
    EXPECT_EQ(sut.get_status(), OtaStatus::FAILED);
}

// ====================================================================
// DOWNLOAD FAILURE TEST
// ====================================================================

TEST_F(OtaManagerTaskTest, DownloadFailsWhenSessionBeginFails)
{
    // 1. Prepare Data
    auto [manifest_json, manifest] = create_standard_manifest();

    esp_app_desc_t running_app = {};
    strncpy(running_app.version, "1.0.0", sizeof(running_app.version));

    // 2. Setup Mocks
    EXPECT_CALL(mock_http_client, fetch(config.manifest_url, _, _))
        .WillOnce(DoAll(SetArgReferee<1>(manifest_json), Return(ESP_OK)));

    EXPECT_CALL(mock_manifest_parser, parse(manifest_json)).WillOnce(Return(manifest));

    EXPECT_CALL(mock_system, get_running_app_desc()).WillRepeatedly(Return(&running_app));

    // Simulate session begin failure
    EXPECT_CALL(mock_ota_session, is_active()).WillOnce(Return(false));
    EXPECT_CALL(mock_ota_session, begin(::testing::_)).WillOnce(Return(ESP_FAIL));

    // Expect abort to be called during failure cleanup
    EXPECT_CALL(mock_ota_session, abort()).Times(1);

    // 3. Execute
    ASSERT_TRUE(sut.init(config));
    ASSERT_TRUE(sut.start_ota());

    // 4. Wait for transition
    int timeout_ms = 500;
    int elapsed_ms = 0;
    while (sut.get_status() != OtaStatus::FAILED && elapsed_ms < timeout_ms) {
        vTaskDelay(pdMS_TO_TICKS(10));
        elapsed_ms += 10;
    }
    EXPECT_EQ(sut.get_status(), OtaStatus::FAILED);
}

// ====================================================================
// SHUTDOWN TEST
// ====================================================================

TEST_F(OtaManagerTaskTest, ShutdownCleansUpResourcesAndStopsTask)
{
    // Setup - start the OTA task
    config.restart_on_success = false;

    EXPECT_CALL(mock_http_client, fetch(_, _, _)).WillRepeatedly(Return(ESP_FAIL));

    ASSERT_TRUE(sut.init(config));
    ASSERT_TRUE(sut.start_ota());

    // Give it a moment to enter the task loop
    vTaskDelay(pdMS_TO_TICKS(50));

    // Expectations for Deinit
    EXPECT_CALL(mock_ota_session, abort()).Times(::testing::AtLeast(1));

    // Execute Deinit
    EXPECT_TRUE(sut.deinit());

    // Wait a bit for the task to be deleted by the scheduler
    vTaskDelay(pdMS_TO_TICKS(50));

    // Verify status is IDLE after deinit
    EXPECT_EQ(sut.get_status(), OtaStatus::IDLE);
}

// ====================================================================
// VERIFICATION FAILURE TEST
// ====================================================================

TEST_F(OtaManagerTaskTest, VerificationFailsTransitionsToFailed)
{
    // 1. Prepare Data
    std::string manifest_json = R"({"version": "2.0.0", "device_type": "test", "firmware_url": "http://link.to/bin"})";
    OtaManifest manifest;
    manifest.version = {2, 0, 0};
    manifest.device_type = "test";
    manifest.firmware_url = "http://link.to/bin";

    esp_app_desc_t running_app = {};
    strncpy(running_app.version, "1.0.0", sizeof(running_app.version));

    // 2. Setup Mocks
    EXPECT_CALL(mock_http_client, fetch(config.manifest_url, _, _))
        .WillOnce(DoAll(SetArgReferee<1>(manifest_json), Return(ESP_OK)));

    EXPECT_CALL(mock_manifest_parser, parse(manifest_json)).WillOnce(Return(manifest));
    EXPECT_CALL(mock_system, get_running_app_desc()).WillRepeatedly(Return(&running_app));
    EXPECT_CALL(mock_ota_session, is_active()).WillRepeatedly(Return(true));
    EXPECT_CALL(mock_ota_session, begin(::testing::_)).WillRepeatedly(Return(ESP_OK));
    EXPECT_CALL(mock_ota_session, perform()).WillRepeatedly(Return(ESP_OK));
    EXPECT_CALL(mock_ota_session, finish()).WillRepeatedly(Return(ESP_OK));

    // Simulate verification failure
    EXPECT_CALL(mock_system, get_update_partition()).WillOnce(Return(nullptr));

    // 3. Execute
    ASSERT_TRUE(sut.init(config));
    ASSERT_TRUE(sut.start_ota());

    // 4. Wait for transition
    int timeout_ms = 500;
    int elapsed_ms = 0;
    while (sut.get_status() != OtaStatus::FAILED && elapsed_ms < timeout_ms) {
        vTaskDelay(pdMS_TO_TICKS(10));
        elapsed_ms += 10;
    }

    EXPECT_EQ(sut.get_status(), OtaStatus::FAILED);
}

// ====================================================================
// CANCEL OTA TEST
// ====================================================================

TEST_F(OtaManagerTaskTest, CancelOtaTransitionsToIdle)
{
    // 1. Prepare Data
    auto [manifest_json, manifest] = create_standard_manifest();

    esp_app_desc_t running_app = {};
    strncpy(running_app.version, "1.0.0", sizeof(running_app.version));

    // 2. Setup Mocks
    EXPECT_CALL(mock_http_client, fetch(config.manifest_url, _, _))
        .WillOnce(DoAll(SetArgReferee<1>(manifest_json), Return(ESP_OK)));

    EXPECT_CALL(mock_manifest_parser, parse(manifest_json)).WillOnce(Return(manifest));
    EXPECT_CALL(mock_system, get_running_app_desc()).WillRepeatedly(Return(&running_app));
    EXPECT_CALL(mock_ota_session, is_active()).WillRepeatedly(Return(true));
    EXPECT_CALL(mock_ota_session, begin(::testing::_)).WillRepeatedly(Return(ESP_OK));

    // Simulate hanging download
    EXPECT_CALL(mock_ota_session, perform()).WillRepeatedly(testing::Invoke([]() {
        vTaskDelay(pdMS_TO_TICKS(50));
        return ESP_ERR_HTTPS_OTA_IN_PROGRESS;
    }));

    // Expect abort to be called during cancellation
    EXPECT_CALL(mock_ota_session, abort()).Times(::testing::AtLeast(1));

    // 3. Execute
    ASSERT_TRUE(sut.init(config));
    ASSERT_TRUE(sut.start_ota());

    // Wait until it enters DOWNLOADING state
    int timeout_ms = 500;
    int elapsed_ms = 0;
    while (sut.get_status() != OtaStatus::DOWNLOADING && elapsed_ms < timeout_ms) {
        vTaskDelay(pdMS_TO_TICKS(10));
        elapsed_ms += 10;
    }
    ASSERT_EQ(sut.get_status(), OtaStatus::DOWNLOADING);

    // 4. Cancel
    sut.cancel_ota();

    // 5. Verify status returns to IDLE
    elapsed_ms = 0;
    while (sut.get_status() != OtaStatus::IDLE && elapsed_ms < timeout_ms) {
        vTaskDelay(pdMS_TO_TICKS(10));
        elapsed_ms += 10;
    }

    EXPECT_EQ(sut.get_status(), OtaStatus::IDLE);
}

TEST_F(OtaManagerTaskTest, CancelThenStartOtaSucceedsWithoutStaleCancelBit)
{
    // 1. Prepare Data
    auto [manifest_json, manifest] = create_standard_manifest();

    esp_app_desc_t running_app = {};
    strncpy(running_app.version, "1.0.0", sizeof(running_app.version));

    // 2. Setup Mocks
    EXPECT_CALL(mock_http_client, fetch(config.manifest_url, _, _))
        .WillRepeatedly(DoAll(SetArgReferee<1>(manifest_json), Return(ESP_OK)));

    EXPECT_CALL(mock_manifest_parser, parse(manifest_json)).WillRepeatedly(Return(manifest));
    EXPECT_CALL(mock_system, get_running_app_desc()).WillRepeatedly(Return(&running_app));
    EXPECT_CALL(mock_ota_session, is_active()).WillRepeatedly(Return(true));
    EXPECT_CALL(mock_ota_session, begin(::testing::_)).WillRepeatedly(Return(ESP_OK));

    EXPECT_CALL(mock_ota_session, perform()).WillRepeatedly(testing::Invoke([]() {
        vTaskDelay(pdMS_TO_TICKS(50));
        return ESP_ERR_HTTPS_OTA_IN_PROGRESS;
    }));

    EXPECT_CALL(mock_ota_session, abort()).Times(::testing::AtLeast(1));

    // 3. Start first OTA session
    ASSERT_TRUE(sut.init(config));
    ASSERT_TRUE(sut.start_ota());

    int timeout_ms = 500;
    int elapsed_ms = 0;
    while (sut.get_status() != OtaStatus::DOWNLOADING && elapsed_ms < timeout_ms) {
        vTaskDelay(pdMS_TO_TICKS(10));
        elapsed_ms += 10;
    }
    ASSERT_EQ(sut.get_status(), OtaStatus::DOWNLOADING);

    // 4. Cancel the session
    sut.cancel_ota();

    elapsed_ms = 0;
    while (sut.get_status() != OtaStatus::IDLE && elapsed_ms < timeout_ms) {
        vTaskDelay(pdMS_TO_TICKS(10));
        elapsed_ms += 10;
    }
    EXPECT_EQ(sut.get_status(), OtaStatus::IDLE);

    // 5. Start a second OTA session immediately without recreating the task
    ASSERT_TRUE(sut.start_ota());

    elapsed_ms = 0;
    while (sut.get_status() != OtaStatus::DOWNLOADING && elapsed_ms < timeout_ms) {
        vTaskDelay(pdMS_TO_TICKS(10));
        elapsed_ms += 10;
    }
    // Verify that the second session reaches DOWNLOADING and was not killed by stale cancel bits
    EXPECT_EQ(sut.get_status(), OtaStatus::DOWNLOADING);
}

