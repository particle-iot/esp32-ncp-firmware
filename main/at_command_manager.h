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

#ifndef ARGON_NCP_FIRMWARE_AT_COMMAND_MANAGER_H
#define ARGON_NCP_FIRMWARE_AT_COMMAND_MANAGER_H

#include "at_transport.h"
#include "common.h"

#include <esp_attr.h>
/* :( */
extern "C" {
#include "esp_at.h"
}

#include "driver/gpio.h"

namespace particle { namespace ncp {

class AtCommandManager {
public:
    int init();

    static AtCommandManager* instance() {
        static AtCommandManager man;
        return &man;
    }

    int writeString(const char* data);
    int writeFormatted(const char* fmt, ...);
    int writeNewLine();

    const char* newLineSequence() const;

protected:
    AtCommandManager() = default;

private:
    gpio_config_t gpioConfiguration_[GPIO_NUM_MAX] = {};
};

inline int AtCommandManager::writeNewLine() {
    return writeString(newLineSequence());
}

} } /* particle::ncp */

#endif /* ARGON_NCP_FIRMWARE_AT_COMMAND_MANAGER_H */
