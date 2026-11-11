
// ================================================================================
// Release channel: t.me/ImGui_Source_FIXNEY (ImGui Source)
// Second channel: t.me/il2cpp_src (shit src 🧨)
// Telegram bot: t.me/imguisource_bot
// ================================================================================

#pragma once

#include "game.hpp"
#include "../other/vector3.h"
#include "../ui/theme/theme.hpp"
#include <cmath>

namespace player {
    struct TransformEntry { Vector4 position; Vector4 rotation; Vector4 scale; };
    struct TransformLayout {
        int data_offset{-1};
        int index_offset{-1};
        int entries_offset{-1};
        int parents_offset{-1};
        int pid{-1};
    };
    struct PlayerPose {
        Vector3 feet{};
        Vector3 head{};
        bool valid{false};
        bool has_head{false};
    };
    struct HealthInfo {
        int hp{0};
        int max_hp{0};
        bool valid{false};
    };
    inline TransformLayout g_transform_layout;
    inline long long g_last_layout_probe = 0;

    inline Vector3 rotate(const Vector4& q, const Vector3& v) noexcept {
        Vector3 u(q.x, q.y, q.z);
        Vector3 uv = Vector3::Cross(u, v);
        Vector3 uuv = Vector3::Cross(u, uv);
        return v + uv * (2.f * q.w) + uuv * 2.f;
    }

    inline bool entry_valid(const TransformEntry& entry) noexcept {
        const float values[] = {entry.position.x, entry.position.y, entry.position.z,
            entry.rotation.x, entry.rotation.y, entry.rotation.z, entry.rotation.w,
            entry.scale.x, entry.scale.y, entry.scale.z};
        for (float value : values)
            if (!std::isfinite(value) || std::fabs(value) > 100000.f) return false;
        const float qn = entry.rotation.x * entry.rotation.x + entry.rotation.y * entry.rotation.y +
                         entry.rotation.z * entry.rotation.z + entry.rotation.w * entry.rotation.w;
        return qn > .05f && qn < 4.f && std::fabs(entry.scale.x) > .00001f &&
               std::fabs(entry.scale.y) > .00001f && std::fabs(entry.scale.z) > .00001f;
    }

    // Unity TransformHierarchy walk: native Transform -> {hierarchy*, index}
    // -> localTransforms[index] combined with every parent up to the root.
    inline bool decode_transform(uint64_t native, int data_offset, int index_offset,
                                 int entries_offset, int parents_offset, Vector3& out) noexcept {
        uint64_t data = rpm<uint64_t>(native + uint64_t(data_offset));
        int index = rpm<int>(native + uint64_t(index_offset));
        if (!plausible(data) || index < 0 || index > 8192) return false;
        uint64_t entries = rpm<uint64_t>(data + uint64_t(entries_offset));
        uint64_t parents = rpm<uint64_t>(data + uint64_t(parents_offset));
        if (!plausible(entries) || !plausible(parents) || entries == parents) return false;
        TransformEntry current{};
        if (!mem_read(entries + uint64_t(index) * sizeof(TransformEntry), &current, sizeof(current)) ||
            !entry_valid(current)) return false;
        Vector3 result(current.position.x, current.position.y, current.position.z);
        int parent = rpm<int>(parents + uint64_t(index) * sizeof(int));
        int guard = 0;
        while (parent >= 0 && parent <= 8192 && guard++ < 128) {
            TransformEntry entry{};
            if (!mem_read(entries + uint64_t(parent) * sizeof(TransformEntry), &entry, sizeof(entry)) ||
                !entry_valid(entry)) return false;
            Vector3 scaled(result.x * entry.scale.x, result.y * entry.scale.y, result.z * entry.scale.z);
            result = rotate(entry.rotation, scaled) + Vector3(entry.position.x, entry.position.y, entry.position.z);
            int next = rpm<int>(parents + uint64_t(parent) * sizeof(int));
            if (next == parent) return false;
            parent = next;
        }
        if (guard >= 128 || parent < -1 || !std::isfinite(result.x) || !std::isfinite(result.y) ||
            !std::isfinite(result.z) || std::fabs(result.x) > 100000.f ||
            std::fabs(result.y) > 100000.f || std::fabs(result.z) > 100000.f) return false;
        out = result;
        return true;
    }

    inline bool native_transform_position(uint64_t native, Vector3& out) noexcept {
        if (g_transform_layout.pid != proc::pid) {
            g_transform_layout = {};
            g_transform_layout.pid = proc::pid;
        }
        if (g_transform_layout.data_offset >= 0 &&
            decode_transform(native, g_transform_layout.data_offset, g_transform_layout.index_offset,
                             g_transform_layout.entries_offset, g_transform_layout.parents_offset, out)) return true;
        if (decode_transform(native, 0x38, 0x40, 0x18, 0x20, out)) {
            g_transform_layout = {0x38, 0x40, 0x18, 0x20, proc::pid};
            return true;
        }

        const long long now = diag::now_ms();
        if (now - g_last_layout_probe < 1000) return false;
        g_last_layout_probe = now;
        diag::log("PROBE", "probing Unity 6 Transform layout native=0x%llx", (unsigned long long)native);
        for (int data_offset = 0x10; data_offset <= 0xF8; data_offset += 8) {
            uint64_t data = rpm<uint64_t>(native + uint64_t(data_offset));
            if (!plausible(data)) continue;
            uint64_t data_words[16]{};
            if (!mem_read(data, data_words, sizeof(data_words))) continue;
            for (int index_offset = 0x10; index_offset <= 0xFC; index_offset += 4) {
                int index = rpm<int>(native + uint64_t(index_offset));
                if (index < 0 || index > 8192) continue;
                for (int entries_i = 0; entries_i < 16; ++entries_i) {
                    if (!plausible(data_words[entries_i])) continue;
                    for (int parents_i = 0; parents_i < 16; ++parents_i) {
                        if (parents_i == entries_i || !plausible(data_words[parents_i])) continue;
                        Vector3 candidate{};
                        if (!decode_transform(native, data_offset, index_offset,
                                              entries_i * 8, parents_i * 8, candidate)) continue;
                        if (std::fabs(candidate.x) <= .001f && std::fabs(candidate.y) <= .001f &&
                            std::fabs(candidate.z) <= .001f) continue;
                        g_transform_layout = {data_offset, index_offset, entries_i * 8,
                                              parents_i * 8, proc::pid};
                        out = candidate;
                        diag::log("INFO", "Unity Transform layout found data=+0x%x index=+0x%x entries=+0x%x parents=+0x%x pos=(%.3f %.3f %.3f)",
                                  data_offset, index_offset, entries_i * 8, parents_i * 8,
                                  out.x, out.y, out.z);
                        return true;
                    }
                }
            }
        }
        diag::log("WARN", "Unity Transform layout probe found no valid hierarchy");
        return false;
    }

    inline Vector4 quaternion_multiply(const Vector4& a, const Vector4& b) noexcept {
        return Vector4(
            a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
            a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z);
    }

    inline bool normalize_quaternion(Vector4& q) noexcept {
        const float norm = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
        if (!std::isfinite(norm) || norm < .25f || norm > 2.f) return false;
        q.x /= norm; q.y /= norm; q.z /= norm; q.w /= norm;
        return true;
    }

    inline bool decode_transform_pose(uint64_t native, Vector3& position, Vector4& rotation) noexcept {
        const auto& layout = g_transform_layout;
        if (layout.pid != proc::pid || layout.data_offset < 0) {
            if (!native_transform_position(native, position)) return false;
        }
        uint64_t data = rpm<uint64_t>(native + uint64_t(g_transform_layout.data_offset));
        int index = rpm<int>(native + uint64_t(g_transform_layout.index_offset));
        uint64_t entries = rpm<uint64_t>(data + uint64_t(g_transform_layout.entries_offset));
        uint64_t parents = rpm<uint64_t>(data + uint64_t(g_transform_layout.parents_offset));
        if (!plausible(data) || !plausible(entries) || !plausible(parents) || index < 0 || index > 8192) return false;
        TransformEntry current{};
        if (!mem_read(entries + uint64_t(index) * sizeof(TransformEntry), &current, sizeof(current)) ||
            !entry_valid(current)) return false;
        position = Vector3(current.position.x, current.position.y, current.position.z);
        rotation = current.rotation;
        int parent = rpm<int>(parents + uint64_t(index) * sizeof(int));
        int guard = 0;
        while (parent >= 0 && parent <= 8192 && guard++ < 128) {
            TransformEntry entry{};
            if (!mem_read(entries + uint64_t(parent) * sizeof(TransformEntry), &entry, sizeof(entry)) ||
                !entry_valid(entry)) return false;
            Vector3 scaled(position.x * entry.scale.x, position.y * entry.scale.y, position.z * entry.scale.z);
            position = rotate(entry.rotation, scaled) + Vector3(entry.position.x, entry.position.y, entry.position.z);
            rotation = quaternion_multiply(entry.rotation, rotation);
            const int next = rpm<int>(parents + uint64_t(parent) * sizeof(int));
            if (next == parent) return false;
            parent = next;
        }
        const float norm = std::sqrt(rotation.x * rotation.x + rotation.y * rotation.y +
                                     rotation.z * rotation.z + rotation.w * rotation.w);
        if (guard >= 128 || parent < -1 || !std::isfinite(norm) || norm < .01f) return false;
        rotation.x /= norm; rotation.y /= norm; rotation.z /= norm; rotation.w /= norm;
        return std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z);
    }

    inline matrix view_from_pose(const Vector3& position, const Vector4& rotation) noexcept {
        const Vector3 right = rotate(rotation, Vector3(1.f, 0.f, 0.f));
        const Vector3 up = rotate(rotation, Vector3(0.f, 1.f, 0.f));
        const Vector3 forward = rotate(rotation, Vector3(0.f, 0.f, 1.f));
        matrix view{};
        float* m = reinterpret_cast<float*>(&view);
        m[0] = right.x;    m[4] = right.y;    m[8] = right.z;
        m[1] = up.x;       m[5] = up.y;       m[9] = up.z;
        m[2] = -forward.x; m[6] = -forward.y; m[10] = -forward.z;
        m[12] = -(right.x * position.x + right.y * position.y + right.z * position.z);
        m[13] = -(up.x * position.x + up.y * position.y + up.z * position.z);
        m[14] = forward.x * position.x + forward.y * position.y + forward.z * position.z;
        m[15] = 1.f;
        return view;
    }

    inline matrix perspective(float vertical_fov_degrees, float aspect) noexcept {
        matrix projection{};
        float* p = reinterpret_cast<float*>(&projection);
        const float near_plane = .03f, far_plane = 1000.f;
        const float f = 1.f / std::tan(vertical_fov_degrees * 0.00872664626f);
        p[0] = f / std::max(aspect, .1f);
        p[5] = f;
        p[10] = -(far_plane + near_plane) / (far_plane - near_plane);
        p[11] = -1.f;
        p[14] = -(2.f * far_plane * near_plane) / (far_plane - near_plane);
        return projection;
    }

    inline Vector4 euler_to_quaternion(const Vector3& degrees) noexcept {
        constexpr float kDegToRad = 0.01745329251994329577f;
        const float hx = degrees.x * kDegToRad * 0.5f;
        const float hy = degrees.y * kDegToRad * 0.5f;
        const float hz = degrees.z * kDegToRad * 0.5f;
        const float cx = std::cos(hx), sx = std::sin(hx);
        const float cy = std::cos(hy), sy = std::sin(hy);
        const float cz = std::cos(hz), sz = std::sin(hz);
        // Unity Quaternion.Euler uses ZXY order: qy * qx * qz.
        const Vector4 qy(0.f, sy, 0.f, cy);
        const Vector4 qx(sx, 0.f, 0.f, cx);
        const Vector4 qz(0.f, 0.f, sz, cz);
        return quaternion_multiply(quaternion_multiply(qy, qx), qz);
    }

    // bor = camera state used by the animation/aim pipeline:
    //   +0x10 ValueTransform {Position, EulerAngles}, +0x48/+0x4C floats.
    // bor.zolb is a const 60, which matches the default vertical FOV, so one
    // of the two floats is treated as FOV when it lies in a sane range.
    inline bool synthetic_from_bor(uint64_t state, int kind, camera_candidate& out) {
        if (!obj_ptr(state)) return false;
        struct RawCameraState { Vector3 position; Vector3 euler; };
        static_assert(sizeof(RawCameraState) == 24, "ValueTransform must be two Vector3");
        RawCameraState raw{};
        if (!mem_read(state + v100::bor_transform, &raw, sizeof(raw))) return false;
        if (!std::isfinite(raw.position.x) || !std::isfinite(raw.position.y) ||
            !std::isfinite(raw.position.z) || !std::isfinite(raw.euler.x) ||
            !std::isfinite(raw.euler.y) || !std::isfinite(raw.euler.z)) return false;
        if (std::fabs(raw.position.x) < .001f && std::fabs(raw.position.y) < .001f &&
            std::fabs(raw.position.z) < .001f) return false;
        if (std::fabs(raw.position.x) > 100000.f || std::fabs(raw.position.y) > 100000.f ||
            std::fabs(raw.position.z) > 100000.f) return false;
        Vector4 rotation = euler_to_quaternion(raw.euler);
        if (!normalize_quaternion(rotation)) return false;
        float fov = 60.f;
        float fovs[2]{};
        if (mem_read(state + v100::bor_float_a, fovs, sizeof(fovs))) {
            if (fovs[0] >= 20.f && fovs[0] <= 120.f) fov = fovs[0];
            else if (fovs[1] >= 20.f && fovs[1] <= 120.f) fov = fovs[1];
        }
        out = {state, view_from_pose(raw.position, rotation),
               perspective(fov, g_sw / std::max(g_sh, 1.f)), -1, -1, kind};
        static long long last_state_log = 0;
        if (diag::every(last_state_log, 5000))
            diag::log("CAMERA", "%s bor=0x%llx pos=(%.2f %.2f %.2f) euler=(%.1f %.1f %.1f) fov=%.1f floats=(%.2f %.2f)",
                      camera_kind_name(kind), (unsigned long long)state,
                      raw.position.x, raw.position.y, raw.position.z,
                      raw.euler.x, raw.euler.y, raw.euler.z, fov, fovs[0], fovs[1]);
        return true;
    }

    // odq+0x78 is vqn, the player camera state holder with two bor fields.
    inline std::vector<camera_candidate> camera_state_candidates(uint64_t local_player) {
        std::vector<camera_candidate> result;
        const uint64_t controller = rpm<uint64_t>(local_player + v100::player_camera_state);
        if (!obj_ptr(controller)) return result;
        for (uint64_t offset : {v100::camera_state_a, v100::camera_state_b}) {
            camera_candidate candidate{};
            if (synthetic_from_bor(rpm<uint64_t>(controller + offset), camera_kind_vqn_state, candidate))
                result.push_back(candidate);
        }
        return result;
    }

    // Last-resort camera: yjp (ios) transform pivot + kgd quaternion, FOV 60.
    inline std::vector<camera_candidate> camera_fallback_candidates(uint64_t local_player) {
        std::vector<camera_candidate> result = camera_state_candidates(local_player);
        const uint64_t ios_class = typeinfo(v100::ios_typeinfo);
        const uint64_t controller = find_player_component(local_player, ios_class);
        if (!controller) return result;

        const uint64_t aim_state = rpm<uint64_t>(controller + v100::ios_aim);
        Vector4 view_rotation = aim_state ? rpm<Vector4>(aim_state + v100::kgd_rotation_a) : Vector4{};
        Vector4 yaw = aim_state ? rpm<Vector4>(aim_state + v100::kgd_rotation_b) : Vector4{};
        const bool view_rotation_valid = normalize_quaternion(view_rotation);
        const bool yaw_valid = normalize_quaternion(yaw);
        Vector4 state_rotation{};
        bool state_rotation_valid = false;
        if (view_rotation_valid) {
            state_rotation = view_rotation;
            state_rotation_valid = true;
        } else if (yaw_valid) {
            state_rotation = yaw;
            state_rotation_valid = true;
        }

        for (uint64_t offset : {v100::ios_transform_b, v100::ios_transform_a}) {
            const uint64_t managed = rpm<uint64_t>(controller + offset);
            const uint64_t native = managed ? rpm<uint64_t>(managed + v100::unity_cached_ptr) : 0;
            Vector3 position{}; Vector4 rotation{};
            if (!native || !decode_transform_pose(native, position, rotation)) continue;
            // The ios pivot is at the character root. The gameplay Camera
            // matrix originates at eye level, so mirror that vertical origin.
            position.y += 1.60f;
            if (state_rotation_valid) rotation = state_rotation;
            if (!normalize_quaternion(rotation)) continue;
            result.push_back({native, view_from_pose(position, rotation),
                              perspective(60.f, g_sw / std::max(g_sh, 1.f)), -1, -1,
                              camera_kind_ios_transform});
            break;
        }
        return result;
    }

    // Transform candidates for a player: odq+0x68 -> mtc -> two Transforms.
    // Fallback: nzk parts array (+0x20 cew[]) for mtc/bis components.
    inline std::vector<uint64_t> transform_candidates(uint64_t p) {
        std::vector<uint64_t> result;
        const uint64_t transforms_class = typeinfo(v100::transforms_typeinfo);
        const uint64_t model_class = typeinfo(v100::model_typeinfo);

        const uint64_t direct = rpm<uint64_t>(p + v100::player_transform);
        if (!obj_ptr(direct)) return result; // despawned/dead NetObject
        if (exact_class(direct, transforms_class)) {
            uint64_t pair[2]{};
            if (mem_read(direct + v100::transform_a, pair, sizeof(pair))) {
                if (obj_ptr(pair[0])) result.push_back(pair[0]);
                if (obj_ptr(pair[1]) && pair[1] != pair[0]) result.push_back(pair[1]);
            }
            if (!result.empty()) return result;
        }

        const uint64_t container = rpm<uint64_t>(p + v100::player_parts);
        const uint64_t array = rpm<uint64_t>(container + v100::parts_array);
        const uint64_t length = rpm<uint64_t>(array + v100::array_length);
        if (!transforms_class || !obj_ptr(container) || !obj_ptr(array) || length == 0 || length > 64) return result;
        for (uint64_t i = 0; i < length; ++i) {
            const uint64_t part = rpm<uint64_t>(array + v100::array_data + i * 8);
            if (!obj_ptr(part)) continue;
            if (exact_class(part, transforms_class)) {
                const uint64_t a = rpm<uint64_t>(part + v100::transform_a);
                const uint64_t b = rpm<uint64_t>(part + v100::transform_b);
                if (obj_ptr(a)) result.push_back(a);
                if (obj_ptr(b) && b != a) result.push_back(b);
            } else if (model_class && exact_class(part, model_class)) {
                const uint64_t transform = rpm<uint64_t>(part + v100::model_transform);
                if (obj_ptr(transform)) result.push_back(transform);
            }
        }
        return result;
    }

    inline PlayerPose pose(uint64_t p) noexcept {
        PlayerPose out{};
        const std::vector<uint64_t> candidates = transform_candidates(p);
        if (candidates.empty()) return out;

        bool found = false;
        float min_y = 1.0e30f, max_y = -1.0e30f;
        Vector3 min_point{}, max_point{};
        for (uint64_t transform : candidates) {
            const uint64_t native = rpm<uint64_t>(transform + v100::unity_cached_ptr);
            Vector3 point{};
            if (!native || !native_transform_position(native, point)) continue;
            if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z)) continue;
            if (std::fabs(point.x) < .001f && std::fabs(point.y) < .001f &&
                std::fabs(point.z) < .001f) continue;
            if (!found || point.y < min_y) { min_y = point.y; min_point = point; }
            if (!found || point.y > max_y) { max_y = point.y; max_point = point; }
            found = true;
        }
        if (!found) return out;

        out.feet = min_point;
        out.head = max_point;
        out.valid = true;
        const float span = max_point.y - min_point.y;
        // The two mtc transforms are the character root and the head/eye
        // pivot. If the runtime layout gives two points with no vertical
        // separation, fall back to the old fixed body height.
        out.has_head = span >= 0.35f && span <= 2.5f;
        if (!out.has_head) out.head = min_point + Vector3(0.f, 1.67f, 0.f);
        return out;
    }

    inline Vector3 position(uint64_t p) noexcept {
        return pose(p).feet;
    }

    inline bool local(uint64_t p) noexcept { return rpm<uint8_t>(p + uint64_t(g_local_flag_offset)) != 0; }
    inline int team(uint64_t p) noexcept { return int(rpm<uint8_t>(p + v100::player_team)); }

    inline HealthInfo health_info(uint64_t p) noexcept {
        HealthInfo out{};
        if (!obj_ptr(p)) return out;
        const uint64_t stats = rpm<uint64_t>(p + v100::player_stats);
        const uint64_t stats_class = typeinfo(v100::stats_typeinfo);
        if (!exact_class(stats, stats_class)) return out;
        // ydm: +0x10 espa (hp), +0x12 htat, +0x14 ktmm (max hp) - one read.
        uint16_t block[3]{};
        if (!mem_read(stats + v100::stats_hp, block, sizeof(block))) return out;
        const uint16_t hp = block[0];
        const uint16_t max_hp = block[(v100::stats_max_hp - v100::stats_hp) / 2];
        // 0xFFFF is the common "not initialized/dead" sentinel. Do not let
        // hp==max==0xFFFF pass the old hp<=max check and draw a full bar.
        if (max_hp == 0 || max_hp > 1000 || max_hp == 0xFFFFU ||
            hp == 0xFFFFU || hp > max_hp) return out;
        out.hp = int(hp);
        out.max_hp = int(max_hp);
        out.valid = true;
        return out;
    }

    inline int health(uint64_t p) noexcept {
        const HealthInfo info = health_info(p);
        return info.valid ? info.hp : 0;
    }

    // ---- snapshot-based fast path -------------------------------------
    // The per-frame cost of pose() is dominated by re-resolving pointers
    // that do not change between frames: mtc -> Transform -> m_CachedPtr ->
    // TransformHierarchy {data, index, entries, parents}. Cache them per
    // player, revalidate twice a second, and read only the hierarchy entries
    // every frame (1 read + 2 per parent level per transform).
    struct hierarchy_ref {
        uint64_t native{0};
        uint64_t entries{0};
        uint64_t parents{0};
        int index{-1};
        bool valid{false};
    };
    struct transform_cache_entry {
        uint64_t mtc{0};
        hierarchy_ref refs[2];
        int count{0};
        long long validated_us{0};
        Vector3 last_feet{};
        bool has_last{false};
    };
    inline std::unordered_map<uint64_t, transform_cache_entry> g_transform_cache;

    inline bool resolve_hierarchy(uint64_t managed, hierarchy_ref& ref) noexcept {
        ref = {};
        if (!obj_ptr(managed)) return false;
        const uint64_t native = rpm<uint64_t>(managed + v100::unity_cached_ptr);
        if (!obj_ptr(native)) return false;
        if (g_transform_layout.pid != proc::pid || g_transform_layout.data_offset < 0) {
            Vector3 probe{};
            if (!native_transform_position(native, probe)) return false; // runs the layout probe
        }
        const auto& layout = g_transform_layout;
        const uint64_t data = rpm<uint64_t>(native + uint64_t(layout.data_offset));
        const int index = rpm<int>(native + uint64_t(layout.index_offset));
        if (!plausible(data) || index < 0 || index > 8192) return false;
        const uint64_t entries = rpm<uint64_t>(data + uint64_t(layout.entries_offset));
        const uint64_t parents = rpm<uint64_t>(data + uint64_t(layout.parents_offset));
        if (!plausible(entries) || !plausible(parents) || entries == parents) return false;
        ref.native = native;
        ref.entries = entries;
        ref.parents = parents;
        ref.index = index;
        ref.valid = true;
        return true;
    }

    inline bool hierarchy_position(const hierarchy_ref& ref, Vector3& out) noexcept {
        if (!ref.valid) return false;
        TransformEntry current{};
        if (!mem_read(ref.entries + uint64_t(ref.index) * sizeof(TransformEntry), &current, sizeof(current)) ||
            !entry_valid(current)) return false;
        Vector3 result(current.position.x, current.position.y, current.position.z);
        int parent = rpm<int>(ref.parents + uint64_t(ref.index) * sizeof(int));
        int guard = 0;
        while (parent >= 0 && parent <= 8192 && guard++ < 128) {
            TransformEntry entry{};
            if (!mem_read(ref.entries + uint64_t(parent) * sizeof(TransformEntry), &entry, sizeof(entry)) ||
                !entry_valid(entry)) return false;
            Vector3 scaled(result.x * entry.scale.x, result.y * entry.scale.y, result.z * entry.scale.z);
            result = rotate(entry.rotation, scaled) + Vector3(entry.position.x, entry.position.y, entry.position.z);
            const int next = rpm<int>(ref.parents + uint64_t(parent) * sizeof(int));
            if (next == parent) return false;
            parent = next;
        }
        if (guard >= 128 || parent < -1 || !std::isfinite(result.x) || !std::isfinite(result.y) ||
            !std::isfinite(result.z) || std::fabs(result.x) > 100000.f ||
            std::fabs(result.y) > 100000.f || std::fabs(result.z) > 100000.f) return false;
        out = result;
        return true;
    }

    inline void forget(uint64_t object) { g_transform_cache.erase(object); }

    inline bool revalidate(const player_snapshot& p, transform_cache_entry& entry) noexcept {
        entry.count = 0;
        entry.mtc = p.transform;
        entry.validated_us = diag::now_us();
        const uint64_t transforms_class = typeinfo(v100::transforms_typeinfo);
        if (!transforms_class || !exact_class(p.transform, transforms_class)) return false;
        uint64_t pair[2]{};
        if (!mem_read(p.transform + v100::transform_a, pair, sizeof(pair))) return false;
        for (uint64_t managed : pair) {
            if (!obj_ptr(managed)) continue;
            if (entry.count && entry.refs[0].native && rpm<uint64_t>(managed + v100::unity_cached_ptr) == entry.refs[0].native) continue;
            if (resolve_hierarchy(managed, entry.refs[entry.count])) ++entry.count;
        }
        return entry.count > 0;
    }

    inline PlayerPose pose(const player_snapshot& p) noexcept {
        PlayerPose out{};
        if (!obj_ptr(p.transform)) { forget(p.object); return out; } // despawned / dead NetObject
        transform_cache_entry& entry = g_transform_cache[p.object];
        const long long now = diag::now_us();
        if (entry.mtc != p.transform || now - entry.validated_us > 500000)
            revalidate(p, entry);
        for (int attempt = 0; attempt < 2; ++attempt) {
            bool found = false;
            float min_y = 1.0e30f, max_y = -1.0e30f;
            Vector3 min_point{}, max_point{};
            for (int i = 0; i < entry.count; ++i) {
                Vector3 point{};
                if (!hierarchy_position(entry.refs[i], point)) { found = false; break; }
                if (std::fabs(point.x) < .001f && std::fabs(point.y) < .001f && std::fabs(point.z) < .001f) continue;
                if (!found || point.y < min_y) { min_y = point.y; min_point = point; }
                if (!found || point.y > max_y) { max_y = point.y; max_point = point; }
                found = true;
            }
            // A hierarchy rebuild (index reuse) shows up as a teleport; re-resolve once.
            const bool jump = found && entry.has_last &&
                (std::fabs(min_point.x - entry.last_feet.x) > 12.f ||
                 std::fabs(min_point.y - entry.last_feet.y) > 12.f ||
                 std::fabs(min_point.z - entry.last_feet.z) > 12.f);
            if (found && (!jump || attempt == 1)) {
                out.feet = min_point;
                out.head = max_point;
                out.valid = true;
                const float span = max_point.y - min_point.y;
                out.has_head = span >= 0.35f && span <= 2.5f;
                if (!out.has_head) out.head = min_point + Vector3(0.f, 1.67f, 0.f);
                entry.last_feet = min_point;
                entry.has_last = true;
                return out;
            }
            if (!revalidate(p, entry)) break;
        }
        entry.has_last = false;
        return out;
    }

    inline HealthInfo health_info(const player_snapshot& p) noexcept {
        HealthInfo out{};
        const uint64_t stats_class = typeinfo(v100::stats_typeinfo);
        if (!stats_class || !exact_class(p.stats, stats_class)) return out;
        uint16_t block[3]{};
        if (!mem_read(p.stats + v100::stats_hp, block, sizeof(block))) return out;
        const uint16_t hp = block[0];
        const uint16_t max_hp = block[(v100::stats_max_hp - v100::stats_hp) / 2];
        if (max_hp == 0 || max_hp > 1000 || max_hp == 0xFFFFU || hp == 0xFFFFU || hp > max_hp) return out;
        out.hp = int(hp);
        out.max_hp = int(max_hp);
        out.valid = true;
        return out;
    }
}
