PROJECT_NAME := argon-ncp-firmware

# Set default IDF_PATH
PROJECT_DIR := $(PWD)
THIRD_PARTY_DIR := $(PROJECT_DIR)/third_party

# esp32-at components
export ESP_AT_PROJECT_PATH := $(THIRD_PARTY_DIR)/esp32-at
EXTRA_COMPONENT_DIRS := $(THIRD_PARTY_DIR)/esp32-at/components

export IDF_PATH ?= $(ESP_AT_PROJECT_PATH)/esp-idf

include $(IDF_PATH)/make/project.mk
include moduleinfo.mk

CXXFLAGS += -std=c++14 -Wall -Werror
