# host_test/test_ota_manager/CMakeLists.txt
cmake_minimum_required(VERSION 3.16)

# Common CMake configuration for all tests in the host_test directory.
include("../extra_dirs_common.cmake")


# Define the project name for this test application.
project(test_ota_manager)


# Set the patterns to extract from the binary
set(EXTRACT_PATTERNS_test_ota_manager "*/ota_manager/src/*" "*/ota_manager/include/*")

# Set the patterns to remove from the binary
set(REMOVE_PATTERNS_test_ota_manager "*/ota_manager/include/*")

# Include common coverage logic
include("../coverage_common.cmake")
setup_gtest_coverage(test_ota_manager test_ota_manager.elf)
