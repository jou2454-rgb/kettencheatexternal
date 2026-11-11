
// ================================================================================
// Сурс взят из бота @imguisource_bot
// Подпишись на ImGui Source: t.me/ImGui_Source_FIXNEY
// Подпишись на shit src 🧨: t.me/il2cpp_src
// ================================================================================

#include "visuals.hpp"
#include "../game/game.hpp"
#include "../game/math.hpp"
#include "../game/player.hpp"
#include "../game/camera.hpp"
#include "../game/weapon.hpp"
#include "../ui/theme/theme.hpp"
#include "../ui/cfg.hpp"
#include "../other/crash.hpp"
#include "../protect/oxorany.hpp"
#include "imgui.h"
#include <cmath>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <array>
#include <fcntl.h>
#include <unistd.h>

extern ImFont* espFont;

static void esp_wait(const char* stage) {
    static long long last = 0;
    if (diag::every(last, 5000)) diag::log("WAIT", "ESP blocked at: %s", stage);
}

namespace {
    struct name_cache_entry { uint32_t id{}; std::string name; long long refreshed{}; };
    std::unordered_map<uint64_t, name_cache_entry> g_name_cache;
    std::unordered_map<uint64_t, long long> g_dead_since;
    std::unordered_map<uint64_t, int> g_last_hp;
    size_t g_name_cursor = 0;
    long long g_last_purge = 0;
    long long g_last_name_us = 0;

    template<typename Map>
    void purge_missing(Map& map, const std::unordered_set<uint64_t>& live) {
        for (auto it = map.begin(); it != map.end();) {
            if (live.count(it->first)) ++it;
            else it = map.erase(it);
        }
    }

    // ---- snapshot interpolation --------------------------------------
    // The game updates enemy transforms once per game frame; the overlay
    // samples them at the display rate. Rendering the latest sample gives
    // stepping whenever the two rates differ (90 fps game vs 120/144 Hz
    // overlay beats at 30 Hz). Keep the last two distinct samples and render
    // the position one update interval in the past, interpolated between
    // them: constant one-game-frame delay (4 cm at running speed), no steps.
    struct motion_sample { Vector3 feet{}; Vector3 head{}; long long t_us{0}; };
    struct motion_state {
        motion_sample prev{}, cur{};
        bool has_prev{false}, has_cur{false};
        double interval_us{8333.0};
    };
    std::unordered_map<uint64_t, motion_state> g_motion;

    float vdist(const Vector3& a, const Vector3& b) {
        const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }

    void motion_update(motion_state& m, const Vector3& feet, const Vector3& head, long long t_us) {
        if (!m.has_cur) {
            m.cur = {feet, head, t_us};
            m.has_cur = true;
            return;
        }
        if (vdist(feet, m.cur.feet) < 0.0005f && vdist(head, m.cur.head) < 0.0005f) return; // game frame not advanced
        const double dt = double(t_us - m.cur.t_us);
        m.prev = m.cur;
        m.has_prev = true;
        m.cur = {feet, head, t_us};
        if (dt > 2000.0 && dt < 60000.0) m.interval_us = m.interval_us * 0.8 + dt * 0.2;
    }

    void motion_render(const motion_state& m, long long now_us, Vector3& feet, Vector3& head) {
        feet = m.cur.feet;
        head = m.cur.head;
        if (!m.has_prev) return;
        const double span = double(m.cur.t_us - m.prev.t_us);
        if (span <= 0.0 || span > 150000.0) return;            // stale pair: snap
        if (vdist(m.prev.feet, m.cur.feet) > 4.f) return;         // teleport / respawn: snap
        const double delay = std::clamp(m.interval_us, 4000.0, 34000.0);
        double alpha = (double(now_us - m.prev.t_us) - delay) / span;
        alpha = std::clamp(alpha, 0.0, 1.0);
        const float a = float(alpha);
        feet = m.prev.feet + (m.cur.feet - m.prev.feet) * a;
        head = m.prev.head + (m.cur.head - m.prev.head) * a;
    }
}

void visuals::draw() {
    if (!(g_sw >= 2.f) || !(g_sh >= 2.f)) return;
    bool any = cfg::esp::box || cfg::esp::name || cfg::esp::health || cfg::esp::weapon;
    if (!any) { esp_wait("all ESP options disabled"); return; }

    crash::stage("esp-players");
    const std::vector<player_snapshot> Players = players();
    if (Players.empty()) { esp_wait("player discovery / registry scan"); return; }
    const long long now_ms = diag::now_ms();
    const long long now_us = diag::now_us();

    // Per-object caches must not outlive the match: a new match reuses heap
    // addresses, and a stale "dead since" entry would hide a live player.
    if (now_ms - g_last_purge > 1000) {
        g_last_purge = now_ms;
        std::unordered_set<uint64_t> live;
        for (const player_snapshot& p : Players) live.insert(p.object);
        purge_missing(g_name_cache, live);
        purge_missing(g_dead_since, live);
        purge_missing(g_last_hp, live);
        purge_missing(g_motion, live);
        purge_missing(player::g_transform_cache, live);
        purge_missing(weapon::g_players, live);
    }

    // Resolve at most one name per 100 ms. Name discovery touches several
    // component objects (dozens of reads); it has no business in a 120 Hz loop.
    if (cfg::esp::name && now_us - g_last_name_us > 100000) {
        g_last_name_us = now_us;
        const player_snapshot& p = Players[g_name_cursor++ % Players.size()];
        name_cache_entry& entry = g_name_cache[p.object];
        if (entry.id != p.id || now_ms - entry.refreshed > 5000) {
            entry.id = p.id;
            entry.name = player_name(p.object);
            entry.refreshed = now_ms;
        }
    }

    const player_snapshot* Local = nullptr;
    for (const player_snapshot& p : Players) if (p.is_local()) { Local = &p; break; }
    if (!Local) { esp_wait("local player flag"); return; }
    const uint64_t LocalPlayer = Local->object;
    const int LocalTeam = Local->team;
    if (LocalTeam != 1 && LocalTeam != 2) { esp_wait("local team validation"); return; }
    const player::PlayerPose LocalPose = player::pose(*Local);
    const Vector3 LocalPosition = LocalPose.feet;
    if (!LocalPose.valid || !std::isfinite(LocalPosition.x) ||
        (LocalPosition.x == 0.f && LocalPosition.y == 0.f && LocalPosition.z == 0.f)) {
        esp_wait("local transform hierarchy"); return;
    }

    // Camera first (4 reads with a lock), then the enemy poses, then the
    // camera again: if it changed in between, the game rendered a frame
    // while we were reading and the poses are re-read so that box and
    // camera describe the same frame.
    crash::stage("esp-camera");
    cam::main_chain chain{};
    std::vector<camera_candidate> Cameras = cam::resolve(LocalPlayer, chain);
    if (Cameras.empty()) { g_camera = 0; esp_wait("camera resolution (controller / bor / transform)"); return; }

    crash::stage("esp-poses");
    int enemies = 0, alive = 0, positioned = 0, projected = 0;
    struct enemy_frame {
        uint64_t object{};
        const player_snapshot* snap{nullptr};
        Vector3 position{};
        Vector3 head{};
        bool has_head{false};
        int hp{0};
        int max_hp{100};
        int weapon{-1};
        float distance{0.f};
    };
    std::vector<enemy_frame> frame_enemies;
    auto read_enemy_pose = [&](enemy_frame& e) -> bool {
        const player::PlayerPose Shape = player::pose(*e.snap);
        if (!Shape.valid) return false;
        e.position = Shape.feet;
        e.head = Shape.has_head ? Shape.head : Shape.feet + Vector3(0.f, 1.67f, 0.f);
        e.has_head = Shape.has_head;
        return true;
    };
    for (const player_snapshot& p : Players) {
        if (p.object == LocalPlayer) continue;
        const int PlayerTeam = p.team;
        if (PlayerTeam == LocalTeam || (PlayerTeam != 1 && PlayerTeam != 2)) continue;
        ++enemies;

        const player::HealthInfo Health = player::health_info(p);
        const int hp_now = Health.valid ? Health.hp : -1;
        const auto hp_prev = g_last_hp.find(p.object);
        if (hp_prev == g_last_hp.end() || hp_prev->second != hp_now) {
            diag::log("HEALTH", "object=0x%llx hp=%d max=%d valid=%d",
                      (unsigned long long)p.object, Health.hp, Health.max_hp, Health.valid ? 1 : 0);
            g_last_hp[p.object] = hp_now;
        }
        if (!Health.valid || Health.hp <= 0) {
            g_dead_since[p.object] = now_ms;
            continue;
        }
        const auto dead = g_dead_since.find(p.object);
        if (dead != g_dead_since.end()) {
            // Hide a dead body for a short grace period even if its stats
            // object briefly reports the pre-death HP again.
            if (now_ms - dead->second < 1500) continue;
            g_dead_since.erase(dead);
        }
        ++alive;

        enemy_frame e{};
        e.object = p.object;
        e.snap = &p;
        e.hp = Health.hp;
        e.max_hp = Health.max_hp;
        if (cfg::esp::weapon) e.weapon = weapon::active_id(p);
        if (!read_enemy_pose(e)) continue;
        ++positioned;
        e.distance = calculate_distance(e.position, LocalPosition);
        if (e.distance > 500.f) continue;
        frame_enemies.push_back(e);
    }

    // Straddle check against the locked native camera.
    if (!Cameras.empty() && Cameras.front().kind == camera_kind_main_native && !frame_enemies.empty()) {
        matrix view_after{}, proj_after{};
        if (cam::reread_lock(view_after, proj_after) &&
            std::memcmp(&view_after, &Cameras.front().view, sizeof(matrix)) != 0) {
            static long long straddle_log = 0;
            static unsigned straddles = 0;
            ++straddles;
            for (enemy_frame& e : frame_enemies) read_enemy_pose(e);
            Cameras.front().view = view_after;
            Cameras.front().projection = proj_after;
            if (diag::every(straddle_log, 10000))
                diag::log("CAMERA", "frame straddle re-reads so far: %u", straddles);
        }
    }

    // Feed the interpolator with the (consistent) samples and replace the
    // rendered positions by the interpolated ones.
    const long long sample_us = diag::now_us();
    for (enemy_frame& e : frame_enemies) {
        motion_state& m = g_motion[e.object];
        motion_update(m, e.position, e.head, sample_us);
        if (cfg::esp::smooth) motion_render(m, sample_us, e.position, e.head);
    }

    // Camera selection. The controller camera whose bor is
    // PlayerAnimationData.MainCamera is the camera that rendered the frame;
    // use it as is. Every other source is scored against the live player
    // coordinates so a UI/preview/arms camera cannot win.
    matrix ViewMatrix{}, ProjectionMatrix{};
    static int selected_kind_prev = camera_kind_unknown;
    uint64_t selected_camera = 0;
    int selected_kind = camera_kind_unknown;
    int selected_score = -1, selected_front = 0, selected_onscreen = 0;
    const bool has_targets = !frame_enemies.empty();
    if (Cameras.front().kind == camera_kind_main_native) {
        const camera_candidate& Camera = Cameras.front();
        selected_camera = Camera.address;
        selected_kind = Camera.kind;
        selected_score = 1000;
        ViewMatrix = Camera.view;
        ProjectionMatrix = Camera.projection;
    } else {
        static long long candidate_log = 0;
        const bool log_candidates = diag::every(candidate_log, 5000);
        for (const auto& Camera : Cameras) {
            int score = 0, front = 0, onscreen = 0;
            const float* view = reinterpret_cast<const float*>(&Camera.view);
            const Vector3 camera_position(
                -(view[0] * view[12] + view[1] * view[13] + view[2] * view[14]),
                -(view[4] * view[12] + view[5] * view[13] + view[6] * view[14]),
                -(view[8] * view[12] + view[9] * view[13] + view[10] * view[14]));
            const float camera_distance = calculate_distance(camera_position, LocalPosition);
            for (const auto& Enemy : frame_enemies) {
                ImVec2 screen_head, screen_foot;
                if (!world_to_screen(Enemy.head, Camera.view, Camera.projection, screen_head) ||
                    !world_to_screen(Enemy.position, Camera.view, Camera.projection, screen_foot)) continue;
                ++front;
                score += 10;
                const bool head_inside = screen_head.x >= -g_sw * .25f && screen_head.x <= g_sw * 1.25f &&
                                         screen_head.y >= -g_sh * .25f && screen_head.y <= g_sh * 1.25f;
                const bool foot_inside = screen_foot.x >= -g_sw * .25f && screen_foot.x <= g_sw * 1.25f &&
                                         screen_foot.y >= -g_sh * .25f && screen_foot.y <= g_sh * 1.25f;
                const float body_height = std::fabs(screen_foot.y - screen_head.y);
                if (head_inside && foot_inside) { ++onscreen; score += 50; }
                if (body_height >= 2.f && body_height <= g_sh * 1.5f) score += 5;
            }
            if (front > 0) score += 50;
            if (camera_distance < 30.f) score += 5;
            // Source quality: real matrices beat synthetic ones, the
            // PlayerAnimationData.MainCamera state beats the vqn/yjp guesses.
            switch (Camera.kind) {
                case camera_kind_kmc_native: score += 40; break;
                case camera_kind_other_native: score += 25; break;
                case camera_kind_main_state: score += 30; break;
                case camera_kind_vqn_state: score += 10; break;
                case camera_kind_ios_transform: score += 0; break;
                case camera_kind_heap_camera: score += 5; break;
                default: break;
            }
            // Strong hysteresis: the synthetic sources (main-bor / vqn-bor /
            // ios-transform) score within a few points of each other and
            // flipping between them every few frames is itself a visible jump.
            if (Camera.address == g_camera && Camera.kind == selected_kind_prev) score += 40;
            if (log_candidates)
                diag::log("CAMERA", "candidate=0x%llx kind=%s score=%d front=%d onscreen=%d localDistance=%.2f proj=(%.3f %.3f)",
                          (unsigned long long)Camera.address, camera_kind_name(Camera.kind), score, front, onscreen,
                          camera_distance, Camera.projection.m11, Camera.projection.m22);
            if (has_targets && front == 0) continue;
            if (score > selected_score || (score == selected_score && Camera.address == g_camera)) {
                selected_score = score;
                selected_front = front;
                selected_onscreen = onscreen;
                selected_camera = Camera.address;
                selected_kind = Camera.kind;
                ViewMatrix = Camera.view;
                ProjectionMatrix = Camera.projection;
            }
        }
    }
    if (!selected_camera || selected_score < 0) {
        g_camera = 0;
        STATUS_SET(camera, "camera: none (%zu candidates)", Cameras.size());
        esp_wait("camera candidates do not project players");
        return;
    }
    if (g_camera != selected_camera) {
        diag::log("CAMERA", "selected=0x%llx kind=%s score=%d front=%d onscreen=%d candidates=%zu",
                  (unsigned long long)selected_camera, camera_kind_name(selected_kind), selected_score,
                  selected_front, selected_onscreen, Cameras.size());
        STATUS_SET(camera, "camera: %s", camera_kind_name(selected_kind));
    }
    g_camera = selected_camera;
    selected_kind_prev = selected_kind;

    crash::stage("esp-draw");
    for (const auto& Enemy : frame_enemies) {
        ImVec2 ScreenHead, ScreenFoot;
        const bool HeadVisible = world_to_screen(Enemy.head, ViewMatrix, ProjectionMatrix, ScreenHead);
        const bool FootVisible = world_to_screen(Enemy.position, ViewMatrix, ProjectionMatrix, ScreenFoot);
        if (!HeadVisible || !FootVisible) continue;
        ++projected;

        float x1 = roundf(ScreenHead.x);
        float y1 = roundf(fminf(ScreenHead.y, ScreenFoot.y));
        float x2 = roundf(ScreenFoot.x);
        float y2 = roundf(fmaxf(ScreenHead.y, ScreenFoot.y));

        float bh = fabsf(y2 - y1);
        float bw = roundf(bh * 0.21f); // total width ~0.42 * height
        float cx = roundf((x1 + x2) * 0.5f);

        ImVec2 BoxMin(cx - bw, y1);
        ImVec2 BoxMax(cx + bw, y2);

        float inv_dist = 1.f / (Enemy.distance + 0.1f);
        float font_sz = std::clamp(400.f * inv_dist, 10.f, 18.f);

        if (cfg::esp::box) dbox(BoxMin, BoxMax, 1.f);
        if (cfg::esp::health) dhp(Enemy.hp, Enemy.max_hp, BoxMin, BoxMax, bh, 1.f);
        if (cfg::esp::name) {
            const auto found = g_name_cache.find(Enemy.object);
            const char* label = found != g_name_cache.end() && !found->second.name.empty()
                ? found->second.name.c_str() : "Enemy";
            dnick(label, BoxMax, cx, font_sz, 1.f);
        }
        if (cfg::esp::weapon) {
            if (const char* weapon_name = weapon::name(Enemy.weapon)) {
                const float y = BoxMax.y + 4.f + (cfg::esp::name && espFont ? font_sz + 2.f : 0.f);
                dweapon(weapon_name, cx, y, font_sz, 1.f);
            }
        }
    }
    // PROBE (read-only, opt-in): while /data/local/tmp/bycmd_probe exists, dumps the szi bytes
    // (Player odq+0x88 -> szi +0x10..+0x1F) of every enemy every 100 ms. A line is logged when the
    // bytes change, and every 2 s regardless. Used to find the wall/visibility flag.
    {
        static long long probe_check = 0, probe_tick = 0, probe_dump = 0;
        static bool probe_on = false;
        static std::unordered_map<uint64_t, std::array<uint8_t, 16>> probe_last;
        if (diag::every(probe_check, 1000)) probe_on = access("/data/local/tmp/bycmd_probe", F_OK) == 0;
        if (probe_on && diag::every(probe_tick, 100)) {
            const bool periodic = diag::every(probe_dump, 2000);
            for (const auto& Enemy : frame_enemies) {
                const uint64_t szi = rpm<uint64_t>(Enemy.object + 0x88);
                std::array<uint8_t, 16> cur{};
                if (!(obj_ptr(szi) && mem_read(szi + 0x10, cur.data(), cur.size()))) cur.fill(0xEE);
                const auto it = probe_last.find(Enemy.object);
                const bool changed = it == probe_last.end() || it->second != cur;
                if (changed || periodic) {
                    char hex[3 * 16 + 1];
                    for (int b = 0; b < 16; ++b) snprintf(hex + b * 3, 4, "%02X ", cur[b]);
                    diag::log("PROBE", "enemy=0x%llx szi=0x%llx dist=%.1f b[10..1F]=%s%s",
                              (unsigned long long)Enemy.object, (unsigned long long)szi, Enemy.distance,
                              hex, changed ? "CHANGED" : "");
                }
                probe_last[Enemy.object] = cur;
            }
        }
    }
    static long long frame_log = 0;
    if (diag::every(frame_log, 5000)) {
        diag::log(projected || !has_targets ? "INFO" : "WARN",
                  "ESP frame players=%zu local=0x%llx team=%d pos=(%.2f %.2f %.2f) enemies=%d alive=%d positioned=%d projected=%d camera=0x%llx kind=%s",
                  Players.size(), (unsigned long long)LocalPlayer, LocalTeam,
                  LocalPosition.x, LocalPosition.y, LocalPosition.z, enemies, alive, positioned,
                  projected, (unsigned long long)g_camera, camera_kind_name(selected_kind));
    }
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

void visuals::dhp(int hp, int max_hp, const ImVec2& t, const ImVec2& b, float box_h, float a) {
    if (a < 0.01f) return;
    (void)box_h;

    ImDrawList* dl = ImGui::GetBackgroundDrawList();

    if (max_hp <= 0) max_hp = 100;
    hp = std::clamp(hp, 0, max_hp);
    float pct = float(hp) / float(max_hp);

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

    if (hp < max_hp && espFont && bh > 20.f) {
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

void visuals::dweapon(const char* label, float cx, float y, float sz, float a) {
    if (!espFont || a < 0.01f || !label) return;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const ImVec2 ts = espFont->CalcTextSizeA(sz, FLT_MAX, 0.f, label);
    const ImU32 col = IM_COL32(
        static_cast<int>(cfg::esp::box_col.x * 255),
        static_cast<int>(cfg::esp::box_col.y * 255),
        static_cast<int>(cfg::esp::box_col.z * 255),
        static_cast<int>(cfg::esp::box_col.w * 255 * a)
    );
    draw_text_outlined(dl, espFont, sz, ImVec2(roundf(cx - ts.x * 0.5f), roundf(y)), col, label);
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
