include ../../mkenv.mk

subdirs-y := \
	fpioa \
	gpio \
	input \
	timer \
	sbus \
	i2c \
	adc \
	tsensor \
	wdt \
	spi \
	fft \
	pwm \
	uart \
	ws2812 \
	onewire \
	touch \
	gzip_decomp

subdirs-$(CONFIG_ENABLE_ROTARY_ENCODER) += rotary_encoder

.PHONY: all clean distclean

all: $(subdirs-y)
	@$(ECHO) [BUILD] libs rtsmart hal drivers done.

ifneq ($(strip $(subdirs-y)),)
.PHONY: $(subdirs-y)

$(subdirs-y):
	@echo "[BUILD] rtsmart libs hal $@"
	@$(MAKE) -C $@ all || exit $?;
endif

clean:
	@if [ -n "$(subdirs-y)" ]; then \
		for dir in $(subdirs-y); do \
			echo "[CLEAN] rtsmart libs hal $$dir"; \
			$(MAKE) -C $$dir clean || exit $?; \
		done; \
	fi

distclean: clean
	@if [ -n "$(subdirs-y)" ]; then \
		for dir in $(subdirs-y); do \
			echo "[DISTCLEAN] rtsmart libs hal $$dir"; \
			$(MAKE) -C $$dir distclean || exit $?; \
		done; \
	fi
