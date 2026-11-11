LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_MODULE := rise_esp

LOCAL_SRC_FILES := \
    ../src/main_android.cpp \
    ../src/draw.cpp \
    ../src/ui/bar.cpp \
    ../src/ui/menu.cpp \
    ../src/ui/widgets/widgets.cpp \
    ../src/touch/Touch.cpp \
    ../../src/esp.cpp \
    ../../src/memory.cpp \
    ../../game/rise_reader.cpp \
    ../third_party/ImGui/imgui.cpp \
    ../third_party/ImGui/imgui_draw.cpp \
    ../third_party/ImGui/imgui_tables.cpp \
    ../third_party/ImGui/imgui_widgets.cpp \
    ../third_party/ImGui/backends/imgui_impl_android.cpp \
    ../third_party/ImGui/backends/imgui_impl_opengl3.cpp

LOCAL_C_INCLUDES := \
    $(LOCAL_PATH)/../.. \
    $(LOCAL_PATH)/../../include \
    $(LOCAL_PATH)/../../game \
    $(LOCAL_PATH)/../third_party \
    $(LOCAL_PATH)/../third_party/ImGui \
    $(LOCAL_PATH)/../third_party/ImGui/backends \
    $(LOCAL_PATH)/../third_party/fonts \
    $(LOCAL_PATH)/../third_party/native_surface \
    $(LOCAL_PATH)/../src \
    $(LOCAL_PATH)/../src/ui

LOCAL_CPPFLAGS := -std=c++17 -fno-rtti -fno-exceptions -O2 -Wno-error=format-security
LOCAL_LDLIBS := -lEGL -lGLESv3 -landroid -ldl -llog

include $(BUILD_EXECUTABLE)
