include ../../mkenv.mk

subdirs-y := canmv_misc netmgmt k230_syscalls k230_ota

.PHONY: all clean distclean

all: $(subdirs-y)
	@$(ECHO) [BUILD] libs rtsmart hal components done.

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

distclean: clean
	@if [ -n "$(subdirs-y)" ]; then \
		for dir in $(subdirs-y); do \
			echo "[DISTCLEAN] rtsmart libs hal $$dir"; \
			$(MAKE) -C $$dir distclean || exit $$?; \
		done; \
	fi
