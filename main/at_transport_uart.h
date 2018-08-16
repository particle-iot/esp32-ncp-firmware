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

#ifndef ARGON_NCP_FIRMWARE_AT_TRANSPORT_UART_H
#define ARGON_NCP_FIRMWARE_AT_TRANSPORT_UART_H

#include "at_transport.h"
#include <atomic>
#include <driver/uart.h>

namespace particle { namespace ncp {

class AtUartTransport : public AtTransportBase {
public:
    struct Config {
        uart_port_t uart;

        int rxPin;
        int txPin;
        int rtsPin;
        int ctsPin;

        uart_config_t config;
    };

    AtUartTransport(const Config& conf);
    virtual ~AtUartTransport();

    virtual int readData(uint8_t* data, ssize_t len, unsigned int timeoutMsec = 1) override;
    virtual int flushInput() override;
    virtual int writeData(const uint8_t* data, size_t len) override;
    virtual int getDataLength() const override;
    virtual int waitWriteComplete(unsigned int timeoutMsec) override;

protected:
    virtual int initTransport() override;
    virtual int destroyTransport() override;
    virtual int postInitTransport() override;

    virtual int statusChanged(esp_at_status_type status) override;
    virtual int preDeepSleep() override;
    virtual int preRestart() override;

private:
    static void run(void* arg);
    void run();

private:
    Config conf_;
    std::atomic_bool exit_;
    std::atomic_bool started_;
    QueueHandle_t queue_ = nullptr;
    TaskHandle_t thread_ = nullptr;

    enum AdditionalEvents {
        TRANSPORT_EVENT_EXIT = UART_EVENT_MAX + 1
    };
};

} } /* particle::ncp */

#endif /* ARGON_NCP_FIRMWARE_AT_TRANSPORT_UART_H */
