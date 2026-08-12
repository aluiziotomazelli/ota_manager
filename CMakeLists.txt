set(component_requires "")
if(NOT IDF_TARGET STREQUAL "linux")
    list(APPEND component_requires 
        esp_https_ota
        app_update
        mbedtls
    )
endif()

set(srcs
    "src/ota_manager.cpp"
    "src/manifest_parser.cpp"
    "src/http_client.cpp"
    "src/ota_session.cpp"
    "src/rollback_manager.cpp"
    "src/version_helper.cpp"
)

if(NOT IDF_TARGET STREQUAL "linux")
    list(APPEND srcs "src/system.cpp")
endif()

idf_component_register(
    SRCS
        ${srcs}
    INCLUDE_DIRS
        "include"

    REQUIRES
        ${component_requires}

    PRIV_REQUIRES
        json
)

if(IDF_TARGET STREQUAL "linux")
    target_include_directories(${COMPONENT_LIB} PRIVATE "host_test/mocks")
    target_compile_options(${COMPONENT_LIB} PUBLIC --coverage)
    target_link_options(${COMPONENT_LIB} PUBLIC --coverage)
endif()