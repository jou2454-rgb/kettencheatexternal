// ================================================================================
// Skeleton ESP. Read-only: no mem_write / wpm anywhere on this path.
//
// Bones come from the Unity TransformHierarchy that the player's root transform
// lives in (odq.aiyh -> mtc.transform_a/b, see player.hpp). Every hierarchy node
// whose parent chain reaches that root is a bone, linked to its parent, so no
// bone-name table is needed. Topology (parent links) is rebuilt every 500 ms;
// world positions are recomputed every frame from one bulk read of the local
// TransformEntry array.
//
// Not verified on device. The root subtree can also contain attachment nodes,
// those are filtered by reach from the root (kMaxBoneReach).
// ================================================================================

#pragma once

#include "game.hpp"
#include "math.hpp"
#include "player.hpp"
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <vector>

namespace skeleton {
    inline constexpr int kMaxNodes = 256;             // hierarchy nodes read per rebuild
    inline constexpr long long kRebuildUs = 500000;   // topology refresh period
    inline constexpr float kMaxBoneReach = 2.5f;      // metres from the root, drops props/attachments
    inline constexpr size_t kMaxSegments = 256;

    struct Topology {
        uint64_t entries{0};          // TransformEntry array of the hierarchy
        int root{-1};                 // index of the player root transform
        int max_index{-1};            // highest index touched by the topology
        std::vector<int> bones;       // root, its ancestors, its subtree
        std::vector<int> parent;      // kMaxNodes; -1 = hierarchy root, -2 = unreadable
        std::vector<char> role;       // 0 outside, 1 root subtree, 2 ancestor of root
        long long built_us{0};
        bool valid{false};
    };
    struct Slot { Topology t[2]; };   // one per player::transform_cache_entry ref
    inline std::unordered_map<uint64_t, Slot> g_topo;

    inline void forget(uint64_t object) { g_topo.erase(object); }

    // Reads parent links, marks the root subtree and its ancestor chain.
    // Returns false when the chain is unreadable, cyclic or too small to be a body.
    inline bool build(const player::hierarchy_ref& ref, Topology& t) {
        t = {};
        if (!ref.valid || ref.index < 0 || ref.index >= kMaxNodes) return false;

        t.parent.assign(kMaxNodes, -2);
        int chunk[64]{};
        for (int base = 0; base < kMaxNodes; base += 64) {
            if (!mem_read(ref.parents + uint64_t(base) * sizeof(int), chunk, sizeof(chunk))) break;
            for (int i = 0; i < 64; ++i) {
                const int p = chunk[i];
                t.parent[base + i] = (p >= -1 && p < kMaxNodes && p != base + i) ? p : -2;
            }
        }
        if (t.parent[ref.index] == -2) return false;

        t.role.assign(kMaxNodes, 0);
        t.role[ref.index] = 1;
        int cur = t.parent[ref.index];
        for (int guard = 0; cur != -1; ++guard) {
            if (cur < 0 || guard >= 64 || t.role[cur]) return false;   // unreadable, too deep, cycle
            t.role[cur] = 2;
            cur = t.parent[cur];
        }

        for (int pass = 0; pass < 64; ++pass) {
            bool changed = false;
            for (int i = 0; i < kMaxNodes; ++i) {
                if (t.role[i] || t.parent[i] < 0) continue;
                if (t.role[t.parent[i]] == 1) { t.role[i] = 1; changed = true; }
            }
            if (!changed) break;
        }

        t.bones.clear();
        t.bones.push_back(ref.index);
        t.max_index = ref.index;
        for (int i = 0; i < kMaxNodes; ++i) {
            if (i == ref.index || !t.role[i]) continue;
            t.bones.push_back(i);
            t.max_index = std::max(t.max_index, i);
        }
        if (t.bones.size() < 6) return false;

        t.entries = ref.entries;
        t.root = ref.index;
        t.valid = true;
        return true;
    }

    struct Trs { Vector3 p{}; Vector4 r{}; Vector3 s{}; char state{0}; }; // state 0 todo, 1 ok, 2 bad

    // Composes local TRS down the parent chain (Unity order: scale, rotate, translate).
    inline bool resolve_node(const Topology& t, const std::vector<player::TransformEntry>& local,
                             std::vector<Trs>& world, int i, int depth) {
        if (world[i].state) return world[i].state == 1;
        if (depth > 64 || !player::entry_valid(local[i])) { world[i].state = 2; return false; }
        const player::TransformEntry& e = local[i];
        Trs w{};
        const int p = t.parent[i];
        if (p < 0 || !t.role[p]) {
            w.p = Vector3(e.position.x, e.position.y, e.position.z);
            w.r = e.rotation;
            w.s = Vector3(e.scale.x, e.scale.y, e.scale.z);
        } else {
            if (!resolve_node(t, local, world, p, depth + 1)) { world[i].state = 2; return false; }
            const Trs& pw = world[p];
            const Vector3 scaled(e.position.x * pw.s.x, e.position.y * pw.s.y, e.position.z * pw.s.z);
            w.p = pw.p + player::rotate(pw.r, scaled);
            w.r = player::quaternion_multiply(pw.r, e.rotation);
            w.s = Vector3(pw.s.x * e.scale.x, pw.s.y * e.scale.y, pw.s.z * e.scale.z);
        }
        w.state = 1;
        world[i] = w;
        return true;
    }

    // One bulk read of the local transforms, then world positions for every bone.
    inline bool world_positions(const Topology& t, std::vector<Vector3>& out) {
        const int n = t.max_index + 1;
        if (n <= 0 || n > kMaxNodes) return false;
        std::vector<player::TransformEntry> local(static_cast<size_t>(n));
        if (!mem_read(t.entries, local.data(), local.size() * sizeof(player::TransformEntry))) return false;
        std::vector<Trs> world(static_cast<size_t>(n));
        for (int i : t.bones) resolve_node(t, local, world, i, 0);
        out.assign(size_t(n), Vector3{});
        for (int i : t.bones)
            if (world[i].state == 1) out[i] = world[i].p;
        return world[t.root].state == 1;
    }

    inline float dist2(const Vector3& a, const Vector3& b) {
        const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
        return dx * dx + dy * dy + dz * dz;
    }

    // Draws the skeleton of one player. Call after player::pose(snapshot) for that
    // player this frame, so g_transform_cache holds fresh hierarchy refs.
    inline void draw(uint64_t object, const matrix& view, const matrix& proj, const ImVec4& color) {
        const auto cit = player::g_transform_cache.find(object);
        if (cit == player::g_transform_cache.end() || cit->second.count <= 0) return;
        const player::transform_cache_entry& cache = cit->second;

        Slot& slot = g_topo[object];
        const long long now = diag::now_us();
        for (int i = 0; i < cache.count && i < 2; ++i) {
            Topology& t = slot.t[i];
            const bool moved = t.valid && (t.entries != cache.refs[i].entries || t.root != cache.refs[i].index);
            if (moved || now - t.built_us > kRebuildUs) {
                build(cache.refs[i], t);
                t.built_us = now;
            }
        }

        const Topology* best = nullptr;
        for (int i = 0; i < cache.count && i < 2; ++i) {
            const Topology& t = slot.t[i];
            if (t.valid && (!best || t.bones.size() > best->bones.size())) best = &t;
        }
        if (!best) return;

        std::vector<Vector3> pos;
        if (!world_positions(*best, pos)) return;

        const Vector3 root = pos[best->root];
        std::vector<std::pair<ImVec2, ImVec2>> segments;
        segments.reserve(64);
        for (int i : best->bones) {
            if (i == best->root || best->role[i] != 1) continue;
            const int p = best->parent[i];
            if (p < 0 || best->role[p] != 1) continue;
            if (dist2(pos[i], root) > kMaxBoneReach * kMaxBoneReach ||
                dist2(pos[p], root) > kMaxBoneReach * kMaxBoneReach) continue;
            ImVec2 a, b;
            if (!world_to_screen(pos[p], view, proj, a) || !world_to_screen(pos[i], view, proj, b)) continue;
            segments.emplace_back(a, b);
            if (segments.size() >= kMaxSegments) break;
        }
        if (segments.empty()) return;

        ImDrawList* dl = ImGui::GetBackgroundDrawList();
        const ImU32 outline = IM_COL32(0, 0, 0, 170);
        const ImU32 col = IM_COL32(int(color.x * 255), int(color.y * 255), int(color.z * 255), int(color.w * 255));
        for (const auto& s : segments) dl->AddLine(s.first, s.second, outline, 3.f);
        for (const auto& s : segments) dl->AddLine(s.first, s.second, col, 1.2f);
    }
}
