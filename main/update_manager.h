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

#ifndef ARGON_NCP_FIRMWARE_UPDATE_MANAGER_H
#define ARGON_NCP_FIRMWARE_UPDATE_MANAGER_H

#include "common.h"

namespace particle { 

class OutputStream;

namespace ncp {

class UpdateManager {
public:
    ~UpdateManager();

    // Note: UpdateManager retains ownership over the stream object
    int beginUpdate(size_t size, OutputStream** strm);
    int finishUpdate();
    void cancelUpdate();

    static UpdateManager* instance();

private:
    struct Data;

    std::unique_ptr<Data> d_;

    UpdateManager();
};

} // particle::ncp

} // particle

#endif /* ARGON_NCP_FIRMWARE_UPDATE_MANAGER_H */
