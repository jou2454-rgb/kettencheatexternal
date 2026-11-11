// ================================================================================
// Watermark pill: "neko" white + ".esp" red, round avatar on the right.
// Drawn on the foreground list at the top-left corner; a tap toggles the menu.
// ================================================================================

#include "bar.hpp"
#include "theme/theme.hpp"
#include "Android_draw/draw.h"
#include "imgui.h"
#include "imgui_internal.h"
#include <cmath>
#include <cfloat>

namespace ui::bar {
    static float ease(float t) { return -(cosf(3.14159265f * t) - 1.f) / 2.f; }

    void set_game_alpha(float a) { g_game_alpha = a; }
    float game_alpha() { return g_game_alpha; }

    void render() {
        if (g_game_alpha < 0.001f) return;

        ImGuiIO& io = ImGui::GetIO();
        const float k = style::k();
        const float ct = float(ImGui::GetTime());
        static bool state = false;     // menu closed on start
        static float prog = 0.f;
        static float last_tap = -1.f;

        ImFont* font = fontWm ? fontWm : (fontMedium ? fontMedium : ImGui::GetFont());
        const float fs = font->FontSize;

        const float h = 84.f * k;
        const float pad = 28.f * k;
        const float gap = 30.f * k;
        const float av = 60.f * k;
        const float rpad = 14.f * k;
        const float r = h * 0.32f;

        const ImVec2 t1 = font->CalcTextSizeA(fs, FLT_MAX, 0.f, "neko");
        const ImVec2 t2 = font->CalcTextSizeA(fs, FLT_MAX, 0.f, ".esp");
        const float w = pad + t1.x + t2.x + gap + av + rpad;

        const ImVec2 p(24.f * k, 24.f * k);
        g_rect_min = p;
        g_rect_max = ImVec2(p.x + w, p.y + h);

        // Tap: a finger is ~10 base px less precise than a mouse, enlarge the hit box.
        const float slack = 12.f * k;
        const ImVec2 mp = io.MousePos;
        const bool hov = mp.x >= p.x - slack && mp.x <= g_rect_max.x + slack &&
                         mp.y >= p.y - slack && mp.y <= g_rect_max.y + slack;
        if (hov && ImGui::IsMouseClicked(0) && ct - last_tap >= 0.25f) {
            state = !state;
            last_tap = ct;
        }

        float dt = io.DeltaTime;
        if (dt <= 0.f || dt > 0.1f) dt = 0.016f;
        const float tgt = state ? 1.f : 0.f;
        const float spd = 10.f * dt;
        if (prog < tgt) prog = ImMin(prog + spd, tgt);
        else if (prog > tgt) prog = ImMax(prog - spd, tgt);
        g_alpha = ease(prog);
        g_open = state;

        const float a = g_game_alpha;
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        const bool pressed = hov && io.MouseDown[0];

        // shadow + pill
        dl->AddRectFilled(ImVec2(p.x - 2.f * k, p.y + 2.f * k), ImVec2(g_rect_max.x + 2.f * k, g_rect_max.y + 4.f * k),
                          IM_COL32(0, 0, 0, int(70 * a)), r + 2.f * k);
        dl->AddRectFilled(p, g_rect_max, IM_COL32(22, 10, 12, int(236 * a)), r);
        if (pressed) dl->AddRectFilled(p, g_rect_max, IM_COL32(255, 255, 255, int(18 * a)), r);
        dl->AddRect(p, g_rect_max, IM_COL32(255, 255, 255, int(20 * a)), r, 0, 1.5f * k);

        // text
        const float ty = p.y + (h - fs) * 0.5f;
        dl->AddText(font, fs, ImVec2(p.x + pad, ty), style::col(clr::text_light, a), "neko");
        dl->AddText(font, fs, ImVec2(p.x + pad + t1.x, ty), style::col(clr::accent, a), ".esp");

        // avatar
        const ImVec2 c(g_rect_max.x - rpad - av * 0.5f, p.y + h * 0.5f);
        const ImVec2 amin(c.x - av * 0.5f, c.y - av * 0.5f);
        const ImVec2 amax(c.x + av * 0.5f, c.y + av * 0.5f);
        if (g_avatar_tex) {
            dl->AddImageRounded(g_avatar_tex, amin, amax, ImVec2(0, 0), ImVec2(1, 1),
                                IM_COL32(255, 255, 255, int(255 * a)), av * 0.5f);
        } else {
            dl->AddCircleFilled(c, av * 0.5f, style::col(clr::accent, a), 32);
        }
        dl->AddCircle(c, av * 0.5f, IM_COL32(255, 255, 255, int(45 * a)), 32, 1.5f * k);
    }
}
