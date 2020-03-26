# Default platform
ifeq ("$(PLATFORM)","")
PLATFORM := atsom
$(info Use default PLATFORM: $(PLATFORM))
endif

ifeq ("$(PLATFORM)","argon")
export ESP_AT_MODULE_NAME ?= ARGON
else ifeq ("$(PLATFORM)","atsom")
export ESP_AT_MODULE_NAME ?= ATSOM
endif

$(info Build for platform: $(PLATFORM))
