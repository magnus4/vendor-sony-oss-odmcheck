LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

LOCAL_C_INCLUDES := \
    bootable/recovery \
    $(LOCAL_PATH)/include

#LOCAL_C_INCLUDES := bootable/recovery/minui/include $(LOCAL_PATH)/include

LOCAL_SRC_FILES := \
    odmcheck.cpp

LOCAL_SHARED_LIBRARIES := \
    liblog \
    libcutils \
    libpng

LOCAL_STATIC_LIBRARIES := \
    libminui

LOCAL_MODULE := odmcheck
ifeq (1,$(filter 1,$(shell echo "$$(( $(PLATFORM_SDK_VERSION) >= 25 ))" )))
LOCAL_MODULE_OWNER := sony
LOCAL_PROPRIETARY_MODULE := true
endif
#LOCAL_MODULE_TAGS := optional

include $(BUILD_EXECUTABLE)

