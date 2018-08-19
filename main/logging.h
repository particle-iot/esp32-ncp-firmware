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

#pragma once

#include <esp_log.h>

#define LOG_TAG "ncp" // FIXME

#define LOG(_level, _fmt, ...) \
        ESP_LOG_LEVEL_LOCAL((esp_log_level_t)::particle::LOG_LEVEL_##_level, LOG_TAG, _fmt, ##__VA_ARGS__)

#ifndef NDEBUG
#define LOG_DEBUG(_level, _fmt, ...) \
        LOG(_level, _fmt, ##__VA_ARGS__)
#else
#define LOG_DEBUG(_level, _fmt, ...)
#endif

namespace particle {

enum LogLevel {
    LOG_LEVEL_TRACE = ESP_LOG_DEBUG,
    LOG_LEVEL_INFO = ESP_LOG_INFO,
    LOG_LEVEL_WARN = ESP_LOG_WARN,
    LOG_LEVEL_ERROR = ESP_LOG_ERROR
};

} // particle
