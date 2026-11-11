
// ================================================================================
// Bot: @imguisource_bot | TG: @ImGui_Source_FIXNEY & @il2cpp_src
// ================================================================================

#ifndef NATIVESURFACE_DRAW_H
#define NATIVESURFACE_DRAW_H

#include <iostream>
#include <thread>
#include <chrono>
#include <dlfcn.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <android/native_window.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES/gl.h>
#include <GLES3/gl3platform.h>
#include <GLES3/gl3ext.h>
#include <GLES3/gl32.h>

#include "native_surface/ANativeWindowCreator.h"
#include "ImGui/imgui.h"
#include "ImGui/imgui_internal.h"
#include "ImGui/backends/imgui_impl_opengl3.h"
#include "ImGui/backends/imgui_impl_android.h"
#include "Android_touch/Touch.hpp"

using namespace std;
using namespace std::chrono_literals;

extern android::ANativeWindowCreator::DisplayInfo displayInfo;
extern bool g_Initialized;
extern ImGuiWindow *g_window;
extern ImFont* fontBold;
extern ImFont* fontMedium;
extern ImFont* fontDesc;
extern ImFont* espFont;
extern ImFont* fontMenu;   // cloudhack menu labels (Vanta Medium, 36 base px)
extern ImFont* fontWm;     // watermark "cloudhack" (Vanta Medium, 54 base px)
extern ImTextureID g_avatar_tex; // watermark avatar (GL texture), nullptr when unavailable
extern int native_window_screen_x, native_window_screen_y;
extern unsigned g_swap_failures;             // total eglSwapBuffers failures
extern unsigned g_swap_consecutive_failures; // failures since the last success
extern unsigned g_swap_last_error;           // last eglGetError() after a failure

bool init_egl(uint32_t _screen_x, uint32_t _screen_y, bool log = false);
bool initGUI_draw(uint32_t _screen_x, uint32_t _screen_y, bool log = false);
bool ImGui_init();
void screen_config();
void drawBegin();
void drawEnd();
void shutdown();

#endif
