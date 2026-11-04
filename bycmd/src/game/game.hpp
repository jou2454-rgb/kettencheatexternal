
// ================================================================================
// -----------------[ TELEGRAM SOURCE ]-----------------
// * BOT:     t.me/imguisource_bot
// * CHANNEL: t.me/ImGui_Source_FIXNEY (ImGui Source)
// * CH 2:    t.me/il2cpp_src (shit src 🧨)
// -----------------------------------------------------
// ================================================================================

#pragma once

#include "../other/memory.hpp"
#include "../protect/oxorany.hpp"
#include <stdint.h>
#include <vector>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <string>
#include <cctype>
#include <unordered_map>

struct matrix {
    float m11, m12, m13, m14;
    float m21, m22, m23, m24;
    float m31, m32, m33, m34;
    float m41, m42, m43, m44;
};

namespace v100 {
    // Standoff 2 Halloween 2026 (Unity 6) ARM64 profile, v16.
    inline constexpr uint64_t registry_typeinfo = 0xCD32028;
    inline constexpr uint64_t store_typeinfo = 0xCD303E8;
    inline constexpr uint64_t player_typeinfo = 0xCD32658;
    inline constexpr uint64_t transforms_typeinfo = 0xCD2BB30;
    inline constexpr uint64_t model_typeinfo = 0xCD2DDE8; // bis (was jgi), matched by field signature
    inline constexpr uint64_t stats_typeinfo = 0xCD360F0;
    inline constexpr uint64_t weapon_part_typeinfo = 0xCD33B78; // ruz (was qjf)
    inline constexpr uint64_t camera_typeinfo = 0xCD1F798; // UnityEngine.Camera_TypeInfo, script.json (was 0xC1F4198, outside TypeInfo table)
    inline constexpr uint64_t ios_typeinfo = 0xCD36248; // yjp (was ios), has kgd@rot + ydm + 2x Transform
    inline constexpr uint64_t player_string_literal = 0xCDADA30; // FILL: any initialised string literal slot (stringliteral.json); "" slot, may be null until used
    inline constexpr uint64_t data_relro_begin = 0xB886F80;
    inline constexpr uint64_t data_end = 0xC46CF80;
    inline constexpr uint64_t text_begin = 0x535F940; // FILL: libunity, stale
    inline constexpr uint64_t text_end = 0xB882E00;
    inline constexpr uint64_t native_camera_get_main = 0x54C1338; // FILL: libunity native, stale, needs new libunity.so

    inline constexpr uint64_t registry_store = 0x30;
    inline constexpr uint64_t store_objects = 0x18;
    inline constexpr uint64_t list_items = 0x10;
    inline constexpr uint64_t list_count = 0x18;
    inline constexpr uint64_t array_length = 0x18;
    inline constexpr uint64_t array_data = 0x20;
    inline constexpr uint64_t player_local = 0x44;
    inline constexpr uint64_t player_parts = 0x28;
    inline constexpr uint64_t player_world = 0x18;
    inline constexpr uint64_t net_object_id = 0x10;
    inline constexpr uint64_t player_stats = 0x70;
    inline constexpr uint64_t player_team = 0x90;
    inline constexpr uint64_t stats_hp = 0x10;
    inline constexpr uint64_t stats_max_hp = 0x14;
    inline constexpr uint64_t parts_list = 0x10;
    inline constexpr uint64_t transform_a = 0x28;
    inline constexpr uint64_t transform_b = 0x30;
    inline constexpr uint64_t model_transform = 0x20;
    inline constexpr uint64_t unity_cached_ptr = 0x10;
    inline constexpr uint64_t camera_view = 0x70;
    inline constexpr uint64_t camera_projection = 0xB0;
}
struct remote_map { uint64_t begin{}, end{}; bool writable{}; };
inline std::vector<remote_map> g_scan_maps;
inline size_t g_registry_map = 0, g_camera_map = 0;
inline uint64_t g_registry_cursor = 0, g_camera_cursor = 0;
inline uint64_t g_camera_next_address = 0;
inline uint64_t g_registry = 0, g_camera = 0;
inline int g_scan_pid = -1;
inline uint64_t g_scan_lib = 0;
inline uint64_t g_registry_scanned = 0, g_camera_scanned = 0;
inline unsigned g_registry_cycles = 0, g_camera_cycles = 0;
struct player_cache_entry { uint32_t id{}; uint64_t object{}; long long seen_ms{}; };
inline std::vector<player_cache_entry> g_player_cache;
inline size_t g_player_scan_map = 0;
inline uint64_t g_player_scan_cursor = 0;
inline uint64_t g_player_scan_bytes = 0;
struct camera_candidate { uint64_t address{}; matrix view{}; matrix projection{}; int view_off{-1}; int proj_off{-1}; };
inline std::vector<camera_candidate> g_camera_candidates;
struct vtable_check { uint64_t address{}; bool valid{}; };
inline std::vector<vtable_check> g_vtable_checks;
inline bool g_get_main_dumped = false;

inline void reset() {
    g_registry = g_camera = 0; g_scan_maps.clear();
    g_registry_map = g_camera_map = 0; g_registry_cursor = g_camera_cursor = 0;
    g_camera_next_address = 0;
    g_registry_scanned = g_camera_scanned = 0;
    g_registry_cycles = g_camera_cycles = 0;
    g_player_cache.clear();
    g_camera_candidates.clear();
    g_vtable_checks.clear();
    g_get_main_dumped = false;
    g_player_scan_map = 0; g_player_scan_cursor = 0; g_player_scan_bytes = 0;
}

inline void ensure_scan_context() {
    if (g_scan_pid != proc::pid || g_scan_lib != proc::lib) {
        reset();
        g_scan_pid = proc::pid;
        g_scan_lib = proc::lib;
        diag::log("INFO", "scan context reset pid=%d libunity=0x%llx", proc::pid,
                  (unsigned long long)proc::lib);
    }
}

inline bool plausible(uint64_t p) { return p >= 0x10000 && p < 0x0001000000000000ULL; }
inline bool exact_class(uint64_t object, uint64_t klass) {
    return object && klass && rpm<uint64_t>(object) == klass;
}

// Heap object pointers on this 64-bit runtime are always above 4 GiB; field
// values also contain floats/ints which must not be probed as pointers.
inline bool obj_ptr(uint64_t p) { return p >= 0x100000000ULL && p < 0x800000000000ULL; }

// ---- il2cpp klass resolution ----
// Read class names straight out of the live Il2CppClass so every object can
// be identified by name (dump.cs lookup) instead of guessed typeinfo RVAs.
//
// This build (v1.0.0 remastered) uses a newer il2cpp metadata layout than the
// old v24 `name@+0x10` scheme. Verified live against a running game instance:
//   * klass + 0x198  => pointer to the class's own string table. The first
//     NUL-terminated entry is the CLASS NAME ("Camera", "gat", etc.); the
//     entries that follow are member/method names for that class.
//   * klass + 0x0d8  => pointer into the containing image's string table,
//     whose first entry is the namespace list start (e.g. "UnityEngine").
//   * klass + 0x050/+0x120/+0x180 hold self-pointers (byval_arg etc.), so no
//     name/ns strings live near +0x08..+0x20 on this build.
// Confirmed classes at runtime: registry klass -> "gat", sibling -> 
// "WorldObjectsDebugView", camera klass -> "Camera" — all exact dump.cs names.
namespace il2cpp {
    inline const uint64_t k_name_off = 0x198;
    inline const uint64_t k_ns_hint_off = 0x0d8;
    inline int g_layout = 0;  // 0 unknown, 5 = new-metadata layout verified
    inline int g_cache_pid = -1;
    inline uint64_t g_cache_lib = 0;
    struct klass_names { std::string name, ns; };
    inline std::unordered_map<uint64_t, klass_names> g_names;

    inline void invalidate_if_context_changed() {
        if (g_cache_pid != proc::pid || g_cache_lib != proc::lib) {
            g_layout = 0;
            g_names.clear();
            g_cache_pid = proc::pid;
            g_cache_lib = proc::lib;
        }
    }

    // Read a NUL-terminated ASCII string from the target. When allow_empty is
    // set an empty string counts as valid (namespaces may be empty).
    inline bool read_ascii(uint64_t ptr, std::string& out, bool allow_empty) {
        out.clear();
        if (!plausible(ptr)) return false;
        char buf[129]{};
        if (!mem_read(ptr, buf, 64)) return false;
        size_t len = 0;
        while (len < 64 && buf[len]) ++len;
        if (len == 64) {
            if (!mem_read(ptr + 64, buf + 64, 64)) return false;
            while (len < 128 && buf[len]) ++len;
            if (len == 128) return false;  // no terminator inside the window
        }
        for (size_t i = 0; i < len; ++i) {
            const unsigned char c = static_cast<unsigned char>(buf[i]);
            if (c < 0x20 || c > 0x7E) return false;
        }
        if (len == 0 && !allow_empty) return false;
        out.assign(buf, len);
        return true;
    }

    inline bool looks_like_identifier(const std::string& s) {
        for (size_t i = 0; i < s.size(); ++i) {
            const char c = s[i];
            const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                            (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '<' ||
                            c == '>' || c == '`' || c == '+' || c == '[' || c == ']' ||
                            c == ',' || c == '/';
            if (!ok) return false;
        }
        return true;
    }

    inline bool probe_layout(uint64_t klass) {
        if (g_layout) return true;
        if (!plausible(klass)) return false;
        // New-metadata layout: [klass + 0x198] is a pointer to the class string
        // table; its first NUL-terminated entry is the class name.
        std::string name, ns;
        if (!read_ascii(rpm<uint64_t>(klass + k_name_off), name, false)) return false;
        g_layout = 5;
        diag::log("KLIB", "il2cpp klass layout=new(0x198) klass=0x%llx name=%s",
                  (unsigned long long)klass, name.c_str());
        return true;
    }

    inline bool ensure_layout() {
        invalidate_if_context_changed();
        if (g_layout) return true;
        static long long last_probe = 0;
        if (!diag::every(last_probe, 1000)) return false;
        // The registry class pointer resolves at runtime, so it is the ground
        // truth for detecting the Il2CppClass layout of this build.
        const uint64_t known = rpm<uint64_t>(proc::lib + v100::registry_typeinfo);
        if (probe_layout(known)) return true;
        if (!plausible(known)) return false;
        // Last-resort heuristic: walk klass+0x0d8 through klass+0x1d8 and pick
        // the first entry that reads back as an identifier-ish string.
        for (uint64_t off = 0x0d8; off <= 0x1d8; off += 8) {
            std::string name;
            if (!read_ascii(rpm<uint64_t>(known + off), name, false)) continue;
            if (!looks_like_identifier(name)) continue;
            g_layout = 5;
            diag::log("KLIB", "il2cpp klass layout=heur off=0x%llx klass=0x%llx name=%s",
                      (unsigned long long)off, (unsigned long long)known, name.c_str());
            return true;
        }
        return false;
    }

    inline bool klass_identity(uint64_t klass, std::string& name, std::string& ns) {
        name.clear();
        ns.clear();
        invalidate_if_context_changed();
        if (!plausible(klass)) return false;
        auto cached = g_names.find(klass);
        if (cached != g_names.end()) {
            name = cached->second.name;
            ns = cached->second.ns;
            return !name.empty();
        }
        if (!ensure_layout()) return false;
        if (g_layout != 5) return false;
        if (!read_ascii(rpm<uint64_t>(klass + k_name_off), name, false)) return false;
        // Namespace hint: first string of the image table (may be empty).
        if (!read_ascii(rpm<uint64_t>(klass + k_ns_hint_off), ns, true)) ns.clear();
        g_names[klass] = {name, ns};
        return true;
    }

    inline std::string klass_label(uint64_t klass) {
        std::string name, ns;
        if (!klass_identity(klass, name, ns)) return "?";
        if (ns.empty()) return name;
        return ns + "." + name;
    }

    inline bool is_camera_class(uint64_t klass) {
        std::string name, ns;
        if (!klass_identity(klass, name, ns)) return false;
        return name == "Camera" && ns.find("UnityEngine") != std::string::npos;
    }
}

inline void refresh_scan_maps() {
    g_scan_maps.clear();
    if (proc::pid <= 0) return;
    char path[64];
    std::snprintf(path, sizeof(path), "/proc/%d/maps", proc::pid);
    FILE* file = std::fopen(path, "r");
    if (!file) {
        diag::log("ERROR", "cannot open %s errno=%d", path, errno);
        return;
    }
    char line[1024];
    while (std::fgets(line, sizeof(line), file)) {
        unsigned long long begin = 0, end = 0;
        char perms[5]{};
        if (std::sscanf(line, "%llx-%llx %4s", &begin, &end, perms) != 3) continue;
        if (perms[0] != 'r' || perms[1] != 'w' || end <= begin) continue;
        const uint64_t size = end - begin;
        if (size < 0x1000 || size > 512ULL * 1024 * 1024) continue;
        // Managed/native objects live in private writable mappings. Avoid files and stacks.
        if (std::strchr(line, '/') || std::strstr(line, "[stack")) continue;
        g_scan_maps.push_back({begin, end, true});
    }
    std::fclose(file);
    g_registry_map = g_camera_map = 0;
    g_registry_cursor = g_camera_cursor = 0;
    g_player_scan_map = 0;
    g_player_scan_cursor = 0;
    uint64_t total = 0;
    for (const auto& map : g_scan_maps) total += map.end - map.begin;
    diag::log("INFO", "scan map snapshot: ranges=%zu eligible=%.1f MiB", g_scan_maps.size(),
              double(total) / 1048576.0);
}

inline bool validate_registry(uint64_t candidate, uint64_t registry_class, uint64_t store_class) {
    if (!exact_class(candidate, registry_class)) return false;
    uint64_t store = rpm<uint64_t>(candidate + v100::registry_store);
    if (!exact_class(store, store_class)) return false;
    uint64_t list = rpm<uint64_t>(store + v100::store_objects);
    int count = rpm<int>(list + v100::list_count);
    uint64_t array = rpm<uint64_t>(list + v100::list_items);
    uint64_t length = rpm<uint64_t>(array + v100::array_length);
    return list && count >= 0 && count <= 4096 && (count == 0 || array) && length >= unsigned(count) && length <= 8192;
}

inline bool scan_registry(size_t budget = 4 * 1024 * 1024) {
    ensure_scan_context();
    const uint64_t registry_class = rpm<uint64_t>(proc::lib + v100::registry_typeinfo);
    const uint64_t store_class = rpm<uint64_t>(proc::lib + v100::store_typeinfo);
    static long long type_log = 0;
    if (diag::every(type_log, 5000))
        diag::log(registry_class && store_class ? "INFO" : "ERROR",
                  "TypeInfo registry=0x%llx store=0x%llx player=0x%llx transforms=0x%llx",
                  (unsigned long long)registry_class, (unsigned long long)store_class,
                  (unsigned long long)rpm<uint64_t>(proc::lib + v100::player_typeinfo),
                  (unsigned long long)rpm<uint64_t>(proc::lib + v100::transforms_typeinfo));
    if (!registry_class || !store_class) return false;
    if (validate_registry(g_registry, registry_class, store_class)) return true;
    g_registry = 0;
    if (g_scan_maps.empty()) refresh_scan_maps();
    std::vector<uint64_t> words;
    size_t consumed = 0;
    while (consumed < budget && g_registry_map < g_scan_maps.size()) {
        const auto& map = g_scan_maps[g_registry_map];
        uint64_t cursor = g_registry_cursor ? g_registry_cursor : map.begin;
        size_t bytes = size_t(std::min<uint64_t>(budget - consumed, std::min<uint64_t>(map.end - cursor, 1024 * 1024)));
        bytes &= ~size_t(7);
        if (bytes < 16) { ++g_registry_map; g_registry_cursor = 0; continue; }
        words.resize(bytes / 8);
        if (mem_read(cursor, words.data(), bytes)) {
            for (size_t i = 0; i + 1 < words.size(); ++i) {
                if (words[i] == registry_class && validate_registry(cursor + i * 8, registry_class, store_class)) {
                    g_registry = cursor + i * 8;
                    diag::log("INFO", "NetObject registry found at 0x%llx after %.1f MiB",
                              (unsigned long long)g_registry, double(g_registry_scanned) / 1048576.0);
                    return true;
                }
            }
        }
        cursor += bytes; consumed += bytes; g_registry_scanned += bytes;
        if (cursor >= map.end) { ++g_registry_map; g_registry_cursor = 0; }
        else g_registry_cursor = cursor;
    }
    if (g_registry_map >= g_scan_maps.size()) {
        g_registry_map = 0; g_registry_cursor = 0; ++g_registry_cycles;
        diag::log("WARN", "registry scan cycle %u complete; no valid registry (%.1f MiB read)",
                  g_registry_cycles, double(g_registry_scanned) / 1048576.0);
        refresh_scan_maps();
    }
    return false;
}

inline void cache_player(uint64_t object, uint32_t id, long long now) {
    if (!id) id = uint32_t((object >> 4) ^ object);
    // A reconnect/respawn may expose the same object through more than one
    // transient network id. Keep one cache record per actual managed object.
    auto same_object = std::find_if(g_player_cache.begin(), g_player_cache.end(),
        [object](const player_cache_entry& entry) { return entry.object == object; });
    if (same_object != g_player_cache.end()) {
        same_object->id = id;
        same_object->seen_ms = now;
        return;
    }
    auto found = std::find_if(g_player_cache.begin(), g_player_cache.end(),
                              [id](const player_cache_entry& entry) { return entry.id == id; });
    if (found == g_player_cache.end()) g_player_cache.push_back({id, object, now});
    else { found->object = object; found->seen_ms = now; }
}

inline size_t scan_player_heap(uint64_t player_class, uint64_t active_world, long long now,
                               size_t budget = 4 * 1024 * 1024) {
    if (!player_class || !active_world) return 0;
    if (g_scan_maps.empty()) refresh_scan_maps();
    std::vector<uint64_t> words;
    size_t consumed = 0, found = 0;
    while (consumed < budget && g_player_scan_map < g_scan_maps.size()) {
        const auto& map = g_scan_maps[g_player_scan_map];
        uint64_t cursor = g_player_scan_cursor ? g_player_scan_cursor : map.begin;
        size_t bytes = size_t(std::min<uint64_t>(budget - consumed,
            std::min<uint64_t>(map.end - cursor, 1024 * 1024)));
        bytes &= ~size_t(7);
        if (bytes < 16) { ++g_player_scan_map; g_player_scan_cursor = 0; continue; }
        words.resize(bytes / 8);
        if (mem_read(cursor, words.data(), bytes)) {
            for (size_t i = 0; i + 1 < words.size(); ++i) {
                if (words[i] != player_class) continue;
                uint64_t object = cursor + i * 8;
                if (rpm<uint64_t>(object + v100::player_world) != active_world ||
                    !rpm<uint64_t>(object + v100::player_parts) ||
                    !rpm<uint64_t>(object + v100::player_stats)) continue;
                uint8_t team = rpm<uint8_t>(object + v100::player_team);
                uint8_t local = rpm<uint8_t>(object + v100::player_local);
                if ((team != 1 && team != 2) || local > 1) continue;
                uint32_t id = rpm<uint32_t>(object + v100::net_object_id);
                cache_player(object, id, now);
                ++found;
            }
        }
        cursor += bytes; consumed += bytes; g_player_scan_bytes += bytes;
        if (cursor >= map.end) { ++g_player_scan_map; g_player_scan_cursor = 0; }
        else g_player_scan_cursor = cursor;
    }
    if (g_player_scan_map >= g_scan_maps.size()) {
        g_player_scan_map = 0; g_player_scan_cursor = 0;
        diag::log("SCAN", "player heap cycle complete scanned=%.1f MiB cached=%zu",
                  double(g_player_scan_bytes) / 1048576.0, g_player_cache.size());
    } else if (found) {
        diag::log("SCAN", "player heap step found=%zu cached=%zu range=%zu/%zu",
                  found, g_player_cache.size(), g_player_scan_map, g_scan_maps.size());
    }
    return found;
}

inline std::vector<uint64_t> players() {
    std::vector<uint64_t> out;
    if (!scan_registry()) return out;
    const uint64_t player_class = rpm<uint64_t>(proc::lib + v100::player_typeinfo);
    uint64_t store = rpm<uint64_t>(g_registry + v100::registry_store);
    uint64_t list = rpm<uint64_t>(store + v100::store_objects);
    int count = rpm<int>(list + v100::list_count);
    uint64_t array = rpm<uint64_t>(list + v100::list_items);
    if (!player_class || count < 0 || count > 4096 || !array) {
        diag::log("WARN", "registry became invalid playerClass=0x%llx count=%d array=0x%llx",
                  (unsigned long long)player_class, count, (unsigned long long)array);
        g_registry = 0; return out;
    }
    const long long now = diag::now_ms();
    size_t raw_exact = 0;
    uint64_t active_world = 0;
    std::vector<uint64_t> current_objects;
    for (int i = 0; i < count; ++i) {
        uint64_t object = rpm<uint64_t>(array + v100::array_data + uint64_t(i) * 8);
        if (!exact_class(object, player_class) || !rpm<uint64_t>(object + v100::player_world)) continue;
        ++raw_exact;
        current_objects.push_back(object);
        uint64_t world = rpm<uint64_t>(object + v100::player_world);
        if (!active_world || rpm<uint8_t>(object + v100::player_local)) active_world = world;
        uint32_t id = rpm<uint32_t>(object + v100::net_object_id);
        cache_player(object, id, now);
    }
    // The authoritative NetObject registry is complete enough for rendering.
    // Never walk multi-gigabyte anonymous heaps from the UI/render thread: on
    // slower devices that stalls both ESP and menu input for several seconds.
    g_player_cache.erase(std::remove_if(g_player_cache.begin(), g_player_cache.end(),
        [now, player_class, active_world](const player_cache_entry& entry) {
            return now - entry.seen_ms > 2500 || !exact_class(entry.object, player_class) ||
                   !rpm<uint64_t>(entry.object + v100::player_world) ||
                   (active_world && rpm<uint64_t>(entry.object + v100::player_world) != active_world);
        }), g_player_cache.end());
    auto append_unique = [&out](uint64_t object) {
        if (object && std::find(out.begin(), out.end(), object) == out.end()) out.push_back(object);
    };
    // Prefer the authoritative registry snapshot. Heap/cache entries only fill
    // holes, and a 5v5 match can never require more than ten live players.
    for (uint64_t object : current_objects) {
        if (out.size() >= 10) break;
        append_unique(object);
    }
    for (const auto& entry : g_player_cache) {
        if (out.size() >= 10) break;
        append_unique(entry.object);
    }
    static long long player_log = 0;
    if (diag::every(player_log, 5000))
        diag::log("INFO", "NetObjects=%d exactNow=%zu cachedPlayers=%zu registry=0x%llx",
                  count, raw_exact, out.size(), (unsigned long long)g_registry);
    return out;
}

inline std::string read_managed_string(uint64_t string_object, uint64_t string_class) {
    if (string_object < 0x100000 || !plausible(string_object) || !string_class ||
        rpm<uint64_t>(string_object) != string_class) return {};
    const int32_t length = rpm<int32_t>(string_object + 0x10);
    if (length <= 0 || length > 40) return {};
    std::vector<uint16_t> chars(static_cast<size_t>(length), uint16_t{});
    if (!mem_read(string_object + 0x14, chars.data(), chars.size() * sizeof(uint16_t))) return {};
    std::string out;
    for (uint16_t ch : chars) {
        if (ch < 0x20 || ch == 0x7f) return {};
        if (ch < 0x80) out.push_back(char(ch));
        else if (ch < 0x800) {
            out.push_back(char(0xC0 | (ch >> 6)));
            out.push_back(char(0x80 | (ch & 0x3F)));
        } else {
            out.push_back(char(0xE0 | (ch >> 12)));
            out.push_back(char(0x80 | ((ch >> 6) & 0x3F)));
            out.push_back(char(0x80 | (ch & 0x3F)));
        }
    }
    return out;
}

inline std::vector<std::string> player_name_candidates(uint64_t player) {
    std::vector<std::string> names;
    const uint64_t known_string = rpm<uint64_t>(proc::lib + v100::player_string_literal);
    const uint64_t string_class = plausible(known_string) ? rpm<uint64_t>(known_string) : 0;
    const uint64_t container = rpm<uint64_t>(player + 0x20);
    if (!string_class || !plausible(container)) return names;
    for (uint64_t field = 0x10; field <= 0x38; field += 8) {
        const uint64_t array = rpm<uint64_t>(container + field);
        const uint64_t count = rpm<uint64_t>(array + v100::array_length);
        if (!plausible(array) || count > 256) continue;
        for (uint64_t i = 0; i < count; ++i) {
            const uint64_t component = rpm<uint64_t>(array + v100::array_data + i * 8);
            if (!plausible(component)) continue;
            uint64_t fields[30]{};
            if (!mem_read(component + 0x10, fields, sizeof(fields))) continue;
            for (uint64_t value : fields) {
                std::string candidate = read_managed_string(value, string_class);
                if (candidate.empty() || candidate == "Player" || candidate.size() > 96) continue;
                if (std::find(names.begin(), names.end(), candidate) == names.end()) names.push_back(candidate);
                if (names.size() >= 6) return names;
            }
        }
    }
    return names;
}

inline bool noisy_player_name(const std::string& value) {
    if (value.empty() || value == "Player" || value.front() == '_' ||
        value.find('/') != std::string::npos || value.find('<') != std::string::npos ||
        value.find('>') != std::string::npos || value.find("http") != std::string::npos ||
        value.find(" SDF") != std::string::npos || value.find('.') != std::string::npos ||
        value.find('"') != std::string::npos) return true;
    bool hex = value.size() >= 16;
    bool short_lower = value.size() <= 4;
    bool repeated = value.size() >= 8;
    for (size_t i = 0; i < value.size(); ++i) {
        const unsigned char ch = static_cast<unsigned char>(value[i]);
        if (!std::isxdigit(ch)) hex = false;
        if (!(ch >= 'a' && ch <= 'z')) short_lower = false;
        if (i && value[i] != value[0]) repeated = false;
    }
    static const char* ui_words[] = {"Normal", "Highlighted", "Pressed", "Selected",
        "Crashlytics", "Diffuse"};
    for (const char* word : ui_words) if (value == word) return true;
    return hex || short_lower || repeated;
}

inline std::string player_name(uint64_t player) {
    // Dump-verified direct path: CharacterPlayer+0x40 -> PhotonPlayer,
    // PhotonPlayer+0x18 actorID, +0x20 nameField string. No guessing.
    {
        const uint64_t known_string = rpm<uint64_t>(proc::lib + v100::player_string_literal);
        const uint64_t string_class = plausible(known_string) ? rpm<uint64_t>(known_string) : 0;
        if (string_class) {
            const uint64_t go_container = rpm<uint64_t>(player + 0x20);
            if (plausible(go_container)) {
                for (uint64_t field = 0x10; field <= 0x38; field += 8) {
                    const uint64_t array = rpm<uint64_t>(go_container + field);
                    const uint64_t count = rpm<uint64_t>(array + v100::array_length);
                    if (!plausible(array) || count > 256) continue;
                    for (uint64_t i = 0; i < count; ++i) {
                        const uint64_t comp = rpm<uint64_t>(array + v100::array_data + i * 8);
                        if (!plausible(comp)) continue;
                        const uint64_t pp = rpm<uint64_t>(comp + 0x40);
                        if (!plausible(pp)) continue;
                        const int actor = rpm<int>(pp + 0x18);
                        const uint64_t nm = rpm<uint64_t>(pp + 0x20);
                        if (actor <= 0 || actor >= 10000 || !plausible(nm)) continue;
                        std::string direct = read_managed_string(nm, string_class);
                        if (!direct.empty() && !noisy_player_name(direct)) return direct;
                    }
                }
            }
        }
    }
    const std::vector<std::string> candidates = player_name_candidates(player);
    for (const std::string& candidate : candidates)
        if (!noisy_player_name(candidate)) return candidate;
    return {};
}

inline bool finite_matrix(const matrix& m) {
    const float* p = reinterpret_cast<const float*>(&m);
    float sum = 0.f;
    for (int i = 0; i < 16; ++i) { if (!std::isfinite(p[i]) || std::fabs(p[i]) > 1000000.f) return false; sum += std::fabs(p[i]); }
    return sum > .01f;
}

inline bool valid_native_vtable(uint64_t vtable) {
    if (vtable < proc::lib + v100::data_relro_begin || vtable >= proc::lib + v100::data_end) return false;
    auto cached = std::find_if(g_vtable_checks.begin(), g_vtable_checks.end(),
        [vtable](const vtable_check& entry) { return entry.address == vtable; });
    if (cached != g_vtable_checks.end()) return cached->valid;
    const uint64_t active_method = rpm<uint64_t>(vtable + 0xD0);
    const bool valid = active_method >= proc::lib + v100::text_begin &&
                       active_method < proc::lib + v100::text_end;
    g_vtable_checks.push_back({vtable, valid});
    return valid;
}

inline bool validate_camera(uint64_t camera, matrix& view, matrix& projection, bool strict_vtable = false) {
    uint64_t vtable = rpm<uint64_t>(camera);
    uint64_t game_object = rpm<uint64_t>(camera + 0x20);
    if (vtable < proc::lib + v100::data_relro_begin || vtable >= proc::lib + v100::data_end ||
        !plausible(game_object) || (strict_vtable && !valid_native_vtable(vtable))) return false;
    if (!mem_read(camera + v100::camera_view, &view, sizeof(view)) || !mem_read(camera + v100::camera_projection, &projection, sizeof(projection))) return false;
    const float* v = reinterpret_cast<const float*>(&view);
    const float* p = reinterpret_cast<const float*>(&projection);
    return finite_matrix(view) && finite_matrix(projection) && std::fabs(v[3]) < .001f && std::fabs(v[7]) < .001f &&
           std::fabs(v[11]) < .001f && std::fabs(v[15] - 1.f) < .01f && std::fabs(p[0]) > .001f && std::fabs(p[5]) > .001f;
}

inline int64_t sign_extend(uint64_t value, unsigned bits) {
    const uint64_t sign = 1ULL << (bits - 1);
    return int64_t((value ^ sign) - sign);
}

inline bool matrix_window_sane(const float* m, bool want_view) {
    for (int i = 0; i < 16; ++i)
        if (!std::isfinite(m[i]) || std::fabs(m[i]) > 1000000.f) return false;
    if (want_view) {
        // View last row ~= (0,0,0,1).
        return std::fabs(m[12]) < 1000.f && std::fabs(m[3]) < .001f &&
               std::fabs(m[7]) < .001f && std::fabs(m[11]) < .001f &&
               std::fabs(m[15] - 1.f) < .01f;
    }
    // Projection: perspective markers + sane focal lengths.
    const bool persp = std::fabs(m[11]) > .5f && std::fabs(m[15]) < .1f;
    if (!persp) return false;
    if (std::fabs(m[0]) < .05f || std::fabs(m[0]) > 20.f) return false;
    if (std::fabs(m[5]) < .05f || std::fabs(m[5]) > 20.f) return false;
    return true;
}

// Generic pattern scan: find (view,proj) matrix pair inside a native object
// by float patterns. No hardcoded offsets.
inline bool detect_pair(uint64_t obj, matrix& view, matrix& projection,
                        int& view_off, int& proj_off, const char* tag) {
    uint8_t buf[0x180]{};
    if (!mem_read(obj, buf, sizeof(buf))) return false;
    auto at = [&](int off) -> const float* {
        return reinterpret_cast<const float*>(buf + off);
    };
    for (int vo = 0x40; vo <= 0x120; vo += 0x10) {
        if (!matrix_window_sane(at(vo), true)) continue;
        for (int po = 0x40; po <= 0x120; po += 0x10) {
            if (po == vo) continue;
            if (!matrix_window_sane(at(po), false)) continue;
            view_off = vo; proj_off = po;
            std::memcpy(&view, at(vo), sizeof(view));
            std::memcpy(&projection, at(po), sizeof(projection));
            const float* p = at(po);
            diag::log("CAMERA", "%s pair obj=0x%llx view=+0x%x proj=+0x%x fov=%.1f proj=(%.3f %.3f)",
                      tag, (unsigned long long)obj, vo, po,
                      2.f * std::atan(1.f / p[5]) * 57.29578f, p[0], p[5]);
            return true;
        }
    }
    return false;
}

inline void remember_camera(uint64_t address, const matrix& view, const matrix& projection,
                            int view_off = -1, int proj_off = -1);

inline bool writable_scan_address(uint64_t address) {
    return std::any_of(g_scan_maps.begin(), g_scan_maps.end(),
        [address](const remote_map& map) { return address >= map.begin && address < map.end; });
}

inline void probe_camera_pointer(uint64_t pointer) {
    if (!plausible(pointer) || !writable_scan_address(pointer)) return;
    matrix view{}, projection{};
    int vo = -1, po = -1;
    if (detect_pair(pointer, view, projection, vo, po, "probe")) {
        remember_camera(pointer, view, projection, vo, po);
        return;
    }
    // Camera::GetMain may reference a manager, vector, handle, or singleton
    // rather than the Camera directly. Walk only small, bounded pointer tables.
    uint64_t children[65]{};
    if (!mem_read(pointer, children, sizeof(children))) return;
    for (size_t index = 0; index < 65; ++index) {
        const uint64_t child = children[index];
        if (!plausible(child)) continue;
        if (detect_pair(child, view, projection, vo, po, "probe-child")) {
            remember_camera(child, view, projection, vo, po);
            continue;
        }
        uint64_t grandchildren[17]{};
        if (!mem_read(child, grandchildren, sizeof(grandchildren))) continue;
        for (uint64_t grandchild : grandchildren) {
            if (!plausible(grandchild)) continue;
            if (detect_pair(grandchild, view, projection, vo, po, "probe-grand")) {
                remember_camera(grandchild, view, projection, vo, po);
            }
        }
    }
}

inline bool is_scoping(uint64_t local_player) noexcept {
    // PlayerController+0x80 -> AimController, which has scoping state
    const uint64_t container = rpm<uint64_t>(local_player + 0x20);
    if (!plausible(container)) return false;
    for (uint64_t field = 0x10; field <= 0x38; field += 8) {
        const uint64_t array = rpm<uint64_t>(container + field);
        const uint64_t count = rpm<uint64_t>(array + v100::array_length);
        if (!plausible(array) || count > 64) continue;
        for (uint64_t i = 0; i < count; ++i) {
            const uint64_t comp = rpm<uint64_t>(array + v100::array_data + i * 8);
            if (!plausible(comp)) continue;
            const uint64_t klass = rpm<uint64_t>(comp);
            // AimController class (from dump: typeinfo at v100::ios_typeinfo related, or direct)
            // AimController is at PlayerController+0x80
            const uint64_t aim_ctrl = rpm<uint64_t>(comp + 0x80);
            if (!plausible(aim_ctrl)) continue;
            // AimController has scoping state - check for typical boolean at known offsets
            // In dump, AimController has various fields. Check for active scope.
            // Simpler: check if weapon has scope active via AimController fields
            // At +0x58/+0x5C sensitivity, +0x68 FPSP_go, +0x70 spineDirector
            // We'll check if aim state is active by checking if camera FOV is narrow
            // Alternative: check PlayerFPSCamera state
        }
    }
    // Fallback: no direct detection, return false
    return false;
}

inline void discover_camera_get_main() {
    uint64_t start = proc::lib + v100::native_camera_get_main;
    // Resolve small ELF/Unity tail-call veneers before decoding global loads.
    for (int hop = 0; hop < 4; ++hop) {
        const uint32_t first = rpm<uint32_t>(start);
        if ((first & 0x7C000000U) != 0x14000000U) break;
        const int64_t displacement = sign_extend(first & 0x03FFFFFFU, 26) * 4;
        const uint64_t target = start + uint64_t(displacement);
        if (target < proc::lib + v100::text_begin || target >= proc::lib + v100::text_end) break;
        start = target;
    }
    uint32_t code[64]{};
    if (!mem_read(start, code, sizeof(code))) return;
    uint64_t registers[32]{};
    bool known[32]{};
    for (size_t i = 0; i < 64; ++i) {
        const uint32_t insn = code[i];
        const uint64_t pc = start + i * 4;
        if ((insn & 0x9F000000U) == 0x90000000U) { // ADRP
            const unsigned rd = insn & 31U;
            const uint64_t imm21 = (uint64_t((insn >> 5) & 0x7FFFFU) << 2) |
                                   uint64_t((insn >> 29) & 3U);
            registers[rd] = (pc & ~0xFFFULL) + uint64_t(sign_extend(imm21, 21) * 4096LL);
            known[rd] = true;
        } else if ((insn & 0xFF000000U) == 0x91000000U) { // ADD Xd, Xn, #imm
            const unsigned rd = insn & 31U, rn = (insn >> 5) & 31U;
            if (known[rn]) {
                uint64_t imm = (insn >> 10) & 0xFFFU;
                if ((insn >> 22) & 1U) imm <<= 12;
                registers[rd] = registers[rn] + imm;
                known[rd] = true;
            }
        } else if ((insn & 0xFFC00000U) == 0xF9400000U) { // LDR Xt, [Xn,#imm]
            const unsigned rt = insn & 31U, rn = (insn >> 5) & 31U;
            if (known[rn]) {
                const uint64_t slot = registers[rn] + uint64_t((insn >> 10) & 0xFFFU) * 8;
                const uint64_t value = rpm<uint64_t>(slot);
                registers[rt] = value;
                known[rt] = plausible(value);
                probe_camera_pointer(value);
                if (!g_get_main_dumped)
                    diag::log("CAMERA", "GetMain global slot=0x%llx value=0x%llx insn=%08x",
                              (unsigned long long)slot, (unsigned long long)value, insn);
            }
        }
    }
    if (!g_get_main_dumped) {
        diag::log("CAMERA", "GetMain RVA=0x%llx words=%08x %08x %08x %08x %08x %08x %08x %08x",
                  (unsigned long long)v100::native_camera_get_main,
                  code[0], code[1], code[2], code[3], code[4], code[5], code[6], code[7]);
        g_get_main_dumped = true;
    }
}

inline uint64_t find_player_component(uint64_t player, uint64_t component_class) {
    if (!player || !component_class) return 0;
    const uint64_t container = rpm<uint64_t>(player + 0x20);
    if (!plausible(container)) return 0;
    for (uint64_t field = 0x10; field <= 0x38; field += 8) {
        const uint64_t array = rpm<uint64_t>(container + field);
        const uint64_t count = rpm<uint64_t>(array + v100::array_length);
        if (!plausible(array) || count > 256) continue;
        for (uint64_t i = 0; i < count; ++i) {
            const uint64_t component = rpm<uint64_t>(array + v100::array_data + i * 8);
            if (exact_class(component, component_class)) return component;
        }
    }
    return 0;
}

inline void discover_camera_from_local_player(uint64_t local_player) {
    const uint64_t camera_class = rpm<uint64_t>(proc::lib + v100::camera_typeinfo);
    const uint64_t ios_class = rpm<uint64_t>(proc::lib + v100::ios_typeinfo);
    const uint64_t ios = find_player_component(local_player, ios_class);
    uint64_t managed_transform_a = ios ? rpm<uint64_t>(ios + 0x50) : 0;
    uint64_t managed_transform_b = ios ? rpm<uint64_t>(ios + 0x58) : 0;
    uint64_t native_transform_a = managed_transform_a ? rpm<uint64_t>(managed_transform_a + v100::unity_cached_ptr) : 0;
    uint64_t native_transform_b = managed_transform_b ? rpm<uint64_t>(managed_transform_b + v100::unity_cached_ptr) : 0;
    uint64_t game_object_a = native_transform_a ? rpm<uint64_t>(native_transform_a + 0x20) : 0;
    uint64_t game_object_b = native_transform_b ? rpm<uint64_t>(native_transform_b + 0x20) : 0;
    probe_camera_pointer(game_object_a);
    if (game_object_b != game_object_a) probe_camera_pointer(game_object_b);
    // Some Unity native layouts keep the component array one pointer away from
    // the GameObject. The bounded graph walker handles both representations.
    if (native_transform_a) probe_camera_pointer(native_transform_a);
    if (native_transform_b && native_transform_b != native_transform_a) probe_camera_pointer(native_transform_b);
    static long long targeted_log = 0;
    if (diag::every(targeted_log, 5000))
        diag::log("CAMERA", "targeted TypeInfo camera=0x%llx ios=0x%llx controller=0x%llx transforms=(0x%llx,0x%llx) gameObjects=(0x%llx,0x%llx) candidates=%zu",
                  (unsigned long long)camera_class, (unsigned long long)ios_class,
                  (unsigned long long)ios, (unsigned long long)native_transform_a,
                  (unsigned long long)native_transform_b, (unsigned long long)game_object_a,
                  (unsigned long long)game_object_b, g_camera_candidates.size());
}

inline void remember_camera(uint64_t address, const matrix& view, const matrix& projection,
                            int view_off, int proj_off) {
    auto found = std::find_if(g_camera_candidates.begin(), g_camera_candidates.end(),
        [address](const camera_candidate& candidate) { return candidate.address == address; });
    if (found == g_camera_candidates.end()) {
        g_camera_candidates.push_back({address, view, projection, view_off, proj_off});
        diag::log("CAMERA", "candidate found address=0x%llx total=%zu",
                  (unsigned long long)address, g_camera_candidates.size());
    } else {
        found->view = view;
        found->projection = projection;
        found->view_off = view_off;
        found->proj_off = proj_off;
    }
}

inline std::vector<camera_candidate> camera_candidates(uint64_t local_player) {
    ensure_scan_context();
    if (g_scan_maps.empty()) refresh_scan_maps();
    discover_camera_from_local_player(local_player);
    if (g_camera_candidates.empty()) discover_camera_get_main();
    for (auto& candidate : g_camera_candidates) {
        if (candidate.view_off >= 0) {
            // Refresh pattern-found matrices from their own offsets.
            matrix v{}, p{};
            if (mem_read(candidate.address + uint64_t(candidate.view_off), &v, sizeof(v)) &&
                mem_read(candidate.address + uint64_t(candidate.proj_off), &p, sizeof(p))) {
                const float* vf = reinterpret_cast<const float*>(&v);
                const float* pf = reinterpret_cast<const float*>(&p);
                if (matrix_window_sane(vf, true) && matrix_window_sane(pf, false)) {
                    candidate.view = v;
                    candidate.projection = p;
                    continue;
                }
            }
            candidate.address = 0; // mark dead
        }
    }
    g_camera_candidates.erase(std::remove_if(g_camera_candidates.begin(), g_camera_candidates.end(),
        [](camera_candidate& candidate) {
            if (!candidate.address) return true;
            if (candidate.view_off >= 0) return false; // already refreshed above
            return !validate_camera(candidate.address, candidate.view, candidate.projection);
        }), g_camera_candidates.end());
    return g_camera_candidates;
}

// ---- Main-camera chain (il2cpp-resolved, direct offsets) ----
// Local player / components -> field holding a managed UnityEngine.Camera
// (identified by its klass name read live from Il2CppClass) -> m_CachedPtr
// (+0x10) -> native Camera -> REAL view/proj matrices via detect_pair.
// The resolved chain (root object + field offsets) is cached and revalidated
// every frame; rediscovery only runs when validation fails (throttled).
struct main_cam_state {
    int view_off{-1};
    int proj_off{-1};
    int confirm{0};
    uint64_t native{0};
};
inline main_cam_state g_main_cam;

struct cam_chain_state {
    int pid{-1};
    uint64_t root{0};   // player object or component holding the chain
    int hop1{-1};       // root -> camera (1-hop) or root -> intermediate (2-hop)
    int hop2{-1};       // intermediate -> camera; -1 when one hop
};
inline cam_chain_state g_cam_chain;
inline long long g_cam_rescan_ms = 0;

inline bool is_camera_managed(uint64_t obj) {
    if (!obj_ptr(obj)) return false;
    return il2cpp::is_camera_class(rpm<uint64_t>(obj));
}

// 1-hop: field inside [base+begin, base+end] is the managed Camera itself.
inline bool scan_camera_1hop(uint64_t base, int begin, int end, int& out_off) {
    for (int off = begin; off <= end; off += 8) {
        if (is_camera_managed(rpm<uint64_t>(base + uint64_t(off)))) {
            out_off = off;
            return true;
        }
    }
    return false;
}

// 2-hop: base field -> intermediate object [mid+0x10, mid+0x80] -> Camera.
inline bool scan_camera_2hop(uint64_t base, int begin, int end,
                             int& out_hop1, int& out_hop2) {
    for (int off = begin; off <= end; off += 8) {
        const uint64_t mid = rpm<uint64_t>(base + uint64_t(off));
        if (!obj_ptr(mid)) continue;
        for (int moff = 0x10; moff <= 0x80; moff += 8) {
            if (is_camera_managed(rpm<uint64_t>(mid + uint64_t(moff)))) {
                out_hop1 = off;
                out_hop2 = moff;
                return true;
            }
        }
    }
    return false;
}

// Throttled intel dump: klass names of the local player, its components and
// every component field pointing at an object whose class mentions Camera or
// Aim. Used to map obfuscated dump classes to their runtime roles.
inline void klib_log_components(uint64_t local_player) {
    static long long last = 0;
    if (!diag::every(last, 10000)) return;
    diag::log("KLIB", "local=0x%llx class=%s", (unsigned long long)local_player,
              il2cpp::klass_label(rpm<uint64_t>(local_player)).c_str());
    const uint64_t container = rpm<uint64_t>(local_player + 0x20);
    if (!plausible(container)) {
        diag::log("KLIB", "container invalid=0x%llx", (unsigned long long)container);
        return;
    }
    for (uint64_t field = 0x10; field <= 0x38; field += 8) {
        const uint64_t array = rpm<uint64_t>(container + field);
        const uint64_t count = rpm<uint64_t>(array + v100::array_length);
        if (!plausible(array) || count > 64) continue;
        for (uint64_t i = 0; i < count; ++i) {
            const uint64_t comp = rpm<uint64_t>(array + v100::array_data + i * 8);
            if (!obj_ptr(comp)) continue;
            diag::log("KLIB", "comp[%llu]=0x%llx class=%s", (unsigned long long)i,
                      (unsigned long long)comp,
                      il2cpp::klass_label(rpm<uint64_t>(comp)).c_str());
            for (int off = 0x10; off <= 0x130; off += 8) {
                const uint64_t value = rpm<uint64_t>(comp + uint64_t(off));
                if (!obj_ptr(value)) continue;
                const std::string label = il2cpp::klass_label(rpm<uint64_t>(value));
                if (label.find("Camera") != std::string::npos ||
                    label.find("Aim") != std::string::npos ||
                    label.find("View") != std::string::npos)
                    diag::log("KLIB", "  comp[%llu]+0x%x -> %s",
                              (unsigned long long)i, off, label.c_str());
            }
        }
    }
}

inline bool discover_cam_chain(uint64_t local_player) {
    int off = -1, h1 = -1, h2 = -1;
    const uint64_t container = rpm<uint64_t>(local_player + 0x20);
    if (plausible(container)) {
        for (uint64_t field = 0x10; field <= 0x38; field += 8) {
            const uint64_t array = rpm<uint64_t>(container + field);
            const uint64_t count = rpm<uint64_t>(array + v100::array_length);
            if (!plausible(array) || count > 64) continue;
            for (uint64_t i = 0; i < count; ++i) {
                const uint64_t comp = rpm<uint64_t>(array + v100::array_data + i * 8);
                if (!obj_ptr(comp)) continue;
                if (scan_camera_1hop(comp, 0x10, 0x130, off)) {
                    g_cam_chain = {proc::pid, comp, off, -1};
                    diag::log("KLIB", "cam chain comp[%llu]=0x%llx +0x%x (1-hop)",
                              (unsigned long long)i, (unsigned long long)comp, off);
                    return true;
                }
            }
        }
        for (uint64_t field = 0x10; field <= 0x38; field += 8) {
            const uint64_t array = rpm<uint64_t>(container + field);
            const uint64_t count = rpm<uint64_t>(array + v100::array_length);
            if (!plausible(array) || count > 64) continue;
            for (uint64_t i = 0; i < count; ++i) {
                const uint64_t comp = rpm<uint64_t>(array + v100::array_data + i * 8);
                if (!obj_ptr(comp)) continue;
                if (scan_camera_2hop(comp, 0x10, 0x130, h1, h2)) {
                    g_cam_chain = {proc::pid, comp, h1, h2};
                    diag::log("KLIB", "cam chain comp[%llu]=0x%llx +0x%x -> +0x%x (2-hop)",
                              (unsigned long long)i, (unsigned long long)comp, h1, h2);
                    return true;
                }
            }
        }
    }
    if (scan_camera_1hop(local_player, 0x10, 0x130, off)) {
        g_cam_chain = {proc::pid, local_player, off, -1};
        diag::log("KLIB", "cam chain local=0x%llx +0x%x (1-hop)",
                  (unsigned long long)local_player, off);
        return true;
    }
    if (scan_camera_2hop(local_player, 0x10, 0x130, h1, h2)) {
        g_cam_chain = {proc::pid, local_player, h1, h2};
        diag::log("KLIB", "cam chain local=0x%llx +0x%x -> +0x%x (2-hop)",
                  (unsigned long long)local_player, h1, h2);
        return true;
    }
    return false;
}

inline uint64_t main_camera_native(uint64_t local_player) {
    klib_log_components(local_player);
    const long long now = diag::now_ms();
    if (g_cam_chain.pid != proc::pid) g_cam_chain = {};
    if (g_cam_chain.root) {
        uint64_t next = rpm<uint64_t>(g_cam_chain.root + uint64_t(g_cam_chain.hop1));
        if (g_cam_chain.hop2 >= 0) next = obj_ptr(next) ? rpm<uint64_t>(next + uint64_t(g_cam_chain.hop2)) : 0;
        if (is_camera_managed(next)) {
            const uint64_t native = rpm<uint64_t>(next + v100::unity_cached_ptr);
            if (native && obj_ptr(native)) return native;
        } else {
            diag::log("KLIB", "cam chain stale, rediscovering");
            g_cam_chain = {};
        }
    }
    // Throttled rediscovery: the scan touches thousands of remote reads.
    if (now - g_cam_rescan_ms < 1000) return 0;
    g_cam_rescan_ms = now;
    if (!discover_cam_chain(local_player)) {
        diag::log("KLIB", "cam chain NOT found local=0x%llx layout=%d",
                  (unsigned long long)local_player, il2cpp::g_layout);
        return 0;
    }
    uint64_t managed = rpm<uint64_t>(g_cam_chain.root + uint64_t(g_cam_chain.hop1));
    if (g_cam_chain.hop2 >= 0) managed = obj_ptr(managed) ? rpm<uint64_t>(managed + uint64_t(g_cam_chain.hop2)) : 0;
    const uint64_t native = managed ? rpm<uint64_t>(managed + v100::unity_cached_ptr) : 0;
    if (!native || !obj_ptr(native)) {
        diag::log("KLIB", "cam chain managed=0x%llx has no native (m_CachedPtr)",
                  (unsigned long long)managed);
        g_cam_chain = {};
        return 0;
    }
    diag::log("KLIB", "cam chain OK managed=0x%llx native=0x%llx",
              (unsigned long long)managed, (unsigned long long)native);
    return native;
}

// Auto-detect view/proj offsets inside the native Camera by float patterns.
// No hardcoded 0x70/0xB0: first stable pair wins and is cached.
inline bool detect_main_cam_offsets(uint64_t native, matrix& view, matrix& projection) {
    if (g_main_cam.native == native && g_main_cam.view_off >= 0 && g_main_cam.confirm >= 3) {
        if (!mem_read(uint64_t(native + g_main_cam.view_off), &view, sizeof(view))) return false;
        if (!mem_read(uint64_t(native + g_main_cam.proj_off), &projection, sizeof(projection))) return false;
        const float* v = reinterpret_cast<const float*>(&view);
        const float* p = reinterpret_cast<const float*>(&projection);
        if (matrix_window_sane(v, true) && matrix_window_sane(p, false)) return true;
        g_main_cam = {};
        g_main_cam.native = native;
    }
    int vo = -1, po = -1;
    if (!detect_pair(native, view, projection, vo, po, "main")) return false;
    if (g_main_cam.native != native || g_main_cam.view_off != vo || g_main_cam.proj_off != po) {
        g_main_cam.native = native;
        g_main_cam.view_off = vo;
        g_main_cam.proj_off = po;
        g_main_cam.confirm = 1;
    } else if (++g_main_cam.confirm == 3) {
        diag::log("CAMERA", "main offsets LOCKED view=+0x%x proj=+0x%x", vo, po);
    }
    return true;
}

inline bool main_camera_matrices(uint64_t local_player, uint64_t& native_out,
                                 matrix& view, matrix& projection) {
    const uint64_t native = main_camera_native(local_player);
    static long long diag_log = 0;
    if (!native && diag::every(diag_log, 5000)) {
        const uint64_t container = rpm<uint64_t>(local_player + 0x20);
        diag::log("CAMDIAG", "local=0x%llx (%s) container=0x%llx",
                  (unsigned long long)local_player,
                  il2cpp::klass_label(rpm<uint64_t>(local_player)).c_str(),
                  (unsigned long long)container);
    }
    if (!native) return false;
    native_out = native;
    return detect_main_cam_offsets(native, view, projection);
}
