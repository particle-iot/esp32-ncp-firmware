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
#include "util.h"

namespace particle { namespace ncp {

#define container_of(ptr, type, member) ({      \
    const decltype( ((type *)0)->member ) *__mptr = (const decltype( ((type *)0)->member ) *)ptr; \
    (type *)( (char *)__mptr - ((size_t) &((type *)0)->member) );})

namespace {

const auto AT_SDIO_WAKE_UP_PERIOD_MS = 1000;

} // anonymous

AtSdioTransport::AtSdioTransport()
        : AtTransportBase(),
          listHead_(nullptr),
          listTail_(nullptr),
          started_(false),
          exit_(false),
          rxThread_(nullptr),
          txThread_(nullptr),
          txBuf_(txBufData_, sizeof(txBufData_)),
          transmitting_(false) {
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
    transmitting_ = false;
    txBuf_.reset();
    exit_ = 0;

    for (int loop = 0; loop < AT_SDIO_BUFFER_NUM; loop++) {
        handle = sdio_slave_recv_register_buf(list_[loop].pbuf);
        assert(handle != nullptr);

        ret = sdio_slave_recv_load_buf(handle);
        assert(ret == ESP_OK);
    }

    sdio_slave_set_host_intena(SDIO_SLAVE_HOSTINT_SEND_NEW_PACKET | SDIO_SLAVE_HOSTINT_BIT0);

    sdio_slave_start();

    // FIXME: we have to start two threads unfortunately since it's impossible to wait for both
    // rx and tx events and the same. In order to avoid busy loop, we'll sacrifice 4k of stack here
    // and having to run an extra thread.
    if (xTaskCreate([](void* arg) -> void {
                auto self = static_cast<AtSdioTransport*>(arg);
                self->rxRun();
                vTaskDelete(nullptr);
            }, "at_sdio_rx_t", 4096, this, tskIDLE_PRIORITY + 8, &rxThread_) != pdPASS) {
        destroyTransport();
    }

    if (xTaskCreate([](void* arg) -> void {
                auto self = static_cast<AtSdioTransport*>(arg);
                self->txRun();
                vTaskDelete(nullptr);
            }, "at_sdio_tx_t", 4096, this, tskIDLE_PRIORITY + 8, &txThread_) != pdPASS) {
        destroyTransport();
    }

    started_ = true;

    return 0;
}

int AtSdioTransport::destroyTransport() {
    LOG(INFO, "Deinitializing SDIO transport");
    if (rxThread_ || txThread_) {
        exit_ = (rxThread_ != nullptr) + (txThread_ != nullptr);

        LOG(INFO, "Waiting for SDIO threads to stop");

        /* Join thread */
        while (exit_) {
            vTaskDelay(100 / portTICK_PERIOD_MS);
        }

        rxThread_ = nullptr;
        txThread_ = nullptr;

        LOG(INFO, "SDIO threads stopped");
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

    auto ret = sdio_slave_recv(&handle, &ptr, &length, timeoutMsec / portTICK_PERIOD_MS);
    if (ret == ESP_ERR_TIMEOUT) {
        return RESULT_TIMEOUT;
    }
    CHECK_ESP(ret);
    Buffer* buf = container_of(ptr, Buffer, pbuf); // get struct list pointer

    buf->handle = handle;
    buf->leftLen = length;
    buf->pos = 0;
    buf->next = nullptr;

    std::lock_guard<std::mutex> lock(rxMutex_);
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
        return RESULT_ERROR;
    }

    if (len == 0) {
        return 0;
    }

    size_t toRead = len;
    size_t pos = 0;
    while (toRead > 0) {
        Buffer* buf = (Buffer*)(listHead_);

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
                std::lock_guard<std::mutex> lock(rxMutex_);
                listHead_ = buf->next;
                buf->next = nullptr;

                if (!listHead_) {
                    listTail_ = nullptr;
                }
            }

            auto ret = sdio_slave_recv_load_buf(buf->handle);
            assert(ret == ESP_OK);

            // Make sure to notify the host that we have a new buffer available
            sdio_slave_send_host_int(HOST_SLC0_TOHOST_BIT0_INT_ENA_S);
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
        return RESULT_ERROR;
    }

    std::lock_guard<std::recursive_mutex> lock(txMutex_);
    const size_t canWrite = CHECK(txBuf_.space());
    const size_t willWrite = std::min(canWrite, len);

    if (willWrite > 0) {
        CHECK(txBuf_.put(data, willWrite));
    }

    CHECK(startTransmission());

    return willWrite;
}

int AtSdioTransport::startTransmission() {
    std::lock_guard<std::recursive_mutex> lock(txMutex_);
    size_t consumable = std::min<size_t>(txBuf_.consumable(), CONFIG_AT_SDIO_BLOCK_SIZE);
    if (transmitting_ || consumable == 0) {
        return 0;
    }

    size_t align = (consumable % 4);
    if (align) {
        align = sizeof(uint32_t) - align;

        if (txBuf_.space() < align) {
            return RESULT_NO_MEMORY;
        }

        // Dummy write to enforce alignment
        CHECK(txBuf_.put(nullptr, align));
        consumable = txBuf_.consumable();
    }

    if (consumable % 4 != 0) {
        return RESULT_ERROR;
    }

    auto ptr = txBuf_.consume(consumable);
    assert(esp_ptr_dma_capable(ptr));

    esp_err_t ret = sdio_slave_send_queue((uint8_t*)ptr, consumable - align, (void*)consumable, 0);
    if (ret != ESP_OK) {
        // Cancel
        txBuf_.consumeCommit(0, consumable);
    }
    CHECK_ESP(ret);

    transmitting_ = true;

    return 0;
}

int AtSdioTransport::waitTransmissionFinished(unsigned int timeoutMsec) {
    size_t consume = 0;
    auto ret = sdio_slave_send_get_finished((void**)&consume, timeoutMsec / portTICK_PERIOD_MS);
    if (ret == ESP_ERR_TIMEOUT) {
        return RESULT_TIMEOUT;
    }
    CHECK_ESP(ret);
    {
        std::lock_guard<std::recursive_mutex> lock(txMutex_);
        txBuf_.consumeCommit(consume);
        transmitting_ = false;
    }
    return 0;
}

int AtSdioTransport::getDataLength() const {
    return (int)rxData_;
}

int AtSdioTransport::waitWriteComplete(unsigned int timeoutMsec) {
    // FIXME: busy-ish loop
    auto start = util::millis();
    while (util::millis() - start < timeoutMsec) {
        while(transmitting_) {
            vTaskDelay(1 / portTICK_PERIOD_MS);
        }
        {
            std::lock_guard<std::recursive_mutex> lock(txMutex_);
            if (txBuf_.empty()) {
                return 0;
            }
        }
    }
    return RESULT_TIMEOUT;
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

void AtSdioTransport::rxRun() {
    LOG(INFO, "SDIO transport RX thread started");

    while (!exit_) {
        // receive data from SDIO host
        auto ret = fetchData(AT_SDIO_WAKE_UP_PERIOD_MS);

        // notify length to AT core
        if (ret > 0) {
            rxData_ += ret;
            notifyReceivedData(ret, AT_SDIO_WAKE_UP_PERIOD_MS);
        }
    }

    LOG(INFO, "SDIO transport RX thread exiting");

    exit_--;
}

void AtSdioTransport::txRun() {
    LOG(INFO, "SDIO transport TX thread started");

    while (!exit_) {
        if (!waitTransmissionFinished(AT_SDIO_WAKE_UP_PERIOD_MS)) {
            startTransmission();
        }
    }

    LOG(INFO, "SDIO transport TX thread exiting");

    // Just in case
    {
        std::lock_guard<std::recursive_mutex> lock(txMutex_);
        txBuf_.reset();
        transmitting_ = false;
    }
    exit_--;
}

} } /* particle::ncp */
