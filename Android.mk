LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
	yaffsunlock.c \
	minui/events.c minui/graphics.c minui/resources.c

LOCAL_MODULE := yaffsunlock

LOCAL_FORCE_STATIC_EXECUTABLE := true

LOCAL_CFLAGS += -DUSE_CUSTOM_FONT="\"roboto_15x24.h\"" \
                 -O3 -Wall -Werror -Wextra -std=c99

LOCAL_STATIC_LIBRARIES := libunz
LOCAL_STATIC_LIBRARIES += libpixelflinger_static libpng libcutils liblog
LOCAL_STATIC_LIBRARIES += libstdc++ libc libz
LOCAL_LDLIBS := -lm

include $(BUILD_EXECUTABLE)

# install the pngs
PNGNAMES = softkeyboard1.png softkeyboard2.png softkeyboard3.png softkeyboard4.png
PNGSRCS := $(addprefix $(LOCAL_PATH)/resources/,$(PNGNAMES))
PNGDST := $(TARGET_OUT)/media/$(LOCAL_MODULE)
$(PNGSRCS): $(LOCAL_INSTALLED_MODULE)
	@echo "Install: $@ -> $(PNGDST)/"
	@mkdir -p $(PNGDST)/
	$(hide) cp $@ $(PNGDST)/

ALL_DEFAULT_INSTALLED_MODULES += $(PNGSRCS)

# "We need this so that the installed files could be picked up based on the
# local module name" - comment from make fragments I'm copying from; I don't really understand...
ALL_MODULES.$(LOCAL_MODULE).INSTALLED := \
    $(ALL_MODULES.$(LOCAL_MODULE).INSTALLED) $(PNGSRCS)
