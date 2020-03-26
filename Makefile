include version.mk
include platform.mk

PROJECT_NAME := esp-at-$(PLATFORM)-$(FIRMWARE_VERSION)
export ESP_AT_PROJECT_PLATFORM ?= PLATFORM_ESP32

# Generate sdkconfig
# $(info Generate sdkconfig for $(ESP_AT_MODULE_NAME))
# $(shell make -f Makefile deconfig)

# Set default IDF_PATH
PROJECT_DIR := $(PWD)
THIRD_PARTY_DIR := $(PROJECT_DIR)/third_party


# esp32-at components
export ESP_AT_PROJECT_PATH := $(THIRD_PARTY_DIR)/esp32-at
EXTRA_COMPONENT_DIRS += $(THIRD_PARTY_DIR)/esp32-at/components
EXTRA_COMPONENT_DIRS += $(PROJECT_DIR)/gsm0710muxer

export ESP_AT_IMAGE_DIR ?= $(ESP_AT_PROJECT_PATH)/components/fs_image
EXTRA_COMPONENT_DIRS += $(ESP_AT_PROJECT_PATH)/tools/mkfatfs

export IDF_PATH ?= $(ESP_AT_PROJECT_PATH)/esp-idf

# add CFLAGS depends on platform
ifeq ($(ESP_AT_PROJECT_PLATFORM), PLATFORM_ESP8266)
EXTRA_CFLAGS += -DconfigUSE_QUEUE_SETS
else ifeq ($(ESP_AT_PROJECT_PLATFORM), PLATFORM_ESP32)
EXTRA_CFLAGS += -DCONFIG_TARGET_PLATFORM_ESP32=1
endif

export SILENCE ?=
ifeq ($(SILENCE), 1)
EXTRA_CFLAGS += -DNDEBUG
endif

ESP_AT_PROJECT_COMMIT_ID := $(shell git rev-parse --short HEAD)
EXTRA_CFLAGS += -DESP_AT_PROJECT_COMMIT_ID=\"$(ESP_AT_PROJECT_COMMIT_ID)\"

ESP_AT_MODULE_CONFIG_DIR ?= module_config/module_$(shell echo $(ESP_AT_MODULE_NAME) | tr A-Z a-z)

ifeq (,$(wildcard $(ESP_AT_MODULE_CONFIG_DIR))) ## if there is no module config, we use platform default config
$(error There is no $(ESP_AT_MODULE_CONFIG_DIR))
endif

ifeq ($(SILENCE), 1)
SDKCONFIG_DEFAULTS := $(ESP_AT_MODULE_CONFIG_DIR)/sdkconfig_silence.defaults
else
SDKCONFIG_DEFAULTS := $(ESP_AT_MODULE_CONFIG_DIR)/sdkconfig.defaults
endif

export PROJECT_VER = "ESP-AT"

ifeq ("$(filter 3.81 3.82,$(MAKE_VERSION))","") ## IDF just support 3.81,3.82 or 4.x newer
include $(IDF_PATH)/make/project.mk
else
ifeq (,$(wildcard $(IDF_PATH)))
ifeq (,$(MAKECMDGOALS))
all:
    @make $(MAKECMDGOALS)
else
$(word 1,$(MAKECMDGOALS)):
    @make $(MAKECMDGOALS)
endif
else
include $(IDF_PATH)/make/project.mk
endif
endif

include moduleinfo.mk

CXXFLAGS += -std=c++14 -Wall -Werror

include factory.mk
