#pragma once

#include <gmock/gmock.h>
#include "interfaces/i_ota_session.hpp"

class MockOtaSession : public IOtaSession
{
public:
    MOCK_METHOD(esp_err_t, begin, (const OtaDownloadRequest& request), (override));
    MOCK_METHOD(esp_err_t, get_img_desc, (esp_app_desc_t *new_app_info), (override));
    MOCK_METHOD(esp_err_t, perform, (), (override));
    MOCK_METHOD(esp_err_t, finish, (), (override));
    MOCK_METHOD(esp_err_t, abort, (), (override));
    MOCK_METHOD(bool, is_active, (), (const, override));
};

