#include "manifest_parser.hpp"
#include <gtest/gtest.h>

class ManifestParserTest : public ::testing::Test
{
protected:
    ManifestParser parser;
};

TEST_F(ManifestParserTest, ParseValidJson)
{
    std::string json = R"({
        "device_type": "water_tank",
        "version": "1.2.3",
        "firmware_url": "http://example.com/fw.bin",
        "firmware_size": 1024,
        "sha256_hex": "deadbeef"
    })";

    auto manifest = parser.parse(json);
    ASSERT_TRUE(manifest.has_value());
    EXPECT_EQ(manifest->device_type, "water_tank");
    EXPECT_EQ(manifest->version.major, 1);
    EXPECT_EQ(manifest->version.minor, 2);
    EXPECT_EQ(manifest->version.patch, 3);
    EXPECT_EQ(manifest->firmware_url, "http://example.com/fw.bin");
    EXPECT_EQ(manifest->firmware_size, 1024);
    EXPECT_EQ(manifest->sha256_hex, "deadbeef");
}

TEST_F(ManifestParserTest, ParseInvalidJson)
{
    EXPECT_FALSE(parser.parse("invalid json").has_value());
    EXPECT_FALSE(parser.parse(R"({"device_type": "water_tank"})").has_value());
}
