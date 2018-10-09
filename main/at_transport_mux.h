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

#ifndef ARGON_NCP_FIRMWARE_AT_TRANSPORT_MUX_H
#define ARGON_NCP_FIRMWARE_AT_TRANSPORT_MUX_H

#include "at_transport.h"
#include <atomic>
#include <driver/uart.h>
#include "gsm0710muxer/muxer.h"
#include "stream.h"
#include "util/ringbuffer.h"

namespace particle { namespace ncp {

using MuxerStream = AtTransportStream;
using Muxer = gsm0710::Muxer<MuxerStream, std::recursive_mutex>;

class AtUartTransport;

enum MuxerChannel {
    MUX_CHANNEL_AT       = 1,
    MUX_CHANNEL_STATION  = 2,
    MUX_CHANNEL_SOFTAP   = 3
};

class AtMuxTransport : public AtTransportBase {
public:
    AtMuxTransport(AtUartTransport* uart);
    virtual ~AtMuxTransport();

    virtual void setActive() override;

    virtual int readData(uint8_t* data, ssize_t len, unsigned int timeoutMsec = 1) override;
    virtual int flushInput() override;
    virtual int writeData(const uint8_t* data, size_t len) override;
    virtual int getDataLength() const override;
    virtual int waitWriteComplete(unsigned int timeoutMsec) override;

    Muxer* getMuxer();
    int startMuxer();
    int stopMuxer();

protected:
    virtual int initTransport() override;
    virtual int destroyTransport() override;
    virtual int postInitTransport() override;

    virtual int statusChanged(esp_at_status_type status) override;
    virtual int preDeepSleep() override;
    virtual int preRestart() override;

private:
    static void dataHandlerCb(size_t len, void* ctx);
    void dataHandler(size_t len);

    static int channelAtDataHandlerCb(const uint8_t* data, size_t len, void* ctx);
    int channelAtDataHandler(const uint8_t* data, size_t len);

    static int channelStaDataHandlerCb(const uint8_t* data, size_t len, void* ctx);
    static int channelApDataHandlerCb(const uint8_t* data, size_t len, void* ctx);

    static int channelStateCb(uint8_t channel, Muxer::ChannelState oldState, Muxer::ChannelState newState, void* ctx);
    int channelState(uint8_t channel, Muxer::ChannelState oldState, Muxer::ChannelState newState);

private:
    AtUartTransport* uart_;

    MuxerStream stream_;
    Muxer muxer_;

    particle::services::RingBuffer<uint8_t> rxBuf_;
    uint8_t rxBufData_[2048];

    std::atomic_bool started_;
};

} } /* particle::ncp */

#endif /* ARGON_NCP_FIRMWARE_AT_TRANSPORT_MUX_H */
