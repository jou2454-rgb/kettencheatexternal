// ================================================================================
// Watermark pill: "cloud" white + "hack" red, round avatar on the right.
// Drawn on the foreground list; starts at the top-left corner.
//   tap (press + release without moving)  -> open / close the menu
//   press + move more than 10 base px     -> drag the pill anywhere on screen
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

    namespace {
        bool g_pos_init = false;
        ImVec2 g_pos;             // top-left of the pill
        bool g_pressed = false;   // finger went down on the pill
        ImVec2 g_press_pos;       // where it went down
        ImVec2 g_grab;            // finger offset from g_pos at press time
        float g_last_tap = -1.f;
        bool g_state = false;     // menu open requested
        float g_prog = 0.f;
    }

    void render() {
        if (g_game_alpha < 0.001f) return;

        ImGuiIO& io = ImGui::GetIO();
        const float k = style::k();
        const float ct = float(ImGui::GetTime());

        ImFont* font = fontWm ? fontWm : (fontMedium ? fontMedium : ImGui::GetFont());
        const float fs = font->FontSize;

        const float h = 84.f * k;
        const float pad = 28.f * k;
        const float gap = 30.f * k;
        const float av = 60.f * k;
        const float rpad = 14.f * k;
        const float r = h * 0.32f;

        const ImVec2 t1 = font->CalcTextSizeA(fs, FLT_MAX, 0.f, "cloud");
        const ImVec2 t2 = font->CalcTextSizeA(fs, FLT_MAX, 0.f, "hack");
        const float w = pad + t1.x + t2.x + gap + av + rpad;

        if (!g_pos_init) { g_pos = ImVec2(24.f * k, 24.f * k); g_pos_init = true; }
        g_pos.x = ImClamp(g_pos.x, 0.f, ImMax(0.f, g_sw - w));
        g_pos.y = ImClamp(g_pos.y, 0.f, ImMax(0.f, g_sh - h));

        const ImVec2 p = g_pos;
        g_rect_min = p;
        g_rect_max = ImVec2(p.x + w, p.y + h);

        // ---- input: tap vs drag ----
        const float slack = 12.f * k;
        const float drag_threshold = 10.f * k;
        const ImVec2 mp = io.MousePos;
        const bool hov = mp.x >= p.x - slack && mp.x <= g_rect_max.x + slack &&
                         mp.y >= p.y - slack && mp.y <= g_rect_max.y + slack;

        if (!g_pressed && hov && ImGui::IsMouseClicked(0)) {
            g_pressed = true;
            g_dragging = false;
            g_press_pos = mp;
            g_grab = ImVec2(mp.x - p.x, mp.y - p.y);
        }
        if (g_pressed) {
            if (io.MouseDown[0]) {
                const float dx = mp.x - g_press_pos.x, dy = mp.y - g_press_pos.y;
                if (!g_dragging && dx * dx + dy * dy > drag_threshold * drag_threshold) g_dragging = true;
                if (g_dragging) {
                    g_pos = ImVec2(mp.x - g_grab.x, mp.y - g_grab.y);
                    g_pos.x = ImClamp(g_pos.x, 0.f, ImMax(0.f, g_sw - w));
                    g_pos.y = ImClamp(g_pos.y, 0.f, ImMax(0.f, g_sh - h));
                }
            } else {
                // released
                if (!g_dragging && ct - g_last_tap >= 0.25f) {
                    g_state = !g_state;
                    g_last_tap = ct;
                }
                g_pressed = false;
                g_dragging = false;
            }
        }

        float dt = io.DeltaTime;
        if (dt <= 0.f || dt > 0.1f) dt = 0.016f;
        const float tgt = g_state ? 1.f : 0.f;
        const float spd = 10.f * dt;
        if (g_prog < tgt) g_prog = ImMin(g_prog + spd, tgt);
        else if (g_prog > tgt) g_prog = ImMax(g_prog - spd, tgt);
        g_alpha = ease(g_prog);
        g_open = g_state;

        // ---- draw (use the possibly updated position) ----
        const ImVec2 q = g_pos;
        const ImVec2 qmax(q.x + w, q.y + h);
        g_rect_min = q;
        g_rect_max = qmax;

        const float a = g_game_alpha;
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        const bool pressed_look = g_pressed;

        dl->AddRectFilled(ImVec2(q.x - 2.f * k, q.y + 2.f * k), ImVec2(qmax.x + 2.f * k, qmax.y + 4.f * k),
                          IM_COL32(0, 0, 0, int(70 * a)), r + 2.f * k);
        dl->AddRectFilled(q, qmax, IM_COL32(22, 10, 12, int(236 * a)), r);
        if (pressed_look) dl->AddRectFilled(q, qmax, IM_COL32(255, 255, 255, int(18 * a)), r);
        dl->AddRect(q, qmax, IM_COL32(255, 255, 255, int(g_dragging ? 60 : 20) * a), r, 0, 1.5f * k);

        const float ty = q.y + (h - fs) * 0.5f;
        dl->AddText(font, fs, ImVec2(q.x + pad, ty), style::col(clr::text_light, a), "cloud");
        dl->AddText(font, fs, ImVec2(q.x + pad + t1.x, ty), style::col(clr::accent, a), "hack");

        const ImVec2 c(qmax.x - rpad - av * 0.5f, q.y + h * 0.5f);
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
