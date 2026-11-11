// Overlay window, EGL and ImGui for the Rise ESP. Ported from the bycmd overlay
// (same window creator and ImGui backends), without touch, menu and watermark.
#pragma once
#include <cstdint>
#include <EGL/egl.h>
#include <GLES3/gl32.h>
#include <android/native_window.h>
#include "native_surface/ANativeWindowCreator.h"
#include "ImGui/imgui.h"
#include "ImGui/backends/imgui_impl_android.h"
#include "ImGui/backends/imgui_impl_opengl3.h"

namespace overlay {

extern ImFont* espFont;

android::ANativeWindowCreator::DisplayInfo displayInfo();
bool init(int width, int height);   // landscape overlay: width >= height
void shutdown();
void beginFrame();
void endFrame();

}  // namespace overlay
