idf_component_register(
    SRCS
        "main.cpp"
        "test_ota_manager.cpp"
        "test_ota_manager_task.cpp"
        "test_version_helper.cpp"
        "test_manifest_parser.cpp"
    INCLUDE_DIRS
        "."
        "../../mocks"
    REQUIRES
        gtest
        ota_manager
    WHOLE_ARCHIVE
)

if(IDF_TARGET STREQUAL "linux")
    target_compile_options(${COMPONENT_LIB} PUBLIC --coverage)
    target_link_options(${COMPONENT_LIB} PUBLIC --coverage)
endif()
