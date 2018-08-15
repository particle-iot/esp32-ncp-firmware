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
#include <sys/types.h>
#include "util.h"

#include <esp_attr.h>
/* :( */
extern "C" {
#include "esp_at.h"
}

namespace particle { namespace ncp {

template <typename DerivedT>
class AtTransportBase {
public:
    int init();
    int destroy();
    int postInit();

    static DerivedT* instance();

protected:
    /* Overridable */
    int initTransport();
    int destroyTransport();
    int postInitTransport();

    int readData(uint8_t* data, ssize_t len);
    int writeData(const uint8_t* data, size_t len);
    int getDataLength() const;
    int waitWriteComplete(unsigned int timeoutMsec);

    int statusChanged(esp_at_status_type status);
    int preDeepSleep();
    int preRestart();

protected:
    AtTransportBase();

private:
    DerivedT& derived();

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
    static DerivedT* instance_;
};

/* Implementation */

template <typename DerivedT>
DerivedT* AtTransportBase<DerivedT>::instance_ = nullptr;

template <typename DerivedT>
inline AtTransportBase<DerivedT>::AtTransportBase() {
    assert(instance_ == nullptr);
    instance_ = &derived();
}

template <typename DerivedT>
inline int AtTransportBase<DerivedT>::init() {
    CHECK(derived().initTransport());

    esp_at_device_ops_struct deviceOps = {
        .read_data = espAtReadData,
        .write_data = espAtWriteData,
        .get_data_length = espAtGetDataLength,
        .wait_write_complete = espAtWaitWriteComplete,
    };

    esp_at_custom_ops_struct customOps = {
        .status_callback = espAtStatusCallback,
        .pre_deepsleep_callback = espAtPreDeepSleepCallback,
        .pre_restart_callback = espAtPreRestartCallback,
    };

    esp_at_device_ops_regist(&deviceOps);
    esp_at_custom_ops_regist(&customOps);

    return 0;
}

template <typename DerivedT>
inline int AtTransportBase<DerivedT>::destroy() {
    CHECK(derived().destroyTransport());

    /* Does this work? */
    esp_at_device_ops_regist(nullptr);
    esp_at_custom_ops_regist(nullptr);

    return 0;
}

template <typename DerivedT>
inline int AtTransportBase<DerivedT>::postInit() {
    return derived().postInitTransport();
}

template <typename DerivedT>
inline DerivedT* AtTransportBase<DerivedT>::instance() {
    assert(instance_ != nullptr);
    return instance_;
}

template <typename DerivedT>
inline DerivedT& AtTransportBase<DerivedT>::derived() {
    return *static_cast<DerivedT*>(this);
}

template <typename DerivedT>
inline int32_t AtTransportBase<DerivedT>::espAtReadData(uint8_t* data, int32_t len) {
    auto self = instance();
    return self->readData(data, len);
}

template <typename DerivedT>
inline int32_t AtTransportBase<DerivedT>::espAtWriteData(uint8_t* data, int32_t len) {
    auto self = instance();
    return self->writeData(data, len);
}

template <typename DerivedT>
inline int32_t AtTransportBase<DerivedT>::espAtGetDataLength() {
    const auto self = instance();
    return self->getDataLength();
}

template <typename DerivedT>
inline bool AtTransportBase<DerivedT>::espAtWaitWriteComplete(int32_t timeoutMsec) {
    auto self = instance();
    return self->waitWriteComplete(timeoutMsec) == 0;
}

template <typename DerivedT>
inline void AtTransportBase<DerivedT>::espAtStatusCallback(esp_at_status_type status) {
    auto self = instance();
    self->statusChanged(status);
}

template <typename DerivedT>
inline void AtTransportBase<DerivedT>::espAtPreDeepSleepCallback() {
    auto self = instance();
    self->preDeepSleep();
}

template <typename DerivedT>
inline void AtTransportBase<DerivedT>::espAtPreRestartCallback() {
    auto self = instance();
    self->preRestart();
}

} } /* particle::ncp */

#endif /* ARGON_NCP_FIRMWARE_AT_TRANSPORT_H */
