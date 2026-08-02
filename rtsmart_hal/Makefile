include ../mkenv.mk

subdirs-y := drivers components utils

.PHONY: all clean distclean

all: $(subdirs-y)
	@$(ECHO) [BUILD] libs rtsmart hal done.

ifneq ($(strip $(subdirs-y)),)
.PHONY: $(subdirs-y)

$(subdirs-y):
	@echo "[BUILD] rtsmart libs hal $@"
	@$(MAKE) -C $@ all
endif

clean:
	@if [ -n "$(subdirs-y)" ]; then \
		for dir in $(subdirs-y); do \
			echo "[CLEAN] rtsmart libs hal $$dir"; \
			$(MAKE) -C $$dir clean || exit $$?; \
		done; \
	fi
	@$(RM) $(RTSMART_HAL_LIB_INSTALL_PATH) $(RTSMART_HAL_INC_INSTALL_PATH)

distclean:
	@if [ -n "$(subdirs-y)" ]; then \
		for dir in $(subdirs-y); do \
			echo "[DISTCLEAN] rtsmart libs hal $$dir"; \
			$(MAKE) -C $$dir distclean || exit $$?; \
		done; \
	fi
