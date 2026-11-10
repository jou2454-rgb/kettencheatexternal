
// ================================================================================
// Gameplay camera resolution for Standoff 2 1.1.0.
//
// Chain (dump.cs 1.1.0):
//   local odq -> wwv.yvjn (cpa, +0x20) -> pjy[] -> chc (presentation component)
//     chc+0xA0  PlayerAnimationData
//       +0x100  bor MainCamera   (camera state of the world camera)
//       +0x90   bor ArmsCamera   (camera state of the first-person arms camera)
//   kmc / wot : cgy (camera controllers, found by the background scanner)
//     +0x10    bor  (same object as one of the two above)
//     +0x20    UnityEngine.Camera -> m_CachedPtr (+0x10) -> native Camera
//              -> worldToCamera / projection matrices (pattern verified)
//
// The controller whose bor is PlayerAnimationData.MainCamera owns the camera
// that renders the world. Its native matrices are what the GPU used for the
// frame, so recoil, crouch and scope FOV are all included - no synthetic
// camera, no 60-degree guess. Everything below the native path is fallback.
// ================================================================================

#pragma once

#include "game.hpp"
#include "player.hpp"

namespace cam {
    struct main_chain {
        uint64_t presentation{0};  // chc
        uint64_t animation{0};     // PlayerAnimationData
        uint64_t main_bor{0};
        uint64_t arms_bor{0};
        bool first_person{false};
    };
    struct native_lock {
        uint64_t controller{0};
        uint64_t managed{0};
        uint64_t native{0};
        int view_off{-1};
        int proj_off{-1};
        int kind{camera_kind_unknown};
        int pid{-1};
    };
    inline native_lock g_lock;
    inline uint64_t g_chain_player = 0;
    inline uint64_t g_chain_presentation = 0;
    inline long long g_last_controller_probe = 0;
    inline long long g_last_heap_probe = 0;
    inline long long g_last_chain_log = 0;

    inline void reset() {
        g_lock = {};
        g_chain_player = 0;
        g_chain_presentation = 0;
    }

    // chc lookup is cached per local player and revalidated with one read.
    inline main_chain resolve_chain(uint64_t local_player) {
        main_chain chain{};
        const uint64_t chc_class = rpm<uint64_t>(proc::lib + v100::presentation_typeinfo);
        const uint64_t animation_class = rpm<uint64_t>(proc::lib + v100::animation_data_typeinfo);
        if (!chc_class || !animation_class || !local_player) return chain;
        if (g_chain_player != local_player || !exact_class(g_chain_presentation, chc_class)) {
            g_chain_player = local_player;
            g_chain_presentation = find_player_component(local_player, chc_class);
        }
        chain.presentation = g_chain_presentation;
        if (!chain.presentation) return chain;
        const uint64_t animation = rpm<uint64_t>(chain.presentation + v100::presentation_animation_data);
        if (!exact_class(animation, animation_class)) return chain;
        chain.animation = animation;
        chain.main_bor = rpm<uint64_t>(animation + v100::animation_main_camera);
        chain.arms_bor = rpm<uint64_t>(animation + v100::animation_arms_camera);
        chain.first_person = rpm<uint8_t>(animation + v100::animation_is_first_person) != 0;
        if (!obj_ptr(chain.main_bor)) chain.main_bor = 0;
        if (!obj_ptr(chain.arms_bor)) chain.arms_bor = 0;
        if (diag::every(g_last_chain_log, 5000))
            diag::log("CAMERA", "chain local=0x%llx chc=0x%llx animation=0x%llx mainBor=0x%llx armsBor=0x%llx firstPerson=%d",
                      (unsigned long long)local_player, (unsigned long long)chain.presentation,
                      (unsigned long long)chain.animation, (unsigned long long)chain.main_bor,
                      (unsigned long long)chain.arms_bor, chain.first_person ? 1 : 0);
        return chain;
    }

    inline int classify_controller(uint64_t controller, const main_chain& chain, uint64_t kmc_class, uint64_t wot_class) {
        const uint64_t klass = rpm<uint64_t>(controller);
        if (klass != kmc_class && klass != wot_class) return camera_kind_unknown;
        const uint64_t state = rpm<uint64_t>(controller + v100::controller_state);
        if (chain.main_bor && state == chain.main_bor) return camera_kind_main_native;
        if (chain.arms_bor && state == chain.arms_bor) return camera_kind_unknown; // arms camera: never for ESP
        if (klass == kmc_class) return camera_kind_kmc_native;
        return camera_kind_other_native;
    }

    inline bool controller_native(uint64_t controller, uint64_t camera_class, uint64_t& managed, uint64_t& native) {
        managed = rpm<uint64_t>(controller + v100::controller_camera);
        if (!exact_class(managed, camera_class)) return false;
        native = rpm<uint64_t>(managed + v100::unity_cached_ptr);
        return obj_ptr(native);
    }

    // Returns the native camera candidates in priority order. The first entry
    // with kind == camera_kind_main_native is authoritative.
    inline std::vector<camera_candidate> native_candidates(uint64_t local_player, const main_chain& chain) {
        std::vector<camera_candidate> out;
        const uint64_t kmc_class = rpm<uint64_t>(proc::lib + v100::camera_main_typeinfo);
        const uint64_t wot_class = rpm<uint64_t>(proc::lib + v100::camera_arms_typeinfo);
        const uint64_t camera_class = rpm<uint64_t>(proc::lib + v100::camera_typeinfo);
        if (!camera_class) return out;
        if (g_lock.pid != proc::pid) g_lock = {};

        // 1. Locked controller: revalidate identity, re-read matrices (4 reads).
        if (g_lock.controller) {
            const int kind = classify_controller(g_lock.controller, chain, kmc_class, wot_class);
            uint64_t managed = 0, native = 0;
            camera_candidate candidate{};
            if (kind != camera_kind_unknown &&
                controller_native(g_lock.controller, camera_class, managed, native) &&
                native == g_lock.native &&
                read_matrix_pair(native, g_lock.view_off, g_lock.proj_off, candidate.view, candidate.projection)) {
                candidate.address = native;
                candidate.view_off = g_lock.view_off;
                candidate.proj_off = g_lock.proj_off;
                candidate.kind = kind;
                g_lock.kind = kind;
                out.push_back(candidate);
                if (kind == camera_kind_main_native) return out;
            } else {
                diag::log("CAMERA", "controller lock lost controller=0x%llx kind=%s",
                          (unsigned long long)g_lock.controller, camera_kind_name(g_lock.kind));
                g_lock = {};
            }
        }

        // 2. Probe the scanner's controller list (throttled: pattern scan reads 0x300 bytes each).
        const long long now = diag::now_ms();
        if (now - g_last_controller_probe >= 250) {
            g_last_controller_probe = now;
            const scanner::results snap = scanner::snapshot();
            if (snap.pid != proc::pid || snap.lib != proc::lib) return out;
            struct probe { uint64_t controller; int kind; };
            std::vector<probe> probes;
            for (uint64_t controller : snap.controllers) {
                const int kind = classify_controller(controller, chain, kmc_class, wot_class);
                if (kind == camera_kind_unknown) continue;
                probes.push_back({controller, kind});
            }
            std::stable_sort(probes.begin(), probes.end(), [](const probe& a, const probe& b) { return a.kind < b.kind; });
            for (const probe& item : probes) {
                if (g_lock.controller == item.controller) continue;
                uint64_t managed = 0, native = 0;
                if (!controller_native(item.controller, camera_class, managed, native)) continue;
                camera_candidate candidate{};
                int vo = -1, po = -1;
                if (!detect_pair(native, candidate.view, candidate.projection, vo, po, camera_kind_name(item.kind))) continue;
                candidate.address = native;
                candidate.view_off = vo;
                candidate.proj_off = po;
                candidate.kind = item.kind;
                if (!g_lock.controller || item.kind < g_lock.kind) {
                    g_lock = {item.controller, managed, native, vo, po, item.kind, proc::pid};
                    diag::log("CAMERA", "controller locked controller=0x%llx klass=0x%llx bor=0x%llx camera=0x%llx native=0x%llx kind=%s view=+0x%x proj=+0x%x",
                              (unsigned long long)item.controller, (unsigned long long)rpm<uint64_t>(item.controller),
                              (unsigned long long)rpm<uint64_t>(item.controller + v100::controller_state),
                              (unsigned long long)managed, (unsigned long long)native,
                              camera_kind_name(item.kind), vo, po);
                }
                out.push_back(candidate);
                if (item.kind == camera_kind_main_native) break;
            }
            if (probes.empty() && snap.passes > 0) {
                static long long last_missing = 0;
                if (diag::every(last_missing, 5000))
                    diag::log("CAMERA", "no kmc/wot controller matched (scanner controllers=%zu, passes=%u); requesting rescan",
                              snap.controllers.size(), snap.passes);
                scanner::request_rescan();
            }
        }
        std::stable_sort(out.begin(), out.end(), [](const camera_candidate& a, const camera_candidate& b) { return a.kind < b.kind; });
        return out;
    }

    // Any UnityEngine.Camera from the heap scan. Only used when no controller
    // camera is available; UI/preview cameras are filtered by the projection
    // scoring in visuals.
    inline std::vector<camera_candidate> heap_candidates() {
        std::vector<camera_candidate> out;
        const long long now = diag::now_ms();
        if (now - g_last_heap_probe < 2000) return out;
        g_last_heap_probe = now;
        const uint64_t camera_class = rpm<uint64_t>(proc::lib + v100::camera_typeinfo);
        const scanner::results snap = scanner::snapshot();
        if (snap.pid != proc::pid || !camera_class) return out;
        for (uint64_t managed : snap.cameras) {
            if (!exact_class(managed, camera_class)) continue;
            const uint64_t native = rpm<uint64_t>(managed + v100::unity_cached_ptr);
            if (!obj_ptr(native)) continue;
            camera_candidate candidate{};
            int vo = -1, po = -1;
            if (!detect_pair(native, candidate.view, candidate.projection, vo, po, "heap-camera")) continue;
            candidate.address = native;
            candidate.view_off = vo;
            candidate.proj_off = po;
            candidate.kind = camera_kind_heap_camera;
            out.push_back(candidate);
            if (out.size() >= 8) break;
        }
        return out;
    }

    // Full candidate list, best first.
    inline std::vector<camera_candidate> resolve(uint64_t local_player, main_chain& chain) {
        chain = resolve_chain(local_player);
        std::vector<camera_candidate> out = native_candidates(local_player, chain);
        if (!out.empty() && out.front().kind == camera_kind_main_native) return out;
        camera_candidate synthetic{};
        if (chain.main_bor && player::synthetic_from_bor(chain.main_bor, camera_kind_main_state, synthetic))
            out.push_back(synthetic);
        const std::vector<camera_candidate> fallback = player::camera_fallback_candidates(local_player);
        out.insert(out.end(), fallback.begin(), fallback.end());
        if (out.empty()) {
            const std::vector<camera_candidate> heap = heap_candidates();
            out.insert(out.end(), heap.begin(), heap.end());
        }
        return out;
    }
}
