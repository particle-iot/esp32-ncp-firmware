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

#ifndef GSM0710_PLATFORM_H
#define GSM0710_PLATFORM_H

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include "common.h"
#include "util.h"

namespace gsm0710 {
namespace portable {

inline uint64_t getMillis() {
    return particle::util::millis();
}

const auto taskStackSize = 4096;
const auto taskPriority = tskIDLE_PRIORITY + 2;

#define GSM0710_ASSERT(x) assert(x)

// #define GSM0710_MODEM_STATUS_SEND_EMPTY_BREAK

} // portable
} // gsm0710

#endif // GSM0710_PLATFORM_H
