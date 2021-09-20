COPYRIGHT_FILE = ../COPYRIGHT
override CXXFLAGS += -I../libopenbw_core/source -I../libopenbw_ui/source
override LOCALIB = ../libopenbw_ui/out/libopenbw_ui.a
override LDLIBS += -lsimple_graphical -lsimple_interactive -lsimple_sdlcore -lSDL2 -lSDL2_mixer

# TODO: figure out the emscripten setup

LOCAL_MAKE_INCLUDE := include
override TEMPLATE := make_templates/binary
override LOCAL_TEMPLATE := $(LOCAL_MAKE_INCLUDE)/$(TEMPLATE)

ifneq ($(shell cat $(LOCAL_TEMPLATE) 2> /dev/null),)
include $(LOCAL_TEMPLATE)
else
include $(TEMPLATE)
endif
