#include "draw.h"

#include <cstdio>

#include "fonts/esp.h"
#include "fonts/pixeloperator.h"

// IM_ASSERT hook declared in third_party/ImGui/imconfig.h. Logs and continues, so an
// ImGui assert never takes the overlay down.
void rise_imgui_assert_fail(const char* expr, const char* file, int line) {
    std::fprintf(stderr, "ImGui assert failed: %s at %s:%d\n", expr, file, line);
}

namespace overlay {

ImFont* espFont = nullptr;

namespace {

EGLDisplay g_display = EGL_NO_DISPLAY;
EGLSurface g_surface = EGL_NO_SURFACE;
EGLContext g_context = EGL_NO_CONTEXT;
ANativeWindow* g_window = nullptr;
int g_width = 0;
int g_height = 0;

bool initEgl() {
    g_window = android::ANativeWindowCreator::Create("RiseOverlay", g_width, g_height, false);
    if (!g_window) return false;
    ANativeWindow_acquire(g_window);
    g_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (g_display == EGL_NO_DISPLAY) return false;
    EGLint major = 0, minor = 0;
    if (eglInitialize(g_display, &major, &minor) != EGL_TRUE) return false;
    const EGLint cfgAttr[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_BLUE_SIZE, 5, EGL_GREEN_SIZE, 6, EGL_RED_SIZE, 5,
        EGL_BUFFER_SIZE, 32, EGL_DEPTH_SIZE, 16, EGL_STENCIL_SIZE, 8,
        EGL_NONE,
    };
    EGLConfig config{};
    EGLint numConfig = 0;
    if (eglChooseConfig(g_display, cfgAttr, &config, 1, &numConfig) != EGL_TRUE || numConfig < 1) return false;
    EGLint visualId = 0;
    if (eglGetConfigAttrib(g_display, config, EGL_NATIVE_VISUAL_ID, &visualId) != EGL_TRUE) return false;
    if (ANativeWindow_setBuffersGeometry(g_window, 0, 0, visualId) != 0) return false;
    const EGLint ctxAttr[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    g_context = eglCreateContext(g_display, config, EGL_NO_CONTEXT, ctxAttr);
    if (g_context == EGL_NO_CONTEXT) return false;
    g_surface = eglCreateWindowSurface(g_display, config, g_window, nullptr);
    if (g_surface == EGL_NO_SURFACE) return false;
    return eglMakeCurrent(g_display, g_surface, g_surface, g_context) == EGL_TRUE;
}

bool initImGui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplAndroid_Init(g_window);
    ImGui_ImplOpenGL3_Init("#version 300 es");
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    // Fonts live in static arrays: the atlas must not free them on DestroyContext.
    ImFontConfig fc;
    fc.FontDataOwnedByAtlas = false;
    io.FontDefault = io.Fonts->AddFontFromMemoryTTF(pixeloperator, sizeof(pixeloperator), 28, &fc,
                                                    io.Fonts->GetGlyphRangesCyrillic());
    espFont = io.Fonts->AddFontFromMemoryTTF(espFontData, sizeof(espFontData), 18, &fc,
                                             io.Fonts->GetGlyphRangesCyrillic());
    return io.FontDefault != nullptr && espFont != nullptr;
}

}  // namespace

android::ANativeWindowCreator::DisplayInfo displayInfo() {
    return android::ANativeWindowCreator::GetDisplayInfo();
}

bool init(int width, int height) {
    g_width = width;
    g_height = height;
    return initEgl() && initImGui();
}

void shutdown() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplAndroid_Shutdown();
    ImGui::DestroyContext();
    if (g_display != EGL_NO_DISPLAY) {
        eglMakeCurrent(g_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (g_surface != EGL_NO_SURFACE) eglDestroySurface(g_display, g_surface);
        if (g_context != EGL_NO_CONTEXT) eglDestroyContext(g_display, g_context);
        eglTerminate(g_display);
    }
    if (g_window) ANativeWindow_release(g_window);
    g_display = EGL_NO_DISPLAY;
    g_surface = EGL_NO_SURFACE;
    g_context = EGL_NO_CONTEXT;
    g_window = nullptr;
}

void beginFrame() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplAndroid_NewFrame(g_width, g_height);
    ImGui::NewFrame();
}

void endFrame() {
    ImGui::Render();
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    eglSwapBuffers(g_display, g_surface);
}

}  // namespace overlay
