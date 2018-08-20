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

namespace {

enum AtGpioMode {
    AT_GPIO_MODE_DISABLED  = 0,
    AT_GPIO_MODE_INPUT     = 1,
    AT_GPIO_MODE_OUTPUT    = 2,
    AT_GPIO_MODE_OUTPUT_OD = 3
};

enum AtGpioPull {
    AT_GPIO_PULL_NONE = 0,
    AT_GPIO_PULL_DOWN = 1,
    AT_GPIO_PULL_UP   = 2
};

int gpioMapAtPullToEspPull(AtGpioPull pull, gpio_pullup_t& espPullUp, gpio_pulldown_t& espPullDown) {
    switch (pull) {
        case AT_GPIO_PULL_NONE: {
            espPullUp = GPIO_PULLUP_DISABLE;
            espPullDown = GPIO_PULLDOWN_DISABLE;
            break;
        }
        case AT_GPIO_PULL_DOWN: {
            espPullUp = GPIO_PULLUP_DISABLE;
            espPullDown = GPIO_PULLDOWN_ENABLE;
            break;
        }
        case AT_GPIO_PULL_UP: {
            espPullUp = GPIO_PULLUP_ENABLE;
            espPullDown = GPIO_PULLDOWN_DISABLE;
            break;
        }
        default: {
            return -1;
        }
    }
    return 0;
}

int gpioMapEspPullToAtPull(gpio_pullup_t espPullUp, gpio_pulldown_t espPullDown, AtGpioPull& pull) {
    if (espPullUp == GPIO_PULLUP_ENABLE && espPullDown == GPIO_PULLDOWN_ENABLE) {
        return -1;
    }

    if (espPullUp == GPIO_PULLUP_ENABLE) {
        pull = AT_GPIO_PULL_UP;
    } else if (espPullDown == GPIO_PULLDOWN_ENABLE) {
        pull = AT_GPIO_PULL_DOWN;
    } else {
        pull = AT_GPIO_PULL_NONE;
    }
    return 0;
}

int gpioMapAtModeToEspMode(AtGpioMode mode, gpio_mode_t& espMode) {
    switch (mode) {
        case AT_GPIO_MODE_DISABLED: {
            espMode = GPIO_MODE_DISABLE;
            break;
        }
        case AT_GPIO_MODE_INPUT: {
            espMode = GPIO_MODE_INPUT;
            break;
        }
        case AT_GPIO_MODE_OUTPUT: {
            espMode = GPIO_MODE_INPUT_OUTPUT;
            break;
        }
        case AT_GPIO_MODE_OUTPUT_OD: {
            espMode = GPIO_MODE_INPUT_OUTPUT_OD;
            break;
        }
        default: {
            return -1;
        }
    }

    return 0;
}

int gpioMapEspModeToGpioMode(gpio_mode_t espMode, AtGpioMode& mode) {
    switch (espMode) {
        case GPIO_MODE_DISABLE: {
            mode = AT_GPIO_MODE_DISABLED;
            break;
        }
        case GPIO_MODE_INPUT: {
            mode = AT_GPIO_MODE_INPUT;
            break;
        }
        case GPIO_MODE_OUTPUT:
        case GPIO_MODE_INPUT_OUTPUT: {
            mode = AT_GPIO_MODE_OUTPUT;
            break;
        }
        case GPIO_MODE_OUTPUT_OD:
        case GPIO_MODE_INPUT_OUTPUT_OD: {
            mode = AT_GPIO_MODE_OUTPUT_OD;
            break;
        }
        default: {
            return -1;
        }
    }
    return 0;
}

} /* anonymous */

namespace particle { namespace ncp {

int AtCommandManager::init() {
    static esp_at_cmd_struct cgmr = {
        (char*)"+CGMR",
        nullptr, /* AT+CGMR=? handler */
        nullptr, /* AT+CGMR? handler */
        nullptr, /* AT+CGMR=(...) handler */
        [](uint8_t*) -> uint8_t { /* AT+CGMR handler */
            AtCommandManager::instance()->writeString(FIRMWARE_VERSION);
            return ESP_AT_RESULT_CODE_OK;
        }
    };
    CHECK_TRUE(esp_at_custom_cmd_array_regist(&cgmr, 1), RESULT_ERROR);

    static esp_at_cmd_struct cmux = {
        (char*)"+CMUX",
        [](uint8_t*) -> uint8_t { /* AT+CMUX=? handler */
            return ESP_AT_RESULT_CODE_ERROR;
        },
        [](uint8_t*) -> uint8_t { /* AT+CMUX? handler */
            return ESP_AT_RESULT_CODE_ERROR;
        },
        [](uint8_t) -> uint8_t { /* AT+CMUX=(...) handler */
            return ESP_AT_RESULT_CODE_ERROR;
        },
        [](uint8_t*) -> uint8_t { /* AT+CMUX handler */
            return ESP_AT_RESULT_CODE_ERROR;
        }
    };
    CHECK_TRUE(esp_at_custom_cmd_array_regist(&cmux, 1), RESULT_ERROR);

    static esp_at_cmd_struct fwupd = {
        (char*)"+FWUPD",
        nullptr, /* AT+FWUPD=? handler */
        nullptr, /* AT+FWUPD? handler */
        [](uint8_t) -> uint8_t { /* AT+FWUPD=(...) handler */
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
        nullptr /* AT+FWUPD handler */
    };
	CHECK_TRUE(esp_at_custom_cmd_array_regist(&fwupd, 1), RESULT_ERROR);

    static esp_at_cmd_struct gpio[] = {
        {
            (char*)"+GPIOC",
            [](uint8_t*) -> uint8_t { /* AT+GPIOC=? handler */
                static const char response[] = "+GPIOC: (0-39),(0-3),(0-2),(0-1)";
                /* +GPIOC=<pin>,<gpio_mode>,[<gpio_pull>],[<gpio_default>]
                 * <pin>: 0-39
                 * <gpio_mode>: 0 - DISABLE, INPUT, OUTPUT, OUTPUT_OD
                 * <gpio_pull>: 0 - none, 1 - pull-down, 2 - pull-up
                 * <gpio_default>: 0 - low, 1 - high
                 */
                auto self = AtCommandManager::instance();
                self->writeString(response);
                return ESP_AT_RESULT_CODE_OK;
            },
            [](uint8_t* cmd) -> uint8_t { /* AT+GPIOC? handler */
                auto term = esp_at_custom_cmd_line_terminator_get();
                AtCommandManager::instance()->writeString((const char*)cmd);
                AtCommandManager::instance()->writeString((const char*)term);
                auto self = AtCommandManager::instance();
                for (uint8_t p = GPIO_NUM_0; p < GPIO_NUM_MAX; p++) {
                    char buf[16] = {};
                    const auto conf = self->gpioConfiguration_[p];
                    if (!conf.pin_bit_mask) {
                        continue;
                    }
                    AtGpioMode mode;
                    if (gpioMapEspModeToGpioMode(conf.mode, mode)) {
                        continue;
                    }
                    AtGpioPull pull;
                    if (gpioMapEspPullToAtPull(conf.pull_up_en, conf.pull_down_en, pull)) {
                        continue;
                    }
                    snprintf(buf, sizeof(buf), "%d,%d,%d%s", p, mode, pull, term);
                    self->writeString(buf);
                }
                return ESP_AT_RESULT_CODE_OK;
            },
            [](uint8_t argc) -> uint8_t { /* AT+GPIOC=(...) handler */
                if (argc < 2) {
                    return ESP_AT_RESULT_CODE_ERROR;
                }

                auto self = AtCommandManager::instance();

                int32_t pin;
                if (esp_at_get_para_as_digit(0, &pin) != ESP_AT_PARA_PARSE_RESULT_OK || !GPIO_IS_VALID_GPIO(pin)) {
                    return ESP_AT_RESULT_CODE_ERROR;
                }

                int32_t mode;
                if (esp_at_get_para_as_digit(1, &mode) != ESP_AT_PARA_PARSE_RESULT_OK) {
                    return ESP_AT_RESULT_CODE_ERROR;
                }

                gpio_config_t conf = {};
                conf.pin_bit_mask = BIT(pin);
                if (gpioMapAtModeToEspMode((AtGpioMode)mode, conf.mode)) {
                    return ESP_AT_RESULT_CODE_ERROR;
                }

                if (argc > 2) {
                    int32_t pull;
                    int r = esp_at_get_para_as_digit(2, &pull);
                    if (r == ESP_AT_PARA_PARSE_RESULT_OK) {
                        if (gpioMapAtPullToEspPull((AtGpioPull)pull, conf.pull_up_en, conf.pull_down_en)) {
                            return ESP_AT_RESULT_CODE_ERROR;
                        }
                    }
                }

                int32_t val = -1;
                if (argc > 3) {
                    if (esp_at_get_para_as_digit(3, &val) == ESP_AT_PARA_PARSE_RESULT_OK) {
                        if (val != 0 && val != 1) {
                            return ESP_AT_RESULT_CODE_ERROR;
                        }
                    }
                }

                if (gpio_config(&conf) != ESP_OK) {
                    return ESP_AT_RESULT_CODE_ERROR;
                }

                self->gpioConfiguration_[pin] = conf;
                if (val >= 0) {
                    gpio_set_level((gpio_num_t)pin, val);
                }
                return ESP_AT_RESULT_CODE_OK;
            },
            nullptr /* AT+GPIOC handler */
        },
        {
            (char*)"+GPIOR",
            [](uint8_t*) -> uint8_t { /* AT+GPIOR=? handler */
                static const char response[] = "+GPIOR: (0-39)";
                /* +GPIOR=<pin>
                 * <pin>: 0-39
                 */
                auto self = AtCommandManager::instance();
                self->writeString(response);
                return ESP_AT_RESULT_CODE_OK;
            },
            nullptr, /* AT+GPIOR? handler */
            [](uint8_t argc) -> uint8_t { /* AT+GPIOR=(...) handler */
                int32_t pin;
                if (esp_at_get_para_as_digit(0, &pin) != ESP_AT_PARA_PARSE_RESULT_OK || !GPIO_IS_VALID_GPIO(pin)) {
                    return ESP_AT_RESULT_CODE_ERROR;
                }

                auto self = AtCommandManager::instance();
                const auto& conf = self->gpioConfiguration_[pin];
                if (conf.mode == GPIO_MODE_DISABLE || conf.mode == GPIO_MODE_OUTPUT ||
                    conf.mode == GPIO_MODE_OUTPUT_OD) {
                    /* Invalid mode */
                    return ESP_AT_RESULT_CODE_ERROR;
                }


                int level = gpio_get_level((gpio_num_t)pin);

                char buf[8] = {};
                snprintf(buf, sizeof(buf), "%d", level);
                self->writeString(buf);

                return ESP_AT_RESULT_CODE_OK;
            },
            nullptr /* AT+GPIOR handler */
        },
        {
            (char*)"+GPIOW",
            [](uint8_t*) -> uint8_t { /* AT+GPIOW=? handler */
                static const char response[] = "+GPIOW: (0-33),(0-1)";
                /* +GPIOW=<pin>,<level>
                 * <pin>: 0-33
                 * <level>: 0 - low, 1 - high
                 */
                auto self = AtCommandManager::instance();
                self->writeString(response);
                return ESP_AT_RESULT_CODE_OK;
            },
            nullptr, /* AT+GPIOW? handler */
            [](uint8_t argc) -> uint8_t { /* AT+GPIOW=(...) handler */
                int32_t pin;
                if (esp_at_get_para_as_digit(0, &pin) != ESP_AT_PARA_PARSE_RESULT_OK || !GPIO_IS_VALID_OUTPUT_GPIO(pin)) {
                    return ESP_AT_RESULT_CODE_ERROR;
                }

                int32_t val;
                if (esp_at_get_para_as_digit(1, &val) != ESP_AT_PARA_PARSE_RESULT_OK ||
                    (val != 0 && val != 1)) {
                    return ESP_AT_RESULT_CODE_ERROR;
                }

                auto self = AtCommandManager::instance();
                const auto& conf = self->gpioConfiguration_[pin];
                if (conf.mode == GPIO_MODE_DISABLE || conf.mode == GPIO_MODE_INPUT) {
                    /* Invalid mode */
                    return ESP_AT_RESULT_CODE_ERROR;
                }

                if (gpio_set_level((gpio_num_t)pin, val) != ESP_OK) {
                    return ESP_AT_RESULT_CODE_ERROR;
                }

                return ESP_AT_RESULT_CODE_OK;
            },
            nullptr /* AT+GPIOW handler */
        }
    };
    CHECK_BOOL(esp_at_custom_cmd_array_regist(gpio, sizeof(gpio) / sizeof(gpio[0])));

    return 0;
}

int AtCommandManager::writeString(const char* str) {
    return esp_at_port_write_data((uint8_t*)str, strlen(str)) >= 0 ? 0 : -1;
}

} } /* particle::ncp */
