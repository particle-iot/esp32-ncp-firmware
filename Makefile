include version.mk

PROJECT_NAME := argon-ncp-firmware-$(FIRMWARE_VERSION)

# Set default IDF_PATH
PROJECT_DIR := $(PWD)
THIRD_PARTY_DIR := $(PROJECT_DIR)/third_party

# esp32-at components
export ESP_AT_PROJECT_PATH := $(THIRD_PARTY_DIR)/esp32-at
EXTRA_COMPONENT_DIRS := $(THIRD_PARTY_DIR)/esp32-at/components
EXTRA_COMPONENT_DIRS += $(PROJECT_DIR)/gsm0710muxer

export IDF_PATH ?= $(ESP_AT_PROJECT_PATH)/esp-idf

include $(IDF_PATH)/make/project.mk
include moduleinfo.mk

CXXFLAGS += -std=c++14 -Wall -Werror

include factory.mk
