#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "ota_manager.hpp"

class OtaManagerTest : public ::testing::Test
{
protected:
};

TEST_F(OtaManagerTest, SmokingTest)
{
    EXPECT_TRUE(true);
}