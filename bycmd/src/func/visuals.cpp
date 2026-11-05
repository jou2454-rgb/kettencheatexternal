
// ================================================================================
// ESP rewrite — Enot approach adapted for external
// Read real V/P matrices from native Camera at hardcoded offsets (0x70/0xB0)
// No Transform fallback = no displacement
// ================================================================================

#include "visuals.hpp"
#include "../game/game.hpp"
#include "../game/math.hpp"
#include "../game/player.hpp"
#include "../ui/theme/theme.hpp"
#include "../ui/cfg.hpp"
#include "../protect/oxorany.hpp"
#include "imgui.h"
#include <cmath>
#include <algorithm>

extern ImFont* espFont;

// ---- Cached camera: address + hardcoded offsets ----
static uint64_t s_cam_addr = 0;
static long long s_cam_resolved = 0;

static bool read_camera_matrices(uint64_t native_cam, matrix& view, matrix& proj) {
    if (!native_cam) return false;
    if (!mem_read(native_cam + v100::camera_view, &view, sizeof(view))) return false;
    if (!mem_read(native_cam + v100::camera_projection, &proj, sizeof(proj))) return false;
    if (!finite_matrix(view) || !finite_matrix(proj)) return false;
    const float* v = reinterpret_cast<const float*>(&view);
    const float* p = reinterpret_cast<const float*>(&proj);
    if (std::fabs(v[3]) > 0.01f || std::fabs(v[7]) > 0.01f ||
        std::fabs(v[15] - 1.f) > 0.05f) return false;
    if (std::fabs(p[0]) < 0.001f || std::fabs(p[5]) < 0.001f) return false;
    return true;
}

static bool resolve_camera(uint64_t local_player, const Vector3& local_pos,
                           matrix& out_view, matrix& out_proj) {
    const long long now = diag::now_ms();

    // Fast path: cached camera, just re-read matrices (2 rpm calls)
    if (s_cam_addr && (now - s_cam_resolved < 3000)) {
        if (read_camera_matrices(s_cam_addr, out_view, out_proj)) {
            g_camera = s_cam_addr;
            return true;
        }
        s_cam_addr = 0;
    }

    // Slow path: find camera through camera_candidates() (runs every ~3s)
    std::vector<camera_candidate> cams = camera_candidates(local_player);

    // Pick best camera: closest to local player, validated by hardcoded offsets
    float best_dist = 1e9f;
    uint64_t best_addr = 0;

    for (const auto& c : cams) {
        matrix v{}, p{};
        if (!read_camera_matrices(c.address, v, p)) continue;

        // Extract camera position from view matrix
        const float* vf = reinterpret_cast<const float*>(&v);
        const Vector3 cam_pos(
            -(vf[0]*vf[12] + vf[1]*vf[13] + vf[2]*vf[14]),
            -(vf[4]*vf[12] + vf[5]*vf[13] + vf[6]*vf[14]),
            -(vf[8]*vf[12] + vf[9]*vf[13] + vf[10]*vf[14]));
        float d = calculate_distance(cam_pos, local_pos);

        if (d < best_dist) {
            best_dist = d;
            best_addr = c.address;
            out_view = v;
            out_proj = p;
        }
    }

    if (best_addr && best_dist < 50.f) {
        s_cam_addr = best_addr;
        s_cam_resolved = now;
        g_camera = best_addr;
        static long long log_t = 0;
        if (diag::every(log_t, 5000))
            diag::log("CAMERA", "resolved cam=0x%llx dist=%.1f candidates=%zu",
                      (unsigned long long)best_addr, best_dist, cams.size());
        return true;
    }

    static long long fail_log = 0;
    if (diag::every(fail_log, 5000))
        diag::log("CAMERA", "NO valid camera found, candidates=%zu", cams.size());
    return false;
}

// ---- ESP draw ----

void visuals::draw() {
    bool any = cfg::esp::box || cfg::esp::name || cfg::esp::health || cfg::esp::distance;
    if (!any) return;

    std::vector<uint64_t> Players = players();
    if (Players.empty()) return;

    uint64_t LocalPlayer = 0;
    for (uint64_t p : Players) if (player::local(p)) { LocalPlayer = p; break; }
    if (!LocalPlayer) return;

    Vector3 LocalPosition = player::position(LocalPlayer);
    int LocalTeam = player::team(LocalPlayer);
    if (LocalTeam != 1 && LocalTeam != 2) return;
    if (!std::isfinite(LocalPosition.x) ||
        (LocalPosition.x == 0.f && LocalPosition.y == 0.f && LocalPosition.z == 0.f))
        return;

    // Camera: real Unity matrices from native Camera (no Transform guess)
    matrix ViewMatrix{}, ProjectionMatrix{};
    if (!resolve_camera(LocalPlayer, LocalPosition, ViewMatrix, ProjectionMatrix))
        return;

    ImVec2 screen = ImGui::GetIO().DisplaySize;

    for (uint64_t Player : Players) {
        if (!Player || Player == LocalPlayer) continue;
        int PlayerTeam = player::team(Player);
        if (PlayerTeam == LocalTeam || (PlayerTeam != 1 && PlayerTeam != 2)) continue;

        // HP every frame — skip dead instantly
        int Health = player::health(Player);
        if (Health <= 0) continue;

        Vector3 PlayerPosition = player::position(Player);
        if (PlayerPosition.x == 0.f && PlayerPosition.y == 0.f && PlayerPosition.z == 0.f)
            continue;

        float Distance = calculate_distance(PlayerPosition, LocalPosition);
        if (Distance > 500.f) continue;

        Vector3 HeadPosition(PlayerPosition.x, PlayerPosition.y + 1.67f, PlayerPosition.z);

        ImVec2 ScreenHead, ScreenFoot;
        if (!world_to_screen(HeadPosition, ViewMatrix, ProjectionMatrix, ScreenHead) ||
            !world_to_screen(PlayerPosition, ViewMatrix, ProjectionMatrix, ScreenFoot))
            continue;

        float x1 = roundf(ScreenHead.x);
        float y1 = roundf(fminf(ScreenHead.y, ScreenFoot.y));
        float x2 = roundf(ScreenFoot.x);
        float y2 = roundf(fmaxf(ScreenHead.y, ScreenFoot.y));

        float bh = fabsf(y2 - y1);
        if (bh < 4.f) continue;
        float bw = roundf(bh * 0.25f);
        float cx = roundf((x1 + x2) * 0.5f);

        ImVec2 BoxMin(cx - bw, y1);
        ImVec2 BoxMax(cx + bw, y2);

        float inv_dist = 1.f / (Distance + 0.1f);
        float font_sz = std::clamp(400.f * inv_dist, 10.f, 18.f);

        if (cfg::esp::box) dbox(BoxMin, BoxMax, 1.f);
        if (cfg::esp::health) dhp(Health, BoxMin, BoxMax, bh, 1.f);

        if (cfg::esp::name) {
            std::string nm = player_name(Player);
            const char* label = nm.empty() ? "Enemy" : nm.c_str();
            dnick(label, BoxMax, cx, font_sz, 1.f);
        }

        if (cfg::esp::distance) ddist(Distance, BoxMax.x, BoxMin.y, font_sz, 1.f);
    }
}

// ---- Drawing helpers ----

void visuals::dbox(const ImVec2& t, const ImVec2& b, float a) {
    if (a < 0.01f) return;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    float x1 = t.x, y1 = t.y, x2 = b.x, y2 = b.y;
    float r = cfg::esp::box_rounding;

    ImU32 col = IM_COL32(
        static_cast<int>(cfg::esp::box_col.x * 255),
        static_cast<int>(cfg::esp::box_col.y * 255),
        static_cast<int>(cfg::esp::box_col.z * 255),
        static_cast<int>(cfg::esp::box_col.w * 255 * a));

    if (cfg::esp::box_type == 0) {
        dl->AddRect(ImVec2(x1+2,y1+2), ImVec2(x2+2,y2+2), IM_COL32(0,0,0,(int)(100*a)), r, 0, 2.f);
        dl->AddRect(ImVec2(x1-1,y1-1), ImVec2(x2+1,y2+1), IM_COL32(0,0,0,(int)(180*a)), r, 0, 1.f);
        dl->AddRect(ImVec2(x1,y1), ImVec2(x2,y2), col, r, 0, 1.f);
    } else {
        float w = x2-x1, h = y2-y1;
        float sz = std::min(w,h) * 0.25f;
        ImU32 s1 = IM_COL32(0,0,0,(int)(100*a));
        ImU32 s2 = IM_COL32(0,0,0,(int)(180*a));
        auto corner = [&](float cx, float cy, float dx, float dy) {
            float ox = dx>0?1.f:-1.f, oy = dy>0?1.f:-1.f;
            dl->AddLine(ImVec2(cx+2*ox,cy+2*oy), ImVec2(cx+dx*sz+2*ox,cy+2*oy), s1, 2.f);
            dl->AddLine(ImVec2(cx+2*ox,cy+2*oy), ImVec2(cx+2*ox,cy+dy*sz+2*oy), s1, 2.f);
            dl->AddLine(ImVec2(cx,cy), ImVec2(cx+dx*sz,cy), s2, 1.f);
            dl->AddLine(ImVec2(cx,cy), ImVec2(cx,cy+dy*sz), s2, 1.f);
            dl->AddLine(ImVec2(cx,cy), ImVec2(cx+dx*sz,cy), col, 1.f);
            dl->AddLine(ImVec2(cx,cy), ImVec2(cx,cy+dy*sz), col, 1.f);
        };
        corner(x1,y1,1,1); corner(x2,y1,-1,1);
        corner(x1,y2,1,-1); corner(x2,y2,-1,-1);
    }
}

void visuals::dhp(int hp, const ImVec2& t, const ImVec2& b, float box_h, float a) {
    if (a < 0.01f) return;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    hp = std::clamp(hp, 0, 100);
    float pct = hp / 100.f;
    float bx = roundf(t.x - 6.f), bw = 3.f;
    float top_y = t.y, bot_y = b.y;
    float fh = (bot_y - top_y) * pct;
    float fill_top = roundf(bot_y - fh);

    dl->AddRectFilled(ImVec2(bx-2,top_y-2), ImVec2(bx+bw+2,bot_y+2), IM_COL32(0,0,0,(int)(120*a)), 1.f);
    dl->AddRectFilled(ImVec2(bx-1,top_y-1), ImVec2(bx+bw+1,bot_y+1), IM_COL32(0,0,0,(int)(200*a)), 0.f);
    dl->AddRect(ImVec2(bx-1,top_y-1), ImVec2(bx+bw+1,bot_y+1), IM_COL32(30,30,30,(int)(255*a)), 0, 0, 1.f);

    ImU32 col_top = IM_COL32(
        static_cast<int>(cfg::esp::health_col.x*255),
        static_cast<int>(cfg::esp::health_col.y*255),
        static_cast<int>(cfg::esp::health_col.z*255),
        static_cast<int>(cfg::esp::health_col.w*255*a));
    ImU32 col_bot = IM_COL32(
        static_cast<int>(cfg::esp::health_col.x*180),
        static_cast<int>(cfg::esp::health_col.y*180),
        static_cast<int>(cfg::esp::health_col.z*180),
        static_cast<int>(cfg::esp::health_col.w*255*a));

    if (fh > 1.f)
        dl->AddRectFilledMultiColor(ImVec2(bx,fill_top), ImVec2(bx+bw,bot_y), col_top, col_top, col_bot, col_bot);

    if (hp < 100 && espFont && box_h > 20.f) {
        char txt[8]; snprintf(txt, sizeof(txt), "%d", hp);
        float fs = 10.f;
        ImVec2 ts = espFont->CalcTextSizeA(fs, FLT_MAX, 0.f, txt);
        float tx = roundf(bx + (bw-ts.x)*0.5f);
        float ty = roundf(fill_top - ts.y*0.5f);
        ty = std::clamp(ty, top_y, bot_y - ts.y);
        draw_text_outlined(dl, espFont, fs, ImVec2(tx,ty), IM_COL32(255,255,255,(int)(255*a)), txt);
    }
}

void visuals::dnick(const char* name, const ImVec2& box_max, float cx, float sz, float a) {
    if (!espFont || a < 0.01f || !name) return;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    ImVec2 ts = espFont->CalcTextSizeA(sz, FLT_MAX, 0.f, name);
    ImVec2 tp(roundf(cx - ts.x*0.5f), roundf(box_max.y + 4.f));
    ImU32 col = IM_COL32(
        static_cast<int>(cfg::esp::name_col.x*255),
        static_cast<int>(cfg::esp::name_col.y*255),
        static_cast<int>(cfg::esp::name_col.z*255),
        static_cast<int>(cfg::esp::name_col.w*255*a));
    draw_text_outlined(dl, espFont, sz, tp, col, name);
}

void visuals::ddist(float dist, float x, float y, float sz, float a) {
    if (!espFont || a < 0.01f) return;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    char txt[16]; snprintf(txt, sizeof(txt), "%dm", static_cast<int>(dist));
    ImVec2 tp(roundf(x + 5.f), roundf(y));
    ImU32 col = IM_COL32(
        static_cast<int>(cfg::esp::distance_col.x*255),
        static_cast<int>(cfg::esp::distance_col.y*255),
        static_cast<int>(cfg::esp::distance_col.z*255),
        static_cast<int>(cfg::esp::distance_col.w*255*a));
    draw_text_outlined(dl, espFont, sz, tp, col, txt);
}

void visuals::draw_text_outlined(ImDrawList* dl, ImFont* font, float size, const ImVec2& pos, ImU32 color, const char* text) {
    if (!font || !dl) return;
    int a = (color >> IM_COL32_A_SHIFT) & 0xFF;
    dl->AddText(font, size, ImVec2(pos.x+2,pos.y+2), IM_COL32(0,0,0,(int)(a*0.4f)), text);
    dl->AddText(font, size, ImVec2(pos.x+1,pos.y+1), IM_COL32(0,0,0,(int)(a*0.7f)), text);
    dl->AddText(font, size, pos, color, text);
}
