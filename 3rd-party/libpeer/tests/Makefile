.DEFAULT_GOAL := test

CC ?= cc
BUILD ?= build
SANITIZERS ?= -fsanitize=address,undefined
CPPFLAGS += -DMBEDTLS_SSL_DTLS_SRTP -I../src -I../../mbedtls/mbedtls/include \
	-I../3rd-party/libsrtp/include -I../3rd-party/usrsctp/usrsctplib \
	-I../../../rtsmart_hal/drivers/pufs -I../../cjson/cJSON
CFLAGS ?= -O1 -g
CFLAGS += -std=gnu99 -ffunction-sections -fdata-sections -pthread $(SANITIZERS)
LDFLAGS += -Wl,--gc-sections -pthread $(SANITIZERS)

HEADERS := Makefile $(wildcard ../src/*.h) ../../../rtsmart_hal/drivers/pufs/drv_pufs.h
RANDOM_SOURCES := test_random.c ../src/peer.c ../src/utils.c ../src/agent.c ../src/stun.c \
	../../mbedtls/port/entropy_poll.c
RANDOM_WRAP := -Wl,--wrap=drv_pufs_open,--wrap=drv_pufs_rng_read,--wrap=drv_pufs_close,--wrap=getrandom
TESTS := $(BUILD)/test_ice_candidates $(BUILD)/test_random_hwrng $(BUILD)/test_random_posix \
	$(BUILD)/test_dtls_receive $(BUILD)/test_crypto_config

.PHONY: test clean
test: $(TESTS)
	$(abspath $(BUILD))/test_ice_candidates
	$(abspath $(BUILD))/test_random_hwrng
	$(abspath $(BUILD))/test_random_posix
	$(abspath $(BUILD))/test_dtls_receive
	$(abspath $(BUILD))/test_crypto_config

$(BUILD):
	mkdir -p $@

$(BUILD)/test_ice_candidates: test_ice_candidates.c ../src/peer_connection.c ../src/agent.c ../src/ice.c ../src/address.c $(HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) test_ice_candidates.c ../src/agent.c ../src/ice.c ../src/address.c $(LDFLAGS) -o $@

$(BUILD)/test_random_hwrng: $(RANDOM_SOURCES) $(HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DCONFIG_USE_HWRNG=1 -DMBEDTLS_ENTROPY_HARDWARE_ALT $(RANDOM_SOURCES) $(LDFLAGS) $(RANDOM_WRAP) -o $@

$(BUILD)/test_random_posix: $(RANDOM_SOURCES) $(HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DCONFIG_USE_HWRNG=0 $(RANDOM_SOURCES) $(LDFLAGS) $(RANDOM_WRAP) -o $@

$(BUILD)/test_dtls_receive: test_dtls_receive.c ../src/peer_connection.c $(HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(LDFLAGS) -o $@

$(BUILD)/test_crypto_config: test_crypto_config.c ../../mbedtls/port/mbedtls_port_config.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(LDFLAGS) -o $@

clean:
	rm -rf $(BUILD)

MBEDTLS_BUILD ?= $(abspath $(BUILD))/mbedtls
SRTP_LIBRARY ?= $(abspath $(BUILD))/srtp-lib/libsrtp2.a
PEER_SOURCES := $(addprefix ../src/,agent.c address.c base64.c dtls_srtp.c ice.c peer.c \
	ports.c rtcp.c rtp.c sctp.c sdp.c socket.c stun.c utils.c)

.PHONY: integration
integration: test $(BUILD)/test_dtls_peers
	$(abspath $(BUILD))/test_dtls_peers

$(MBEDTLS_BUILD)/library/libmbedtls.a: Makefile
	cmake -S ../../mbedtls/mbedtls -B $(MBEDTLS_BUILD) -DENABLE_PROGRAMS=OFF -DENABLE_TESTING=OFF \
		-DCMAKE_C_FLAGS=-DMBEDTLS_SSL_DTLS_SRTP
	cmake --build $(MBEDTLS_BUILD) -j4

$(SRTP_LIBRARY):
	$(MAKE) -C ../3rd-party/libsrtp all CC='$(CC)' AR=ar \
		BUILD='$(abspath $(BUILD))/srtp' LIB_DIR='$(dir $(SRTP_LIBRARY))' \
		CFLAGS='-std=gnu99 -O1 -g -DHAVE_CONFIG_H -D_GNU_SOURCE -I. -Iinclude -Icrypto/include'

$(BUILD)/test_dtls_peers: test_dtls_peers.c ../src/peer_connection.c $(PEER_SOURCES) $(HEADERS) \
	$(MBEDTLS_BUILD)/library/libmbedtls.a $(SRTP_LIBRARY) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DCONFIG_USE_USRSCTP=0 test_dtls_peers.c $(PEER_SOURCES) \
		$(LDFLAGS) -Wl,--wrap=getrandom,--wrap=agent_recv \
		-L$(MBEDTLS_BUILD)/library -lmbedtls -lmbedx509 -lmbedcrypto $(SRTP_LIBRARY) -lpthread -o $@
