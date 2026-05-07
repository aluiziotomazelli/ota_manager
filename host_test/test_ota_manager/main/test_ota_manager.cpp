#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "ota_manager.hpp"

#include "mock_i_http_client.hpp"
#include "mock_i_manifest_parser.hpp"
#include "mock_i_ota_session.hpp"
#include "mock_i_rollback_manager.hpp"
#include "mock_i_system.hpp"
#include "mock_i_task_scheduler.hpp"

using ::testing::_;
using ::testing::DoAll;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SetArgPointee;
using ::testing::SetArgReferee;
using ::testing::SetArrayArgument;
#include "ota_manager.hpp"
#include "testable_ota_manager.hpp"

class OtaManagerTest : public ::testing::Test
{
protected:
    NiceMock<MockHttpClient> mock_http_client;
    NiceMock<MockManifestParser> mock_manifest_parser;
    NiceMock<MockOtaSession> mock_ota_session;
    NiceMock<MockRollbackManager> mock_rollback_manager;
    NiceMock<MockSystem> mock_system;
    NiceMock<MockTaskScheduler> mock_task_scheduler;

    OtaDependencies deps = {
        .http_client = mock_http_client,
        .manifest_parser = mock_manifest_parser,
        .ota_session = mock_ota_session,
        .system = mock_system,
        .task_scheduler = mock_task_scheduler,
        .rollback_manager = mock_rollback_manager};

    OtaConfig config = {
        .device_type = "test",
        .manifest_url = "http://localhost:8080/manifest.json",
        .task_stack_size = 4096,
        .task_priority = 5,
        .http_timeout_ms = 30000,
        .allow_same_version = true,
        .restart_on_success = true};

    OtaManagerTestable ota_manager = OtaManagerTestable(deps);

    SemaphoreHandle_t fake_mutex = reinterpret_cast<SemaphoreHandle_t>(0x1);
    SemaphoreHandle_t fake_shutdown_done = reinterpret_cast<SemaphoreHandle_t>(0x2);
    TaskHandle_t fake_task = reinterpret_cast<TaskHandle_t>(0x3);

    static constexpr uint32_t OTA_START_BIT = 0x01;
    static constexpr uint32_t OTA_STOP_BIT = 0x02;
    static constexpr uint32_t OTA_CANCEL_BIT = 0x04;

    void SetUp() override
    {
        ON_CALL(mock_task_scheduler, mutex_create()).WillByDefault(Return(fake_mutex));
        ON_CALL(mock_task_scheduler, semaphore_binary_create()).WillByDefault(Return(fake_shutdown_done));
        ON_CALL(mock_task_scheduler, create_task(_, _, _, _, _, _))
            .WillByDefault(DoAll(SetArgPointee<5>(fake_task), Return(pdPASS)));
        ON_CALL(mock_task_scheduler, task_notify_wait(_, _, _, _)).WillByDefault(Return(pdPASS));
        ON_CALL(mock_task_scheduler, notify_task(_, _, _)).WillByDefault(Return(pdPASS));
        ON_CALL(mock_task_scheduler, semaphore_give(_)).WillByDefault(Return(pdPASS));
        ON_CALL(mock_task_scheduler, semaphore_take(_, _)).WillByDefault(Return(pdPASS));
        ON_CALL(mock_task_scheduler, task_delay(_)).WillByDefault(Return());
        ON_CALL(mock_task_scheduler, delete_task(_)).WillByDefault(Return());
        ON_CALL(mock_task_scheduler, semaphore_delete(_)).WillByDefault(Return());
    }
};

// ====================================================================
// INIT TESTS
// ====================================================================

TEST_F(OtaManagerTest, InitCreateMutexAndSemaphoreSuccess)
{
    EXPECT_CALL(mock_task_scheduler, mutex_create()).WillOnce(Return(fake_mutex));
    EXPECT_CALL(mock_task_scheduler, semaphore_binary_create()).WillOnce(Return(fake_shutdown_done));
    EXPECT_TRUE(ota_manager.init(config));
    EXPECT_EQ(ota_manager.get_status(), OtaStatus::IDLE);
}

TEST_F(OtaManagerTest, InitCreateMutexFailReturnsFalse)
{
    ON_CALL(mock_task_scheduler, mutex_create).WillByDefault(Return(nullptr));
    EXPECT_FALSE(ota_manager.init(config));
}

TEST_F(OtaManagerTest, InitCreateShutdownDoneFailReturnsFalse)
{
    ON_CALL(mock_task_scheduler, semaphore_binary_create).WillByDefault(Return(nullptr));
    EXPECT_FALSE(ota_manager.init(config));
}

TEST_F(OtaManagerTest, InitIdempotent)
{
    EXPECT_TRUE(ota_manager.init(config));
    EXPECT_EQ(ota_manager.get_status(), OtaStatus::IDLE);

    EXPECT_CALL(mock_task_scheduler, mutex_create()).Times(0);
    EXPECT_CALL(mock_task_scheduler, semaphore_binary_create()).Times(0);
    EXPECT_TRUE(ota_manager.init(config));
}

TEST_F(OtaManagerTest, InitGoesToIdleState)
{
    EXPECT_TRUE(ota_manager.init(config));
    EXPECT_EQ(ota_manager.get_status(), OtaStatus::IDLE);
}

// ====================================================================
// DEINIT TESTS
// ====================================================================

TEST_F(OtaManagerTest, DeinitSuccess)
{
    EXPECT_TRUE(ota_manager.init(config));
    EXPECT_TRUE(ota_manager.deinit());
    EXPECT_EQ(ota_manager.get_status(), OtaStatus::IDLE);
}

TEST_F(OtaManagerTest, DeinitWithoutTaskCleansResourcesWithoutErrors)
{
    ASSERT_TRUE(ota_manager.init(config));

    EXPECT_CALL(mock_ota_session, abort()).Times(1);
    EXPECT_CALL(mock_task_scheduler, semaphore_delete(fake_shutdown_done)).Times(1);
    EXPECT_CALL(mock_task_scheduler, semaphore_delete(fake_mutex)).Times(1);

    EXPECT_TRUE(ota_manager.deinit());
    EXPECT_EQ(ota_manager.get_status(), OtaStatus::IDLE);
}

TEST_F(OtaManagerTest, DeinitWithActiveTaskNotifiesStopBitAndWaitsForShutdown)
{
    ASSERT_TRUE(ota_manager.init(config));
    ASSERT_TRUE(ota_manager.start_ota());

    EXPECT_CALL(mock_ota_session, abort()).Times(1);

    EXPECT_CALL(mock_task_scheduler, semaphore_take(fake_mutex, portMAX_DELAY)).Times(1).WillOnce(Return(pdPASS));

    EXPECT_CALL(mock_task_scheduler, semaphore_give(fake_mutex)).Times(1).WillOnce(Return(pdPASS));

    // Shutdown semaphore take
    EXPECT_CALL(mock_task_scheduler, semaphore_take(fake_shutdown_done, pdMS_TO_TICKS(1000))).WillOnce(Return(pdPASS));

    EXPECT_CALL(mock_task_scheduler, notify_task(fake_task, OTA_STOP_BIT, eSetBits)).Times(1);
    EXPECT_CALL(mock_task_scheduler, notify_task(fake_task, OTA_CANCEL_BIT, eSetBits)).Times(0);
    EXPECT_CALL(mock_task_scheduler, semaphore_delete(fake_shutdown_done)).Times(1);
    EXPECT_CALL(mock_task_scheduler, semaphore_delete(fake_mutex)).Times(1);

    EXPECT_TRUE(ota_manager.deinit());
    EXPECT_EQ(ota_manager.get_status(), OtaStatus::IDLE);
}

TEST_F(OtaManagerTest, DeinitWithShutdownTimeoutReturnsFalseAndPreservesResources)
{
    ASSERT_TRUE(ota_manager.init(config));
    ASSERT_TRUE(ota_manager.start_ota());

    EXPECT_CALL(mock_ota_session, abort()).Times(1);

    EXPECT_CALL(mock_task_scheduler, semaphore_take(fake_mutex, portMAX_DELAY)).Times(1).WillOnce(Return(pdPASS));

    EXPECT_CALL(mock_task_scheduler, semaphore_give(fake_mutex)).Times(1).WillOnce(Return(pdPASS));

    // Shutdown semaphore TIMES OUT
    EXPECT_CALL(mock_task_scheduler, semaphore_take(fake_shutdown_done, pdMS_TO_TICKS(1000))).WillOnce(Return(pdFAIL));

    EXPECT_CALL(mock_task_scheduler, notify_task(fake_task, OTA_STOP_BIT, eSetBits)).Times(1);
    EXPECT_CALL(mock_task_scheduler, notify_task(fake_task, OTA_CANCEL_BIT, eSetBits)).Times(0);

    // Timeout should preserve resources and report failure
    EXPECT_CALL(mock_task_scheduler, semaphore_delete(fake_shutdown_done)).Times(0);
    EXPECT_CALL(mock_task_scheduler, semaphore_delete(fake_mutex)).Times(0);

    EXPECT_FALSE(ota_manager.deinit());
}

TEST_F(OtaManagerTest, GetStatusStillWorksAfterShutdownTimeout)
{
    ASSERT_TRUE(ota_manager.init(config));
    ASSERT_TRUE(ota_manager.start_ota());

    EXPECT_CALL(mock_ota_session, abort()).Times(1);

    EXPECT_CALL(mock_task_scheduler, semaphore_take(fake_mutex, portMAX_DELAY)).Times(1).WillOnce(Return(pdPASS));
    EXPECT_CALL(mock_task_scheduler, semaphore_give(fake_mutex)).Times(1).WillOnce(Return(pdPASS));
    EXPECT_CALL(mock_task_scheduler, semaphore_take(fake_shutdown_done, pdMS_TO_TICKS(1000))).WillOnce(Return(pdFAIL));
    EXPECT_CALL(mock_task_scheduler, notify_task(fake_task, OTA_STOP_BIT, eSetBits)).Times(1);
    EXPECT_CALL(mock_task_scheduler, semaphore_delete(fake_shutdown_done)).Times(0);
    EXPECT_CALL(mock_task_scheduler, semaphore_delete(fake_mutex)).Times(0);

    EXPECT_FALSE(ota_manager.deinit());

    EXPECT_CALL(mock_task_scheduler, semaphore_take(fake_mutex, portMAX_DELAY)).WillOnce(Return(pdPASS));
    EXPECT_CALL(mock_task_scheduler, semaphore_give(fake_mutex)).Times(1);

    EXPECT_EQ(ota_manager.get_status(), OtaStatus::IDLE);
}

TEST_F(OtaManagerTest, DeinitDeletesMutexAndSemaphore)
{
    ASSERT_TRUE(ota_manager.init(config));

    EXPECT_CALL(mock_task_scheduler, semaphore_delete(fake_shutdown_done)).Times(1);
    EXPECT_CALL(mock_task_scheduler, semaphore_delete(fake_mutex)).Times(1);

    EXPECT_TRUE(ota_manager.deinit());
}

TEST_F(OtaManagerTest, DeinitCallsAbortOnOtaSession)
{
    ASSERT_TRUE(ota_manager.init(config));

    EXPECT_CALL(mock_ota_session, abort()).Times(1);

    EXPECT_TRUE(ota_manager.deinit());
}

TEST_F(OtaManagerTest, DeinitSetsStatusToIdle)
{
    ASSERT_TRUE(ota_manager.init(config));

    EXPECT_TRUE(ota_manager.deinit());

    EXPECT_EQ(ota_manager.get_status(), OtaStatus::IDLE);
}

TEST_F(OtaManagerTest, MultipleDeinitCallsAreSafe)
{
    ASSERT_TRUE(ota_manager.init(config));

    // First deinit
    EXPECT_CALL(mock_ota_session, abort()).Times(2); // Called twice
    EXPECT_CALL(mock_task_scheduler, semaphore_delete(fake_shutdown_done)).Times(1);
    EXPECT_CALL(mock_task_scheduler, semaphore_delete(fake_mutex)).Times(1);

    EXPECT_TRUE(ota_manager.deinit());

    // Second deinit should not crash or double-free
    // abort() is called again, but mutex/semaphore are already null
    EXPECT_TRUE(ota_manager.deinit());

    EXPECT_EQ(ota_manager.get_status(), OtaStatus::IDLE);
}

// ==================================================================================
// start_ota() tests
// ==================================================================================

TEST_F(OtaManagerTest, StartOtaWhenStatusIsNotIdleOrFailedReturnsFalse)
{
    OtaManagerTestable testable_manager(deps);
    ASSERT_TRUE(testable_manager.init(config));

    // Set status to something other than IDLE or FAILED
    testable_manager.set_status(OtaStatus::DOWNLOADING);

    EXPECT_FALSE(testable_manager.start_ota());

    testable_manager.set_status(OtaStatus::VERIFYING);
    EXPECT_FALSE(testable_manager.start_ota());

    testable_manager.set_status(OtaStatus::READY_TO_RESTART);
    EXPECT_FALSE(testable_manager.start_ota());
}

TEST_F(OtaManagerTest, StartOtaWhenStatusIsIdleCreatesTaskAndNotifiesStartBit)
{
    ASSERT_TRUE(ota_manager.init(config));
    ASSERT_EQ(ota_manager.get_status(), OtaStatus::IDLE);

    EXPECT_CALL(mock_task_scheduler, create_task(_, _, _, _, _, _))
        .WillOnce(DoAll(SetArgPointee<5>(fake_task), Return(pdPASS)));

    EXPECT_CALL(mock_task_scheduler, notify_task(fake_task, OTA_START_BIT, eSetBits)).Times(1);

    EXPECT_TRUE(ota_manager.start_ota());
}

TEST_F(OtaManagerTest, StartOtaWhenStatusIsFailedCreatesTaskAndNotifiesStartBit)
{
    OtaManagerTestable testable_manager(deps);
    ASSERT_TRUE(testable_manager.init(config));

    // Set status to FAILED
    testable_manager.set_status(OtaStatus::FAILED);

    EXPECT_CALL(mock_task_scheduler, create_task(_, _, _, _, _, _))
        .WillOnce(DoAll(SetArgPointee<5>(fake_task), Return(pdPASS)));

    EXPECT_CALL(mock_task_scheduler, notify_task(fake_task, OTA_START_BIT, eSetBits)).Times(1);

    EXPECT_TRUE(testable_manager.start_ota());
}

TEST_F(OtaManagerTest, StartOtaWhenTaskAlreadyExistsOnlyNotifiesStartBit)
{
    ASSERT_TRUE(ota_manager.init(config));

    // First start_ota creates the task
    EXPECT_CALL(mock_task_scheduler, create_task(_, _, _, _, _, _))
        .WillOnce(DoAll(SetArgPointee<5>(fake_task), Return(pdPASS)));

    EXPECT_CALL(mock_task_scheduler, notify_task(fake_task, OTA_START_BIT, eSetBits))
        .Times(2); // Called twice: once on creation, once on second start

    ASSERT_TRUE(ota_manager.start_ota());

    // Second start_ota should NOT create task again
    EXPECT_CALL(mock_task_scheduler, create_task(_, _, _, _, _, _)).Times(0); // Should not be called

    EXPECT_TRUE(ota_manager.start_ota());
}

TEST_F(OtaManagerTest, StartOtaWhenCreateTaskFailsReturnsFalse)
{
    ASSERT_TRUE(ota_manager.init(config));

    // create_task fails
    EXPECT_CALL(mock_task_scheduler, create_task(_, _, _, _, _, _)).WillOnce(Return(pdFAIL));

    // Should not notify since task creation failed
    EXPECT_CALL(mock_task_scheduler, notify_task(_, _, _)).Times(0);

    EXPECT_FALSE(ota_manager.start_ota());
}

// ==================================================================================
// cancel_ota() tests
// ==================================================================================

TEST_F(OtaManagerTest, CancelOtaCallsAbortOnOtaSession)
{
    ASSERT_TRUE(ota_manager.init(config));

    EXPECT_CALL(mock_ota_session, abort()).Times(1);

    ota_manager.cancel_ota();
}

TEST_F(OtaManagerTest, CancelOtaNotifiesCancelBitIfTaskIsActive)
{
    ASSERT_TRUE(ota_manager.init(config));
    ASSERT_TRUE(ota_manager.start_ota()); // Creates the task

    EXPECT_CALL(mock_ota_session, abort()).Times(1);
    EXPECT_CALL(mock_task_scheduler, notify_task(fake_task, OTA_STOP_BIT, eSetBits)).Times(0);
    EXPECT_CALL(mock_task_scheduler, notify_task(fake_task, OTA_CANCEL_BIT, eSetBits)).Times(1);

    ota_manager.cancel_ota();
}

TEST_F(OtaManagerTest, CancelOtaWithoutActiveTaskOnlyCallsAbort)
{
    ASSERT_TRUE(ota_manager.init(config));
    // Don't call start_ota(), so task is not active

    EXPECT_CALL(mock_ota_session, abort()).Times(1);
    EXPECT_CALL(mock_task_scheduler, notify_task(_, _, _)).Times(0); // Should NOT notify since task doesn't exist

    ota_manager.cancel_ota();
}

// ==================================================================================
// Delegation tests (Wrapper Methods)
// ==================================================================================

TEST_F(OtaManagerTest, CheckPendingVerifyReturnsTrueFromRollbackManager)
{
    EXPECT_CALL(mock_rollback_manager, is_pending_verify()).WillOnce(Return(true));

    EXPECT_TRUE(ota_manager.check_pending_verify());
}

TEST_F(OtaManagerTest, CheckPendingVerifyReturnsFalseFromRollbackManager)
{
    EXPECT_CALL(mock_rollback_manager, is_pending_verify()).WillOnce(Return(false));

    EXPECT_FALSE(ota_manager.check_pending_verify());
}

TEST_F(OtaManagerTest, ConfirmAppValidReturnsTrueWhenMarkAppValidReturnsEspOk)
{
    EXPECT_CALL(mock_rollback_manager, mark_app_valid()).WillOnce(Return(ESP_OK));

    EXPECT_TRUE(ota_manager.confirm_app_valid());
}

TEST_F(OtaManagerTest, ConfirmAppValidReturnsFalseWhenMarkAppValidFails)
{
    EXPECT_CALL(mock_rollback_manager, mark_app_valid()).WillOnce(Return(ESP_FAIL));

    EXPECT_FALSE(ota_manager.confirm_app_valid());
}

TEST_F(OtaManagerTest, RollbackAndRebootCallsRollbackManager)
{
    EXPECT_CALL(mock_rollback_manager, rollback_and_reboot()).Times(1);

    ota_manager.rollback_and_reboot();
}

// ==================================================================================
// Thread Safety tests (get_status)
// ==================================================================================

TEST_F(OtaManagerTest, GetStatusReturnsStatusWithoutLockingIfMutexIsNull)
{
    // Don't call init(), so state_mutex_ remains nullptr
    OtaManagerTestable testable_manager(deps);
    testable_manager.set_status(OtaStatus::DOWNLOADING);

    // Should not attempt to take mutex
    EXPECT_CALL(mock_task_scheduler, semaphore_take(_, _)).Times(0);

    EXPECT_EQ(testable_manager.get_status(), OtaStatus::DOWNLOADING);
}

TEST_F(OtaManagerTest, GetStatusReturnsStatusWithoutLockingIfSemaphoreTakeFails)
{
    ASSERT_TRUE(ota_manager.init(config));

    // Make semaphore_take fail
    EXPECT_CALL(mock_task_scheduler, semaphore_take(fake_mutex, portMAX_DELAY)).WillOnce(Return(pdFAIL));

    // Should still return status (the unprotected read)
    EXPECT_EQ(ota_manager.get_status(), OtaStatus::IDLE);

    // semaphore_give should NOT be called since take failed
    EXPECT_CALL(mock_task_scheduler, semaphore_give(_)).Times(0);
}

TEST_F(OtaManagerTest, GetStatusReturnsStatusProtectedByMutexWhenAvailable)
{
    OtaManagerTestable testable_manager(deps);
    ASSERT_TRUE(testable_manager.init(config));

    testable_manager.set_status(OtaStatus::VERIFYING);

    // Should take and give mutex
    EXPECT_CALL(mock_task_scheduler, semaphore_take(fake_mutex, portMAX_DELAY)).WillOnce(Return(pdPASS));

    EXPECT_CALL(mock_task_scheduler, semaphore_give(fake_mutex)).Times(1);

    EXPECT_EQ(testable_manager.get_status(), OtaStatus::VERIFYING);
}

// ==================================================================================
// handle_manifest_state() tests
// ==================================================================================

TEST_F(OtaManagerTest, HandleManifestStateFetchFailsReturnsFailed)
{
    OtaManagerTestable testable_manager(deps);
    ASSERT_TRUE(testable_manager.init(config));

    std::string dummy_content;
    EXPECT_CALL(mock_http_client, fetch(config.manifest_url, ::testing::_)).WillOnce(Return(ESP_FAIL));

    EXPECT_EQ(testable_manager.handle_manifest_state(), OtaStepResult::FAILED);
}

TEST_F(OtaManagerTest, HandleManifestStateParseFailsReturnsFailed)
{
    OtaManagerTestable testable_manager(deps);
    ASSERT_TRUE(testable_manager.init(config));

    std::string manifest_content = R"({"invalid": "json"})";
    EXPECT_CALL(mock_http_client, fetch(config.manifest_url, ::testing::_))
        .WillOnce(DoAll(SetArgReferee<1>(manifest_content), Return(ESP_OK)));

    EXPECT_CALL(mock_manifest_parser, parse(manifest_content)).WillOnce(Return(std::nullopt));

    EXPECT_EQ(testable_manager.handle_manifest_state(), OtaStepResult::FAILED);
}

TEST_F(OtaManagerTest, HandleManifestStateParseSuccessPopulatesManifestAndReturnsSuccess)
{
    OtaManagerTestable testable_manager(deps);
    ASSERT_TRUE(testable_manager.init(config));

    std::string manifest_content = R"({
        "version": "1.2.3",
        "device_type": "test",
        "firmware_url": "http://example.com/fw.bin",
        "sha256": "abcd1234"
    })";

    OtaManifest expected_manifest;
    expected_manifest.version = {1, 2, 3};
    expected_manifest.device_type = "test";
    expected_manifest.firmware_url = "http://example.com/fw.bin";
    expected_manifest.sha256_hex = "abcd1234";

    EXPECT_CALL(mock_http_client, fetch(config.manifest_url, ::testing::_))
        .WillOnce(DoAll(SetArgReferee<1>(manifest_content), Return(ESP_OK)));

    EXPECT_CALL(mock_manifest_parser, parse(manifest_content)).WillOnce(Return(expected_manifest));

    EXPECT_EQ(testable_manager.handle_manifest_state(), OtaStepResult::SUCCESS);

    // Verify manifest was populated
    EXPECT_EQ(testable_manager.get_manifest_ref().version.major, 1);
    EXPECT_EQ(testable_manager.get_manifest_ref().version.minor, 2);
    EXPECT_EQ(testable_manager.get_manifest_ref().version.patch, 3);
    EXPECT_EQ(testable_manager.get_manifest_ref().device_type, "test");
}

// ==================================================================================
// handle_version_state() tests
// ==================================================================================

TEST_F(OtaManagerTest, HandleVersionStateDeviceTypeMismatchReturnsFailed)
{
    OtaManagerTestable testable_manager(deps);
    ASSERT_TRUE(testable_manager.init(config));

    // Setup manifest with different device type
    OtaManifest& manifest = testable_manager.get_manifest_ref();
    manifest.device_type = "different_device";
    manifest.version = {2, 0, 0};

    EXPECT_EQ(testable_manager.handle_version_state(), OtaStepResult::FAILED);
}

TEST_F(OtaManagerTest, HandleVersionStateFailsToParseCurrentVersionReturnsFailed)
{
    OtaManagerTestable testable_manager(deps);
    ASSERT_TRUE(testable_manager.init(config));

    // Setup manifest with correct device type
    OtaManifest& manifest = testable_manager.get_manifest_ref();
    manifest.device_type = config.device_type;
    manifest.version = {2, 0, 0};

    // Mock app descriptor with invalid version string
    esp_app_desc_t app_desc = {};
    strncpy(app_desc.version, "invalid_version", sizeof(app_desc.version));

    EXPECT_CALL(mock_system, get_running_app_desc()).WillOnce(Return(&app_desc));

    EXPECT_EQ(testable_manager.handle_version_state(), OtaStepResult::FAILED);
}

TEST_F(OtaManagerTest, HandleVersionStateManifestVersionIsLowerReturnsFailed)
{
    OtaManagerTestable testable_manager(deps);
    ASSERT_TRUE(testable_manager.init(config));

    OtaManifest& manifest = testable_manager.get_manifest_ref();
    manifest.device_type = config.device_type;
    manifest.version = {1, 0, 0}; // Lower than current

    esp_app_desc_t app_desc = {};
    strncpy(app_desc.version, "2.0.0", sizeof(app_desc.version));

    EXPECT_CALL(mock_system, get_running_app_desc()).WillOnce(Return(&app_desc));

    EXPECT_EQ(testable_manager.handle_version_state(), OtaStepResult::FAILED);
}

TEST_F(OtaManagerTest, HandleVersionStateSameVersionWithAllowSameFalseReturnsFailed)
{
    OtaManagerTestable testable_manager(deps);

    OtaConfig test_config = config;
    test_config.allow_same_version = false;
    ASSERT_TRUE(testable_manager.init(test_config));

    OtaManifest& manifest = testable_manager.get_manifest_ref();
    manifest.device_type = test_config.device_type;
    manifest.version = {1, 2, 3};

    esp_app_desc_t app_desc = {};
    strncpy(app_desc.version, "1.2.3", sizeof(app_desc.version));

    EXPECT_CALL(mock_system, get_running_app_desc()).WillOnce(Return(&app_desc));

    EXPECT_EQ(testable_manager.handle_version_state(), OtaStepResult::FAILED);
}

TEST_F(OtaManagerTest, HandleVersionStateSameVersionWithAllowSameTrueReturnsSuccess)
{
    OtaManagerTestable testable_manager(deps);

    OtaConfig test_config = config;
    test_config.allow_same_version = true;
    ASSERT_TRUE(testable_manager.init(test_config));

    OtaManifest& manifest = testable_manager.get_manifest_ref();
    manifest.device_type = test_config.device_type;
    manifest.version = {1, 2, 3};

    esp_app_desc_t app_desc = {};
    strncpy(app_desc.version, "1.2.3", sizeof(app_desc.version));

    EXPECT_CALL(mock_system, get_running_app_desc()).WillOnce(Return(&app_desc));

    EXPECT_EQ(testable_manager.handle_version_state(), OtaStepResult::SUCCESS);
}

TEST_F(OtaManagerTest, HandleVersionStateManifestVersionIsHigherReturnsSuccess)
{
    OtaManagerTestable testable_manager(deps);
    ASSERT_TRUE(testable_manager.init(config));

    OtaManifest& manifest = testable_manager.get_manifest_ref();
    manifest.device_type = config.device_type;
    manifest.version = {2, 0, 0}; // Higher than current

    esp_app_desc_t app_desc = {};
    strncpy(app_desc.version, "1.5.0", sizeof(app_desc.version));

    EXPECT_CALL(mock_system, get_running_app_desc()).WillOnce(Return(&app_desc));

    EXPECT_EQ(testable_manager.handle_version_state(), OtaStepResult::SUCCESS);
}

// ==================================================================================
// handle_verification_state() tests
// ==================================================================================

TEST_F(OtaManagerTest, HandleVerificationStateGetUpdatePartitionReturnsNullptr)
{
    OtaManagerTestable testable_manager(deps);
    ASSERT_TRUE(testable_manager.init(config));

    // Setup manifest
    OtaManifest& manifest = testable_manager.get_manifest_ref();
    manifest.device_type = config.device_type;
    manifest.version = {1, 2, 3};
    manifest.sha256_hex = "abcd1234567890";

    EXPECT_CALL(mock_system, get_update_partition()).WillOnce(Return(nullptr));

    EXPECT_EQ(testable_manager.handle_verification_state(), OtaStepResult::FAILED);
}

TEST_F(OtaManagerTest, HandleVerificationStateGetPartitionSha256Fails)
{
    OtaManagerTestable testable_manager(deps);
    ASSERT_TRUE(testable_manager.init(config));

    // Setup manifest
    OtaManifest& manifest = testable_manager.get_manifest_ref();
    manifest.device_type = config.device_type;
    manifest.version = {1, 2, 3};
    manifest.sha256_hex = "abcd1234567890";

    // Mock partition (not nullptr)
    const esp_partition_t* mock_partition = reinterpret_cast<const esp_partition_t*>(0x1);
    EXPECT_CALL(mock_system, get_update_partition()).WillOnce(Return(mock_partition));
    EXPECT_CALL(mock_system, get_partition_sha256(mock_partition, ::testing::_, ::testing::_)).WillOnce(Return(ESP_FAIL));

    EXPECT_EQ(testable_manager.handle_verification_state(), OtaStepResult::FAILED);
}

TEST_F(OtaManagerTest, HandleVerificationStateSha256MismatchWithManifest)
{
    OtaManagerTestable testable_manager(deps);
    ASSERT_TRUE(testable_manager.init(config));

    // Setup manifest
    OtaManifest& manifest = testable_manager.get_manifest_ref();
    manifest.device_type = config.device_type;
    manifest.version = {1, 2, 3};
    manifest.sha256_hex = "manifest_hash_1234567890";

    // Mock partition (not nullptr)
    const esp_partition_t* mock_partition = reinterpret_cast<const esp_partition_t*>(0x1);
    EXPECT_CALL(mock_system, get_update_partition()).WillOnce(Return(mock_partition));

    // Mock SHA256 calculation that doesn't match manifest
    uint8_t expected_sha256[32] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b,
                                   0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16,
                                   0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20};
    EXPECT_CALL(mock_system, get_partition_sha256(mock_partition, ::testing::_, ::testing::_))
        .WillOnce(DoAll(SetArrayArgument<2>(expected_sha256, expected_sha256 + 32), Return(ESP_OK)));

    EXPECT_EQ(testable_manager.handle_verification_state(), OtaStepResult::FAILED);
}

TEST_F(OtaManagerTest, HandleVerificationStateSha256MatchesManifestSuccess)
{
    OtaManagerTestable testable_manager(deps);
    ASSERT_TRUE(testable_manager.init(config));

    // Setup manifest with correct SHA256
    OtaManifest& manifest = testable_manager.get_manifest_ref();
    manifest.device_type = config.device_type;
    manifest.version = {1, 2, 3};
    manifest.sha256_hex = "0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20";

    // Mock partition (not nullptr)
    const esp_partition_t* mock_partition = reinterpret_cast<const esp_partition_t*>(0x1);
    EXPECT_CALL(mock_system, get_update_partition()).WillOnce(Return(mock_partition));

    // Mock SHA256 calculation that matches manifest
    uint8_t expected_sha256[32] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b,
                                   0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16,
                                   0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20};
    EXPECT_CALL(mock_system, get_partition_sha256(mock_partition, ::testing::_, ::testing::_))
        .WillOnce(DoAll(SetArrayArgument<2>(expected_sha256, expected_sha256 + 32), Return(ESP_OK)));

    EXPECT_EQ(testable_manager.handle_verification_state(), OtaStepResult::SUCCESS);
}

// ==================================================================================
// handle_download_state() tests
// ==================================================================================

TEST_F(OtaManagerTest, HandleDownloadStateSessionNotActiveBeginFails)
{
    OtaManagerTestable testable_manager(deps);
    ASSERT_TRUE(testable_manager.init(config));

    // Setup manifest
    OtaManifest& manifest = testable_manager.get_manifest_ref();
    manifest.device_type = config.device_type;
    manifest.version = {2, 0, 0};
    manifest.firmware_url = "http://example.com/firmware.bin";
    manifest.sha256_hex = "test_hash";

    // Mock session is not active
    EXPECT_CALL(mock_ota_session, is_active()).WillOnce(Return(false));
    
    // Mock begin() failure
    EXPECT_CALL(mock_ota_session, begin(::testing::_)).WillOnce(Return(ESP_FAIL));

    EXPECT_EQ(testable_manager.handle_download_state(), OtaStepResult::FAILED);
}

TEST_F(OtaManagerTest, HandleDownloadStateSessionNotActiveGetImgDescFails)
{
    OtaManagerTestable testable_manager(deps);
    ASSERT_TRUE(testable_manager.init(config));

    // Setup manifest
    OtaManifest& manifest = testable_manager.get_manifest_ref();
    manifest.device_type = config.device_type;
    manifest.version = {2, 0, 0};
    manifest.firmware_url = "http://example.com/firmware.bin";
    manifest.sha256_hex = "test_hash";

    // Mock session is not active
    EXPECT_CALL(mock_ota_session, is_active()).WillOnce(Return(false));
    
    // Mock begin() success
    EXPECT_CALL(mock_ota_session, begin(::testing::_)).WillOnce(Return(ESP_OK));
    
    // Mock get_img_desc() failure
    EXPECT_CALL(mock_ota_session, get_img_desc(::testing::_)).WillOnce(Return(ESP_FAIL));
    
    // Mock abort() should be called
    EXPECT_CALL(mock_ota_session, abort()).Times(1);

    EXPECT_EQ(testable_manager.handle_download_state(), OtaStepResult::FAILED);
}

TEST_F(OtaManagerTest, HandleDownloadStateSessionNotActiveImageVersionInvalid)
{
    OtaManagerTestable testable_manager(deps);
    ASSERT_TRUE(testable_manager.init(config));

    // Setup manifest
    OtaManifest& manifest = testable_manager.get_manifest_ref();
    manifest.device_type = config.device_type;
    manifest.version = {2, 0, 0};
    manifest.firmware_url = "http://example.com/firmware.bin";
    manifest.sha256_hex = "test_hash";

    // Mock session is not active
    EXPECT_CALL(mock_ota_session, is_active()).WillOnce(Return(false));
    
    // Mock begin() success
    EXPECT_CALL(mock_ota_session, begin(::testing::_)).WillOnce(Return(ESP_OK));
    
    // Mock get_img_desc() success
    esp_app_desc_t new_app_info = {};
    strncpy(new_app_info.version, "1.0.0", sizeof(new_app_info.version)); // Older version
    EXPECT_CALL(mock_ota_session, get_img_desc(::testing::_))
        .WillOnce(DoAll(SetArgPointee<0>(new_app_info), Return(ESP_OK)));
    
    // Mock running app with newer version
    esp_app_desc_t running_app = {};
    strncpy(running_app.version, "2.0.0", sizeof(running_app.version));
    EXPECT_CALL(mock_system, get_running_app_desc()).WillOnce(Return(&running_app));
    
    // Mock abort() should be called
    EXPECT_CALL(mock_ota_session, abort()).Times(1);

    EXPECT_EQ(testable_manager.handle_download_state(), OtaStepResult::FAILED);
}

TEST_F(OtaManagerTest, HandleDownloadStateSessionActivePerformReturnsInProgress)
{
    OtaManagerTestable testable_manager(deps);
    ASSERT_TRUE(testable_manager.init(config));

    // Setup manifest
    OtaManifest& manifest = testable_manager.get_manifest_ref();
    manifest.device_type = config.device_type;
    manifest.version = {2, 0, 0};
    manifest.firmware_url = "http://example.com/firmware.bin";
    manifest.sha256_hex = "test_hash";

    // Mock session is active
    EXPECT_CALL(mock_ota_session, is_active()).WillOnce(Return(true));
    
    // Mock perform() returns IN_PROGRESS
    EXPECT_CALL(mock_ota_session, perform()).WillOnce(Return(ESP_ERR_HTTPS_OTA_IN_PROGRESS));

    EXPECT_EQ(testable_manager.handle_download_state(), OtaStepResult::IN_PROGRESS);
}

TEST_F(OtaManagerTest, HandleDownloadStateSessionActivePerformFails)
{
    OtaManagerTestable testable_manager(deps);
    ASSERT_TRUE(testable_manager.init(config));

    // Setup manifest
    OtaManifest& manifest = testable_manager.get_manifest_ref();
    manifest.device_type = config.device_type;
    manifest.version = {2, 0, 0};
    manifest.firmware_url = "http://example.com/firmware.bin";
    manifest.sha256_hex = "test_hash";

    // Mock session is active
    EXPECT_CALL(mock_ota_session, is_active()).WillOnce(Return(true));
    
    // Mock perform() fails
    EXPECT_CALL(mock_ota_session, perform()).WillOnce(Return(ESP_ERR_INVALID_ARG));
    
    // Mock abort() should be called
    EXPECT_CALL(mock_ota_session, abort()).Times(1);

    EXPECT_EQ(testable_manager.handle_download_state(), OtaStepResult::FAILED);
}

TEST_F(OtaManagerTest, HandleDownloadStateSessionActiveFinishFails)
{
    OtaManagerTestable testable_manager(deps);
    ASSERT_TRUE(testable_manager.init(config));

    // Setup manifest
    OtaManifest& manifest = testable_manager.get_manifest_ref();
    manifest.device_type = config.device_type;
    manifest.version = {2, 0, 0};
    manifest.firmware_url = "http://example.com/firmware.bin";
    manifest.sha256_hex = "test_hash";

    // Mock session is active
    EXPECT_CALL(mock_ota_session, is_active()).WillOnce(Return(true));
    
    // Mock perform() success
    EXPECT_CALL(mock_ota_session, perform()).WillOnce(Return(ESP_OK));
    
    // Mock finish() fails
    EXPECT_CALL(mock_ota_session, finish()).WillOnce(Return(ESP_FAIL));

    EXPECT_EQ(testable_manager.handle_download_state(), OtaStepResult::FAILED);
}

TEST_F(OtaManagerTest, HandleDownloadStateDownloadCompleteSuccess)
{
    OtaManagerTestable testable_manager(deps);
    ASSERT_TRUE(testable_manager.init(config));

    // Setup manifest
    OtaManifest& manifest = testable_manager.get_manifest_ref();
    manifest.device_type = config.device_type;
    manifest.version = {2, 0, 0};
    manifest.firmware_url = "http://example.com/firmware.bin";
    manifest.sha256_hex = "test_hash";

    // Mock session is active
    EXPECT_CALL(mock_ota_session, is_active()).WillOnce(Return(true));
    
    // Mock perform() success
    EXPECT_CALL(mock_ota_session, perform()).WillOnce(Return(ESP_OK));
    
    // Mock finish() success
    EXPECT_CALL(mock_ota_session, finish()).WillOnce(Return(ESP_OK));

    EXPECT_EQ(testable_manager.handle_download_state(), OtaStepResult::SUCCESS);
}
