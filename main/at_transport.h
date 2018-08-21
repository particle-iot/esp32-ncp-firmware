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

#ifndef ARGON_NCP_FIRMWARE_AT_TRANSPORT_H
#define ARGON_NCP_FIRMWARE_AT_TRANSPORT_H

#include <cstdint>
#include <cassert>
#include <atomic>
#include <sys/types.h>
#include "common.h"

#include <esp_attr.h>
/* :( */
extern "C" {
#include "esp_at.h"
}

namespace particle { namespace ncp {

class AtTransportBase {
public:
    virtual ~AtTransportBase();

    int init();
    int destroy();
    int postInit();

    void setDirectMode(bool direct);
    bool isDirectMode() const;

    static AtTransportBase* instance();

    virtual int readData(uint8_t* data, ssize_t len, unsigned int timeoutMsec = 1) = 0;
    virtual int flushInput() = 0;
    virtual int writeData(const uint8_t* data, size_t len) = 0;
    virtual int getDataLength() const = 0;
    virtual int waitWriteComplete(unsigned int timeoutMsec) = 0;

protected:
    virtual int initTransport() = 0;
    virtual int destroyTransport() = 0;
    virtual int postInitTransport() = 0;

    virtual int statusChanged(esp_at_status_type status) = 0;
    virtual int preDeepSleep() = 0;
    virtual int preRestart() = 0;

    int notifyReceivedData(size_t len, unsigned int timeoutMsec);

protected:
    AtTransportBase();

private:
    /* esp32-at device ops callbacks */
    static int32_t espAtReadData(uint8_t* data, int32_t len);
    static int32_t espAtWriteData(uint8_t* data, int32_t len);
    static int32_t espAtGetDataLength();
    static bool espAtWaitWriteComplete(int32_t timeoutMsec);

    /* esp32-at custom ops callbacks */
    static void espAtStatusCallback(esp_at_status_type status);
    static void espAtPreDeepSleepCallback();
    static void espAtPreRestartCallback();

private:
    std::atomic_bool direct_;
    static AtTransportBase* instance_;
};

} } /* particle::ncp */

#endif /* ARGON_NCP_FIRMWARE_AT_TRANSPORT_H */
