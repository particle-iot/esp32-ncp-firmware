# Default platform
ifeq ("$(PLATFORM)","")
PLATFORM := atsom
$(info Use default PLATFORM: $(PLATFORM))
endif

ifeq ("$(PLATFORM)","argon")
export ESP_AT_MODULE_NAME ?= ARGON
CXXFLAGS += -DPLATFORM_ID=12
else ifeq ("$(PLATFORM)","atsom")
export ESP_AT_MODULE_NAME ?= ATSOM
CXXFLAGS += -DPLATFORM_ID=26
endif

$(info Build for platform: $(PLATFORM))
