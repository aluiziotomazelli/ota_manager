#include <gtest/gtest.h>
#include "version_helper.hpp"

TEST(VersionHelperTest, ParseValidVersion) {
    auto v = VersionHelper::parse("1.2.3");
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(v->major, 1);
    EXPECT_EQ(v->minor, 2);
    EXPECT_EQ(v->patch, 3);
}

TEST(VersionHelperTest, ParseInvalidVersion) {
    EXPECT_FALSE(VersionHelper::parse("1.2").has_value());
    EXPECT_FALSE(VersionHelper::parse("1.2.3.4").has_value());
    EXPECT_FALSE(VersionHelper::parse("a.b.c").has_value());
    EXPECT_FALSE(VersionHelper::parse("").has_value());
}

TEST(VersionHelperTest, ParseZeroVersion) {
    auto v = VersionHelper::parse("0.0.0");
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(v->major, 0);
    EXPECT_EQ(v->minor, 0);
    EXPECT_EQ(v->patch, 0);
}
