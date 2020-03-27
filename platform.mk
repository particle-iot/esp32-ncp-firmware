# Default platform
ifeq ("$(PLATFORM)","")
PLATFORM := tracker
$(info Use default PLATFORM: $(PLATFORM))
endif

ifeq ("$(PLATFORM)","argon")
export ESP_AT_MODULE_NAME ?= ARGON
CXXFLAGS += -DPLATFORM_ID=12
else ifeq ("$(PLATFORM)","tracker")
export ESP_AT_MODULE_NAME ?= TRACKER
CXXFLAGS += -DPLATFORM_ID=26
endif

$(info Build for platform: $(PLATFORM))
