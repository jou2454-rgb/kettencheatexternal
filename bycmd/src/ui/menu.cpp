// ================================================================================
// cloudhack menu: a compact panel anchored under the watermark.
//   Box / Name / Health / Weapon    - toggle switches
//   Color                           - one ESP colour (box, health bar)
//   unload                          - clean shutdown through status::g_quit_request
// Panel: tap a control = release without moving; press + move = drag the panel.
// Everything is drawn directly on the foreground draw list; no ImGui windows,
// so touch handling is a plain rectangle test on io.MousePos + IsMouseClicked.
// Geometry is in base px for a 1224 px tall screen, scaled by style::k().
// ================================================================================

#define IMGUI_DEFINE_MATH_OPERATORS
#include "menu.hpp"
#include "bar.hpp"
#include "cfg.hpp"
#include "theme/theme.hpp"
#include "imgui.h"
#include "imgui_internal.h"
#include "Android_draw/draw.h"
#include "../other/status.hpp"
#include <cmath>
#include <cfloat>
#include <string>

namespace ui::menu {
    using namespace style;

    namespace {
        float g_ma = 0.f;                 // menu alpha 0..1
        bool g_color_open = false;        // colour popup
        ImVec4 g_hsv{0.f, 1.f, 1.f, 1.f}; // popup state (hue, sat, val)
        bool g_hsv_init = false;
        int g_picker = 0;                 // 0 none, 1 sat/val square, 2 hue bar
        // Panel drag: by default the panel sits under the watermark and
        // follows it; once the user drags the panel it keeps its own place.
        bool g_menu_custom = false;
        ImVec2 g_menu_pos;
        bool g_panel_pressed = false;
        bool g_panel_dragging = false;
        ImVec2 g_panel_press;
        ImVec2 g_panel_grab;
        bool g_resize_active = false;
        ImVec2 g_resize_press;
        float g_resize_sx0 = 1.f, g_resize_sy0 = 1.f, g_resize_w0 = 1.f, g_resize_h0 = 1.f;
        ImVec2 g_grip_min, g_grip_max;         // resize grip of this frame; taps starting there are ignored

        float lrp(float a, float b, float t) { return a + (b - a) * t; }
        ImVec4 lrp_col(const ImVec4& a, const ImVec4& b, float t) {
            return ImVec4(lrp(a.x, b.x, t), lrp(a.y, b.y, t), lrp(a.z, b.z, t), lrp(a.w, b.w, t));
        }
        bool inside(const ImVec2& p, const ImVec2& mn, const ImVec2& mx) {
            return p.x >= mn.x && p.x <= mx.x && p.y >= mn.y && p.y <= mx.y;
        }
        // A control fires on release: finger went down inside the rect and
        // came up inside it without travelling more than the drag threshold.
        // That is what lets the same finger drag the panel without flipping
        // the toggle it happened to land on.
        bool tapped(const ImVec2& mn, const ImVec2& mx) {
            ImGuiIO& io = ImGui::GetIO();
            if (!ImGui::IsMouseReleased(0) || g_panel_dragging || bar::g_dragging) return false;
            const ImVec2 down = io.MouseClickedPos[0];
            const ImVec2 up = io.MousePos;
            if (inside(down, g_grip_min, g_grip_max)) return false;
            if (!inside(down, mn, mx) || !inside(up, mn, mx)) return false;
            const float thr = 10.f * style::k();
            const float dx = up.x - down.x, dy = up.y - down.y;
            return dx * dx + dy * dy <= thr * thr;
        }

        ImFont* menu_font() { return fontMenu ? fontMenu : (fontMedium ? fontMedium : ImGui::GetFont()); }

        // Subtle dot grid like the reference panel.
        void dots(ImDrawList* dl, const ImVec2& mn, const ImVec2& mx, float a, float k) {
            const float step = 14.f * k;
            const float sz = ImMax(1.f, 2.f * k);
            const ImU32 c = col(clr::dot, a);
            for (float y = mn.y + step; y < mx.y - step * 0.5f; y += step)
                for (float x = mn.x + step; x < mx.x - step * 0.5f; x += step)
                    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + sz, y + sz), c);
        }

        // Label on the left, iOS-style switch on the right. Returns true on tap.
        bool toggle_row(ImDrawList* dl, const char* label, bool* v, const ImVec2& pos, float w, float h, float a, float k) {
            bool tap = false;
            if (!g_color_open && tapped(pos, ImVec2(pos.x + w, pos.y + h))) { *v = !*v; tap = true; }

            ImFont* f = menu_font();
            dl->AddText(f, f->FontSize, ImVec2(pos.x, pos.y + (h - f->FontSize) * 0.5f), col(clr::text, a), label);

            const float tw = 92.f * k, th = 46.f * k;
            const ImVec2 tmin(pos.x + w - tw, pos.y + (h - th) * 0.5f);
            const ImVec2 tmax(tmin.x + tw, tmin.y + th);
            const float t = anim(std::string("tg_") + label, *v ? 1.f : 0.f, 16.f);

            dl->AddRectFilled(tmin, tmax, col(lrp_col(clr::widget, clr::track_on, t), a), th * 0.5f);
            dl->AddRect(tmin, tmax, col(clr::border, a), th * 0.5f, 0, 1.f * k);
            const float kr = th * 0.5f - 6.f * k;
            const float kx = tmin.x + th * 0.5f + (tw - th) * t;
            const ImVec2 kc(kx, tmin.y + th * 0.5f);
            dl->AddCircleFilled(ImVec2(kc.x, kc.y + 1.5f * k), kr, IM_COL32(0, 0, 0, int(60 * a)), 24);
            dl->AddCircleFilled(kc, kr, col(lrp_col(clr::knob_off, clr::accent, t), a), 24);
            return tap;
        }

        // Label + rounded colour swatch. Tap toggles the popup.
        void color_row(ImDrawList* dl, const char* label, ImVec4* v, const ImVec2& pos, float w, float h, float a, float k) {
            ImFont* f = menu_font();
            dl->AddText(f, f->FontSize, ImVec2(pos.x, pos.y + (h - f->FontSize) * 0.5f), col(clr::text, a), label);

            const float s = 58.f * k;
            const ImVec2 smin(pos.x + w - s, pos.y + (h - s) * 0.5f);
            const ImVec2 smax(smin.x + s, smin.y + s);
            if (tapped(smin, smax)) {
                g_color_open = !g_color_open;
                if (g_color_open) {
                    ImGui::ColorConvertRGBtoHSV(v->x, v->y, v->z, g_hsv.x, g_hsv.y, g_hsv.z);
                    g_hsv_init = true;
                }
            }
            dl->AddRectFilled(ImVec2(smin.x, smin.y + 2.f * k), ImVec2(smax.x, smax.y + 2.f * k), IM_COL32(0, 0, 0, int(60 * a)), 16.f * k);
            dl->AddRectFilled(smin, smax, IM_COL32(int(v->x * 255), int(v->y * 255), int(v->z * 255), int(255 * a)), 16.f * k);
            dl->AddRect(smin, smax, IM_COL32(255, 255, 255, int((g_color_open ? 90 : 35) * a)), 16.f * k, 0, 1.5f * k);
        }

        // Full-width filled red button. Returns true on tap.
        bool big_button(ImDrawList* dl, const char* label, const ImVec2& pos, float w, float h, float a, float k) {
            const ImVec2 mx(pos.x + w, pos.y + h);
            ImGuiIO& io = ImGui::GetIO();
            const bool hov = inside(io.MousePos, pos, mx);
            const bool press = !g_color_open && tapped(pos, mx);
            const float ha = anim(std::string("bt_") + label, (hov && io.MouseDown[0] && !g_panel_dragging) ? 1.f : 0.f, 18.f);
            const float r = 20.f * k;
            dl->AddRectFilled(ImVec2(pos.x, pos.y + 3.f * k), ImVec2(mx.x, mx.y + 3.f * k), IM_COL32(0, 0, 0, int(70 * a)), r);
            dl->AddRectFilled(pos, mx, col(lrp_col(clr::accent_dark, clr::accent_light, ha), a), r);
            dl->AddRect(pos, mx, IM_COL32(255, 255, 255, int(25 * a)), r, 0, 1.f * k);
            ImFont* f = menu_font();
            const ImVec2 ts = f->CalcTextSizeA(f->FontSize, FLT_MAX, 0.f, label);
            dl->AddText(f, f->FontSize, ImVec2(pos.x + (w - ts.x) * 0.5f, pos.y + (h - ts.y) * 0.5f), col(clr::text_light, a), label);
            return press;
        }

        // HSV picker to the right of the panel (or below it when there is no room).
        void color_popup(ImDrawList* dl, ImVec4* v, const ImVec2& pmin, const ImVec2& pmax, float a, float k) {
            if (!g_color_open) return;
            if (!g_hsv_init) { ImGui::ColorConvertRGBtoHSV(v->x, v->y, v->z, g_hsv.x, g_hsv.y, g_hsv.z); g_hsv_init = true; }

            const float pad = 16.f * k, sv = 190.f * k, hue_w = 26.f * k, gap = 12.f * k;
            const float pw = pad + sv + gap + hue_w + pad;
            const float ph = pad + sv + pad;
            ImVec2 o(pmax.x + 14.f * k, pmin.y);
            if (o.x + pw > g_sw - 8.f * k) o = ImVec2(pmin.x, pmax.y + 14.f * k);
            if (o.y + ph > g_sh - 8.f * k) o.y = ImMax(8.f * k, g_sh - 8.f * k - ph);
            const ImVec2 omax(o.x + pw, o.y + ph);

            dl->AddRectFilled(ImVec2(o.x - 2.f * k, o.y + 2.f * k), ImVec2(omax.x + 2.f * k, omax.y + 4.f * k), IM_COL32(0, 0, 0, int(70 * a)), 24.f * k);
            dl->AddRectFilled(o, omax, col(clr::bg, a), 22.f * k);
            dl->AddRect(o, omax, col(clr::border_light, a), 22.f * k, 0, 1.5f * k);

            const ImVec2 sv_min(o.x + pad, o.y + pad), sv_max(sv_min.x + sv, sv_min.y + sv);
            const ImVec2 hue_min(sv_max.x + gap, sv_min.y), hue_max(hue_min.x + hue_w, sv_max.y);

            ImVec4 hc;
            ImGui::ColorConvertHSVtoRGB(g_hsv.x, 1.f, 1.f, hc.x, hc.y, hc.z);
            const ImU32 hu = IM_COL32(int(hc.x * 255), int(hc.y * 255), int(hc.z * 255), int(255 * a));
            dl->AddRectFilledMultiColor(sv_min, sv_max, IM_COL32(255, 255, 255, int(255 * a)), hu, hu, IM_COL32(255, 255, 255, int(255 * a)));
            dl->AddRectFilledMultiColor(sv_min, sv_max, IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, int(255 * a)), IM_COL32(0, 0, 0, int(255 * a)));
            dl->AddRect(sv_min, sv_max, col(clr::border_dark, a), 0, 0, 1.f * k);

            const float seg = (hue_max.y - hue_min.y) / 6.f;
            for (int i = 0; i < 6; ++i) {
                ImVec4 c1, c2;
                ImGui::ColorConvertHSVtoRGB(i / 6.f, 1.f, 1.f, c1.x, c1.y, c1.z);
                ImGui::ColorConvertHSVtoRGB((i + 1) / 6.f, 1.f, 1.f, c2.x, c2.y, c2.z);
                const ImU32 u1 = IM_COL32(int(c1.x * 255), int(c1.y * 255), int(c1.z * 255), int(255 * a));
                const ImU32 u2 = IM_COL32(int(c2.x * 255), int(c2.y * 255), int(c2.z * 255), int(255 * a));
                dl->AddRectFilledMultiColor(ImVec2(hue_min.x, hue_min.y + seg * i), ImVec2(hue_max.x, hue_min.y + seg * (i + 1)), u1, u1, u2, u2);
            }
            dl->AddRect(hue_min, hue_max, col(clr::border_dark, a), 0, 0, 1.f * k);

            ImGuiIO& io = ImGui::GetIO();
            const ImVec2 mp = io.MousePos;
            if (io.MouseDown[0]) {
                if (g_picker == 0 && ImGui::IsMouseClicked(0)) {
                    if (inside(mp, sv_min, sv_max)) g_picker = 1;
                    else if (inside(mp, hue_min, hue_max)) g_picker = 2;
                    else if (!inside(mp, o, omax)) g_color_open = false; // tap outside closes
                }
                if (g_picker == 1) {
                    g_hsv.y = ImClamp((mp.x - sv_min.x) / (sv_max.x - sv_min.x), 0.f, 1.f);
                    g_hsv.z = 1.f - ImClamp((mp.y - sv_min.y) / (sv_max.y - sv_min.y), 0.f, 1.f);
                } else if (g_picker == 2) {
                    g_hsv.x = ImClamp((mp.y - hue_min.y) / (hue_max.y - hue_min.y), 0.f, 1.f);
                }
                if (g_picker) ImGui::ColorConvertHSVtoRGB(g_hsv.x, g_hsv.y, g_hsv.z, v->x, v->y, v->z);
            } else {
                g_picker = 0;
            }

            const float cx = sv_min.x + g_hsv.y * sv, cy = sv_min.y + (1.f - g_hsv.z) * sv;
            dl->AddCircle(ImVec2(cx, cy), 7.f * k, IM_COL32(0, 0, 0, int(200 * a)), 16, 3.f * k);
            dl->AddCircle(ImVec2(cx, cy), 7.f * k, IM_COL32(255, 255, 255, int(255 * a)), 16, 1.5f * k);
            const float hy = hue_min.y + g_hsv.x * (hue_max.y - hue_min.y);
            dl->AddRectFilled(ImVec2(hue_min.x - 3.f * k, hy - 2.f * k), ImVec2(hue_max.x + 3.f * k, hy + 2.f * k), IM_COL32(255, 255, 255, int(255 * a)), 2.f * k);
        }
    }

    void render() {
        bar::render();

        ImGuiIO& io = ImGui::GetIO();
        float dt = io.DeltaTime;
        if (dt <= 0.f || dt > 0.1f) dt = 0.016f;
        g_ma = lrp(g_ma, bar::g_open ? 1.f : 0.f, ImClamp(12.f * dt, 0.f, 1.f));
        if (g_ma < 0.01f) { g_color_open = false; g_picker = 0; return; }

        tick();
        const float k = style::k();
        const float a = g_ma * bar::game_alpha();

        // One colour for the whole ESP (the name stays white).
        cfg::esp::health_col = cfg::esp::box_col;

        const float pad = 24.f * k;
        const float sx = cfg::menu::scale_x, sy = cfg::menu::scale_y;
        const float w = 330.f * k * sx;
        const float row = 64.f * k * sy;
        const float crow = 72.f * k * sy;
        const float btn = 64.f * k * sy;
        const float ph = pad + row * 4.f + crow + 10.f * k + btn + 6.f * k + 16.f * k;

        ImVec2 p;
        if (g_menu_custom) {
            p = g_menu_pos;
        } else {
            p = ImVec2(bar::g_rect_min.x - (1.f - g_ma) * 40.f * k, bar::g_rect_max.y + 14.f * k);
            if (p.y + ph > g_sh - 8.f * k) p.y = ImMax(8.f * k, g_sh - 8.f * k - ph);
            if (p.x + w > g_sw) p.x = ImMax(0.f, g_sw - w);
        }

        // ---- resize grip, bottom-right corner: scales the panel in x and y ----
        {
            const ImVec2 mp = io.MousePos;
            const float grip = 48.f * k;
            g_grip_min = ImVec2(p.x + w - grip, p.y + ph - grip);
            g_grip_max = ImVec2(p.x + w, p.y + ph);
            if (!g_resize_active && !g_panel_pressed && !bar::g_dragging && g_ma > 0.6f &&
                ImGui::IsMouseClicked(0) && inside(mp, g_grip_min, g_grip_max)) {
                g_resize_active = true;
                g_resize_press = mp;
                g_resize_sx0 = sx;
                g_resize_sy0 = sy;
                g_resize_w0 = w;
                g_resize_h0 = ph;
            }
            if (g_resize_active) {
                if (io.MouseDown[0]) {
                    const float dx = mp.x - g_resize_press.x, dy = mp.y - g_resize_press.y;
                    cfg::menu::scale_x = ImClamp(g_resize_sx0 * (g_resize_w0 + dx) / g_resize_w0, 1.f, 2.5f);
                    cfg::menu::scale_y = ImClamp(g_resize_sy0 * (g_resize_h0 + dy) / g_resize_h0, 1.f, 2.5f);
                } else {
                    g_resize_active = false;
                }
            }
        }

        // ---- panel drag (press anywhere on the panel, move > threshold) ----
        {
            const ImVec2 mp = io.MousePos;
            const ImVec2 cur_max(p.x + w, p.y + ph);
            const float thr = 10.f * k;
            if (!g_panel_pressed && !bar::g_dragging && !g_resize_active && g_ma > 0.6f && ImGui::IsMouseClicked(0) &&
                inside(mp, p, cur_max) && !inside(mp, g_grip_min, g_grip_max)) {
                g_panel_pressed = true;
                g_panel_dragging = false;
                g_panel_press = mp;
                g_panel_grab = ImVec2(mp.x - p.x, mp.y - p.y);
            }
            if (g_panel_pressed) {
                if (io.MouseDown[0]) {
                    const float dx = mp.x - g_panel_press.x, dy = mp.y - g_panel_press.y;
                    if (!g_panel_dragging && g_picker == 0 && dx * dx + dy * dy > thr * thr) g_panel_dragging = true;
                    if (g_panel_dragging) {
                        g_menu_custom = true;
                        g_menu_pos = ImVec2(mp.x - g_panel_grab.x, mp.y - g_panel_grab.y);
                        g_menu_pos.x = ImClamp(g_menu_pos.x, 0.f, ImMax(0.f, g_sw - w));
                        g_menu_pos.y = ImClamp(g_menu_pos.y, 0.f, ImMax(0.f, g_sh - ph));
                        p = g_menu_pos;
                    }
                } else {
                    g_panel_pressed = false;
                    // keep g_panel_dragging true for this frame so release-taps are ignored
                }
            } else {
                g_panel_dragging = false;
            }
        }
        const ImVec2 pmax(p.x + w, p.y + ph);

        ImDrawList* dl = ImGui::GetForegroundDrawList();
        const float r = 28.f * k;
        dl->AddRectFilled(ImVec2(p.x - 2.f * k, p.y + 3.f * k), ImVec2(pmax.x + 2.f * k, pmax.y + 6.f * k), IM_COL32(0, 0, 0, int(80 * a)), r + 2.f * k);
        dl->AddRectFilled(p, pmax, col(clr::bg, a), r);
        dl->PushClipRect(p, pmax, true);
        dots(dl, ImVec2(p.x + 6.f * k, p.y + 6.f * k), ImVec2(pmax.x - 6.f * k, pmax.y - 6.f * k), a, k);
        dl->PopClipRect();
        dl->AddRect(p, pmax, col(clr::border_light, a), r, 0, 1.5f * k);
        for (int i = 1; i <= 3; ++i) {
            const float o = 8.f * k * float(i);
            dl->AddLine(ImVec2(pmax.x - o, pmax.y - 6.f * k), ImVec2(pmax.x - 6.f * k, pmax.y - o), col(clr::text_dim, a), 1.5f * k);
        }

        const float cw = w - pad * 2.f;
        ImVec2 c(p.x + pad, p.y + pad);
        const bool interactive = g_ma > 0.6f;

        struct item { const char* name; bool* value; };
        const item items[] = {
            {"Box", &cfg::esp::box},
            {"Name", &cfg::esp::name},
            {"Health", &cfg::esp::health},
            {"Weapon", &cfg::esp::weapon},
        };
        for (const item& it : items) {
            bool v = *it.value;
            if (toggle_row(dl, it.name, &v, c, cw, row, a, k) && interactive) *it.value = v;
            c.y += row;
        }
        color_row(dl, "Color", &cfg::esp::box_col, c, cw, crow, a, k);
        c.y += crow + 10.f * k;
        if (big_button(dl, "unload", c, cw, btn, a, k) && interactive) {
            status::g_quit_request = true;
        }
        c.y += btn + 6.f * k;

        color_popup(dl, &cfg::esp::box_col, p, pmax, a, k);
        if (!g_panel_pressed) g_panel_dragging = false;
    }
}
