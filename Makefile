BUILD_COMPONENTS := libopenbw_ui replay_viewer
INSTALL_COMPONENTS := libopenbw_core
COMPONENTS := $(BUILD_COMPONENTS) $(INSTALL_COMPONENTS)

ifneq ($(COMMON_BUILD_DIR),)
export PREFIX = $(COMMON_BUILD_DIR)
export override LDFLAGS += -Wl,-rpath,$(PREFIX)/lib
endif

all: $(BUILD_COMPONENTS)
install: $(COMPONENTS)
uninstall: $(COMPONENTS)
clean: $(BUILD_COMPONENTS)
distclean: $(BUILD_COMPONENTS)

$(BUILD_COMPONENTS):
	$(MAKE) -C $@ $(MAKECMDGOALS) --no-print-directory

$(INSTALL_COMPONENTS):
	$(MAKE) -C $@ $(MAKECMDGOALS) --no-print-directory

.PHONY: all install uninstall clean distclean $(COMPONENTS)
.NOTPARALLEL:
