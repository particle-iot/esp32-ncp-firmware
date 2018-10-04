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

#include "util/scope_guard.h"
#include "update_manager.h"
#include "xmodem_receiver.h"
#include "stream.h"
#include "version.h"

#include <esp_system.h>

#include <freertos/FreeRTOS.h>

#include <cstring>
#include <cstdio>
#include <cstdarg>

/* :( */
extern "C" {
#include <esp_at.h>
}

#include <memory>
#include "at_transport_mux.h"

extern std::unique_ptr<particle::ncp::AtMuxTransport> g_muxTransport;

namespace particle { namespace ncp {

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

using XmodemStream = ::particle::ncp::AtTransportStream;

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

int AtCommandManager::init() {
    static esp_at_cmd_struct cgmr = {
        (char*)"+CGMR",
        nullptr, /* AT+CGMR=? handler */
        nullptr, /* AT+CGMR? handler */
        nullptr, /* AT+CGMR=(...) handler */
        [](uint8_t*) -> uint8_t { /* AT+CGMR handler */
            AtCommandManager::instance()->writeString(FIRMWARE_VERSION_STRING);
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
            // Do not allow to execut AT+CMUX within multiplexed session
            if (g_muxTransport->isActive()) {
                LOG(ERROR, "Received AT+CMUX while in multiplexed mode");
                return ESP_AT_RESULT_CODE_ERROR;
            }

            LOG(INFO, "Received AT+CMUX, switching to multiplexed mode");

            CHECK_TRUE(g_muxTransport, ESP_AT_RESULT_CODE_ERROR);
            CHECK_RETURN(g_muxTransport->startMuxer(), ESP_AT_RESULT_CODE_ERROR);

            esp_at_response_result(ESP_AT_RESULT_CODE_OK);
            esp_at_port_wait_write_complete(1000);

            // Switch to muxed transport
            g_muxTransport->setActive();
            return ESP_AT_RESULT_CODE_PROCESS_DONE;
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
            int32_t size;
            if (esp_at_get_para_as_digit(0, &size) != ESP_AT_PARA_PARSE_RESULT_OK || size <= 0) {
                return ESP_AT_RESULT_CODE_ERROR;
            }
            // Initiate the update
            OutputStream* updStrm = nullptr;
            const auto updMgr = UpdateManager::instance();
            CHECK_RETURN(updMgr->beginUpdate(size, &updStrm), ESP_AT_RESULT_CODE_ERROR);
            SCOPE_GUARD({
                updMgr->cancelUpdate();
            });
            const auto at = AtTransportBase::instance();
            XmodemStream atStrm(at);
            XmodemReceiver xmodem;
            CHECK(xmodem.init(&atStrm, updStrm, size));
            // Send an intermediate result code
            const auto self = AtCommandManager::instance();
            self->writeString("+FWUPD: ONGOING");
            self->writeNewLine();
            // Receive the firmware binary
            at->setDirectMode(true);
            int ret = 0;
            do {
                ret = xmodem.run();
            } while (ret == XmodemReceiver::RUNNING);
            // Discard any extra CAN bytes that might have been sent by the sender at the end of
            // the XModem transfer
            at->flushInput();
            at->setDirectMode(false);
            if (ret != XmodemReceiver::DONE) {
                LOG(ERROR, "XModem transfer failed: %d", ret);
                return ESP_AT_RESULT_CODE_ERROR;
            }
            // Apply the update
            ret = updMgr->finishUpdate();
            if (ret == 0) {
                LOG(INFO, "Resetting the system to apply the update");
                // Send a final result code
                esp_at_response_result(ESP_AT_RESULT_CODE_OK);
                at->waitWriteComplete(1000);
                esp_restart();
            }
            LOG(ERROR, "Firmware update failed: %d", ret);
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
                auto self = AtCommandManager::instance();
                for (uint8_t p = GPIO_NUM_0; p < GPIO_NUM_MAX; p++) {
                    char buf[32] = {};
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
                    snprintf(buf, sizeof(buf), "%s: %d,%d,%d%s", (const char*)cmd, p, mode, pull, term);
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
                if (!conf.pin_bit_mask || conf.mode == GPIO_MODE_DISABLE) {
                    /* Invalid mode */
                    return ESP_AT_RESULT_CODE_ERROR;
                }


                int level = gpio_get_level((gpio_num_t)pin);

                char buf[32] = {};
                snprintf(buf, sizeof(buf), "+GPIOR: %d", level);
                self->writeString(buf);
                self->writeNewLine();

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
                if (!conf.pin_bit_mask || conf.mode == GPIO_MODE_DISABLE || conf.mode == GPIO_MODE_INPUT) {
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
    CHECK_TRUE(esp_at_custom_cmd_array_regist(gpio, sizeof(gpio) / sizeof(gpio[0])), RESULT_ERROR);

    static esp_at_cmd_struct mver = {
        (char*)"+MVER",
        nullptr, // AT+MVER=?
        nullptr, // AT+MVER?
        nullptr, // AT+MVER=...
        [](uint8_t*) -> uint8_t { // AT+MVER
            const auto self = AtCommandManager::instance();
            self->writeFormatted("%u", (unsigned)FIRMWARE_MODULE_VERSION);
            return ESP_AT_RESULT_CODE_OK;
        }
    };
    CHECK_TRUE(esp_at_custom_cmd_array_regist(&mver, 1), RESULT_ERROR);

    static esp_at_cmd_struct mac = {
        (char*)"+GETMAC",
        [](uint8_t*) -> uint8_t { /* AT+GETMAC=? handler */
            static const char response[] = "+GETMAC: (0-3)";
            /* +GETMAC=<type>
             * <type>: 0 - WiFi Station, 1 - WiFi AP, 2 - Bluetooth, 3 - Ethernet
             */
            auto self = AtCommandManager::instance();
            self->writeString(response);
            return ESP_AT_RESULT_CODE_OK;
        },
        nullptr, // AT+GETMAC?
        [](uint8_t argc) -> uint8_t { // AT+GETMAC=(...)
            int32_t type;
            if (esp_at_get_para_as_digit(0, &type) != ESP_AT_PARA_PARSE_RESULT_OK ||
                ((type != ESP_MAC_WIFI_STA) && (type != ESP_MAC_WIFI_SOFTAP) &&
                 (type != ESP_MAC_BT) && (type != ESP_MAC_ETH))) {
                return ESP_AT_RESULT_CODE_ERROR;
            }

            const auto self = AtCommandManager::instance();
            uint8_t mac[6] = {};
            CHECK_ESP_RESULT(esp_read_mac(mac, (esp_mac_type_t)type), ESP_AT_RESULT_CODE_ERROR);
            char buf[32] = {};
            snprintf(buf, sizeof(buf), "+GETMAC: \"%02x:%02x:%02x:%02x:%02x:%02x\"",
                        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
            self->writeString(buf);
            return ESP_AT_RESULT_CODE_OK;
        },
        nullptr // AT+GETMAC
    };
    CHECK_TRUE(esp_at_custom_cmd_array_regist(&mac, 1), RESULT_ERROR);

    return 0;
}

int AtCommandManager::writeString(const char* str) {
    return esp_at_port_write_data((uint8_t*)str, strlen(str)) >= 0 ? 0 : -1;
}

int AtCommandManager::writeFormatted(const char* fmt, ...) {
    char buf[128];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (n >= (int)sizeof(buf)) {
        std::unique_ptr<char[]> buf(new char[n + 1]); // Use a larger buffer
        va_start(args, fmt);
        n = vsnprintf(buf.get(), n + 1, fmt, args);
        va_end(args);
        if (n > 0) {
            n = writeString(buf.get());
        }
    } else if (n > 0) {
        n = writeString(buf);
    }
    return n;
}

const char* AtCommandManager::newLineSequence() const {
    return (const char*)esp_at_custom_cmd_line_terminator_get();
}

} } /* particle::ncp */
