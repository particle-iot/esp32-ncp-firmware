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

#include "at_transport.h"
#include "xmodem_receiver.h"
#include "stream.h"
#include "util.h"

#include "esp_system.h"
#include "esp_ota_ops.h"

namespace particle { namespace ncp {

namespace {

class FirmwareUpdateStream: public OutputStream {
public:
    explicit FirmwareUpdateStream(esp_ota_handle_t ota) :
            ota_(ota) {
    }

    int write(const char* data, size_t size) override {
        CHECK_ESP(esp_ota_write(ota_, data, size));
        return size;
    }

private:
    esp_ota_handle_t ota_;
};

// TODO: Make AtTransportBase a stream
class XmodemStream: public Stream {
public:
    explicit XmodemStream(AtTransportBase* at) :
            at_(at) {
    }

    int read(char* data, size_t size) override {
        return at_->readData((uint8_t*)data, size);
    }

    int write(const char* data, size_t size) override {
        return at_->writeData((const uint8_t*)data, size);
    }

private:
    AtTransportBase* at_;
};

} // particle::ncp::

UpdateManager::UpdateManager() {
}

int UpdateManager::update(size_t size) {
    LOG(INFO, "Initiating update");

    const auto transport = AtTransportBase::instance();
    CHECK_TRUE(transport, RESULT_ERROR);
    transport->flushInput();

    XmodemStream srcStrm(transport);

    /* Initiate OTA update */
    const esp_partition_t* curPart = esp_ota_get_running_partition();
    LOG(INFO, "Running partition: type: %d, subtype: %d, offset: 0x%08x", (int)curPart->type, (int)curPart->subtype,
            (unsigned)curPart->address);
    const esp_partition_t* updPart = esp_ota_get_next_update_partition(nullptr);
    LOG(INFO, "Writing to partition: type: %d, subtype: %d, offset: 0x%08x", (int)updPart->type, (int)updPart->subtype,
            (unsigned)updPart->address);

    esp_ota_handle_t ota = 0;
    CHECK_ESP(esp_ota_begin(updPart, size, &ota));
    LOG(INFO, "Expected size %d", size);

    FirmwareUpdateStream destStrm(ota);

    XmodemReceiver xmodem;
    CHECK(xmodem.init(&srcStrm, &destStrm, size));

    int ret = 0;
    do {
        ret = xmodem.run();
    } while (ret == XmodemReceiver::RUNNING);

    if (ret == 0) {
        /* Apply the update */
        LOG(INFO, "Applying the update...");
        CHECK_ESP(esp_ota_end(ota));
        CHECK_ESP(esp_ota_set_boot_partition(updPart));
        transport->waitWriteComplete(1000);
        esp_restart();
    } else {
        LOG(ERROR, "Xmodem receiver error: %d", ret);
        esp_ota_end(ota);
        return RESULT_ERROR;
    }

    return 0;
}

} } /* particle::ncp */
