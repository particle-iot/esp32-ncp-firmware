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
/*
 * ESPRESSIF MIT License
 *
 * Copyright (c) 2018 <ESPRESSIF SYSTEMS (SHANGHAI) PTE LTD>
 *
 * Permission is hereby granted for use on ESPRESSIF SYSTEMS ESP32 only, in which case,
 * it is free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished
 * to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

// This code is based on at_sdio_task.c implementation

#include "at_transport_sdio.h"
#include "logging.h"

namespace particle { namespace ncp {

#define container_of(ptr, type, member) ({      \
    const decltype( ((type *)0)->member ) *__mptr = (const decltype( ((type *)0)->member ) *)ptr; \
    (type *)( (char *)__mptr - ((size_t) &((type *)0)->member) );})

AtSdioTransport::AtSdioTransport()
        : AtTransportBase(),
          listHead_(nullptr),
          listTail_(nullptr),
          started_(false),
          exit_(false),
          thread_(nullptr) {
}

AtSdioTransport::~AtSdioTransport() {
}

int AtSdioTransport::initTransport()  {
    LOG(INFO, "Initializing SDIO transport");
    sdio_slave_config_t config = {};
    config.sending_mode = SDIO_SLAVE_SEND_STREAM;
    config.send_queue_size = AT_SDIO_QUEUE_SIZE;
    config.recv_buffer_size = AT_SDIO_BUFFER_SIZE;

    sdio_slave_buf_handle_t handle;

    esp_err_t ret = sdio_slave_initialize(&config);
    assert(ret == ESP_OK);

    rxData_ = 0;

    for (int loop = 0; loop < AT_SDIO_BUFFER_NUM; loop++) {
        handle = sdio_slave_recv_register_buf(list_[loop].pbuf);
        assert(handle != nullptr);

        ret = sdio_slave_recv_load_buf(handle);
        assert(ret == ESP_OK);
    }

    sdio_slave_set_host_intena(SDIO_SLAVE_HOSTINT_SEND_NEW_PACKET | SDIO_SLAVE_HOSTINT_BIT0);

    sdio_slave_start();

    if (xTaskCreate(run, "at_sdio_t", 8192, this, tskIDLE_PRIORITY + 1, &thread_) != pdPASS) {
        destroyTransport();
    }

    started_ = true;

    return 0;
}

int AtSdioTransport::destroyTransport() {
    LOG(INFO, "Deinitializing SDIO transport");
    if (thread_) {
        exit_ = true;

        LOG(INFO, "Waiting for SDIO thread to stop");

        /* Join thread */
        while (exit_) {
            vTaskDelay(100 / portTICK_PERIOD_MS);
        }

        thread_ = nullptr;

        LOG(INFO, "SDIO thread stopped");
    }

    waitWriteComplete(portMAX_DELAY);

    started_ = false;

    // Deinitialize SDIO pins
    sdio_slave_deinit();

    LOG(INFO, "SDIO transport deinitialized");

    return 0;
}

int AtSdioTransport::postInitTransport() {
    return 0;
}

int AtSdioTransport::fetchData(unsigned int timeoutMsec) {
    sdio_slave_buf_handle_t handle;
    size_t length = 0;
    uint8_t* ptr = nullptr;

    CHECK_ESP(sdio_slave_recv(&handle, &ptr, &length, timeoutMsec / portTICK_PERIOD_MS));
    Buffer* buf = container_of(ptr, Buffer, pbuf); // get struct list pointer

    buf->handle = handle;
    buf->leftLen = length;
    buf->pos = 0;
    buf->next = nullptr;

    std::lock_guard<std::mutex> lock(mutex_);
    if (!listTail_) {
        listTail_ = buf;
        listHead_ = listTail_;
    } else {
        listTail_->next = buf;
        listTail_ = buf;
    }

    return length;
}

int AtSdioTransport::readData(uint8_t* data, ssize_t len, unsigned int timeoutMsec) {
    if (data == nullptr || len < 0) {
        return -1;
    }

    if (len == 0) {
        return 0;
    }

    size_t toRead = len;
    size_t pos = 0;
    while (toRead > 0) {
        auto buf = listHead_;

        // We don't have any more data
        if (!buf) {
            break;
        }

        ssize_t canRead = std::min<size_t>(buf->leftLen, toRead);
        memcpy(data + pos, buf->pbuf + buf->pos, canRead);
        buf->pos += canRead;
        buf->leftLen -= canRead;
        pos += canRead;
        toRead -= canRead;

        if (buf->leftLen == 0) {
            // Can be given back
            {
                std::lock_guard<std::mutex> lock(mutex_);
                listHead_ = buf->next;
                buf->next = nullptr;

                if (!listHead_) {
                    listTail_ = nullptr;
                }
            }

            auto ret = sdio_slave_recv_load_buf(buf->handle);
            assert(ret == ESP_OK);
        }
    }

    rxData_ -= pos;

    return pos;
}

int AtSdioTransport::flushInput() {
    return 0;
}

int AtSdioTransport::writeData(const uint8_t* data, size_t len) {
    if (len <= 0 || data == nullptr) {
        return -1;
    }

    esp_err_t ret = ESP_FAIL;
    // Buffer should be in RAM/flash that can be used for DMA and 32-bit aligned
    if (!esp_ptr_dma_capable(data) || (reinterpret_cast<uintptr_t>(data) % 4) != 0) {
        // Cannot DMA directly, allocate a temporary buffer
        auto buf = (uint8_t*)heap_caps_malloc(len, MALLOC_CAP_DMA);
        if (buf == nullptr) {
            return -1;
        }
        memcpy(buf, data, len);
        ret = sdio_slave_transmit(buf, len);
        free(buf);
    } else {
        // We are lucky and we can use the buffer as-is
        ret = sdio_slave_transmit((uint8_t*)data, len);
    }

    return (ret == ESP_OK) ? len : -1;
}

int AtSdioTransport::getDataLength() const {
    return (int)rxData_;
}

int AtSdioTransport::waitWriteComplete(unsigned int timeoutMsec) {
    return 0;
}

int AtSdioTransport::statusChanged(esp_at_status_type status) {
    /* Nothing to do here, because we don't use any commands that switch to non-AT mode */
    return 0;
}

int AtSdioTransport::preDeepSleep() {
    return destroyTransport();
}

int AtSdioTransport::preRestart() {
    return destroyTransport();
}

void AtSdioTransport::run(void* arg) {
    auto self = static_cast<AtSdioTransport*>(arg);
    self->run();
    vTaskDelete(nullptr);
}

void AtSdioTransport::run() {
    LOG(INFO, "SDIO transport thread started");

    while (!exit_) {
        // receive data from SDIO host
        auto ret = fetchData(portMAX_DELAY);

        // notify length to AT core
        if (ret > 0) {
            rxData_ += ret;
            notifyReceivedData(ret, portMAX_DELAY);
        }
    }

    LOG(INFO, "SDIO transport thread exiting");

    exit_ = false;
}

} } /* particle::ncp */
