# Default platform
ifeq ("$(PLATFORM)","")
$(error PLATFORM was not specified)
endif

ifeq ("$(PLATFORM)","argon")
export ESP_AT_MODULE_NAME ?= ARGON
CPPFLAGS += -DPLATFORM_ID=12
else ifeq ("$(PLATFORM)","tracker")
export ESP_AT_MODULE_NAME ?= TRACKER
CPPFLAGS += -DPLATFORM_ID=26
endif

$(info Build for platform: $(PLATFORM))
