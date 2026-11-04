
// ================================================================================
// Сурс взят из бота @imguisource_bot
// Подпишись на ImGui Source: t.me/ImGui_Source_FIXNEY
// Подпишись на shit src 🧨: t.me/il2cpp_src
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
#include <unordered_map>

extern ImFont* espFont;

// ---- Name cache (1 resolve per frame to avoid stalls) ----
struct name_cache_entry { uint32_t id{}; std::string name; long long refreshed{}; };
static std::unordered_map<uint64_t, name_cache_entry> g_name_cache;
static size_t g_name_cursor = 0;

static void update_name_cache(const std::vector<uint64_t>& players) {
    if (players.empty()) return;
    const uint64_t object = players[g_name_cursor++ % players.size()];
    const uint32_t id = rpm<uint32_t>(object + v100::net_object_id);
    name_cache_entry& entry = g_name_cache[object];
    const long long now = diag::now_ms();
    if (entry.id != id || now - entry.refreshed > 5000) {
        entry.id = id;
        entry.name = player_name(object);
        entry.refreshed = now;
    }
}

// ---- Camera resolution with caching ----
// Heavy resolve (il2cpp, detect_pair scans) runs at most once per 2 seconds.
// Fast path: re-read cached address + offsets (2 rpm calls).
struct cam_cache {
    uint64_t addr{};
    int view_off{-1};
    int proj_off{-1};
    int tier{0};
    long long resolved_at{0};
};
static cam_cache s_cam;

static bool cam_fast_read(const cam_cache& c, matrix& v, matrix& p) {
    if (!c.addr || c.view_off < 0) return false;
    if (!mem_read(uint64_t(c.addr + c.view_off), &v, sizeof(v))) return false;
    if (!mem_read(uint64_t(c.addr + c.proj_off), &p, sizeof(p))) return false;
    const float* vf = reinterpret_cast<const float*>(&v);
    const float* pf = reinterpret_cast<const float*>(&p);
    float vsum = 0, psum = 0;
    for (int i = 0; i < 16; ++i) { vsum += fabsf(vf[i]); psum += fabsf(pf[i]); }
    return vsum > 0.01f && psum > 0.01f;
}

static bool resolve_camera_heavy(uint64_t local_player, const Vector3& local_pos,
                                 matrix& out_view, matrix& out_proj) {
    // Tier 1: main_camera_matrices — il2cpp chain -> native Camera -> detect_pair
    {
        uint64_t native = 0;
        if (main_camera_matrices(local_player, native, out_view, out_proj)) {
            if (g_main_cam.view_off >= 0 && g_main_cam.confirm >= 3) {
                s_cam = {native, g_main_cam.view_off, g_main_cam.proj_off, 1, diag::now_ms()};
            }
            g_camera = native;
            static long long t1_log = 0;
            if (diag::every(t1_log, 5000))
                diag::log("CAMERA", "tier1 native=0x%llx", (unsigned long long)native);
            return true;
        }
    }
    // Tier 2: camera_candidates with detect_pair only
    {
        std::vector<camera_candidate> cams = camera_candidates(local_player);
        float best_dist = 1e9f;
        const camera_candidate* best = nullptr;
        for (const auto& c : cams) {
            if (c.view_off < 0) continue;
            const float* v = reinterpret_cast<const float*>(&c.view);
            const Vector3 cam_pos(
                -(v[0]*v[12] + v[1]*v[13] + v[2]*v[14]),
                -(v[4]*v[12] + v[5]*v[13] + v[6]*v[14]),
                -(v[8]*v[12] + v[9]*v[13] + v[10]*v[14]));
            float d = calculate_distance(cam_pos, local_pos);
            if (d < best_dist) { best_dist = d; best = &c; }
        }
        if (best && best_dist < 30.f) {
            out_view = best->view;
            out_proj = best->projection;
            s_cam = {best->address, best->view_off, best->proj_off, 2, diag::now_ms()};
            g_camera = best->address;
            static long long t2_log = 0;
            if (diag::every(t2_log, 5000))
                diag::log("CAMERA", "tier2 cam=0x%llx dist=%.1f", (unsigned long long)g_camera, best_dist);
            return true;
        }
    }
    // Tier 3: transform fallback (last resort, may have displacement)
    {
        std::vector<camera_candidate> fb = player::camera_fallback_candidates(local_player);
        if (!fb.empty()) {
            out_view = fb[0].view;
            out_proj = fb[0].projection;
            s_cam = {fb[0].address, fb[0].view_off, fb[0].proj_off, 3, diag::now_ms()};
            g_camera = fb[0].address;
            static long long t3_log = 0;
            if (diag::every(t3_log, 5000))
                diag::log("CAMERA", "tier3 fallback cam=0x%llx", (unsigned long long)g_camera);
            return true;
        }
    }
    return false;
}

static bool resolve_camera(uint64_t local_player, const Vector3& local_pos,
                           matrix& out_view, matrix& out_proj) {
    const long long now = diag::now_ms();
    // Fast path: use cached address + offsets (2 rpm reads, no scanning)
    if (s_cam.addr && s_cam.view_off >= 0 && (now - s_cam.resolved_at < 2000)) {
        if (cam_fast_read(s_cam, out_view, out_proj)) {
            g_camera = s_cam.addr;
            return true;
        }
        s_cam = {};
    }
    // Heavy path: full resolution (throttled to once per 2s)
    return resolve_camera_heavy(local_player, local_pos, out_view, out_proj);
}

void visuals::draw() {
    bool any = cfg::esp::box || cfg::esp::name || cfg::esp::health || cfg::esp::distance;
    if (!any) return;

    // ---- Player discovery ----
    std::vector<uint64_t> Players = players();
    if (Players.empty()) return;

    update_name_cache(Players);

    uint64_t LocalPlayer = 0;
    for (uint64_t p : Players) if (player::local(p)) { LocalPlayer = p; break; }
    if (!LocalPlayer) return;

    Vector3 LocalPosition = player::position(LocalPlayer);
    int LocalTeam = player::team(LocalPlayer);
    if (LocalTeam != 1 && LocalTeam != 2) return;
    if (!std::isfinite(LocalPosition.x) ||
        (LocalPosition.x == 0.f && LocalPosition.y == 0.f && LocalPosition.z == 0.f))
        return;

    // ---- Camera: real Unity matrices ----
    matrix ViewMatrix{}, ProjectionMatrix{};
    if (!resolve_camera(LocalPlayer, LocalPosition, ViewMatrix, ProjectionMatrix))
        return;

    // ---- Draw enemies ----
    int projected = 0;

    for (uint64_t Player : Players) {
        if (!Player || Player == LocalPlayer) continue;
        int PlayerTeam = player::team(Player);
        if (PlayerTeam == LocalTeam || (PlayerTeam != 1 && PlayerTeam != 2)) continue;

        // HP check every frame — dead players filtered instantly
        int Health = player::health(Player);
        if (Health <= 0) continue;

        Vector3 PlayerPosition = player::position(Player);
        if (PlayerPosition.x == 0.f && PlayerPosition.y == 0.f && PlayerPosition.z == 0.f) continue;

        float Distance = calculate_distance(PlayerPosition, LocalPosition);
        if (Distance > 500.f) continue;

        Vector3 HeadPosition(PlayerPosition.x, PlayerPosition.y + 1.67f, PlayerPosition.z);

        ImVec2 ScreenHead, ScreenFoot;
        if (!world_to_screen(HeadPosition, ViewMatrix, ProjectionMatrix, ScreenHead) ||
            !world_to_screen(PlayerPosition, ViewMatrix, ProjectionMatrix, ScreenFoot))
            continue;
        ++projected;

        float x1 = roundf(ScreenHead.x);
        float y1 = roundf(fminf(ScreenHead.y, ScreenFoot.y));
        float x2 = roundf(ScreenFoot.x);
        float y2 = roundf(fmaxf(ScreenHead.y, ScreenFoot.y));

        float bh = fabsf(y2 - y1);
        float bw = roundf(bh * 0.25f);
        float cx = roundf((x1 + x2) * 0.5f);

        ImVec2 BoxMin(cx - bw, y1);
        ImVec2 BoxMax(cx + bw, y2);

        float inv_dist = 1.f / (Distance + 0.1f);
        float font_sz = std::clamp(400.f * inv_dist, 10.f, 18.f);

        if (cfg::esp::box) dbox(BoxMin, BoxMax, 1.f);
        if (cfg::esp::health) dhp(Health, BoxMin, BoxMax, bh, 1.f);

        if (cfg::esp::name) {
            const auto found = g_name_cache.find(Player);
            const char* label = found != g_name_cache.end() && !found->second.name.empty()
                ? found->second.name.c_str() : "Enemy";
            dnick(label, BoxMax, cx, font_sz, 1.f);
        }

        if (cfg::esp::distance) ddist(Distance, BoxMax.x, BoxMin.y, font_sz, 1.f);
    }

    static long long frame_log = 0;
    if (diag::every(frame_log, 5000))
        diag::log(projected ? "INFO" : "WARN",
                  "ESP frame players=%zu projected=%d camera=0x%llx",
                  Players.size(), projected, (unsigned long long)g_camera);
}

void visuals::dbox(const ImVec2& t, const ImVec2& b, float a) {
    if (a < 0.01f) return;

    ImDrawList* dl = ImGui::GetBackgroundDrawList();

    float x1 = t.x, y1 = t.y, x2 = b.x, y2 = b.y;
    float r = cfg::esp::box_rounding;

    ImU32 col = IM_COL32(
        static_cast<int>(cfg::esp::box_col.x * 255),
        static_cast<int>(cfg::esp::box_col.y * 255),
        static_cast<int>(cfg::esp::box_col.z * 255),
        static_cast<int>(cfg::esp::box_col.w * 255 * a)
    );

    if (cfg::esp::box_type == 0) {
        dl->AddRect(ImVec2(x1 + 2.f, y1 + 2.f), ImVec2(x2 + 2.f, y2 + 2.f), IM_COL32(0, 0, 0, (int)(100 * a)), r, 0, 2.f);
        dl->AddRect(ImVec2(x1 - 1.f, y1 - 1.f), ImVec2(x2 + 1.f, y2 + 1.f), IM_COL32(0, 0, 0, (int)(180 * a)), r, 0, 1.f);
        dl->AddRect(ImVec2(x1, y1), ImVec2(x2, y2), col, r, 0, 1.f);
    } else {
        float w = x2 - x1, h = y2 - y1;
        float sz = std::min(w, h) * 0.25f;
        float cr = std::min(r, sz * 0.5f);

        ImU32 s1 = IM_COL32(0, 0, 0, (int)(100 * a));
        ImU32 s2 = IM_COL32(0, 0, 0, (int)(180 * a));

        auto corner = [&](float cx, float cy, float dx, float dy) {
            if (cr > 0.5f) {
                float arc_cx = cx + dx * cr;
                float arc_cy = cy + dy * cr;

                float angle_start, angle_end;
                if (dx > 0 && dy > 0) { angle_start = 3.14159265f; angle_end = 4.71238898f; }
                else if (dx < 0 && dy > 0) { angle_start = 4.71238898f; angle_end = 6.28318530f; }
                else if (dx > 0 && dy < 0) { angle_start = 1.57079632f; angle_end = 3.14159265f; }
                else { angle_start = 0.f; angle_end = 1.57079632f; }

                dl->PathArcTo(ImVec2(arc_cx + 2.f * (dx > 0 ? 1 : -1), arc_cy + 2.f * (dy > 0 ? 1 : -1)), cr, angle_start, angle_end, 8);
                dl->PathStroke(s1, 0, 2.f);
                dl->AddLine(ImVec2(cx + dx * cr, cy + 2.f * (dy > 0 ? 1 : -1)), ImVec2(cx + dx * sz, cy + 2.f * (dy > 0 ? 1 : -1)), s1, 2.f);
                dl->AddLine(ImVec2(cx + 2.f * (dx > 0 ? 1 : -1), cy + dy * cr), ImVec2(cx + 2.f * (dx > 0 ? 1 : -1), cy + dy * sz), s1, 2.f);

                dl->PathArcTo(ImVec2(arc_cx, arc_cy), cr, angle_start, angle_end, 8);
                dl->PathStroke(s2, 0, 1.f);
                dl->AddLine(ImVec2(cx + dx * cr, cy), ImVec2(cx + dx * sz, cy), s2, 1.f);
                dl->AddLine(ImVec2(cx, cy + dy * cr), ImVec2(cx, cy + dy * sz), s2, 1.f);

                dl->PathArcTo(ImVec2(arc_cx, arc_cy), cr, angle_start, angle_end, 8);
                dl->PathStroke(col, 0, 1.f);
                dl->AddLine(ImVec2(cx + dx * cr, cy), ImVec2(cx + dx * sz, cy), col, 1.f);
                dl->AddLine(ImVec2(cx, cy + dy * cr), ImVec2(cx, cy + dy * sz), col, 1.f);
            } else {
                dl->AddLine(ImVec2(cx + 2.f * (dx > 0 ? 1 : -1), cy + 2.f * (dy > 0 ? 1 : -1)), ImVec2(cx + dx * sz + 2.f * (dx > 0 ? 1 : -1), cy + 2.f * (dy > 0 ? 1 : -1)), s1, 2.f);
                dl->AddLine(ImVec2(cx + 2.f * (dx > 0 ? 1 : -1), cy + 2.f * (dy > 0 ? 1 : -1)), ImVec2(cx + 2.f * (dx > 0 ? 1 : -1), cy + dy * sz + 2.f * (dy > 0 ? 1 : -1)), s1, 2.f);

                dl->AddLine(ImVec2(cx, cy), ImVec2(cx + dx * sz, cy), s2, 1.f);
                dl->AddLine(ImVec2(cx, cy), ImVec2(cx, cy + dy * sz), s2, 1.f);

                dl->AddLine(ImVec2(cx, cy), ImVec2(cx + dx * sz, cy), col, 1.f);
                dl->AddLine(ImVec2(cx, cy), ImVec2(cx, cy + dy * sz), col, 1.f);
            }
        };

        corner(x1, y1, 1, 1);
        corner(x2, y1, -1, 1);
        corner(x1, y2, 1, -1);
        corner(x2, y2, -1, -1);
    }
}

void visuals::dhp(int hp, const ImVec2& t, const ImVec2& b, float box_h, float a) {
    if (a < 0.01f) return;

    ImDrawList* dl = ImGui::GetBackgroundDrawList();

    hp = std::clamp(hp, 0, 100);
    float pct = hp / 100.f;

    float bx = roundf(t.x - 6.f);
    float bw = 3.f;
    float bh = roundf(b.y - t.y);
    float fh = bh * pct;

    float top_y = t.y;
    float bot_y = b.y;
    float fill_top = roundf(bot_y - fh);

    dl->AddRectFilled(ImVec2(bx - 2.f, top_y - 2.f), ImVec2(bx + bw + 2.f, bot_y + 2.f), IM_COL32(0, 0, 0, (int)(120 * a)), 1.f);
    dl->AddRectFilled(ImVec2(bx - 1.f, top_y - 1.f), ImVec2(bx + bw + 1.f, bot_y + 1.f), IM_COL32(0, 0, 0, (int)(200 * a)), 0.f);
    dl->AddRect(ImVec2(bx - 1.f, top_y - 1.f), ImVec2(bx + bw + 1.f, bot_y + 1.f), IM_COL32(30, 30, 30, (int)(255 * a)), 0, 0, 1.f);

    ImU32 col_top = IM_COL32(
        static_cast<int>(cfg::esp::health_col.x * 255),
        static_cast<int>(cfg::esp::health_col.y * 255),
        static_cast<int>(cfg::esp::health_col.z * 255),
        static_cast<int>(cfg::esp::health_col.w * 255 * a)
    );
    ImU32 col_bot = IM_COL32(
        static_cast<int>(cfg::esp::health_col.x * 180),
        static_cast<int>(cfg::esp::health_col.y * 180),
        static_cast<int>(cfg::esp::health_col.z * 180),
        static_cast<int>(cfg::esp::health_col.w * 255 * a)
    );

    if (fh > 1.f) {
        dl->AddRectFilledMultiColor(ImVec2(bx, fill_top), ImVec2(bx + bw, bot_y), col_top, col_top, col_bot, col_bot);
    }

    if (hp < 100 && espFont && bh > 20.f) {
        char txt[8];
        snprintf(txt, sizeof(txt), "%d", hp);
        float fs = 10.f;
        ImVec2 ts = espFont->CalcTextSizeA(fs, FLT_MAX, 0.f, txt);
        float tx = roundf(bx + (bw - ts.x) * 0.5f);
        float ty = roundf(fill_top - ts.y * 0.5f);
        if (ty < top_y) ty = top_y;
        if (ty + ts.y > bot_y) ty = bot_y - ts.y;
        draw_text_outlined(dl, espFont, fs, ImVec2(tx, ty), IM_COL32(255, 255, 255, (int)(255 * a)), txt);
    }
}

void visuals::dnick(const char* name, const ImVec2& box_max, float cx, float sz, float a) {
    if (!espFont || a < 0.01f || !name) return;

    ImDrawList* dl = ImGui::GetBackgroundDrawList();

    ImVec2 ts = espFont->CalcTextSizeA(sz, FLT_MAX, 0.f, name);
    ImVec2 tp(roundf(cx - ts.x * 0.5f), roundf(box_max.y + 4.f));

    ImU32 col = IM_COL32(
        static_cast<int>(cfg::esp::name_col.x * 255),
        static_cast<int>(cfg::esp::name_col.y * 255),
        static_cast<int>(cfg::esp::name_col.z * 255),
        static_cast<int>(cfg::esp::name_col.w * 255 * a)
    );
    draw_text_outlined(dl, espFont, sz, tp, col, name);
}

void visuals::ddist(float dist, float x, float y, float sz, float a) {
    if (!espFont || a < 0.01f) return;

    ImDrawList* dl = ImGui::GetBackgroundDrawList();

    char txt[16];
    snprintf(txt, sizeof(txt), "%dm", static_cast<int>(dist));

    ImVec2 tp(roundf(x + 5.f), roundf(y));

    ImU32 col = IM_COL32(
        static_cast<int>(cfg::esp::distance_col.x * 255),
        static_cast<int>(cfg::esp::distance_col.y * 255),
        static_cast<int>(cfg::esp::distance_col.z * 255),
        static_cast<int>(cfg::esp::distance_col.w * 255 * a)
    );
    draw_text_outlined(dl, espFont, sz, tp, col, txt);
}

void visuals::draw_text_outlined(ImDrawList* dl, ImFont* font, float size, const ImVec2& pos, ImU32 color, const char* text) {
    if (!font || !dl) return;

    int a = (color >> IM_COL32_A_SHIFT) & 0xFF;
    int s1 = static_cast<int>(a * 0.4f);
    int s2 = static_cast<int>(a * 0.7f);

    dl->AddText(font, size, ImVec2(pos.x + 2.f, pos.y + 2.f), IM_COL32(0, 0, 0, s1), text);
    dl->AddText(font, size, ImVec2(pos.x + 1.f, pos.y + 1.f), IM_COL32(0, 0, 0, s2), text);
    dl->AddText(font, size, pos, color, text);
}
