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

#include "update_manager.h"

#include "stream.h"

#include <esp_ota_ops.h>

namespace particle {

namespace ncp {

struct UpdateManager::Data: public OutputStream {
    esp_ota_handle_t ota; // OTA handle
    const esp_partition_t* otaPart; // Target partition

    int write(const char* data, size_t size) override {
        CHECK_ESP(esp_ota_write(ota, data, size));
        return size;
    }
};

UpdateManager::UpdateManager() {
}

UpdateManager::~UpdateManager() {
}

int UpdateManager::beginUpdate(size_t size, OutputStream** strm) {
    CHECK_FALSE(d_, RESULT_INVALID_STATE);
    std::unique_ptr<Data> d(new(std::nothrow) Data);
    CHECK_TRUE(d, RESULT_NO_MEMORY);
    LOG(INFO, "Initiating the update; expected size: %u", (unsigned)size);
    const auto curPart = esp_ota_get_running_partition();
    LOG(INFO, "Running partition: type: %d, subtype: %d, offset: 0x%08x", (int)curPart->type, (int)curPart->subtype,
            (unsigned)curPart->address);
    d->otaPart = esp_ota_get_next_update_partition(nullptr);
    LOG(INFO, "Writing to partition: type: %d, subtype: %d, offset: 0x%08x", (int)d->otaPart->type,
            (int)d->otaPart->subtype, (unsigned)d->otaPart->address);
    CHECK_ESP(esp_ota_begin(d->otaPart, size, &d->ota));
    d_ = std::move(d);
    *strm = d_.get();
    return 0;
}

int UpdateManager::finishUpdate() {
    CHECK_TRUE(d_, RESULT_INVALID_STATE);
    const std::unique_ptr<Data> d(d_.release());
    LOG(INFO, "Finishing the update");
    CHECK_ESP(esp_ota_end(d->ota));
    CHECK_ESP(esp_ota_set_boot_partition(d->otaPart));
    return 0;
}

void UpdateManager::cancelUpdate() {
    if (d_) {
        // Finish the update but don't change the boot partition
        LOG(INFO, "Cancelling the update");
        esp_ota_end(d_->ota);
        d_.reset();
    }
}

UpdateManager* UpdateManager::instance() {
    static UpdateManager m;
    return &m;
}

} // particle::ncp

} // particle
