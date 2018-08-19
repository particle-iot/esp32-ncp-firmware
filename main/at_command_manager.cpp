/*
 * Copyright (c) 2018 Particle Industries, Inc.  All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation, either
 * version 3 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "at_command_manager.h"
#include <cstring>
#include <cstdio>
#include "update_manager.h"
#include "freertos/FreeRTOS.h"

/* :( */
extern "C" {
#include "esp_at.h"
}

extern const char* FIRMWARE_VERSION;

namespace particle { namespace ncp {

int AtCommandManager::init() {
    static esp_at_cmd_struct cgmr = {
        (char*)"+CGMR",
        nullptr,
        nullptr,
        nullptr,
        [](uint8_t*) -> uint8_t {
            esp_at_port_write_data((uint8_t*)FIRMWARE_VERSION, strlen(FIRMWARE_VERSION));
            return ESP_AT_RESULT_CODE_OK;
        }
    };

    CHECK_TRUE(esp_at_custom_cmd_array_regist(&cgmr, 1), RESULT_ERROR);

    static esp_at_cmd_struct cmux = {
        (char*)"+CMUX",
        [](uint8_t*) -> uint8_t {
            return ESP_AT_RESULT_CODE_ERROR;
        },
        [](uint8_t*) -> uint8_t {
            return ESP_AT_RESULT_CODE_ERROR;
        },
        [](uint8_t) -> uint8_t {
            return ESP_AT_RESULT_CODE_ERROR;
        },
        [](uint8_t*) -> uint8_t {
            return ESP_AT_RESULT_CODE_ERROR;
        }
    };

    CHECK_TRUE(esp_at_custom_cmd_array_regist(&cmux, 1), RESULT_ERROR);

    static esp_at_cmd_struct fwupd = {
        (char*)"+FWUPD",
        nullptr,
        nullptr,
        [](uint8_t) -> uint8_t {
            int32_t sizeVal;
            if (esp_at_get_para_as_digit(0, &sizeVal) == ESP_AT_PARA_PARSE_RESULT_OK && sizeVal > 0) {
                auto transport = AtTransportBase::instance();
                if (transport) {
                    esp_at_response_result(ESP_AT_RESULT_CODE_OK);
                    esp_at_port_wait_write_complete(portMAX_DELAY);

                    transport->setDirectMode(true);
                    int r = UpdateManager::instance()->update(sizeVal);
                    transport->setDirectMode(false);
                    if (r) {
                        LOG(ERROR, "Update failed: %d", r);
                    }
                }
            }
            return ESP_AT_RESULT_CODE_ERROR;
        },
        nullptr
    };

    CHECK_TRUE(esp_at_custom_cmd_array_regist(&fwupd, 1), RESULT_ERROR);

    return 0;
}

} } /* particle::ncp */
