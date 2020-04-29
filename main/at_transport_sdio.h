/*
 * Copyright (c) 2020 Particle Industries, Inc.  All rights reserved.
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

#pragma once

#include "at_transport.h"
#include <atomic>
#include <driver/sdio_slave.h>

namespace particle { namespace ncp {

static constexpr int ESP_AT_SDIO_BUFFER_SIZE = CONFIG_AT_SDIO_BLOCK_SIZE;
static constexpr int ESP_AT_SDIO_BUFFER_NUM  = CONFIG_AT_SDIO_BUFFER_NUM;
static constexpr int ESP_AT_SDIO_QUEUE_SIZE  = CONFIG_AT_SDIO_QUEUE_SIZE;

class AtSdioTransport : public AtTransportBase {

typedef struct sdio_list {
    uint8_t                 pbuf[ESP_AT_SDIO_BUFFER_SIZE];
    struct sdio_list*       next;
    sdio_slave_buf_handle_t handle;
    uint32_t                left_len;
    uint32_t                pos;
} esp_at_sdio_list_t;

public:
    AtSdioTransport();
    virtual ~AtSdioTransport();

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
    int fetchData(unsigned int timeoutMsec);
    static void run(void* arg);
    void run();

private:
    esp_at_sdio_list_t WORD_ALIGNED_ATTR list_[ESP_AT_SDIO_BUFFER_NUM];
    esp_at_sdio_list_t* listHead_;
    esp_at_sdio_list_t* listTail_;
    xSemaphoreHandle semahandle_;

    std::atomic_bool started_;
    std::atomic_bool exit_;
    TaskHandle_t thread_;
};

} } /* particle::ncp */
