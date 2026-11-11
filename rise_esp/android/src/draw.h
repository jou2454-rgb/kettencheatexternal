// Overlay window, EGL and ImGui for the Rise ESP. Ported from the bycmd overlay
// (same window creator and ImGui backends). The menu, watermark and touch come
// from the bycmd source (src/ui, src/touch); their fonts and avatar live in draw.cpp.
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

// Presentation status for the log: failed eglSwapBuffers calls and GL errors of frames.
struct SwapStats {
    unsigned swapFailures = 0;
    unsigned lastEglError = 0;
    unsigned glErrors = 0;
    unsigned lastGlError = 0;
};
SwapStats swapStats();

android::ANativeWindowCreator::DisplayInfo displayInfo();
bool init(int width, int height);   // landscape overlay: width >= height
void shutdown();
void beginFrame();
void endFrame();

}  // namespace overlay
