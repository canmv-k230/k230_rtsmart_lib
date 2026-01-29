ifneq ($(MKENV_INCLUDED),1)
export SDK_SRC_ROOT_DIR := $(realpath $(dir $(realpath $(lastword $(MAKEFILE_LIST))))/../../../)
endif

include $(SDK_SRC_ROOT_DIR)/tools/mkenv.mk

include $(SDK_SRC_ROOT_DIR)/.config

# for 3rd-party self test install
RTSMART_LIBS_3RD_PARTY_ELF_INSTALL_PATH := $(SDK_RTSMART_SRC_DIR)/examples/elf/3rd_party/

RTSMART_HAL_LIB_INSTALL_PATH := $(SDK_RTSMART_BUILD_DIR)/libs/rtsmart_hal/lib
RTSMART_HAL_INC_INSTALL_PATH := $(SDK_RTSMART_BUILD_DIR)/libs/rtsmart_hal/include

RTSMART_3RD_PARTY_LIB_INSTALL_PATH := $(SDK_RTSMART_BUILD_DIR)/libs/3rd-party/lib
RTSMART_3RD_PARTY_INC_INSTALL_PATH := $(SDK_RTSMART_BUILD_DIR)/libs/3rd-party/include

export MKENV_INCLUDED_RTSMART_LIBS=1
