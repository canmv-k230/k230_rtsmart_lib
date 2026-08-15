include mkenv.mk

subdirs-y := 

ifndef RTT_LIBS_DISABLED
subdirs-y += rtsmart_hal 3rd-party wlan_offload
endif

.PHONY: all clean distclean

all:
	@if [ -n "$(subdirs-y)" ]; then \
		for dir in $(subdirs-y); do \
			echo "[BUILD] rtsmart libs $$dir"; \
			$(MAKE) -C $$dir all; \
		done; \
	fi

clean:
	@if [ -n "$(subdirs-y)" ]; then \
		for dir in $(subdirs-y); do \
			echo "[CLEAN] rtsmart libs $$dir"; \
			$(MAKE) -C $$dir clean; \
		done; \
	fi

distclean:
	@if [ -n "$(subdirs-y)" ]; then \
		for dir in $(subdirs-y); do \
			echo "[DISTCLEAN] rtsmart libs $$dir"; \
			$(MAKE) -C $$dir distclean; \
		done; \
	fi
