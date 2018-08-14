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

#ifndef ARGON_NCP_FIRMWARE_UTIL_H
#define ARGON_NCP_FIRMWARE_UTIL_H

#include <esp_log.h>

#ifdef __cplusplus

namespace particle { namespace util {

int nvsInitialize();

} } /* particle::util */

extern "C" {
#endif /* __cplusplus */

/* FIXME */
#define LOG_TAG "ncp"

#define LOG(_level, _fmt, ...) \
        ESP_LOG_LEVEL_LOCAL(ESP_LOG_##_level, LOG_TAG, _fmt, ##__VA_ARGS__)

#define CHECK(_expr) \
        ({ \
            const auto _ret = _expr; \
            if (_ret < 0) { \
                LOG(ERROR, #_expr " failed: %d", _ret); \
                return _ret; \
            } \
            _ret; \
        })

#define CHECK_BOOL(_expr) \
        ({ \
            const auto _ret = _expr; \
            if (!_ret) { \
                LOG(ERROR, #_expr " failed"); \
                return -1; \
            } \
            0; \
        })

#define ESP_ERROR_TO_NEGATIVE(_err) \
        (_err > 0 ? -_err : _err)

#define CHECK_ESP(_expr) \
        ({ \
            const auto _ret = _expr; \
            if (_ret != ESP_OK) { \
                LOG(ERROR, #_expr " failed: %s", esp_err_to_name(_ret)); \
                return ESP_ERROR_TO_NEGATIVE(_ret); \
            } \
            _ret; \
        })

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* ARGON_NCP_FIRMWARE_UTIL_H */
