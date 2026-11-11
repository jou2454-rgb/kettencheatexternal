// ================================================================================
// Fonts and the watermark texture the cloudhack UI draws with. They are created
// in android/src/draw.cpp, once the ImGui context and the EGL context exist.
// ================================================================================

#pragma once
#include "imgui.h"

extern ImFont* fontMedium;        // menu rows, buttons, colour labels (pixeloperator, 28 px)
extern ImFont* fontMenu;          // menu labels (esp font, 36 base px)
extern ImFont* fontWm;            // watermark "cloudhack" (esp font, 54 base px)
extern ImTextureID g_avatar_tex;  // watermark avatar, nullptr when the upload failed
