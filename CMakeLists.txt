idf_component_register(
    SRCS
        "src/ota_manager.cpp"
        "src/manifest_parser.cpp"
        "src/http_client.cpp"
        "src/ota_session.cpp"
        "src/rollback_manager.cpp"
    INCLUDE_DIRS
        "include"
    
    REQUIRES
        esp_https_ota
        app_update
        
    PRIV_REQUIRES
        json
)

if(IDF_TARGET STREQUAL "linux")
    target_compile_options(${COMPONENT_LIB} PUBLIC --coverage)
    target_link_options(${COMPONENT_LIB} PUBLIC --coverage)
endif()