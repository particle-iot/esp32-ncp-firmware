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

#include "at_transport.h"

namespace particle { namespace ncp {

AtTransportBase* AtTransportBase::instance_ = nullptr;

AtTransportBase::AtTransportBase()
        : direct_(false) {
}

AtTransportBase::~AtTransportBase() {
    destroy();
}

int AtTransportBase::init() {
    CHECK(initTransport());

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

    if (!instance_) {
        setActive();
    }

    return 0;
}

int AtTransportBase::destroy() {
    if (!instance_) {
        return 0;
    }

    if (instance_ == this) {
        instance_ = nullptr;
    }

    CHECK(destroyTransport());

    if (!instance_) {
        /* Does this work? */
        esp_at_device_ops_regist(nullptr);
        esp_at_custom_ops_regist(nullptr);
    }

    return 0;
}

int AtTransportBase::postInit() {
    return postInitTransport();
}

void AtTransportBase::setDirectMode(bool direct, DataNotificationHandler handler, void* ctx) {
    direct_ = direct;
    handler_ = handler;
    handlerCtx_ = ctx;
}

bool AtTransportBase::isDirectMode() const {
    return direct_;
}

AtTransportBase* AtTransportBase::instance() {
    return instance_;
}

void AtTransportBase::setActive() {
    instance_ = this;
}

bool AtTransportBase::isActive() const {
    return instance_ == this;
}

int AtTransportBase::notifyReceivedData(size_t len, unsigned int timeoutMsec) {
    if (direct_) {
        if (handler_) {
            handler_(len, handlerCtx_);
        }
        return 0;
    }

    return esp_at_port_recv_data_notify(len, timeoutMsec) ? 0 : -1;
}

int32_t AtTransportBase::espAtReadData(uint8_t* data, int32_t len) {
    auto self = instance();
    if (!self || self->isDirectMode()) {
        return -1;
    }

    return self->readData(data, len);
}

int32_t AtTransportBase::espAtWriteData(uint8_t* data, int32_t len) {
    auto self = instance();
    if (!self || self->isDirectMode()) {
        return -1;
    }

    return self->writeData(data, len);
}

int32_t AtTransportBase::espAtGetDataLength() {
    auto self = instance();
    if (!self || self->isDirectMode()) {
        return -1;
    }

    return self->getDataLength();
}

bool AtTransportBase::espAtWaitWriteComplete(int32_t timeoutMsec) {
    auto self = instance();
    if (!self || self->isDirectMode()) {
        return true;
    }

    return self->waitWriteComplete(timeoutMsec) == 0;
}

void AtTransportBase::espAtStatusCallback(esp_at_status_type status) {
    auto self = instance();
    if (!self || self->isDirectMode()) {
        return;
    }

    self->statusChanged(status);
}

void AtTransportBase::espAtPreDeepSleepCallback() {
    auto self = instance();
    if (!self || self->isDirectMode()) {
        return;
    }

    self->preDeepSleep();
}

void AtTransportBase::espAtPreRestartCallback() {
    auto self = instance();
    if (!self || self->isDirectMode()) {
        return;
    }

    self->preRestart();
}

} } /* particle::ncp */
