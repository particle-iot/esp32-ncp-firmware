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

#include <cstring>
#include "update_manager.h"
#include "util.h"
#include "esp_system.h"

extern "C" {
int xmodemReceive(int destsz, void(*callback)(const char* data, size_t size, void* callback_data), void* callback_data);

int xmodem_read(unsigned short timeout) {
    auto transport = particle::ncp::AtTransportBase::instance();
    if (!transport) {
        return -1;
    }

    uint8_t c;
    const int ret = transport->readData(&c, sizeof(c), timeout);
    if (ret != sizeof(c)) {
        return -1;
    }

    return (int)c;
}

void xmodem_write(int c) {
    auto transport = particle::ncp::AtTransportBase::instance();
    if (!transport) {
        return;
    }

    const uint8_t b = c;
    const int ret = transport->writeData(&b, sizeof(b));
    if (ret != sizeof(b)) {
        LOG(ERROR, "writeData() failed: %d", ret);
    }
}
} /* extern "C" */

namespace particle { namespace ncp {

const auto MAX_FW_SIZE = 1024 * 1024;

static const char* esp_err_to_name(const esp_err_t ret) {
    return "ESP_ERROR";
}

UpdateManager::UpdateManager() {
}

int UpdateManager::update(size_t size) {
    LOG(INFO, "Initiating update");
    reset();

    auto transport = AtTransportBase::instance();
    if (!transport) {
        return -1;
    }

    transport->flushInput();

    /* Initiate OTA update */
    const esp_partition_t* curPart = esp_ota_get_running_partition();
    LOG(INFO, "Running partition: type: %d, subtype: %d, offset: 0x%08x", (int)curPart->type, (int)curPart->subtype,
            (unsigned)curPart->address);
    const esp_partition_t* updPart = esp_ota_get_next_update_partition(nullptr);
    LOG(INFO, "Writing to partition: type: %d, subtype: %d, offset: 0x%08x", (int)updPart->type, (int)updPart->subtype,
            (unsigned)updPart->address);

    imageSize_ = size;

    CHECK_ESP(esp_ota_begin(updPart, imageSize_, &ota_));
    LOG(INFO, "Expected size %d", imageSize_);

    int ret = xmodemReceive(MAX_FW_SIZE, [](const char* data, size_t size, void* ctx) {
        auto self = static_cast<UpdateManager*>(ctx);
        if (self->error_ == 0) {
            if (self->currentSize_ + size > self->imageSize_) {
                int sz = self->imageSize_ - self->currentSize_;
                if (sz <= 0) {
                    LOG(ERROR, "Received more data than necessary");
                    self->error_ = -1;
                    return;
                }

                size = sz;
            }
            const auto ret = esp_ota_write(self->ota_, data, size);
            if (ret != ESP_OK) {
                LOG(ERROR, "esp_ota_write() failed: %s", esp_err_to_name(ret));
                self->error_ = ret;
            } else {
                self->currentSize_ += size;
                LOG(INFO, "Received %d", self->currentSize_);
            }
        }
    }, this);

    if (ret > 0 && error_ == 0 && currentSize_ == imageSize_) {
        /* Apply the update */
        LOG(INFO, "Applying the update...");
        CHECK_ESP(esp_ota_end(ota_));
        CHECK_ESP(esp_ota_set_boot_partition(updPart));
        transport->waitWriteComplete(1000);
        esp_restart();
    } else {
        LOG(ERROR, "Xmodem receiver error: %d", ret);
        LOG(ERROR, "Expected size: %u", imageSize_);
        LOG(ERROR, "Received: %u", currentSize_);
        esp_ota_end(ota_);
        return -1;
    }

    return 0;
}

void UpdateManager::reset() {
    imageSize_ = 0;
    currentSize_ = 0;
    error_ = 0;
    memset(&ota_, 0, sizeof(ota_));
}

} } /* particle::ncp */
