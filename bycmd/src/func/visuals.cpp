
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
#include "../other/safe_read.hpp"
#include "../protect/oxorany.hpp"
#include "imgui.h"
#include <cmath>
#include <algorithm>
#include <unordered_map>

extern ImFont* espFont;

// ---- Cached state: heavy reads at ~30Hz, draw at full framerate ----

struct cached_enemy {
    uint64_t object;
    uint64_t native_transform;
    int health;
};

static struct {
    std::vector<cached_enemy> enemies;
    uint64_t local_player{};
    uint64_t local_native{};
    int local_team{};
    uint64_t cam_native{};
    uint64_t cam_state{};
    float cam_fov{60.f};
    long long last_heavy{};
    bool ready{};
} g_fc;

static std::unordered_map<uint64_t, std::string> g_names;
static size_t g_name_idx = 0;

static uint64_t first_native(uint64_t object) {
    auto candidates = player::transform_candidates(object);
    for (uint64_t managed : candidates) {
        uint64_t native = rpm<uint64_t>(managed + v100::unity_cached_ptr);
        if (native) return native;
    }
    return 0;
}

static bool refresh_camera(uint64_t local_player) {
    if (player::g_stable_cam_controller) {
        const uint64_t cs_class = rpm<uint64_t>(proc::lib + v100::camera_state_typeinfo);
        const uint64_t state = rpm<uint64_t>(player::g_stable_cam_controller + v100::cc_state);
        if (state && cs_class && exact_class(state, cs_class)) {
            const uint64_t mt = rpm<uint64_t>(state + v100::cs_value_transform);
            const uint64_t nt = mt ? rpm<uint64_t>(mt + v100::unity_cached_ptr) : 0;
            if (nt) {
                g_fc.cam_native = nt;
                g_fc.cam_state = state;
                float fov = rpm<float>(state + v100::cs_fov_float);
                if (std::isfinite(fov) && fov >= 5.f && fov <= 170.f)
                    g_fc.cam_fov = fov;
                return true;
            }
        }
        player::g_stable_cam_controller = 0;
    }
    player::stable_camera cam = player::resolve_camera_state(local_player);
    if (!cam.valid) return false;
    g_fc.cam_fov = cam.fov;
    if (player::g_stable_cam_controller) {
        const uint64_t cs_class = rpm<uint64_t>(proc::lib + v100::camera_state_typeinfo);
        const uint64_t state = rpm<uint64_t>(player::g_stable_cam_controller + v100::cc_state);
        if (state && cs_class && exact_class(state, cs_class)) {
            const uint64_t mt = rpm<uint64_t>(state + v100::cs_value_transform);
            const uint64_t nt = mt ? rpm<uint64_t>(mt + v100::unity_cached_ptr) : 0;
            if (nt) { g_fc.cam_native = nt; g_fc.cam_state = state; return true; }
        }
    }
    g_fc.cam_native = 0;
    return false;
}

static void heavy_tick() {
    std::vector<uint64_t> all = players();
    if (all.empty()) { g_fc.ready = false; return; }

    uint64_t local = 0;
    for (uint64_t p : all) if (player::local(p)) { local = p; break; }
    if (!local) { g_fc.ready = false; return; }

    int team = player::team(local);
    if (team != 1 && team != 2) { g_fc.ready = false; return; }

    g_fc.local_player = local;
    g_fc.local_team = team;
    g_fc.local_native = first_native(local);

    if (!refresh_camera(local)) { g_fc.ready = false; return; }

    g_fc.enemies.clear();
    for (uint64_t p : all) {
        if (!p || p == local) continue;
        int pt = player::team(p);
        if (pt == team || (pt != 1 && pt != 2)) continue;
        int hp = player::health(p);
        if (hp <= 0) continue;
        uint64_t nt = first_native(p);
        if (!nt) continue;
        g_fc.enemies.push_back({p, nt, hp});
    }

    if (!g_fc.enemies.empty()) {
        const auto& e = g_fc.enemies[g_name_idx++ % g_fc.enemies.size()];
        if (g_names.find(e.object) == g_names.end())
            g_names[e.object] = player_name(e.object);
    }

    for (auto it = g_names.begin(); it != g_names.end();) {
        bool found = false;
        for (const auto& e : g_fc.enemies)
            if (e.object == it->first) { found = true; break; }
        if (!found) it = g_names.erase(it);
        else ++it;
    }

    g_fc.ready = true;

    static long long diag_ms = 0;
    if (diag::every(diag_ms, 5000))
        diag::log("INFO", "heavy_tick players=%zu enemies=%zu local=0x%llx cam=0x%llx fov=%.1f",
                  all.size(), g_fc.enemies.size(), (unsigned long long)local,
                  (unsigned long long)g_fc.cam_native, g_fc.cam_fov);
}


void visuals::draw() {
    bool any = cfg::esp::box || cfg::esp::name || cfg::esp::health || cfg::esp::distance;
    if (!any) return;

    safe::g_active = 1;
    if (sigsetjmp(safe::g_jmp, 1) != 0) {
        static long long crash_log = 0;
        if (diag::every(crash_log, 2000))
            diag::log("SAFE", "ESP draw recovered from crash");
        return;
    }

    const long long now = diag::now_ms();
    if (now - g_fc.last_heavy >= 33) {
        g_fc.last_heavy = now;
        heavy_tick();
    }

    if (!g_fc.ready || g_fc.enemies.empty() || !g_fc.cam_native) {
        safe::g_active = 0;
        return;
    }

    // ---- Per-frame: only fast reads from cached native pointers ----

    Vector3 cam_pos{};
    Vector4 cam_rot{};
    if (!player::decode_transform_pose(g_fc.cam_native, cam_pos, cam_rot)) {
        safe::g_active = 0;
        return;
    }

    if (g_fc.cam_state) {
        float fov = rpm<float>(g_fc.cam_state + v100::cs_fov_float);
        if (std::isfinite(fov) && fov >= 5.f && fov <= 170.f)
            g_fc.cam_fov = fov;
    }

    matrix vp = mat_mul(
        player::perspective(g_fc.cam_fov, g_sw / std::max(g_sh, 1.f)),
        player::view_from_pose(cam_pos, cam_rot));

    Vector3 local_pos{};
    if (g_fc.local_native)
        player::native_transform_position(g_fc.local_native, local_pos);

    int projected = 0;

    for (const auto& enemy : g_fc.enemies) {
        Vector3 pos{};
        if (!player::native_transform_position(enemy.native_transform, pos)) continue;
        if (pos.x == 0.f && pos.y == 0.f && pos.z == 0.f) continue;

        float dist = calculate_distance(pos, local_pos);
        if (dist > 500.f) continue;

        Vector3 head(pos.x, pos.y + 1.67f, pos.z);

        ImVec2 sh, sf;
        if (!world_to_screen_vp(head, vp, sh) ||
            !world_to_screen_vp(pos, vp, sf)) continue;
        ++projected;

        float x1 = roundf(sh.x);
        float y1 = roundf(fminf(sh.y, sf.y));
        float x2 = roundf(sf.x);
        float y2 = roundf(fmaxf(sh.y, sf.y));

        float bh = fabsf(y2 - y1);
        float bw = roundf(bh * 0.25f);
        float cx = roundf((x1 + x2) * 0.5f);

        ImVec2 box_min(cx - bw, y1);
        ImVec2 box_max(cx + bw, y2);

        float inv_dist = 1.f / (dist + 0.1f);
        float font_sz = std::clamp(400.f * inv_dist, 10.f, 18.f);

        if (cfg::esp::box) dbox(box_min, box_max, 1.f);
        if (cfg::esp::health) dhp(enemy.health, box_min, box_max, bh, 1.f);

        if (cfg::esp::name) {
            auto it = g_names.find(enemy.object);
            const char* label = (it != g_names.end() && !it->second.empty())
                ? it->second.c_str() : "Enemy";
            dnick(label, box_max, cx, font_sz, 1.f);
        }

        if (cfg::esp::distance) ddist(dist, box_max.x, box_min.y, font_sz, 1.f);
    }

    safe::g_active = 0;

    static long long frame_log = 0;
    if (diag::every(frame_log, 5000))
        diag::log(projected ? "INFO" : "WARN",
                  "ESP frame enemies=%zu projected=%d fov=%.1f cam=(%.2f %.2f %.2f)",
                  g_fc.enemies.size(), projected, g_fc.cam_fov,
                  cam_pos.x, cam_pos.y, cam_pos.z);
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
