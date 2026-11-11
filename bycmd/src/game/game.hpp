
// ================================================================================
// -----------------[ TELEGRAM SOURCE ]-----------------
// * BOT:     t.me/imguisource_bot
// * CHANNEL: t.me/ImGui_Source_FIXNEY (ImGui Source)
// * CH 2:    t.me/il2cpp_src (shit src 🧨)
// -----------------------------------------------------
// ================================================================================

#pragma once

#include "../other/memory.hpp"
#include "../other/status.hpp"
#include "../other/crash.hpp"
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
#include <thread>
#include <mutex>
#include <atomic>
#include <sys/resource.h>
#include <sys/syscall.h>

struct matrix {
    float m11, m12, m13, m14;
    float m21, m22, m23, m24;
    float m31, m32, m33, m34;
    float m41, m42, m43, m44;
};

// ---------------------------------------------------------------------------
// Build profile: Standoff 2 1.1.0 (Halloween 2026, Unity 6000.3.13f1), ARM64.
// Every value below is tied to this exact libunity.so. Sources:
//   * TypeInfo RVAs      -> script.json  (dump x64 1.1.0, Il2CppDumper)
//   * field offsets      -> dump.cs      (same dump)
//   * build-id / sha256  -> remap_final/offsets_remap.json (not re-derived here)
// Anything that is only a hypothesis is marked as such.
// ---------------------------------------------------------------------------
namespace v100 {
    inline constexpr char profile_name[] = "Standoff2-1.1.0-Halloween2026-Unity6-arm64-v17";
    // GNU build-id note of libunity.so (first 8 bytes, lld "fast" style).
    // Taken from remap_final; game::verify_build() refuses to run ESP when the
    // mapped library reports a different id.
    inline constexpr char libunity_build_id[] = "69815f2befe135c2";
    inline constexpr char libunity_sha256[] = "ba0966976204fa3f7a2755c9588d32299fe7eb794b712c9f1a444e046cbe7c13";

    // ---- TypeInfo slots (RVA), script.json ----
    inline constexpr uint64_t registry_typeinfo = 0xCD32028;      // nba
    inline constexpr uint64_t store_typeinfo = 0xCD303E8;         // ibc
    inline constexpr uint64_t player_typeinfo = 0xCD32658;        // odq : wwv
    inline constexpr uint64_t transforms_typeinfo = 0xCD31E68;    // mtc : yvt (player transform component)
    inline constexpr uint64_t model_typeinfo = 0xCD2DDE8;         // bis
    inline constexpr uint64_t stats_typeinfo = 0xCD360F0;         // ydm
    inline constexpr uint64_t camera_typeinfo = 0xCD1F798;        // UnityEngine.Camera
    inline constexpr uint64_t ios_typeinfo = 0xCD36248;           // yjp : urz (kgd + ydm + vqn + 2x Transform)
    inline constexpr uint64_t presentation_typeinfo = 0xCD2E320;  // chc : pjy (vqn, uyg, kgd, yvt, szi, PlayerAnimationData)
    inline constexpr uint64_t animation_data_typeinfo = 0xCD28488;// Axlebolt...PlayerAnimation.PlayerAnimationData
    inline constexpr uint64_t camera_main_typeinfo = 0xCD311F8;   // kmc : cgy, etx (world camera controller, hypothesis)
    inline constexpr uint64_t camera_arms_typeinfo = 0xCD357B0;   // wot : cgy, nri (arms camera controller, hypothesis)
    inline constexpr uint64_t camera_state_typeinfo = 0xCD35080;  // vqn (two bor)
    inline constexpr uint64_t bor_typeinfo = 0xCD2DEF0;           // bor (ValueTransform camera state)
    inline constexpr uint64_t kgd_typeinfo = 0xCD31080;           // kgd (two quaternions)
    inline constexpr uint64_t player_string_literal = 0xCDADA30;  // ScriptString index 0 (""), slot may be null until used
    inline constexpr uint64_t data_relro_begin = 0xB886F80;
    inline constexpr uint64_t data_end = 0xC46CF80;
    inline constexpr uint64_t text_begin = 0x535F940;
    inline constexpr uint64_t text_end = 0xB882E00;
    inline constexpr uint64_t native_camera_get_main = 0x6BE9604; // UnityEngine.Camera.get_main (reference only)

    // ---- field offsets, dump.cs 1.1.0 ----
    inline constexpr uint64_t registry_store = 0x30;        // nba.fume : ibc
    inline constexpr uint64_t store_objects = 0x18;         // ibc.bwol (List<ohn>, validated at runtime)
    inline constexpr uint64_t list_items = 0x10;
    inline constexpr uint64_t list_count = 0x18;
    inline constexpr uint64_t array_length = 0x18;
    inline constexpr uint64_t array_data = 0x20;
    inline constexpr uint64_t net_object_id = 0x10;         // wwv.sumb
    inline constexpr uint64_t player_world = 0x18;          // wwv.vuxu : qyn
    inline constexpr uint64_t player_components = 0x20;     // wwv.yvjn : cpa (six component arrays +0x10..+0x38)
    inline constexpr uint64_t player_parts = 0x28;          // wwv.bvvg : nzk
    inline constexpr uint64_t player_local = 0x44;          // wwv +0x44/+0x45/+0x46 bool flags
    inline constexpr uint64_t player_transform = 0x68;      // odq.aiyh : yvt (mtc)
    inline constexpr uint64_t player_stats = 0x70;          // odq.djka : ydm
    inline constexpr uint64_t player_camera_state = 0x78;   // odq.gjvt : vqn
    inline constexpr uint64_t player_view_mode = 0x80;      // odq.jkhm : dvp (0 third person, 1 first person)
    inline constexpr uint64_t player_team = 0x90;           // odq.<pley> byte
    inline constexpr uint64_t stats_hp = 0x10;              // ydm.espa (ushort), getter bdqp
    inline constexpr uint64_t stats_max_hp = 0x14;          // ydm.ktmm (ushort), getter nfln
    inline constexpr uint64_t parts_array = 0x20;           // nzk.kevw : cew[]
    inline constexpr uint64_t transform_a = 0x28;           // mtc.<jpkx> Transform
    inline constexpr uint64_t transform_b = 0x30;           // mtc.<mpwq> Transform
    inline constexpr uint64_t model_transform = 0x20;       // bis.<dcul> Transform
    inline constexpr uint64_t unity_cached_ptr = 0x10;      // UnityEngine.Object.m_CachedPtr
    inline constexpr uint64_t camera_view = 0x70;           // native Camera worldToCamera (observed, pattern-verified at runtime)
    inline constexpr uint64_t camera_projection = 0xB0;     // native Camera projection (observed, pattern-verified at runtime)
    inline constexpr uint64_t presentation_animation_data = 0xA0; // chc.whjb : PlayerAnimationData
    inline constexpr uint64_t presentation_camera_state = 0x60;   // chc.ydtf : vqn
    inline constexpr uint64_t animation_arms_camera = 0x90;       // PlayerAnimationData.<ArmsCamera> : bor
    inline constexpr uint64_t animation_main_camera = 0x100;      // PlayerAnimationData.<MainCamera> : bor
    inline constexpr uint64_t animation_is_first_person = 0xA4;   // PlayerAnimationData.<IsFirstPerson>
    inline constexpr uint64_t controller_state = 0x10;      // cgy.elgy : bor
    inline constexpr uint64_t controller_transform = 0x18;  // cgy.hlsr : Transform
    inline constexpr uint64_t controller_camera = 0x20;     // cgy.<kmek> : UnityEngine.Camera
    inline constexpr uint64_t bor_transform = 0x10;         // bor.cowu : ValueTransform {Vector3 Position; Vector3 EulerAngles}
    inline constexpr uint64_t bor_float_a = 0x48;           // bor.<rrdl> float (FOV candidate, const zolb = 60)
    inline constexpr uint64_t bor_float_b = 0x4C;           // bor.<urpe> float (FOV candidate)
    inline constexpr uint64_t camera_state_a = 0x10;        // vqn.<razn> : bor
    inline constexpr uint64_t camera_state_b = 0x18;        // vqn.<ublg> : bor
    inline constexpr uint64_t ios_aim = 0x38;               // yjp.zmyf : kgd
    inline constexpr uint64_t ios_transform_a = 0x50;       // yjp.iohk : Transform
    inline constexpr uint64_t ios_transform_b = 0x58;       // yjp.lotd : Transform
    inline constexpr uint64_t kgd_rotation_a = 0x10;        // kgd.hzqi : Quaternion
    inline constexpr uint64_t kgd_rotation_b = 0x20;        // kgd.lacb : Quaternion

    // ---- weapon (dump.cs + script.json 1.1.0, libunity build-id 69815f2befe135c2) ----
    inline constexpr uint64_t weapon_part_typeinfo = 0xCD34CE0;   // uyg_TypeInfo (uyg : cew, WeaponPart)
    inline constexpr uint64_t weapon_typeinfo = 0xCD30D00;        // jrx_TypeInfo (active weapon instance)
    inline constexpr uint64_t weapon_part_owner = 0x30;           // uyg.kbdx WeaponContext @0x30 -> Owner NetObjectId._id (uint) @+0x0
    inline constexpr uint64_t weapon_part_team = 0x40;            // WeaponContext.Team (byte) @+0x10
    inline constexpr uint64_t weapon_part_active = 0x80;          // WeaponContext.ActiveWeapon : jrx @+0x50
    inline constexpr uint64_t weapon_id = 0x10;                   // jrx.<oqrg> WeaponId { byte Raw }
}

struct remote_map { uint64_t begin{}, end{}; bool writable{}; };
inline std::vector<remote_map> g_scan_maps;
inline uint64_t g_registry = 0, g_camera = 0;
inline int g_scan_pid = -1;
inline uint64_t g_scan_lib = 0;
// NetObject has three adjacent byte flags at +0x44/+0x45/+0x46. Their
// obfuscated build order is not stable between updates; resolve which one is
// the local-player flag from the live set instead of trusting the old order.
inline int g_local_flag_offset = int(v100::player_local);
struct player_cache_entry { uint32_t id{}; uint64_t object{}; long long seen_ms{}; };
inline std::vector<player_cache_entry> g_player_cache;

enum camera_kind : int {
    camera_kind_unknown = 0,
    camera_kind_main_native = 1,   // cgy whose bor == PlayerAnimationData.MainCamera -> UnityEngine.Camera
    camera_kind_kmc_native = 2,    // kmc controller camera, bor identity not confirmed
    camera_kind_other_native = 3,  // wot / unclassified controller camera
    camera_kind_main_state = 4,    // synthetic from PlayerAnimationData.MainCamera bor
    camera_kind_vqn_state = 5,     // synthetic from odq.vqn bor
    camera_kind_ios_transform = 6, // synthetic from yjp transform + kgd quaternion
    camera_kind_heap_camera = 7,   // any UnityEngine.Camera found on the heap
};
inline const char* camera_kind_name(int kind) {
    switch (kind) {
        case camera_kind_main_native: return "main-native";
        case camera_kind_kmc_native: return "kmc-native";
        case camera_kind_other_native: return "other-native";
        case camera_kind_main_state: return "main-bor";
        case camera_kind_vqn_state: return "vqn-bor";
        case camera_kind_ios_transform: return "ios-transform";
        case camera_kind_heap_camera: return "heap-camera";
        default: return "unknown";
    }
}
struct camera_candidate {
    uint64_t address{};
    matrix view{};
    matrix projection{};
    int view_off{-1};
    int proj_off{-1};
    int kind{camera_kind_unknown};
};

inline void reset() {
    g_registry = g_camera = 0;
    g_scan_maps.clear();
    g_player_cache.clear();
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

// Il2CppClass* from a TypeInfo slot. On this il2cpp the slots are lazy: until
// generated code touches the class the slot holds an encoded metadata index
// (observed 0x2002xxxx for kmc/wot during a whole match, because they are
// constructed through generic/DI paths that never initialise the global
// slot). Such a value must never be compared against object headers.
inline uint64_t typeinfo(uint64_t rva, uint64_t lib = 0) {
    const uint64_t base = lib ? lib : proc::lib;
    if (!base) return 0;
    const uint64_t value = rpm<uint64_t>(base + rva);
    return obj_ptr(value) ? value : 0;
}

// ---- il2cpp klass name resolution (sanity checks only) ----
// This build uses a newer il2cpp metadata layout than the old v24
// `name@+0x10` scheme. Observed on a live 1.0.0 instance by the previous
// maintainer: klass + 0x198 points at the class string table whose first
// NUL-terminated entry is the class name. Used here only to double-check
// that Camera_TypeInfo really resolves to "Camera" on the running build.
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
            if (len == 128) return false;
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
        if (s.empty()) return false;
        for (char c : s) {
            const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                            (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '<' ||
                            c == '>' || c == '`' || c == '+' || c == '[' || c == ']' ||
                            c == ',' || c == '/';
            if (!ok) return false;
        }
        return true;
    }

    inline bool ensure_layout(bool force = false) {
        invalidate_if_context_changed();
        if (g_layout) return true;
        static long long last_probe = 0;
        if (!force && !diag::every(last_probe, 1000)) return false;
        const uint64_t known = typeinfo(v100::registry_typeinfo);
        if (!plausible(known)) return false;
        std::string name;
        if (read_ascii(rpm<uint64_t>(known + k_name_off), name, false) && looks_like_identifier(name)) {
            g_layout = 5;
            diag::log("KLIB", "il2cpp klass layout=new(0x198) klass=0x%llx name=%s",
                      (unsigned long long)known, name.c_str());
            return true;
        }
        for (uint64_t off = 0x0d8; off <= 0x1d8; off += 8) {
            if (!read_ascii(rpm<uint64_t>(known + off), name, false)) continue;
            if (!looks_like_identifier(name)) continue;
            g_layout = 5;
            diag::log("KLIB", "il2cpp klass layout=heur off=0x%llx klass=0x%llx name=%s",
                      (unsigned long long)off, (unsigned long long)known, name.c_str());
            return true;
        }
        return false;
    }

    inline bool klass_identity(uint64_t klass, std::string& name, std::string& ns, bool force = false) {
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
        if (!ensure_layout(force)) return false;
        if (!read_ascii(rpm<uint64_t>(klass + k_name_off), name, false)) return false;
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
}

// ---------------------------------------------------------------------------
// Build verification. ESP never runs against an unverified libunity.so:
// a stale profile would otherwise read garbage through every TypeInfo slot.
// ---------------------------------------------------------------------------
namespace game {
    inline int build_attempts = 0;

    inline bool verify_build() {
        if (proc::build_checked) return proc::build_ok;
        if (proc::pid <= 0 || proc::lib == 0) return false;
        static long long last_attempt = 0;
        if (!diag::every(last_attempt, 500)) return false;
        ++build_attempts;

        char id[48]{};
        const bool have_id = elf_build_id(proc::lib, id, sizeof(id));
        if (have_id) std::snprintf(proc::build_id, sizeof(proc::build_id), "%s", id);
        const size_t expected_len = std::strlen(v100::libunity_build_id);
        const bool id_match = have_id && std::strncmp(id, v100::libunity_build_id, expected_len) == 0;

        // Secondary check: the Camera TypeInfo slot must name "Camera" on the
        // live il2cpp metadata. Needs the klass layout probe to succeed.
        const uint64_t camera_klass = typeinfo(v100::camera_typeinfo);
        std::string name, ns;
        const bool name_readable = il2cpp::klass_identity(camera_klass, name, ns, true);
        const bool camera_ok = name_readable && name == "Camera";

        if (have_id) {
            proc::build_checked = true;
            proc::build_ok = id_match;
            diag::log(id_match ? "INFO" : "ERROR",
                      "libunity build-id=%s expected=%s match=%d camera-klass=%s profile=%s",
                      id, v100::libunity_build_id, id_match ? 1 : 0,
                      name_readable ? name.c_str() : "?", v100::profile_name);
        } else if (name_readable) {
            proc::build_checked = true;
            proc::build_ok = camera_ok;
            diag::log(camera_ok ? "WARN" : "ERROR",
                      "libunity build-id note unreadable; Camera TypeInfo resolves to '%s' (%s)",
                      name.c_str(), camera_ok ? "accepting build by klass name" : "profile mismatch");
        } else if (build_attempts >= 20) {
            proc::build_checked = true;
            proc::build_ok = false;
            diag::log("ERROR", "cannot verify libunity build: no build-id note and klass names unreadable");
        } else {
            return false;
        }

        if (!proc::build_ok && proc::skip_build_check) {
            diag::log("WARN", "--skip-build-check: running against an UNVERIFIED build, offsets may be wrong");
            proc::build_ok = true;
        }
        STATUS_SET(build, "build: %s (%s)", proc::build_ok ? "OK" : "MISMATCH",
                   proc::build_id[0] ? proc::build_id : "no build-id");
        return proc::build_ok;
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
        if (size < 0x1000 || size > 4096ULL * 1024 * 1024) continue;
        // Managed/native objects live in private writable mappings. Avoid files and stacks.
        if (std::strchr(line, '/') || std::strstr(line, "[stack")) continue;
        g_scan_maps.push_back({begin, end, true});
    }
    std::fclose(file);
}

inline bool writable_scan_address(uint64_t address) {
    return std::any_of(g_scan_maps.begin(), g_scan_maps.end(),
        [address](const remote_map& map) { return address >= map.begin && address < map.end; });
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

// ---------------------------------------------------------------------------
// Background heap scanner. Finds the few singleton-like managed objects the
// ESP needs (NetObject registry, camera controllers, PlayerAnimationData,
// UnityEngine.Camera wrappers) by their klass pointer. Runs on its own
// thread so a multi-hundred-MiB GC heap never stalls the render loop; the
// render thread only validates and uses the published addresses.
// ---------------------------------------------------------------------------
namespace scanner {
    struct results {
        int pid{-1};
        uint64_t lib{0};
        uint64_t registry{0};
        std::vector<uint64_t> controllers; // kmc / wot instances
        std::vector<uint64_t> animation;   // PlayerAnimationData instances
        std::vector<uint64_t> cameras;     // UnityEngine.Camera managed wrappers
        unsigned passes{0};
        long long last_pass_ms{0};
        double last_pass_mib{0};
        long long last_pass_duration_ms{0};
    };
    inline std::mutex g_mutex;
    inline results g_results;
    inline std::atomic<bool> g_run{false};
    inline std::atomic<bool> g_rescan{true};
    inline std::atomic<bool> g_busy{false};
    // Set by the camera resolver while it has no locked camera. Without it
    // the scanner only re-passes when the registry is missing: a full pass
    // reads ~4 GiB on this game and must not run every 15 s for nothing.
    inline std::atomic<bool> g_want_more{true};
    inline std::thread g_thread;

    // Mappings that can never hold il2cpp objects. Names come from
    // prctl(PR_SET_VMA_ANON_NAME) on Android 11+.
    inline bool skip_mapping(const char* line) {
        static const char* skip[] = {"[anon:dalvik", "[anon:stack_and_tls", "[anon:linker_alloc",
            "[anon:thread signal stack", "[anon:bionic_alloc", "[anon:cfi shadow",
            "[anon:arc4random", "[anon:atexit", "[anon:System property", "[anon:.bss]",
            "[vvar]", "[vdso]", "[heap]"};
        for (const char* name : skip)
            if (std::strstr(line, name)) return true;
        return false;
    }

    inline results snapshot() {
        std::lock_guard<std::mutex> lock(g_mutex);
        return g_results;
    }

    inline void request_rescan() { g_rescan = true; }

    inline void sleep_ms(long long ms) {
        for (long long slept = 0; slept < ms && g_run; slept += 50) usleep(50000);
    }

    inline std::vector<remote_map> read_maps(int pid) {
        std::vector<remote_map> maps;
        char path[64];
        std::snprintf(path, sizeof(path), "/proc/%d/maps", pid);
        FILE* file = std::fopen(path, "r");
        if (!file) return maps;
        char line[1024];
        while (std::fgets(line, sizeof(line), file)) {
            unsigned long long begin = 0, end = 0;
            char perms[5]{};
            if (std::sscanf(line, "%llx-%llx %4s", &begin, &end, perms) != 3) continue;
            if (perms[0] != 'r' || perms[1] != 'w' || end <= begin) continue;
            const uint64_t size = end - begin;
            if (size < 0x1000 || size > 4096ULL * 1024 * 1024) continue;
            if (std::strchr(line, '/') || std::strstr(line, "[stack") || skip_mapping(line)) continue;
            maps.push_back({begin, end, true});
        }
        std::fclose(file);
        return maps;
    }

    inline void pass(int pid, uint64_t lib) {
        const uint64_t registry_class = typeinfo(v100::registry_typeinfo, lib);
        const uint64_t store_class = typeinfo(v100::store_typeinfo, lib);
        const uint64_t kmc_class = typeinfo(v100::camera_main_typeinfo, lib);
        const uint64_t wot_class = typeinfo(v100::camera_arms_typeinfo, lib);
        const uint64_t animation_class = typeinfo(v100::animation_data_typeinfo, lib);
        const uint64_t camera_class = typeinfo(v100::camera_typeinfo, lib);
        if (!registry_class || !store_class) {
            diag::log("SCAN", "TypeInfo registry=0x%llx store=0x%llx not initialised yet (lazy slots)",
                      (unsigned long long)rpm<uint64_t>(lib + v100::registry_typeinfo),
                      (unsigned long long)rpm<uint64_t>(lib + v100::store_typeinfo));
            return;
        }
        const std::vector<remote_map> maps = read_maps(pid);
        results local;
        local.pid = pid;
        local.lib = lib;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            local.passes = g_results.passes;
            if (g_results.pid == pid && g_results.lib == lib &&
                validate_registry(g_results.registry, registry_class, store_class))
                local.registry = g_results.registry;
        }
        const long long started = diag::now_ms();
        uint64_t scanned = 0;
        std::vector<uint64_t> words;
        for (const remote_map& map : maps) {
            uint64_t cursor = map.begin;
            while (cursor < map.end) {
                if (!g_run || proc::pid != pid) return;
                size_t bytes = size_t(std::min<uint64_t>(map.end - cursor, 1024 * 1024));
                bytes &= ~size_t(7);
                if (bytes < 16) break;
                words.resize(bytes / 8);
                if (mem_read(cursor, words.data(), bytes)) {
                    for (size_t i = 0; i < words.size(); ++i) {
                        const uint64_t word = words[i];
                        if (word < 0x10000) continue;
                        const uint64_t object = cursor + i * 8;
                        if (word == registry_class) {
                            if (!local.registry && validate_registry(object, registry_class, store_class)) {
                                local.registry = object;
                                std::lock_guard<std::mutex> lock(g_mutex);
                                g_results.pid = pid;
                                g_results.lib = lib;
                                g_results.registry = object;
                                diag::log("SCAN", "NetObject registry found at 0x%llx after %.1f MiB",
                                          (unsigned long long)object, double(scanned) / 1048576.0);
                            }
                        } else if ((word == kmc_class && kmc_class) || (word == wot_class && wot_class)) {
                            if (local.controllers.size() < 32) local.controllers.push_back(object);
                        } else if (word == animation_class && animation_class) {
                            if (local.animation.size() < 32) local.animation.push_back(object);
                        } else if (word == camera_class && camera_class) {
                            if (local.cameras.size() < 128 && obj_ptr(rpm<uint64_t>(object + v100::unity_cached_ptr)))
                                local.cameras.push_back(object);
                        }
                    }
                }
                cursor += bytes;
                scanned += bytes;
                // ~400 MiB/s ceiling keeps one core from being pinned on
                // weaker devices while the game is rendering.
                usleep(1500);
            }
        }
        local.passes += 1;
        local.last_pass_ms = diag::now_ms();
        local.last_pass_mib = double(scanned) / 1048576.0;
        local.last_pass_duration_ms = local.last_pass_ms - started;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_results = local;
        }
        diag::log("SCAN", "pass %u done: %.1f MiB in %lld ms registry=0x%llx controllers=%zu animation=%zu cameras=%zu maps=%zu",
                  local.passes, local.last_pass_mib, local.last_pass_duration_ms,
                  (unsigned long long)local.registry, local.controllers.size(),
                  local.animation.size(), local.cameras.size(), maps.size());
    }

    inline void loop() {
        // Background priority: the scan must never steal a core from the game.
        ::setpriority(PRIO_PROCESS, static_cast<id_t>(::syscall(SYS_gettid)), 19);
        unsigned idle_passes = 0; // passes that found nothing new while the registry is known
        while (g_run) {
            const int pid = proc::pid;
            const uint64_t lib = proc::lib;
            if (pid <= 0 || lib == 0 || !proc::build_ok) {
                sleep_ms(200);
                continue;
            }
            {
                std::lock_guard<std::mutex> lock(g_mutex);
                if (g_results.pid != pid || g_results.lib != lib) {
                    g_results = results{};
                    g_results.pid = pid;
                    g_results.lib = lib;
                    g_rescan = true;
                }
            }
            bool essential_missing = false;
            long long last_pass = 0;
            {
                std::lock_guard<std::mutex> lock(g_mutex);
                essential_missing = g_results.registry == 0 || g_results.cameras.empty();
                last_pass = g_results.last_pass_ms;
            }
            const long long since_pass = diag::now_ms() - last_pass;
            // A pass is worth its CPU only while something is still missing:
            // the registry (before/after a match) or a camera for the
            // resolver. Once everything is locked the thread idles. Inside a
            // match (registry known) repeated fruitless passes back off up to
            // two minutes so a layout mismatch does not cost a 4 GiB scan
            // every 15 s while the player is in a round.
            const bool registry_known = !essential_missing || g_results.registry != 0;
            long long interval = 15000;
            if (registry_known && idle_passes > 0) interval = std::min<long long>(120000, 15000LL << std::min<unsigned>(idle_passes, 3));
            const bool due = (g_rescan && since_pass > 3000) ||
                             ((essential_missing || g_want_more) && since_pass > interval);
            if (!due) {
                sleep_ms(100);
                continue;
            }
            const bool wanted_more = g_want_more;
            g_rescan = false;
            g_busy = true;
            pass(pid, lib);
            g_busy = false;
            (void)crash::g_frame; // the scanner thread shares the crash handler; its stage is implicit
            {
                std::lock_guard<std::mutex> lock(g_mutex);
                if (g_results.registry && wanted_more) ++idle_passes; else idle_passes = 0;
            }
            sleep_ms(3000);
        }
    }

    inline void start() {
        if (g_run) return;
        g_run = true;
        g_thread = std::thread(loop);
    }

    inline void stop() {
        if (!g_run) return;
        g_run = false;
        if (g_thread.joinable()) g_thread.join();
    }
}

inline void cache_player(uint64_t object, uint32_t id, long long now) {
    if (!id) id = uint32_t((object >> 4) ^ object);
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

inline bool registry_ready() {
    ensure_scan_context();
    // The registry pointer is revalidated (5 reads) four times a second;
    // players() still checks count/array on every frame.
    static long long last_validation = 0;
    if (g_registry && !diag::every(last_validation, 250)) return true;
    const uint64_t registry_class = typeinfo(v100::registry_typeinfo);
    const uint64_t store_class = typeinfo(v100::store_typeinfo);
    static long long type_log = 0;
    if (diag::every(type_log, 5000))
        diag::log(registry_class && store_class ? "INFO" : "ERROR",
                  "TypeInfo registry=0x%llx store=0x%llx player=0x%llx transforms=0x%llx kmc=0x%llx wot=0x%llx (0x2xxxxxxx = lazy slot, class not initialised by generated code)",
                  (unsigned long long)registry_class, (unsigned long long)store_class,
                  (unsigned long long)rpm<uint64_t>(proc::lib + v100::player_typeinfo),
                  (unsigned long long)rpm<uint64_t>(proc::lib + v100::transforms_typeinfo),
                  (unsigned long long)rpm<uint64_t>(proc::lib + v100::camera_main_typeinfo),
                  (unsigned long long)rpm<uint64_t>(proc::lib + v100::camera_arms_typeinfo));
    if (!registry_class || !store_class) return false;
    if (validate_registry(g_registry, registry_class, store_class)) return true;
    g_registry = 0;
    const scanner::results snap = scanner::snapshot();
    if (snap.pid == proc::pid && snap.lib == proc::lib &&
        validate_registry(snap.registry, registry_class, store_class)) {
        g_registry = snap.registry;
        diag::log("INFO", "NetObject registry in use: 0x%llx", (unsigned long long)g_registry);
        return true;
    }
    scanner::request_rescan();
    STATUS_SET(scan, "scan: %s pass=%u %.0f MiB", scanner::g_busy ? "running" : "waiting",
               snap.passes, snap.last_pass_mib);
    return false;
}

// One block read per NetObject (odq): wwv header, flags, mtc/ydm/vqn pointers
// and the team byte. Everything the frame needs from the player object itself.
struct player_snapshot {
    uint64_t object{0};
    uint32_t id{0};
    uint64_t world{0};
    uint8_t flags[3]{0, 0, 0};   // +0x44 +0x45 +0x46
    uint64_t transform{0};       // odq+0x68 mtc
    uint64_t stats{0};           // odq+0x70 ydm
    uint64_t camera_state{0};    // odq+0x78 vqn
    uint8_t view_mode{0};        // odq+0x80 dvp
    uint8_t team{0};             // odq+0x90
    bool is_local() const { return flags[g_local_flag_offset - int(v100::player_local)] != 0; }
};

inline bool read_player_snapshot(uint64_t object, uint64_t player_class, player_snapshot& out) {
    if (!obj_ptr(object)) return false;
    uint8_t block[0x98]{};
    if (!mem_read(object, block, sizeof(block))) return false;
    uint64_t klass = 0;
    std::memcpy(&klass, block, 8);
    if (klass != player_class) return false;
    out.object = object;
    std::memcpy(&out.id, block + v100::net_object_id, 4);
    std::memcpy(&out.world, block + v100::player_world, 8);
    std::memcpy(out.flags, block + v100::player_local, 3);
    std::memcpy(&out.transform, block + v100::player_transform, 8);
    std::memcpy(&out.stats, block + v100::player_stats, 8);
    std::memcpy(&out.camera_state, block + v100::player_camera_state, 8);
    out.view_mode = block[v100::player_view_mode];
    out.team = block[v100::player_team];
    return out.world != 0;
}

inline int resolve_local_flag(const std::vector<player_snapshot>& objects) {
    if (objects.empty()) return g_local_flag_offset;
    for (int offset : {0x44, 0x45, 0x46}) {
        int true_count = 0;
        for (const player_snapshot& p : objects)
            if (p.flags[offset - 0x44] != 0) ++true_count;
        // A live match has exactly one local NetObject. If a flag is true for
        // zero or several objects it is IsServer/IsLocalOwned, not IsLocal.
        if (true_count == 1) {
            if (g_local_flag_offset != offset) {
                diag::log("INFO", "local NetObject flag resolved at +0x%x (old +0x%x)",
                          offset, g_local_flag_offset);
                g_local_flag_offset = offset;
            }
            return offset;
        }
    }
    return g_local_flag_offset;
}

inline std::vector<player_snapshot> players() {
    std::vector<player_snapshot> out;
    if (!registry_ready()) return out;
    const uint64_t player_class = typeinfo(v100::player_typeinfo);
    uint64_t store = rpm<uint64_t>(g_registry + v100::registry_store);
    uint64_t list = rpm<uint64_t>(store + v100::store_objects);
    int count = rpm<int>(list + v100::list_count);
    uint64_t array = rpm<uint64_t>(list + v100::list_items);
    if (!player_class || count < 0 || count > 4096 || !array) {
        diag::log("WARN", "registry became invalid playerClass=0x%llx count=%d array=0x%llx",
                  (unsigned long long)player_class, count, (unsigned long long)array);
        g_registry = 0;
        scanner::request_rescan();
        return out;
    }
    const long long now = diag::now_ms();
    size_t raw_exact = 0;
    uint64_t active_world = 0;
    std::vector<player_snapshot> current;
    std::vector<uint64_t> items(size_t(count), 0);
    if (count > 0 && !mem_read(array + v100::array_data, items.data(), items.size() * 8)) return out;
    for (int i = 0; i < count; ++i) {
        player_snapshot snap{};
        if (!read_player_snapshot(items[size_t(i)], player_class, snap)) continue;
        ++raw_exact;
        if (!active_world || snap.is_local()) active_world = snap.world;
        cache_player(snap.object, snap.id, now);
        current.push_back(snap);
    }
    // Cache validation costs three reads per entry; once every 500 ms is enough.
    static long long last_cache_check = 0;
    if (diag::every(last_cache_check, 500)) {
        g_player_cache.erase(std::remove_if(g_player_cache.begin(), g_player_cache.end(),
            [now, player_class, active_world](const player_cache_entry& entry) {
                return now - entry.seen_ms > 2500 || !exact_class(entry.object, player_class) ||
                       !rpm<uint64_t>(entry.object + v100::player_world) ||
                       (active_world && rpm<uint64_t>(entry.object + v100::player_world) != active_world);
            }), g_player_cache.end());
    }
    for (const player_snapshot& snap : current) {
        if (out.size() >= 10) break;
        out.push_back(snap);
    }
    // A dead player disappears from the registry but can remain in the cache
    // for a couple of seconds, still carrying the last pre-death stats. Once
    // the registry has a real match snapshot, do not resurrect cache-only objects.
    if (current.size() < 2) {
        for (const auto& entry : g_player_cache) {
            if (out.size() >= 10) break;
            bool present = false;
            for (const player_snapshot& snap : out) if (snap.object == entry.object) { present = true; break; }
            if (present) continue;
            player_snapshot snap{};
            if (read_player_snapshot(entry.object, player_class, snap)) out.push_back(snap);
        }
    }
    if (!out.empty()) resolve_local_flag(out);
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

// Walk the six component arrays of wwv.yvjn (cpa +0x10..+0x38) and return the
// first component whose klass matches.
inline uint64_t find_player_component(uint64_t player, uint64_t component_class) {
    if (!player || !component_class) return 0;
    const uint64_t container = rpm<uint64_t>(player + v100::player_components);
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

// Heuristic name lookup: no field in dump.cs 1.1.0 is confirmed to hold the
// nickname, so this walks the player's component fields for a managed string
// that looks like a nickname. Treat the result as best-effort.
inline std::string player_name(uint64_t player) {
    const uint64_t known_string = rpm<uint64_t>(proc::lib + v100::player_string_literal);
    const uint64_t string_class = plausible(known_string) ? rpm<uint64_t>(known_string) : 0;
    const uint64_t container = rpm<uint64_t>(player + v100::player_components);
    if (!string_class || !plausible(container)) return {};
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
                if (!obj_ptr(value)) continue;
                std::string candidate = read_managed_string(value, string_class);
                if (candidate.empty() || candidate.size() > 96 || noisy_player_name(candidate)) continue;
                return candidate;
            }
        }
    }
    return {};
}

inline bool finite_matrix(const matrix& m) {
    const float* p = reinterpret_cast<const float*>(&m);
    float sum = 0.f;
    for (int i = 0; i < 16; ++i) { if (!std::isfinite(p[i]) || std::fabs(p[i]) > 1000000.f) return false; sum += std::fabs(p[i]); }
    return sum > .01f;
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

inline bool validate_camera(uint64_t camera, matrix& view, matrix& projection) {
    uint64_t vtable = rpm<uint64_t>(camera);
    uint64_t game_object = rpm<uint64_t>(camera + 0x20);
    if (vtable < proc::lib + v100::data_relro_begin || vtable >= proc::lib + v100::data_end ||
        !plausible(game_object)) return false;
    if (!mem_read(camera + v100::camera_view, &view, sizeof(view)) || !mem_read(camera + v100::camera_projection, &projection, sizeof(projection))) return false;
    const float* v = reinterpret_cast<const float*>(&view);
    const float* p = reinterpret_cast<const float*>(&projection);
    return finite_matrix(view) && finite_matrix(projection) && std::fabs(v[3]) < .001f && std::fabs(v[7]) < .001f &&
           std::fabs(v[11]) < .001f && std::fabs(v[15] - 1.f) < .01f && std::fabs(p[0]) > .001f && std::fabs(p[5]) > .001f;
}

// Snapshot of a native Camera object. Unity keeps worldToCamera, projection,
// implicit/skybox/clip variants inline; the object is larger than 0x300, so
// read up to 0x800 and fall back to shorter windows when the tail is unmapped.
inline constexpr size_t k_camera_block = 0x800;
inline size_t read_camera_block(uint64_t native, uint8_t* buf) {
    for (size_t size : {k_camera_block, size_t(0x400), size_t(0x300)}) {
        if (mem_read(native, buf, size)) return size;
    }
    return 0;
}

// Every 16-byte aligned window that looks like a view or a projection matrix.
inline void scan_matrix_windows(const uint8_t* buf, size_t size, std::vector<int>& views, std::vector<int>& projs) {
    views.clear();
    projs.clear();
    for (size_t off = 0x40; off + sizeof(matrix) <= size; off += 0x10) {
        const float* m = reinterpret_cast<const float*>(buf + off);
        if (matrix_window_sane(m, true)) views.push_back(int(off));
        else if (matrix_window_sane(m, false)) projs.push_back(int(off));
    }
}

// Generic pattern scan: find (view,proj) matrix pair inside a native Camera
// by float patterns. The known Unity layout (+0x70/+0xB0) is tried first.
inline bool detect_pair(uint64_t obj, matrix& view, matrix& projection,
                        int& view_off, int& proj_off, const char* tag) {
    uint8_t buf[k_camera_block]{};
    const size_t size = read_camera_block(obj, buf);
    if (!size) return false;
    auto at = [&](int off) -> const float* {
        return reinterpret_cast<const float*>(buf + off);
    };
    if (matrix_window_sane(at(int(v100::camera_view)), true) &&
        matrix_window_sane(at(int(v100::camera_projection)), false)) {
        view_off = int(v100::camera_view);
        proj_off = int(v100::camera_projection);
    } else {
        std::vector<int> views, projs;
        scan_matrix_windows(buf, size, views, projs);
        if (views.empty() || projs.empty()) return false;
        view_off = views.front();
        proj_off = projs.front();
    }
    std::memcpy(&view, at(view_off), sizeof(view));
    std::memcpy(&projection, at(proj_off), sizeof(projection));
    const float* p = at(proj_off);
    diag::log("CAMERA", "%s pair obj=0x%llx view=+0x%x proj=+0x%x fov=%.1f proj=(%.3f %.3f)",
              tag, (unsigned long long)obj, view_off, proj_off,
              2.f * std::atan(1.f / p[5]) * 57.29578f, p[0], p[5]);
    return true;
}

// Re-read a pattern-located matrix pair. Two syscalls per frame.
inline bool read_matrix_pair(uint64_t native, int view_off, int proj_off, matrix& view, matrix& projection) {
    if (!native || view_off < 0 || proj_off < 0) return false;
    if (!mem_read(native + uint64_t(view_off), &view, sizeof(view)) ||
        !mem_read(native + uint64_t(proj_off), &projection, sizeof(projection))) return false;
    return matrix_window_sane(reinterpret_cast<const float*>(&view), true) &&
           matrix_window_sane(reinterpret_cast<const float*>(&projection), false);
}
