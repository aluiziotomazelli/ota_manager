#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "ota_manager.hpp"

#include "mock_i_http_client.hpp"
#include "mock_i_manifest_parser.hpp"
#include "mock_i_ota_session.hpp"
#include "mock_i_rollback_manager.hpp"
#include "mock_i_system.hpp"
#include "mock_i_task_scheduler.hpp"

using ::testing::NiceMock;
using ::testing::Return;

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

    OtaManager ota_manager = OtaManager(deps, config);

    SemaphoreHandle_t fake_mutex = reinterpret_cast<SemaphoreHandle_t>(0x1);
    SemaphoreHandle_t fake_shutdown_done = reinterpret_cast<SemaphoreHandle_t>(0x2);
    // TaskHandle_t fake_ota_task = reinterpret_cast<TaskHandle_t>(0x3);

    void SetUp() override
    {
        ON_CALL(mock_task_scheduler, mutex_create).WillByDefault(Return(fake_mutex));
        ON_CALL(mock_task_scheduler, semaphore_binary_create).WillByDefault(Return(fake_shutdown_done));
        // ON_CALL(mock_task_scheduler, create_task).WillByDefault(Return(pdPASS));
    }
};

TEST_F(OtaManagerTest, InitCreateMutexAndSemaphoreSuccess)
{
    EXPECT_CALL(mock_task_scheduler, mutex_create).WillOnce(Return(fake_mutex));
    EXPECT_CALL(mock_task_scheduler, semaphore_binary_create).WillOnce(Return(fake_shutdown_done));
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
