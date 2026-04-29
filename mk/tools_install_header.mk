# ==============================
# Header install helper (robust)
# ==============================

HEADER_INSTALL ?= /tmp/include
HEADER_DIRS ?=      # Format: "src_dir:dest_subdir" or "src_dir" (flatten)
HEADER_FILES ?=     # Format: "src_file:dest_name" or "src_file"

.PHONY: install-headers clean-headers

install-headers:
	@$(MKDIR) -p $(HEADER_INSTALL)
	# Install directories
	@for item in $(HEADER_DIRS); do \
	    src_dir=$${item%:*} ; \
	    dest_sub=$${item#*:} ; \
	    if [ "$$src_dir" = "$$dest_sub" ]; then \
	        dest_sub="" ; \
	    fi ; \
	    if [ -n "$$dest_sub" ]; then \
	        echo "[INSTALL-DIR] $$src_dir -> $(HEADER_INSTALL)/$$dest_sub" ; \
	        $(MKDIR) -p $(HEADER_INSTALL)/$$dest_sub ; \
	        cp -r $$src_dir/. $(HEADER_INSTALL)/$$dest_sub/ ; \
	    else \
	        echo "[INSTALL-FLAT] $$src_dir -> $(HEADER_INSTALL)" ; \
	        cp -r $$src_dir/. $(HEADER_INSTALL)/ ; \
	    fi ; \
	done
	# Install files
	@for item in $(HEADER_FILES); do \
	    src_file=$${item%:*} ; \
	    dest_name=$${item#*:} ; \
	    if [ "$$src_file" = "$$dest_name" ]; then \
	        dest_name=$$(basename $$src_file) ; \
	    fi ; \
	    echo "[INSTALL] $$src_file -> $(HEADER_INSTALL)/$$dest_name" ; \
	    $(INSTALL) -m644 $$src_file $(HEADER_INSTALL)/$$dest_name ; \
	done
	@$(ECHO) "[INSTALL] headers done."

clean-headers:
	@$(RM) -r $(HEADER_INSTALL)

all: install-headers
clean: clean-headers
