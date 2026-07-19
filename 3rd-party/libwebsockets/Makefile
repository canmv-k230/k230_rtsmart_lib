# libwebsockets - Makefile for RT-Smart SDK
# Wraps the upstream CMake build: delegates source selection and config
# generation to cmake, then copies the resulting static lib + headers.
# The upstream source is never modified.
#
# Upstream:  libwebsockets/
# Port cmake toolchain: port/riscv64.cmake

include ../../mkenv.mk
include $(SDK_RTSMART_SRC_DIR)/libs/mk/toolchain_riscv64_musl.mk

DIR          := $(shell basename `pwd`)
LOCAL_DIR    := $(shell pwd)
UPSTREAM     := $(LOCAL_DIR)/libwebsockets

LIB          := $(RTSMART_3RD_PARTY_LIB_INSTALL_PATH)/libwebsockets.a
BUILD_DIR    := $(SDK_RTSMART_BUILD_DIR)/libs/3rd-party/$(DIR)/build
STAGING      := $(SDK_RTSMART_BUILD_DIR)/libs/3rd-party/$(DIR)/staging

UPSTREAM_SRC := $(BUILD_DIR)/src
PATCH_DIR    := $(LOCAL_DIR)/port/patches
PATCHES      := $(sort $(wildcard $(PATCH_DIR)/*.patch))
OVERLAY_DIR  := $(LOCAL_DIR)/port/overlay
OVERLAY_FILES := $(shell find $(OVERLAY_DIR) -type f 2>/dev/null)
UPSTREAM_REV := $(shell git -C $(UPSTREAM) rev-parse HEAD 2>/dev/null)
SOURCE_STAMP := $(BUILD_DIR)/.source-$(UPSTREAM_REV)-patched
CMAKE_STAMP  := $(BUILD_DIR)/.cmake-configured

# ---- mbedtls dependency (already built by SDK) ----
MBEDTLS_BASE     := $(SDK_RTSMART_SRC_DIR)/libs/3rd-party/mbedtls
MBEDTLS_INC      := $(MBEDTLS_BASE)/mbedtls/include
MBEDTLS_PORT_INC := $(MBEDTLS_BASE)/port
MBEDTLS_PUFS_INC := $(SDK_RTSMART_SRC_DIR)/libs/rtsmart_hal/drivers/pufs
MBEDTLS_LIBDIR   := $(RTSMART_3RD_PARTY_LIB_INSTALL_PATH)
MBEDTLS_LIBS     := -Wl,--start-group;$(MBEDTLS_LIBDIR)/libmbedtls.a;$(MBEDTLS_LIBDIR)/libmbedx509.a;$(MBEDTLS_LIBDIR)/libmbedcrypto.a;$(MBEDTLS_LIBDIR)/libmbedtls_port.a;$(RTSMART_HAL_LIB_INSTALL_PATH)/libpufs.a;$(RTSMART_HAL_LIB_INSTALL_PATH)/libutils.a;-Wl,--end-group

# ---- cmake flags ----
CMAKE_TOOLCHAIN := $(LOCAL_DIR)/port/riscv64.cmake
CMAKE           := cmake

CMAKE_FLAGS := \
	-DCMAKE_TOOLCHAIN_FILE=$(CMAKE_TOOLCHAIN) \
	-DCMAKE_BUILD_TYPE=Release \
	-DDISABLE_WERROR=ON \
	-DLWS_PLAT_RTSMART=ON \
	-DLWS_WITH_SHARED=OFF \
	-DLWS_WITH_STATIC=ON \
	-DCMAKE_C_FLAGS="-DMBEDTLS_USER_CONFIG_FILE='\"mbedtls_port_config.h\"' -I$(MBEDTLS_PORT_INC) -I$(MBEDTLS_PUFS_INC) -I$(RTSMART_HAL_INC_INSTALL_PATH) -I$(LOCAL_DIR)/port -Wno-error" \
	-DCMAKE_INSTALL_PREFIX=$(STAGING) \
	-DLWS_WITH_MBEDTLS=ON \
	-DLWS_MBEDTLS_INCLUDE_DIRS="$(MBEDTLS_INC);$(MBEDTLS_PORT_INC);$(MBEDTLS_PUFS_INC)" \
	-DLWS_MBEDTLS_LIBRARIES="$(MBEDTLS_LIBS)" \
	-DLWS_WITH_POLL=ON \
	-DLWS_WITHOUT_EXTENSIONS=ON \
	-DLWS_WITH_PLUGINS=OFF \
	-DLWS_WITH_SECURE_STREAMS=OFF \
	-DLWS_WITH_JOSE=OFF \
	-DLWS_WITH_COSE=OFF \
	-DLWS_WITH_ZLIB=OFF \
	-DLWS_WITH_ZIP_FOPS=OFF \
	-DLWS_WITH_HTTP2=OFF \
	-DLWS_WITH_HTTP3=OFF \
	-DLWS_ROLE_QUIC=OFF \
	-DLWS_ROLE_H3=OFF \
	-DLWS_ROLE_WT=OFF \
	-DLWS_WITH_CGI=OFF \
	-DLWS_IPV6=OFF \
	-DLWS_WITH_UNIX_SOCK=OFF \
	-DLWS_WITH_NETLINK=OFF \
	-DLWS_WITH_BINDTODEVICE=OFF \
	-DLWS_WITH_LIBCAP=OFF \
	-DLWS_WITH_THREADPOOL=OFF \
	-DLWS_WITH_PEER_LIMITS=OFF \
	-DLWS_WITH_SOCKS5=OFF \
	-DLWS_WITH_SYS_ASYNC_DNS=OFF \
	-DLWS_WITHOUT_EVENTFD=ON \
	-DLWS_WITH_LEJP=ON \
	-DLWS_WITH_LWSAC=ON \
	-DLWS_WITH_DLO=OFF \
	-DLWS_WITH_LHP=OFF \
	-DLWS_WITH_JSONRPC=OFF \
	-DLWS_WITH_UPNG=OFF \
	-DLWS_WITH_JPEG=OFF \
	-DLWS_WITH_CONMON=OFF \
	-DLWS_WITH_WOL=OFF \
	-DLWS_WITH_CACHE_NSCOOKIEJAR=OFF \
	-DLWS_WITH_DIR=OFF \
	-DLWS_WITH_LEJP_CONF=OFF \
	-DLWS_WITHOUT_TESTAPPS=ON \
	-DLWS_WITHOUT_TEST_SERVER=ON \
	-DLWS_WITH_SELFTESTS=OFF \
	-DLWS_WITH_MINIMAL_EXAMPLES=OFF \
	-DLWS_WITH_GENCRYPTO=ON \
	-DLWS_WITH_TLS_SESSIONS=ON \
	-DLWS_WITH_NETWORK=ON \
	-DLWS_WITH_SERVER=ON \
	-DLWS_WITH_CLIENT=ON \
	-DLWS_ROLE_WS=ON \
	-DLWS_ROLE_H1=ON \
	-DLWS_ROLE_RAW=ON \
	-DLWS_ROLE_RAW_FILE=ON \
	-DLWS_SUPPRESS_DEPRECATED_API_WARNINGS=ON \
	-DLWS_WITH_FILE_OPS=ON \
	-DLWS_WITH_UDP=ON \
	-DLWS_HAVE_MBEDTLS_NET_SOCKETS=ON \
	-DLWS_HAVE_MBEDTLS_SSL_NEW_SESSION_TICKET=ON

.PHONY: all clean distclean cmake-configure

all: $(LIB)
	@$(ECHO) [BUILD] libs 3rd-party $(DIR) done.

# ---- prepare patched source + cmake configure ----
$(SOURCE_STAMP): Makefile $(PATCH_DIR) $(PATCHES) $(OVERLAY_DIR) $(OVERLAY_FILES) | $(BUILD_DIR)
	@echo "[SOURCE] preparing patched $(DIR) source ..."
	@if [ -z "$(UPSTREAM_REV)" ]; then echo "[ERROR] $(UPSTREAM) is not a git checkout"; exit 1; fi
	@rm -rf $(UPSTREAM_SRC)
	@$(MKDIR) -p $(UPSTREAM_SRC)
	@git -C $(UPSTREAM) archive --format=tar $(UPSTREAM_REV) | tar -xf - -C $(UPSTREAM_SRC) || exit $$?
	@if [ -d "$(OVERLAY_DIR)" ]; then cp -a "$(OVERLAY_DIR)/." "$(UPSTREAM_SRC)/"; fi
	@set -e; \
	if [ -n "$(PATCHES)" ]; then \
		for p in $(PATCHES); do \
			echo "[PATCH] $$(basename $$p)"; \
			patch -d $(UPSTREAM_SRC) -p1 < $$p; \
		done; \
	fi
	@rm -f $(BUILD_DIR)/.source-*-patched
	@touch $@

$(CMAKE_STAMP): $(SOURCE_STAMP) $(CMAKE_TOOLCHAIN) | $(BUILD_DIR)
	@echo "[CMAKE] configuring $(DIR) ..."
	@cd $(BUILD_DIR) && $(CMAKE) --fresh $(CMAKE_FLAGS) $(UPSTREAM_SRC) || exit $$?
	@touch $@

cmake-configure: $(CMAKE_STAMP)

# ---- build via cmake ----
$(BUILD_DIR)/lib/libwebsockets.a: $(CMAKE_STAMP)
	@echo "[CMAKE] building $(DIR) ..."
	@$(CMAKE) --build $(BUILD_DIR) --target websockets -- -j$$(nproc) || exit $$?

# ---- copy static lib + install headers ----
$(LIB): $(BUILD_DIR)/lib/libwebsockets.a
	@echo "[INSTALL] $@"
	@cp $(BUILD_DIR)/lib/libwebsockets.a $@
	@$(MKDIR) -p $(RTSMART_3RD_PARTY_INC_INSTALL_PATH)/libwebsockets/libwebsockets
	@cp $(UPSTREAM_SRC)/include/libwebsockets.h $(RTSMART_3RD_PARTY_INC_INSTALL_PATH)/libwebsockets/
	@cp $(UPSTREAM_SRC)/include/libwebsockets/*.h $(RTSMART_3RD_PARTY_INC_INSTALL_PATH)/libwebsockets/libwebsockets/
	@cp $(BUILD_DIR)/lws_config.h $(RTSMART_3RD_PARTY_INC_INSTALL_PATH)/libwebsockets/

$(BUILD_DIR):
	@$(MKDIR) -p $@

# ---- clean ----
clean:
	@rm -rf $(BUILD_DIR) $(STAGING)

distclean: clean
	@rm -f $(LIB)
