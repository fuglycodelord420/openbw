COPYRIGHT_FILE = ../COPYRIGHT

LOCAL_MAKE_INCLUDE := include
override TEMPLATE := make_templates/header_only_lib
override LOCAL_TEMPLATE := $(LOCAL_MAKE_INCLUDE)/$(TEMPLATE)

ifneq ($(shell cat $(LOCAL_TEMPLATE) 2> /dev/null),)
include $(LOCAL_TEMPLATE)
else
include $(TEMPLATE)
endif
