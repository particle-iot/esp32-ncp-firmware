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

#include "at_transport_mux.h"
#include "at_transport_uart.h"
#include <lwip/netif.h>
#include <tcpip_adapter.h>
#include <esp_wifi.h>
#include <esp_wifi_internal.h>

#pragma GCC diagnostic ignored "-Wformat"

extern "C" int tcpip_adapter_ipc_check(tcpip_adapter_api_msg_t* msg);

namespace {

const char* TAG = "AtMuxTransport";

int outputEthernetPacket(tcpip_adapter_if_t iface, const uint8_t* data, size_t len) {
    struct Data {
        const uint8_t* data;
        size_t len;
    } d {data, len};
    bool tcpip_inited = true;
    auto f = [](struct tcpip_adapter_api_msg_s* msg) -> int {
        netif* iface = nullptr;
        CHECK_ESP(tcpip_adapter_get_netif(msg->tcpip_if, (void**)&iface));
        if (netif_is_up(iface)) {
            Data* d = (Data*)msg->data;
            esp_wifi_internal_tx((wifi_interface_t)msg->tcpip_if, (void*)d->data, d->len);
        }

        return 0;
    };
    TCPIP_ADAPTER_IPC_CALL(iface, nullptr, nullptr, &d, f);

    return 0;
}

const auto MUXER_MAX_FRAME_SIZE = 1536;
const auto MUXER_MAX_WRITE_TIMEOUT = 10000; // ms

} // anonymous

namespace particle { namespace ncp {

AtMuxTransport::AtMuxTransport(AtUartTransport* uart)
        : uart_(uart),
          stream_((AtTransportBase*)uart),
          muxer_(&stream_),
          rxBuf_(rxBufData_, sizeof(rxBufData_)),
          started_(false) {
}

AtMuxTransport::~AtMuxTransport() {
}

int AtMuxTransport::initTransport()  {
    LOG(INFO, "Initializing GSM07.10 mux transport");
    started_ = true;
    return 0;
}

int AtMuxTransport::destroyTransport() {
    LOG(INFO, "Deinitializing GSM07.10 mux transport");
    started_ = false;
    LOG(INFO, "GSM07.10 transport deinitialized");

    return 0;
}

int AtMuxTransport::postInitTransport() {
    return 0;
}

int AtMuxTransport::readData(uint8_t* data, ssize_t len, unsigned int timeoutMsec) {
    if (!data || len < 0 || !started_) {
        return -1;
    }

    if (len == 0) {
        return 0;
    }

    if (isDirectMode() && muxer_.isRunning()) {
        // XModem is running in a busy-loop in AtUartTransport thread,
        // we have to manually pump the data here, unfortunately.
        if (uart_->getDataLength() > 0) {
            muxer_.notifyInput(uart_->getDataLength());
        }
        // I have no idea why vTaskDelay is needed here, but without it
        // muxer thread does not get any execution time, despite the fact
        // that we've just supposedly unblocked it in notifyInput()
        vTaskDelay(1);
    }

    const ssize_t canRead = CHECK(rxBuf_.data());

    const ssize_t willRead = std::min(len, canRead);
    if (willRead == 0) {
        return 0;
    }

    int r = rxBuf_.get(data, willRead);
    return r;
}

int AtMuxTransport::flushInput() {
    rxBuf_.reset();
    return 0;
}

int AtMuxTransport::writeData(const uint8_t* data, size_t len) {
    if (!started_) {
        return -1;
    }

    // Note: waiting up to MUXER_MAX_WRITE_TIMEOUT seconds here if the remote
    // end is not ready to receive data (~RTS)
    CHECK(muxer_.writeChannel(MUX_CHANNEL_AT, data, len, MUXER_MAX_WRITE_TIMEOUT));
    return len;
}

int AtMuxTransport::getDataLength() const {
    if (!started_) {
        return -1;
    }

    return CHECK_RETURN(rxBuf_.data(), 0);
}

int AtMuxTransport::waitWriteComplete(unsigned int timeoutMsec) {
    if (!started_) {
        return -1;
    }

    return uart_->waitWriteComplete(timeoutMsec);
}

Muxer* AtMuxTransport::getMuxer() {
    return &muxer_;
}

int AtMuxTransport::startMuxer() {
    muxer_.setChannelStateHandler(channelStateCb, this);
    muxer_.setMaxFrameSize(MUXER_MAX_FRAME_SIZE);
    // Start in server mode
    CHECK(muxer_.start(false));
    return 0;
}

void AtMuxTransport::setActive() {
    uart_->setDirectMode(true, dataHandlerCb, this);
    AtTransportBase::setActive();
}

int AtMuxTransport::stopMuxer() {
    muxer_.stop();
    muxer_.setChannelStateHandler(nullptr, nullptr);
    uart_->setActive();
    uart_->setDirectMode(false);
    rxBuf_.reset();
    return 0;
}

int AtMuxTransport::statusChanged(esp_at_status_type status) {
    /* Nothing to do here, because we don't use any commands that switch to non-AT mode */
    return 0;
}

int AtMuxTransport::preDeepSleep() {
    return destroyTransport();
}

int AtMuxTransport::preRestart() {
    return destroyTransport();
}

void AtMuxTransport::dataHandlerCb(size_t len, void* ctx) {
    auto self = static_cast<AtMuxTransport*>(ctx);
    self->dataHandler(len);
}

void AtMuxTransport::dataHandler(size_t len) {
    if (muxer_.isRunning()) {
        muxer_.notifyInput(len);
    }

    if (!muxer_.isRunning()) {
        stopMuxer();
    }
}

int AtMuxTransport::channelAtDataHandlerCb(const uint8_t* data, size_t len, void* ctx) {
    auto self = static_cast<AtMuxTransport*>(ctx);
    return self->channelAtDataHandler(data, len);
}

int AtMuxTransport::channelAtDataHandler(const uint8_t* data, size_t len) {
    rxBuf_.put(data, len);
    notifyReceivedData(len, 1);
    return 0;
}

int AtMuxTransport::channelStaDataHandlerCb(const uint8_t* data, size_t len, void* ctx) {
    return outputEthernetPacket(TCPIP_ADAPTER_IF_STA, data, len);
}

int AtMuxTransport::channelApDataHandlerCb(const uint8_t* data, size_t len, void* ctx) {
    return outputEthernetPacket(TCPIP_ADAPTER_IF_AP, data, len);
}

int AtMuxTransport::channelStateCb(uint8_t channel, Muxer::ChannelState oldState, Muxer::ChannelState newState, void* ctx) {
    auto self = static_cast<AtMuxTransport*>(ctx);
    return self->channelState(channel, oldState, newState);
}

int AtMuxTransport::channelState(uint8_t channel, Muxer::ChannelState oldState, Muxer::ChannelState newState) {
    if (channel == MUX_CHANNEL_AT) {
        muxer_.setChannelDataHandler(channel, channelAtDataHandlerCb, this);
        return 0;
    } else if (channel == MUX_CHANNEL_STATION) {
        muxer_.setChannelDataHandler(channel, channelStaDataHandlerCb, this);
        return 0;
    } else if (channel == MUX_CHANNEL_SOFTAP) {
        muxer_.setChannelDataHandler(channel, channelApDataHandlerCb, this);
        return 0;
    } else if (channel == 0) {
        // Control channel
        return 0;
    }
    // Allow only these specific channels
    return 1;
}

} } /* particle::ncp */
