# module version as a C literal
MODULE_VERSION?=5
# module version as hex string in little endian format
MODULE_VERSION_LE?=0500

FIRMWARE_VERSION?=0.0.5

CXXFLAGS += -DARGON_FIRMWARE_VERSION=$(FIRMWARE_VERSION)
CXXFLAGS += -DARGON_FIRMWARE_MODULE_VERSION=$(MODULE_VERSION)
