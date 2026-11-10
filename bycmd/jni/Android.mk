# Android.mk

LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_MODULE           := eeeww               # sometimes needed

LOCAL_SRC_FILES := \
    ../src/main.cpp \
    ../src/ui/menu.cpp \
    ../src/ui/bar.cpp \
    ../src/ui/widgets/widgets.cpp \
    ../src/func/visuals.cpp \
    ../src/protect/oxorany.cpp \
    ../includes/draw/Android_draw/draw.cpp \
    ../includes/draw/Android_touch/Touch.cpp \
    ../includes/draw/ImGui/imgui.cpp \
    ../includes/draw/ImGui/imgui_draw.cpp \
    ../includes/draw/ImGui/imgui_tables.cpp \
    ../includes/draw/ImGui/imgui_widgets.cpp \
    ../includes/draw/ImGui/backends/imgui_impl_android.cpp \
    ../includes/draw/ImGui/backends/imgui_impl_opengl3.cpp

LOCAL_C_INCLUDES := \
    $(LOCAL_PATH)/../includes \
    $(LOCAL_PATH)/../includes/fonts \
    $(LOCAL_PATH)/../includes/internal \
    $(LOCAL_PATH)/../includes/internal/ImGui \
    $(LOCAL_PATH)/../includes/internal/ImGui/backends \
    $(LOCAL_PATH)/../includes/draw/ImGui \
    $(LOCAL_PATH)/../includes/draw/ImGui/backends \
    $(LOCAL_PATH)/../src \
    $(LOCAL_PATH)/../src/ui

LOCAL_CPPFLAGS := \
    -std=c++17 \
    -fno-rtti \
    -fno-exceptions \
    -fvisibility=hidden \
    -fvisibility-inlines-hidden \
    -Oz \
    -ffunction-sections \
    -fdata-sections \
    -fomit-frame-pointer \
    -Wno-error=format-security \
    -fno-color-diagnostics \
    -fmerge-all-constants \
    -fno-ident \
    -DBYCMD_BUILD_ID=\"$(if $(BYCMD_BUILD_ID),$(BYCMD_BUILD_ID),dev)\"


# If you really want -fexceptions (very rare in size-optimized Android native code)
# LOCAL_CPPFLAGS += -fexceptions

# No --strip-all here: ndk-build strips the installed copy in libs/ anyway,
# while obj/local/arm64-v8a/eeeww keeps symbols for crash-log symbolisation.
LOCAL_LDFLAGS := \
    -Wl,--gc-sections \
    -Wl,--build-id=none \
    -Wl,--no-undefined \
    -pie \
    -Wl,-z,relro \
    -Wl,-z,now \
    -Wl,-z,noexecstack \
    -Wl,-z,max-page-size=16384 \
    -Wl,-z,common-page-size=16384

LOCAL_LDLIBS := \
    -llog \
    -landroid \
    -lEGL \
    -lGLESv3

# Very aggressive stripping — sometimes causes issues, test carefully
# LOCAL_LDFLAGS += -s

include $(BUILD_EXECUTABLE)