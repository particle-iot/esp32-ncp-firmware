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

#include "at_transport_sdio.h"
#include "logging.h"

namespace particle { namespace ncp {

static const char* TAG = "SDIO-AT";

#define container_of(ptr, type, member) ({      \
    const decltype( ((type *)0)->member ) *__mptr = (const decltype( ((type *)0)->member ) *)ptr; \
    (type *)( (char *)__mptr - ((size_t) &((type *)0)->member) );})

AtSdioTransport::AtSdioTransport()
        : AtTransportBase(),
          listHead_(nullptr),
          listTail_(nullptr),
          semahandle_(nullptr),
          started_(false),
          exit_(false),
          thread_(nullptr) {
}

AtSdioTransport::~AtSdioTransport() {
}

int AtSdioTransport::initTransport()  {
    LOG(INFO, "Initializing SDIO transport");
    sdio_slave_config_t config = {};
    config.sending_mode        = SDIO_SLAVE_SEND_STREAM;
    config.send_queue_size     = ESP_AT_SDIO_QUEUE_SIZE;
    config.recv_buffer_size    = ESP_AT_SDIO_BUFFER_SIZE;

    sdio_slave_buf_handle_t handle;

    semahandle_ = xSemaphoreCreateMutex();
    esp_err_t ret = sdio_slave_initialize(&config);
    assert(ret == ESP_OK);

    for (int loop = 0; loop < ESP_AT_SDIO_BUFFER_NUM; loop++) {
        handle = sdio_slave_recv_register_buf(list_[loop].pbuf);
        assert(handle != NULL);

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

    // Destroy semaphore
    vSemaphoreDelete(semahandle_);

    // Deinitialize SDIO pins
    sdio_slave_deinit();

    LOG(INFO, "SDIO transport deinitialized");

    return 0;
}

int AtSdioTransport::postInitTransport() {
    return 0;
}

int AtSdioTransport::readData(uint8_t* data, ssize_t len, unsigned int timeoutMsec) {
    uint32_t copy_len = 0;
    if (data == NULL || len < 0) {
        ESP_LOGI(TAG , "Cannot get read data address.");
        return -1;
    }

    if (len == 0) {
        ESP_LOGI(TAG , "Empty read data.");
        return 0;
    }

    if (listHead_ == NULL) {
        return 0;
    }

    while (copy_len < len) {
        if (!listHead_) {
            break;
        }

        esp_at_sdio_list_t* p_list = listHead_;

        if (len < p_list->left_len) {
            memcpy(data, p_list->pbuf + p_list->pos, len);
            p_list->pos += len;
            p_list->left_len -= len;
            copy_len += len;
        } else {
            memcpy(data, p_list->pbuf + p_list->pos, p_list->left_len);
            p_list->pos += p_list->left_len;
            copy_len += p_list->left_len;
            p_list->left_len = 0;
            xSemaphoreTake(semahandle_, portMAX_DELAY);
            listHead_ = p_list->next;
            p_list->next = NULL;

            if (!listHead_) {
                listTail_ = NULL;
            }

            xSemaphoreGive(semahandle_);

            sdio_slave_recv_load_buf(p_list->handle);
        }
    }

    return copy_len;
}

int AtSdioTransport::flushInput() {
    return 0;
}

int AtSdioTransport::writeData(const uint8_t* data, size_t len) {
    esp_err_t ret = ESP_OK;
    int32_t length = len;
    uint8_t* sendbuf = NULL;
    if (len <= 0 || data == NULL) {
        ESP_LOGE(TAG , "Write data error, len:%d", len);
        return -1;
    }

    sendbuf = (uint8_t*)heap_caps_malloc(len, MALLOC_CAP_DMA);
    if (sendbuf == NULL) {
        ESP_LOGE(TAG , "Malloc send buffer fail!");
        return 0;
    }

    memcpy(sendbuf, data, length);

    ret = sdio_slave_transmit(sendbuf, len);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG , "sdio slave transmit error, ret : 0x%x\r\n", ret);
        length = 0;
    }

    free(sendbuf);
    return length;
}

int AtSdioTransport::getDataLength() const {
    uint32_t copy_len = 0;

    if (listHead_ == NULL) {
        return 0;
    }

    esp_at_sdio_list_t* tmpHead = listHead_;
    while (tmpHead) {
        esp_at_sdio_list_t* p_list = tmpHead;
        copy_len += p_list->left_len;
        tmpHead = p_list->next;
    }

    return copy_len;
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

    sdio_slave_buf_handle_t handle;
    size_t length = 0;
    uint8_t* ptr = NULL;

    while (!exit_) {
        // receive data from SDIO host
        esp_err_t ret = sdio_slave_recv(&handle, &ptr, &length, portMAX_DELAY);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Recv error,ret:%x", ret);
            continue;
        }

        esp_at_sdio_list_t* p_list = container_of(ptr, esp_at_sdio_list_t, pbuf); // get struct list pointer

        p_list->handle = handle;
        p_list->left_len = length;
        p_list->pos = 0;
        p_list->next = NULL;
        xSemaphoreTake(semahandle_, portMAX_DELAY);

        if (!listTail_) {
            listTail_ = p_list;
            listHead_ = listTail_;
        } else {
            listTail_->next = p_list;
            listTail_ = p_list;
        }

        xSemaphoreGive(semahandle_);

        // notify length to AT core
        esp_at_port_recv_data_notify(length, portMAX_DELAY);
    }

    LOG(INFO, "SDIO transport thread exiting");

    exit_ = false;
}

} } /* particle::ncp */
