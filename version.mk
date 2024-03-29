# module version as a C literal
MODULE_VERSION?=8
# module version as hex string in little endian format
MODULE_VERSION_LE?=0800

FIRMWARE_VERSION?=0.0.8

CPPFLAGS += -DESP32_NCP_FIRMWARE_VERSION=$(FIRMWARE_VERSION)
CPPFLAGS += -DESP32_NCP_FIRMWARE_MODULE_VERSION=$(MODULE_VERSION)

ifeq ("$(PLATFORM)","tracker")
# Dependency on system-part1 version 3000 (3.0.0-alpha.1)
MODULE_DEPENDENCY=0401b80b
else
MODULE_DEPENDENCY=00000000
endif

MODULE_DEPENDENCY2=00000000
