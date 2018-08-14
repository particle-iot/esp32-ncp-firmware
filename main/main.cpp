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

#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "util.h"

/* :( */
extern "C" {
#include "esp_at.h"
}

extern "C" void app_main(void);

const char* FIRMWARE_VERSION = "v0.0.1";

using namespace particle;
using namespace particle::util;

int atInitialize() {
    /* TODO: initialize transport */

    /* Initialize AT library */
    esp_at_module_init(CONFIG_LWIP_MAX_SOCKETS - 1, (const uint8_t*)FIRMWARE_VERSION);

    /* Initialize command sets */
    /* Base AT commands */
    CHECK_BOOL(esp_at_base_cmd_regist());
    /* WiFi AT commands */
    CHECK_BOOL(esp_at_wifi_cmd_regist());

    /* TODO: custom AT commands */
    /* esp_at_custom_cmd_array_regist() */

    return 0;
}

int main() {
    LOG(INFO, "Starting Argon NCP firmware version: %s", FIRMWARE_VERSION);

    CHECK(nvsInitialize());
    CHECK(atInitialize());

    LOG(INFO, "Initialized");

    return 0;
}

void app_main(void) {
    main();

    while(true) {
        vTaskDelay(1000);
    }
}
