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

#include "at_transport_uart.h"

namespace particle { namespace ncp {

AtUartTransport::AtUartTransport(const Config& conf)
        : AtTransportBase(),
          conf_(conf),
          exit_(false) {

}

AtUartTransport::~AtUartTransport() {
}

int AtUartTransport::initTransport()  {
    LOG(INFO, "Initializing UART transport");
    CHECK_ESP(uart_param_config(conf_.uart, &conf_.config));
    CHECK_ESP(uart_set_pin(conf_.uart, conf_.txPin, conf_.rxPin, conf_.rtsPin, conf_.ctsPin));
    CHECK_ESP(uart_driver_install(conf_.uart, 2048, 2048, 30, &queue_, 0));

    if (xTaskCreate(run, "at_uart_t", 8192, this, tskIDLE_PRIORITY + 1, &thread_) != pdPASS) {
        destroyTransport();
    }

    started_ = true;

    return 0;
}

int AtUartTransport::destroyTransport() {
    LOG(INFO, "Deinitializing UART transport");
    if (thread_) {
        exit_ = true;

        if (queue_) {
            uart_event_t ev = {};
            ev.type = (uart_event_type_t)TRANSPORT_EVENT_EXIT;
            xQueueSend(queue_, &ev, portMAX_DELAY);
        }

        LOG(INFO, "Waiting for UART thread to stop");

        /* Join thread */
        while (exit_) {
            vTaskDelay(100 / portTICK_PERIOD_MS);
        }

        thread_ = nullptr;

        LOG(INFO, "UART thread stopped");
    }

    waitWriteComplete(portMAX_DELAY);

    queue_ = nullptr;
    started_ = false;

    uart_driver_delete(conf_.uart);

    gpio_set_direction((gpio_num_t)conf_.txPin, GPIO_MODE_INPUT);
    gpio_set_direction((gpio_num_t)conf_.rxPin, GPIO_MODE_INPUT);
    gpio_set_direction((gpio_num_t)conf_.rtsPin, GPIO_MODE_INPUT);
    gpio_set_direction((gpio_num_t)conf_.ctsPin, GPIO_MODE_INPUT);

    LOG(INFO, "UART transport deinitialized");

    return 0;
}

int AtUartTransport::postInitTransport() {
    return 0;
}

int AtUartTransport::readData(uint8_t* data, ssize_t len, unsigned int timeoutMsec) {
    if (!data || len < 0 || !started_) {
        return -1;
    }

    if (len == 0) {
        return 0;
    }

    return uart_read_bytes(conf_.uart, data, len, timeoutMsec / portTICK_PERIOD_MS);
}

int AtUartTransport::flushInput() {
    return uart_flush_input(conf_.uart);
}

int AtUartTransport::writeData(const uint8_t* data, size_t len) {
    if (!started_) {
        return -1;
    }
    return uart_write_bytes(conf_.uart, (const char*)data, len);
}

int AtUartTransport::getDataLength() const {
    if (!started_) {
        return -1;
    }

    size_t size = 0;
    uart_get_buffered_data_len(conf_.uart, &size);
    return size;
}

int AtUartTransport::waitWriteComplete(unsigned int timeoutMsec) {
    if (!started_) {
        return -1;
    }

    /* Avoid multiplication here */
    const unsigned int t = timeoutMsec != portMAX_DELAY ? timeoutMsec / portTICK_PERIOD_MS : timeoutMsec;
    return CHECK_ESP(uart_wait_tx_done(conf_.uart, t));
}

int AtUartTransport::statusChanged(esp_at_status_type status) {
    /* Nothing to do here, because we don't use any commands that switch to non-AT mode */
    return 0;
}

int AtUartTransport::preDeepSleep() {
    return destroyTransport();
}

int AtUartTransport::preRestart() {
    return destroyTransport();
}

void AtUartTransport::run(void* arg) {
    auto self = static_cast<AtUartTransport*>(arg);
    self->run();
    vTaskDelete(nullptr);
}

void AtUartTransport::run() {
    LOG(INFO, "UART transport thread started");

    while (!exit_) {
        uart_event_t event = {};
        if (xQueueReceive(queue_, (void*)&event, portMAX_DELAY)) {
            switch (event.type) {
                case UART_DATA:
                case UART_BUFFER_FULL: {
                    notifyReceivedData(event.size, portMAX_DELAY);
                    break;
                }

                default: {
                    /* Unhandled event */
                    break;
                }
            }
        }
    }

    LOG(INFO, "UART transport thread exiting");

    exit_ = false;
}

} } /* particle::ncp */
