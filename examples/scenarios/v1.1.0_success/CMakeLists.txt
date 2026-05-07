# examples/scenarios/v1.1.0_success/CMakeLists.txt
cmake_minimum_required(VERSION 3.16)

# The project root is three levels up from examples/scenarios/v1.1.0_success
get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../" ABSOLUTE)

list(APPEND EXTRA_COMPONENT_DIRS
    "${PROJECT_ROOT}"
    "$ENV{IDF_PATH}/components"
)

set(COMPONENTS main ota_manager)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(test_ota_manager)
