#include "visuals.hpp"
#include "../game/game.hpp"
#include "../game/math.hpp"
#include "../game/player.hpp"
#include "../ui/theme/theme.hpp"
#include "../ui/cfg.hpp"
#include "../protect/oxorany.hpp"
#include "imgui.h"
#include <cmath>
#include <cstring>
#include <algorithm>
#include <unordered_map>

extern ImFont* espFont;

static void esp_wait(const char* stage) {
    static long long last = 0;
    if (diag::every(last, 5000)) diag::log("WAIT", "ESP blocked at: %s", stage);
}

void visuals::draw() {
    bool any = cfg::esp::box || cfg::esp::name || cfg::esp::health ||
               cfg::esp::distance || cfg::esp::snapline;
    if (!any) { esp_wait("all ESP options disabled"); return; }

    std::vector<uint64_t> Players = players();
    if (Players.empty()) { esp_wait("player discovery / registry scan"); return; }

    struct name_cache_entry { uint32_t id{}; std::string name; long long refreshed{}; };
    static std::unordered_map<uint64_t, name_cache_entry> name_cache;
    static size_t name_cursor = 0;
    {
        const uint64_t object = Players[name_cursor++ % Players.size()];
        const uint32_t id = rpm<uint32_t>(object + v100::net_object_id);
        const long long now = diag::now_ms();
        name_cache_entry& entry = name_cache[object];
        if (entry.id != id || now - entry.refreshed > 5000) {
            entry.id = id;
            entry.name = player_name(object);
            entry.refreshed = now;
        }
    }

    uint64_t LocalPlayer = 0;
    for (uint64_t p : Players) if (player::local(p)) { LocalPlayer = p; break; }
    if (!LocalPlayer) { esp_wait("local player flag"); return; }

    struct alive_entry { uint32_t net_id{}; uint8_t wid{}; bool armed{}; bool has{}; };
    static std::unordered_map<uint64_t, alive_entry> alive_cache;
    static size_t alive_cursor = 0;
    if (!Players.empty()) {
        const uint64_t probe = Players[alive_cursor++ % Players.size()];
        const uint32_t nid = rpm<uint32_t>(probe + v100::net_object_id);
        uint8_t wid = 0;
        bool got = false;
        const std::vector<uint64_t> wps = player::find_weapon_parts(probe);
        if (!wps.empty()) {
            const uint64_t act = rpm<uint64_t>(wps[0] + 0x80);
            if (obj_ptr(act)) { wid = rpm<uint8_t>(act + 0x10); got = true; }
        }
        if (got) {
            alive_entry& e = alive_cache[probe];
            if (!e.has || e.net_id != nid) { e.net_id = nid; e.armed = false; e.has = true; }
            e.wid = wid;
            if (wid) e.armed = true;
        }
        if (alive_cache.size() > 24) {
            for (auto it = alive_cache.begin(); it != alive_cache.end();) {
                if (std::find(Players.begin(), Players.end(), it->first) == Players.end())
                    it = alive_cache.erase(it);
                else ++it;
            }
        }
    }
    auto is_dead = [&](uint64_t object) -> bool {
        auto it = alive_cache.find(object);
        return it != alive_cache.end() && it->second.has && it->second.armed && it->second.wid == 0;
    };

    Vector3 LocalPosition = player::position(LocalPlayer);
    int LocalTeam = player::team(LocalPlayer);
    if (LocalTeam != 1 && LocalTeam != 2) { esp_wait("local team validation"); return; }
    if (!std::isfinite(LocalPosition.x) ||
        (LocalPosition.x == 0.f && LocalPosition.y == 0.f && LocalPosition.z == 0.f)) {
        esp_wait("local transform hierarchy"); return;
    }

    int enemies = 0, alive = 0, positioned = 0, projected = 0, hidden_dead = 0;
    struct enemy_frame {
        uint64_t object{};
        Vector3 position{};
        int health{};
        float distance{};
    };
    std::vector<enemy_frame> frame_enemies;
    for (uint64_t Player : Players) {
        if (!Player || Player == LocalPlayer) continue;
        int PlayerTeam = player::team(Player);
        if (PlayerTeam == LocalTeam || (PlayerTeam != 1 && PlayerTeam != 2)) continue;
        if (is_dead(Player)) { ++hidden_dead; continue; }
        ++enemies;

        Vector3 PlayerPosition = player::position(Player);
        if (PlayerPosition.x == 0.f && PlayerPosition.y == 0.f && PlayerPosition.z == 0.f) continue;
        ++positioned;

        int Health = player::health(Player);
        if (Health <= 0) continue;
        ++alive;

        float Distance = calculate_distance(PlayerPosition, LocalPosition);
        if (Distance > 500.f) continue;

        frame_enemies.push_back({Player, PlayerPosition, Health, Distance});
    }
    static long long dead_log = 0;
    if (hidden_dead && diag::every(dead_log, 5000))
        diag::log("ESP", "hiding %d dead players", hidden_dead);

    matrix ViewMatrix{}, ProjectionMatrix{};
    uint64_t selected_camera = 0;
    bool direct_used = false;
    {
        matrix mv{}, mp{};
        uint64_t mn = 0;
        if (main_camera_matrices(LocalPlayer, mn, mv, mp) && mn) {
            const float* v = reinterpret_cast<const float*>(&mv);
            const float* p = reinterpret_cast<const float*>(&mp);
            const bool sane = p[5] >= 0.5f && p[5] <= 12.f &&
                              std::fabs(p[11]) > .5f && std::fabs(p[15]) < .1f &&
                              std::fabs(v[3]) < .001f && std::fabs(v[7]) < .001f &&
                              std::fabs(v[11]) < .001f && std::fabs(v[15] - 1.f) < .01f;
            if (sane) {
                ViewMatrix = mv;
                ProjectionMatrix = mp;
                selected_camera = mn;
                direct_used = true;
                g_camera = mn;
                static long long main_log = 0;
                if (diag::every(main_log, 5000))
                    diag::log("CAMERA", "direct main-cam native=0x%llx fov=%.1f proj=(%.3f %.3f)",
                              (unsigned long long)mn,
                              2.f * std::atan(1.f / p[5]) * 57.29578f, p[0], p[5]);
            }
        }
    }

    if (!direct_used) {
        std::vector<camera_candidate> Cameras = camera_candidates(LocalPlayer);
        if (Cameras.empty()) { esp_wait("camera discovery"); return; }

        float selected_score = -1.f;
        int selected_front = 0, selected_onscreen = 0;
        float selected_camera_distance = 1000000.f;

        for (const auto& Camera : Cameras) {
            const float* p = reinterpret_cast<const float*>(&Camera.projection);
            if (p[5] < 0.5f || p[5] > 12.f) continue;

            int front = 0, onscreen = 0;
            float camera_distance = 0.f;

            for (const auto& Enemy : frame_enemies) {
                ImVec2 sp;
                if (world_to_screen(Enemy.position, Camera.view, Camera.projection, sp)) {
                    ++onscreen;
                    if (sp.x > 0.f && sp.x < g_sw && sp.y > 0.f && sp.y < g_sh) ++front;
                }
                camera_distance += Enemy.distance;
            }

            float score = float(front) * 10.f + float(onscreen);
            if (score > selected_score ||
                (score == selected_score && camera_distance < selected_camera_distance)) {
                selected_score = score;
                selected_front = front;
                selected_onscreen = onscreen;
                selected_camera_distance = camera_distance;
                selected_camera = Camera.address;
                ViewMatrix = Camera.view;
                ProjectionMatrix = Camera.projection;
            }
        }

        if (!selected_camera || (selected_camera_distance >= 30.f && selected_onscreen == 0)) {
            esp_wait("camera scoring"); return;
        }
        if (g_camera != selected_camera) {
            diag::log("CAMERA", "selected=0x%llx score=%.0f front=%d onscreen=%d dist=%.1f total=%zu",
                      (unsigned long long)selected_camera, selected_score, selected_front,
                      selected_onscreen, selected_camera_distance, Cameras.size());
        }
        g_camera = selected_camera;

        for (auto& Camera : Cameras) {
            if (Camera.address == selected_camera && Camera.view_off >= 0) {
                matrix v{}, p{};
                if (mem_read(Camera.address + uint64_t(Camera.view_off), &v, sizeof(v)) &&
                    mem_read(Camera.address + uint64_t(Camera.proj_off), &p, sizeof(p))) {
                    const float* vf = reinterpret_cast<const float*>(&v);
                    const float* pf = reinterpret_cast<const float*>(&p);
                    if (matrix_window_sane(vf, true) && matrix_window_sane(pf, false)) {
                        ViewMatrix = v;
                        ProjectionMatrix = p;
                    }
                }
                break;
            }
        }
    }

    {
        const float* p = reinterpret_cast<const float*>(&ProjectionMatrix);
        g_vw = g_sw; g_vh = g_sh; g_vx = 0.f; g_vy = 0.f;
        if (p[0] > 0.01f && p[5] > 0.01f) {
            float cam_aspect = p[5] / p[0];
            float scr_aspect = g_sw / g_sh;
            if (cam_aspect < scr_aspect - 0.02f) {
                g_vw = g_sh * cam_aspect;
                g_vx = (g_sw - g_vw) * 0.5f;
            } else if (cam_aspect > scr_aspect + 0.02f) {
                g_vh = g_sw / cam_aspect;
                g_vy = (g_sh - g_vh) * 0.5f;
            }
        }
        static long long vp_log = 0;
        if (diag::every(vp_log, 10000))
            diag::log("VIEWPORT", "screen=%.0fx%.0f viewport=%.0fx%.0f+%.0f+%.0f aspect_cam=%.4f aspect_scr=%.4f p0=%.4f p5=%.4f",
                      g_sw, g_sh, g_vw, g_vh, g_vx, g_vy,
                      (p[0] > 0.01f && p[5] > 0.01f) ? p[5]/p[0] : 0.f,
                      g_sw / g_sh, p[0], p[5]);
    }

    for (const auto& Enemy : frame_enemies) {
        const uint64_t Player = Enemy.object;
        const Vector3 PlayerPosition = Enemy.position;
        const int Health = Enemy.health;
        const float Distance = Enemy.distance;

        Vector3 HeadPosition(PlayerPosition.x, PlayerPosition.y + 1.67f, PlayerPosition.z);

        ImVec2 ScreenHead, ScreenFoot;
        bool HeadVisible = world_to_screen(HeadPosition, ViewMatrix, ProjectionMatrix, ScreenHead);
        bool FootVisible = world_to_screen(PlayerPosition, ViewMatrix, ProjectionMatrix, ScreenFoot);
        if (!HeadVisible || !FootVisible) continue;
        ++projected;

        float x1 = roundf(ScreenHead.x);
        float y1 = roundf(fminf(ScreenHead.y, ScreenFoot.y));
        float x2 = roundf(ScreenFoot.x);
        float y2 = roundf(fmaxf(ScreenHead.y, ScreenFoot.y));

        float bh = fabsf(y2 - y1);
        float bw = roundf(bh * 0.21f);
        float cx = roundf((x1 + x2) * 0.5f);

        ImVec2 BoxMin(cx - bw, y1 + 1.f);
        ImVec2 BoxMax(cx + bw, y2 - 1.f);

        float inv_dist = 1.f / (Distance + 0.1f);
        float font_sz = std::clamp(320.f * inv_dist, 8.f, 13.f);

        if (cfg::esp::box) {
            dbox(BoxMin, BoxMax, 1.f);
        }

        if (cfg::esp::health) {
            dhp(Health, BoxMin, BoxMax, bh, 1.f);
        }

        if (cfg::esp::name) {
            const auto found = name_cache.find(Player);
            const char* label = found != name_cache.end() && !found->second.name.empty()
                ? found->second.name.c_str() : "Enemy";
            dnick(label, BoxMax, cx, font_sz, 1.f);
        }

        if (cfg::esp::distance) {
            ddist(Distance, BoxMax.x, BoxMin.y, font_sz, 1.f);
        }

        if (cfg::esp::snapline) {
            dline(ImVec2(cx, y2), 1.f);
        }
    }

    static long long frame_log = 0;
    if (diag::every(frame_log, 5000))
        diag::log("ESP",
                  "frame players=%zu local=0x%llx team=%d pos=(%.2f %.2f %.2f) enemies=%d positioned=%d alive=%d projected=%d camera=0x%llx",
                  Players.size(), (unsigned long long)LocalPlayer, LocalTeam,
                  LocalPosition.x, LocalPosition.y, LocalPosition.z, enemies, positioned, alive,
                  projected, (unsigned long long)g_camera);
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
        dl->AddRect(ImVec2(x1, y1), ImVec2(x2, y2), IM_COL32(0, 0, 0, (int)(170 * a)), r, 0, 2.f);
        dl->AddRect(ImVec2(x1, y1), ImVec2(x2, y2), col, r, 0, 1.f);
    } else if (cfg::esp::box_type == 2) {
        float w = x2 - x1, h = y2 - y1;
        float dep_x = w * 0.12f;
        float dep_y = h * 0.10f;

        ImVec2 f_tl(x1, y1), f_tr(x2, y1), f_bl(x1, y2), f_br(x2, y2);
        ImVec2 b_tl(x1 + dep_x, y1 - dep_y);
        ImVec2 b_tr(x2 - dep_x, y1 - dep_y);
        ImVec2 b_bl(x1 + dep_x, y2 - dep_y);
        ImVec2 b_br(x2 - dep_x, y2 - dep_y);

        ImU32 shadow = IM_COL32(0, 0, 0, (int)(140 * a));
        auto line2 = [&](ImVec2 a2, ImVec2 b2) {
            dl->AddLine(ImVec2(a2.x+1.5f,a2.y+1.5f), ImVec2(b2.x+1.5f,b2.y+1.5f), shadow, 1.5f);
        };
        line2(b_tl,b_tr); line2(b_tr,b_br); line2(b_br,b_bl); line2(b_bl,b_tl);
        line2(f_tl,b_tl); line2(f_tr,b_tr); line2(f_bl,b_bl); line2(f_br,b_br);
        line2(f_tl,f_tr); line2(f_tr,f_br); line2(f_br,f_bl); line2(f_bl,f_tl);

        ImVec4 cv = ImGui::ColorConvertU32ToFloat4(col);
        ImU32 col_dim = IM_COL32((int)(cv.x*180),(int)(cv.y*180),(int)(cv.z*180),(int)(cv.w*255*a*0.55f));
        dl->AddLine(b_tl,b_tr,col_dim,1.f); dl->AddLine(b_tr,b_br,col_dim,1.f);
        dl->AddLine(b_br,b_bl,col_dim,1.f); dl->AddLine(b_bl,b_tl,col_dim,1.f);
        ImU32 col_edge = IM_COL32((int)(cv.x*200),(int)(cv.y*200),(int)(cv.z*200),(int)(cv.w*255*a*0.7f));
        dl->AddLine(f_tl,b_tl,col_edge,1.f); dl->AddLine(f_tr,b_tr,col_edge,1.f);
        dl->AddLine(f_bl,b_bl,col_edge,1.f); dl->AddLine(f_br,b_br,col_edge,1.f);
        dl->AddLine(f_tl,f_tr,col,1.f); dl->AddLine(f_tr,f_br,col,1.f);
        dl->AddLine(f_br,f_bl,col,1.f); dl->AddLine(f_bl,f_tl,col,1.f);
    } else {
        float w = x2 - x1, h = y2 - y1;
        float sz = std::min(w, h) * 0.25f;
        float cr = std::min(r, sz * 0.5f);

        ImU32 s1 = IM_COL32(0, 0, 0, (int)(90 * a));
        ImU32 s2 = IM_COL32(0, 0, 0, (int)(170 * a));

        auto corner = [&](float ccx, float ccy, float dx, float dy) {
            if (cr > 0.5f) {
                float arc_cx = ccx + dx * cr;
                float arc_cy = ccy + dy * cr;
                float angle_start, angle_end;
                if (dx > 0 && dy > 0) { angle_start = 3.14159265f; angle_end = 4.71238898f; }
                else if (dx < 0 && dy > 0) { angle_start = 4.71238898f; angle_end = 6.28318530f; }
                else if (dx > 0 && dy < 0) { angle_start = 1.57079632f; angle_end = 3.14159265f; }
                else { angle_start = 0.f; angle_end = 1.57079632f; }
                dl->PathArcTo(ImVec2(arc_cx + 2.f * (dx > 0 ? 1 : -1), arc_cy + 2.f * (dy > 0 ? 1 : -1)), cr, angle_start, angle_end, 8);
                dl->PathStroke(s1, 0, 2.f);
                dl->AddLine(ImVec2(ccx + dx * cr, ccy + 2.f * (dy > 0 ? 1 : -1)), ImVec2(ccx + dx * sz, ccy + 2.f * (dy > 0 ? 1 : -1)), s1, 2.f);
                dl->AddLine(ImVec2(ccx + 2.f * (dx > 0 ? 1 : -1), ccy + dy * cr), ImVec2(ccx + 2.f * (dx > 0 ? 1 : -1), ccy + dy * sz), s1, 2.f);
                dl->PathArcTo(ImVec2(arc_cx, arc_cy), cr, angle_start, angle_end, 8);
                dl->PathStroke(s2, 0, 1.f);
                dl->AddLine(ImVec2(ccx + dx * cr, ccy), ImVec2(ccx + dx * sz, ccy), s2, 1.f);
                dl->AddLine(ImVec2(ccx, ccy + dy * cr), ImVec2(ccx, ccy + dy * sz), s2, 1.f);
                dl->PathArcTo(ImVec2(arc_cx, arc_cy), cr, angle_start, angle_end, 8);
                dl->PathStroke(col, 0, 1.f);
                dl->AddLine(ImVec2(ccx + dx * cr, ccy), ImVec2(ccx + dx * sz, ccy), col, 1.f);
                dl->AddLine(ImVec2(ccx, ccy + dy * cr), ImVec2(ccx, ccy + dy * sz), col, 1.f);
            } else {
                dl->AddLine(ImVec2(ccx + 2.f * (dx > 0 ? 1 : -1), ccy + 2.f * (dy > 0 ? 1 : -1)), ImVec2(ccx + dx * sz + 2.f * (dx > 0 ? 1 : -1), ccy + 2.f * (dy > 0 ? 1 : -1)), s1, 2.f);
                dl->AddLine(ImVec2(ccx + 2.f * (dx > 0 ? 1 : -1), ccy + 2.f * (dy > 0 ? 1 : -1)), ImVec2(ccx + 2.f * (dx > 0 ? 1 : -1), ccy + dy * sz + 2.f * (dy > 0 ? 1 : -1)), s1, 2.f);
                dl->AddLine(ImVec2(ccx, ccy), ImVec2(ccx + dx * sz, ccy), s2, 1.f);
                dl->AddLine(ImVec2(ccx, ccy), ImVec2(ccx, ccy + dy * sz), s2, 1.f);
                dl->AddLine(ImVec2(ccx, ccy), ImVec2(ccx + dx * sz, ccy), col, 1.f);
                dl->AddLine(ImVec2(ccx, ccy), ImVec2(ccx, ccy + dy * sz), col, 1.f);
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

    dl->AddRectFilled(ImVec2(bx - 2.f, top_y - 2.f), ImVec2(bx + bw + 2.f, bot_y + 2.f),
                      IM_COL32(0, 0, 0, (int)(130 * a)), 2.f);
    dl->AddRectFilled(ImVec2(bx, top_y), ImVec2(bx + bw, bot_y),
                      IM_COL32(20, 20, 20, (int)(180 * a)), 1.f);

    if (fh > 1.f) {
        int rT, gT, bT, rB, gB, bB2;
        if (pct >= 0.5f) {
            float t2 = (pct - 0.5f) * 2.f;
            rT = (int)(0x1E + (0xFF - 0x1E) * (1.f - t2));
            gT = (int)(0xFF - (0xFF - 0xD7) * (1.f - t2));
            bT = (int)(0x6E * t2);
        } else {
            float t2 = pct * 2.f;
            rT = (int)(0xFF);
            gT = (int)(0xD7 * t2);
            bT = 0;
        }
        rB = (int)(rT * 0.7f); gB = (int)(gT * 0.7f); bB2 = (int)(bT * 0.7f);
        ImU32 col_top = IM_COL32(rT, gT, bT, (int)(255 * a));
        ImU32 col_bot = IM_COL32(rB, gB, bB2, (int)(255 * a));
        dl->AddRectFilledMultiColor(
            ImVec2(bx, fill_top), ImVec2(bx + bw, bot_y),
            col_top, col_top, col_bot, col_bot);
        dl->AddLine(ImVec2(bx, fill_top), ImVec2(bx, bot_y),
                    IM_COL32(255,255,255,(int)(30*a)), 1.f);
    }

    if (hp < 100 && espFont && bh > 20.f) {
        char txt[8];
        snprintf(txt, sizeof(txt), "%d", hp);
        float fs = 8.f;
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

void visuals::dline(const ImVec2& foot, float a) {
    if (a < 0.01f) return;

    ImDrawList* dl = ImGui::GetBackgroundDrawList();

    ImU32 col = IM_COL32(
        static_cast<int>(cfg::esp::line_col.x * 255),
        static_cast<int>(cfg::esp::line_col.y * 255),
        static_cast<int>(cfg::esp::line_col.z * 255),
        static_cast<int>(cfg::esp::line_col.w * 255 * a)
    );
    const float vw = g_vw > 1.f ? g_vw : g_sw;
    const float vh = g_vh > 1.f ? g_vh : g_sh;
    const float vx = g_vw > 1.f ? g_vx : 0.f;
    const float vy = g_vh > 1.f ? g_vy : 0.f;
    dl->AddLine(ImVec2(vx + vw * 0.5f, vy + vh), foot, col, 1.f);
}

void visuals::draw_text_outlined(ImDrawList* dl, ImFont* font, float size, const ImVec2& pos, ImU32 color, const char* text) {
    if (!font || !dl) return;

    int alpha = (color >> IM_COL32_A_SHIFT) & 0xFF;
    int s1 = static_cast<int>(alpha * 0.4f);
    int s2 = static_cast<int>(alpha * 0.7f);

    dl->AddText(font, size, ImVec2(pos.x + 2.f, pos.y + 2.f), IM_COL32(0, 0, 0, s1), text);
    dl->AddText(font, size, ImVec2(pos.x + 1.f, pos.y + 1.f), IM_COL32(0, 0, 0, s2), text);
    dl->AddText(font, size, ImVec2(pos.x - 1.f, pos.y - 1.f), IM_COL32(0, 0, 0, s2), text);
    dl->AddText(font, size, ImVec2(pos.x + 1.f, pos.y - 1.f), IM_COL32(0, 0, 0, s2), text);
    dl->AddText(font, size, ImVec2(pos.x - 1.f, pos.y + 1.f), IM_COL32(0, 0, 0, s2), text);
    dl->AddText(font, size, pos, color, text);
}
