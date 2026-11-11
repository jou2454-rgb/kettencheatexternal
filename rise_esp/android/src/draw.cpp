#include "draw.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>

#include "dbg/log.hpp"
#include "fonts/avatar.h"
#include "fonts/esp.h"
#include "fonts/pixeloperator.h"
#include "ui/ui_fonts.h"

// IM_ASSERT hook declared in third_party/ImGui/imconfig.h. Logs and continues, so an
// ImGui assert never takes the overlay down.
void rise_imgui_assert_fail(const char* expr, const char* file, int line) {
    dbg::line("ImGui assert failed: %s at %s:%d", expr, file, line);
}

// Menu and watermark font and texture (declared in ui/ui_fonts.h).
ImFont* fontMedium = nullptr;
ImFont* fontMenu = nullptr;
ImFont* fontWm = nullptr;
ImTextureID g_avatar_tex = nullptr;

namespace overlay {

ImFont* espFont = nullptr;

namespace {

EGLDisplay g_display = EGL_NO_DISPLAY;
EGLSurface g_surface = EGL_NO_SURFACE;
EGLContext g_context = EGL_NO_CONTEXT;
ANativeWindow* g_window = nullptr;
GLuint g_avatar_gl = 0;
int g_width = 0;
int g_height = 0;
SwapStats g_swap;

bool initEgl() {
    dbg::line("overlay: creating window %dx%d", g_width, g_height);
    g_window = android::ANativeWindowCreator::Create("RiseOverlay", g_width, g_height, false);
    if (!g_window) {
        dbg::line("overlay: window creation failed");
        return false;
    }
    ANativeWindow_acquire(g_window);
    g_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (g_display == EGL_NO_DISPLAY) {
        dbg::line("overlay: eglGetDisplay failed (egl 0x%x)", static_cast<unsigned>(eglGetError()));
        return false;
    }
    EGLint major = 0, minor = 0;
    if (eglInitialize(g_display, &major, &minor) != EGL_TRUE) {
        dbg::line("overlay: eglInitialize failed (egl 0x%x)", static_cast<unsigned>(eglGetError()));
        return false;
    }
    const EGLint cfgAttr[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_BLUE_SIZE, 5, EGL_GREEN_SIZE, 6, EGL_RED_SIZE, 5,
        EGL_BUFFER_SIZE, 32, EGL_DEPTH_SIZE, 16, EGL_STENCIL_SIZE, 8,
        EGL_NONE,
    };
    EGLConfig config{};
    EGLint numConfig = 0;
    if (eglChooseConfig(g_display, cfgAttr, &config, 1, &numConfig) != EGL_TRUE || numConfig < 1) {
        dbg::line("overlay: eglChooseConfig failed (egl 0x%x, configs %d)",
                  static_cast<unsigned>(eglGetError()), numConfig);
        return false;
    }
    EGLint visualId = 0;
    if (eglGetConfigAttrib(g_display, config, EGL_NATIVE_VISUAL_ID, &visualId) != EGL_TRUE) {
        dbg::line("overlay: eglGetConfigAttrib failed (egl 0x%x)", static_cast<unsigned>(eglGetError()));
        return false;
    }
    if (ANativeWindow_setBuffersGeometry(g_window, 0, 0, visualId) != 0) {
        dbg::line("overlay: ANativeWindow_setBuffersGeometry failed");
        return false;
    }
    const EGLint ctxAttr[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    g_context = eglCreateContext(g_display, config, EGL_NO_CONTEXT, ctxAttr);
    if (g_context == EGL_NO_CONTEXT) {
        dbg::line("overlay: eglCreateContext failed (egl 0x%x)", static_cast<unsigned>(eglGetError()));
        return false;
    }
    g_surface = eglCreateWindowSurface(g_display, config, g_window, nullptr);
    if (g_surface == EGL_NO_SURFACE) {
        dbg::line("overlay: eglCreateWindowSurface failed (egl 0x%x)", static_cast<unsigned>(eglGetError()));
        return false;
    }
    if (eglMakeCurrent(g_display, g_surface, g_surface, g_context) != EGL_TRUE) {
        dbg::line("overlay: eglMakeCurrent failed (egl 0x%x)", static_cast<unsigned>(eglGetError()));
        return false;
    }
    dbg::line("overlay: EGL %d.%d, surface ready", major, minor);
    return true;
}

// Watermark avatar: 128x128 RGBA8 from avatar.h. Needs the EGL context to be current.
void uploadAvatar() {
    GLuint tex = 0;
    glGenTextures(1, &tex);
    if (!tex) return;
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, neko_avatar_w, neko_avatar_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, neko_avatar_rgba);
    if (glGetError() == GL_NO_ERROR) {
        g_avatar_gl = tex;
        g_avatar_tex = reinterpret_cast<ImTextureID>(static_cast<intptr_t>(tex));
    } else {
        dbg::line("overlay: avatar texture upload failed");
        glDeleteTextures(1, &tex);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
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
    fontMedium = io.Fonts->AddFontFromMemoryTTF(pixeloperator, sizeof(pixeloperator), 28, &fc,
                                                io.Fonts->GetGlyphRangesCyrillic());
    io.FontDefault = fontMedium;
    espFont = io.Fonts->AddFontFromMemoryTTF(espFontData, sizeof(espFontData), 18, &fc,
                                             io.Fonts->GetGlyphRangesCyrillic());
    // Menu and watermark sizes follow the overlay height (1224 px base, clamped like style::k()),
    // so the glyphs are rasterised at their final size instead of being scaled up.
    const float k = std::min(3.f, std::max(0.5f, float(g_height) / 1224.f));
    fontMenu = io.Fonts->AddFontFromMemoryTTF(espFontData, sizeof(espFontData), std::round(36.f * k), &fc,
                                              io.Fonts->GetGlyphRangesCyrillic());
    fontWm = io.Fonts->AddFontFromMemoryTTF(espFontData, sizeof(espFontData), std::round(54.f * k), &fc,
                                            io.Fonts->GetGlyphRangesCyrillic());
    if (!fontMedium || !espFont || !fontMenu || !fontWm) {
        dbg::line("overlay: font atlas creation failed");
        return false;
    }
    uploadAvatar();
    dbg::line("overlay: ImGui ready, menu font %.0f px, watermark font %.0f px", fontMenu->FontSize,
              fontWm->FontSize);
    return true;
}

}  // namespace

android::ANativeWindowCreator::DisplayInfo displayInfo() {
    return android::ANativeWindowCreator::GetDisplayInfo();
}

bool init(int width, int height) {
    g_width = width;
    g_height = height;
    if (!initEgl()) return false;
    return initImGui();
}

void shutdown() {
    if (g_avatar_gl) {
        glDeleteTextures(1, &g_avatar_gl);
        g_avatar_gl = 0;
        g_avatar_tex = nullptr;
    }
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
    const GLenum glError = glGetError();
    if (glError != GL_NO_ERROR) {
        ++g_swap.glErrors;
        g_swap.lastGlError = static_cast<unsigned>(glError);
    }
    if (eglSwapBuffers(g_display, g_surface) != EGL_TRUE) {
        ++g_swap.swapFailures;
        g_swap.lastEglError = static_cast<unsigned>(eglGetError());
    }
}

SwapStats swapStats() { return g_swap; }

}  // namespace overlay
