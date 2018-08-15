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

#include "util.h"

#include <esp_attr.h>
/* :( */
extern "C" {
#include "esp_at.h"
}

namespace particle { namespace ncp {

class AtCommandManager {
public:
    int init();
};

} } /* particle::ncp */

#endif /* ARGON_NCP_FIRMWARE_AT_COMMAND_MANAGER_H */
