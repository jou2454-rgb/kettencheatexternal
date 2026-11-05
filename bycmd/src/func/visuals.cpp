// visuals.cpp — Internal autonomous ESP for Standoff 2 (Unity 6, ARM64)
// No hardcoded offsets. Resolves il2cpp API + Unity icalls at runtime.
// Discovers player class by field-signature fingerprinting.

#include "visuals.hpp"
#include "../ui/cfg.hpp"
#include "../ui/theme/theme.hpp"
#include "imgui.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>

#ifdef __ANDROID__
#include <android/log.h>
#define ESPLOG(fmt, ...) __android_log_print(ANDROID_LOG_INFO, "esp", fmt, ##__VA_ARGS__)
#define ESPWARN(fmt, ...) __android_log_print(ANDROID_LOG_WARN, "esp", fmt, ##__VA_ARGS__)
#else
#define ESPLOG(fmt, ...) do { fprintf(stderr, "[ESP] " fmt "\n", ##__VA_ARGS__); } while(0)
#define ESPWARN(fmt, ...) do { fprintf(stderr, "[ESP-W] " fmt "\n", ##__VA_ARGS__); } while(0)
#endif

extern ImFont* espFont;

// ===================================================================
// Math
// ===================================================================
struct Vec3  { float x, y, z; };
struct Mat4  { float m[4][4]; };

static Mat4 mat_mul(const Mat4& a, const Mat4& b) {
    Mat4 r{};
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            for (int k = 0; k < 4; k++)
                r.m[i][j] += a.m[i][k] * b.m[k][j];
    return r;
}

static bool w2s(const Mat4& vp, const Vec3& pos, float sw, float sh, ImVec2& out) {
    float x = vp.m[0][0]*pos.x + vp.m[0][1]*pos.y + vp.m[0][2]*pos.z + vp.m[0][3];
    float y = vp.m[1][0]*pos.x + vp.m[1][1]*pos.y + vp.m[1][2]*pos.z + vp.m[1][3];
    float w = vp.m[3][0]*pos.x + vp.m[3][1]*pos.y + vp.m[3][2]*pos.z + vp.m[3][3];
    if (w < 0.001f) return false;
    float iw = 1.f / w;
    out.x = (0.5f + x * iw * 0.5f) * sw;
    out.y = (0.5f - y * iw * 0.5f) * sh;
    return true;
}

// ===================================================================
// Safe pointer validation (/dev/null write check)
// ===================================================================
static int g_null_fd = -1;

static bool ptr_ok(const void* p, size_t sz = 4) {
    if (!p) return false;
    auto a = (uintptr_t)p;
    if (a < 0x10000) return false;
    if (g_null_fd < 0) g_null_fd = open("/dev/null", O_WRONLY | O_CLOEXEC);
    if (g_null_fd < 0) return a > 0x10000;
    return write(g_null_fd, p, sz) == (ssize_t)sz;
}

template<typename T>
static bool safe_read(const void* addr, T& out) {
    if (!ptr_ok(addr, sizeof(T))) return false;
    __builtin_memcpy(&out, addr, sizeof(T));
    return true;
}

static bool read_il2cpp_string(void* str, char* buf, int buf_sz) {
    if (!ptr_ok(str, 0x16)) { buf[0] = 0; return false; }
    int32_t len;
    __builtin_memcpy(&len, (uint8_t*)str + 0x10, 4);
    if (len <= 0 || len > 200) { buf[0] = 0; return false; }
    const char16_t* chars = (const char16_t*)((uint8_t*)str + 0x14);
    if (!ptr_ok(chars, len * 2)) { buf[0] = 0; return false; }
    int o = 0;
    for (int i = 0; i < len && o < buf_sz - 1; i++) {
        char16_t c;
        __builtin_memcpy(&c, &chars[i], 2);
        buf[o++] = (c < 128) ? (char)c : '?';
    }
    buf[o] = 0;
    return o > 0;
}

// ===================================================================
// Il2Cpp API function pointer types
// ===================================================================
#define ILFN(ret, name, ...) typedef ret (*fn_##name)(__VA_ARGS__)

ILFN(void*,  domain_get,            void);
ILFN(void**, domain_get_assemblies,  void*, size_t*);
ILFN(void*,  assembly_get_image,     void*);
ILFN(size_t, image_get_class_count,  void*);
ILFN(void*,  image_get_class,        void*, size_t);
ILFN(const char*, image_get_name,    void*);
ILFN(const char*, class_get_name,    void*);
ILFN(const char*, class_get_namespace, void*);
ILFN(void*,  class_get_parent,       void*);
ILFN(size_t, class_num_fields,       void*);
ILFN(void*,  class_get_fields,       void*, void**);
ILFN(const char*, field_get_name,    void*);
ILFN(size_t, field_get_offset,       void*);
ILFN(void*,  field_get_type,         void*);
ILFN(int,    type_get_type,          void*);
ILFN(void*,  class_get_type,         void*);
ILFN(void*,  type_get_object,        void*);
ILFN(void*,  resolve_icall,          const char*);
ILFN(void*,  thread_attach,          void*);
ILFN(void,   thread_detach,          void*);
ILFN(void*,  class_from_name,        void*, const char*, const char*);
ILFN(void*,  class_get_method_from_name, void*, const char*, int);
ILFN(void*,  runtime_invoke,         void*, void*, void**, void**);
ILFN(int,    field_get_flags,        void*);

typedef void* (*fn_cam_main)(void);
typedef void  (*fn_cam_matrix)(void*, Mat4*);
typedef void* (*fn_get_transform)(void*);
typedef void  (*fn_get_position)(void*, Vec3*);

enum {
    T_BOOL   = 0x02,
    T_I4     = 0x08,
    T_U4     = 0x09,
    T_R4     = 0x0C,
    T_STRING = 0x0E,
    T_CLASS  = 0x12,
    T_ARRAY  = 0x14,
    T_GENINS = 0x15,
};

// ===================================================================
// API singleton
// ===================================================================
static struct API {
    bool ok = false;
    void* lib = nullptr;

    fn_domain_get              domain_get;
    fn_domain_get_assemblies   domain_get_assemblies;
    fn_assembly_get_image      assembly_get_image;
    fn_image_get_class_count   image_get_class_count;
    fn_image_get_class         image_get_class;
    fn_image_get_name          image_get_name;
    fn_class_get_name          class_get_name;
    fn_class_get_namespace     class_get_namespace;
    fn_class_get_parent        class_get_parent;
    fn_class_num_fields        class_num_fields;
    fn_class_get_fields        class_get_fields;
    fn_field_get_name          field_get_name;
    fn_field_get_offset        field_get_offset;
    fn_field_get_type          field_get_type;
    fn_type_get_type           type_get_type;
    fn_class_get_type          class_get_type;
    fn_type_get_object         type_get_object;
    fn_resolve_icall           resolve_icall;
    fn_thread_attach           thread_attach;
    fn_thread_detach           thread_detach;
    fn_class_from_name         class_from_name;
    fn_class_get_method_from_name class_get_method_from_name;
    fn_runtime_invoke          runtime_invoke;
    fn_field_get_flags         field_get_flags;

    fn_cam_main        cam_main;
    fn_cam_matrix      cam_view;
    fn_cam_matrix      cam_proj;
    fn_get_transform   get_transform;
    fn_get_position    get_position;
} api;

// ===================================================================
// Stage 1: Resolve il2cpp API via dlsym
// ===================================================================
static bool resolve_api() {
    if (api.ok) return true;

    api.lib = dlopen("libil2cpp.so", RTLD_NOLOAD);
    if (!api.lib) return false;

#define S(f, sym) api.f = (decltype(api.f))dlsym(api.lib, sym); \
    if (!api.f) { ESPWARN("sym miss: %s", sym); return false; }

    S(domain_get,             "il2cpp_domain_get")
    S(domain_get_assemblies,  "il2cpp_domain_get_assemblies")
    S(assembly_get_image,     "il2cpp_assembly_get_image")
    S(image_get_class_count,  "il2cpp_image_get_class_count")
    S(image_get_class,        "il2cpp_image_get_class")
    S(image_get_name,         "il2cpp_image_get_name")
    S(class_get_name,         "il2cpp_class_get_name")
    S(class_get_namespace,    "il2cpp_class_get_namespace")
    S(class_get_parent,       "il2cpp_class_get_parent")
    S(class_num_fields,       "il2cpp_class_num_fields")
    S(class_get_fields,       "il2cpp_class_get_fields")
    S(field_get_name,         "il2cpp_field_get_name")
    S(field_get_offset,       "il2cpp_field_get_offset")
    S(field_get_type,         "il2cpp_field_get_type")
    S(type_get_type,          "il2cpp_type_get_type")
    S(class_get_type,         "il2cpp_class_get_type")
    S(type_get_object,        "il2cpp_type_get_object")
    S(resolve_icall,          "il2cpp_resolve_icall")
    S(thread_attach,          "il2cpp_thread_attach")
    S(thread_detach,          "il2cpp_thread_detach")
    S(class_from_name,        "il2cpp_class_from_name")
    S(class_get_method_from_name, "il2cpp_class_get_method_from_name")
    S(runtime_invoke,         "il2cpp_runtime_invoke")
#undef S

    api.field_get_flags = (fn_field_get_flags)dlsym(api.lib, "il2cpp_field_get_flags");
    api.ok = true;
    ESPLOG("il2cpp API resolved");
    return true;
}

// ===================================================================
// Stage 2: Resolve Unity icalls + thread attach
// ===================================================================
static void* g_thread = nullptr;

static bool resolve_icalls() {
    if (api.cam_main) return true;

    void* domain = api.domain_get();
    if (!domain) return false;

    if (!g_thread) {
        g_thread = api.thread_attach(domain);
        if (!g_thread) return false;
        ESPLOG("thread attached");
    }

    auto try_icall = [](const char* a, const char* b = nullptr) -> void* {
        void* p = api.resolve_icall(a);
        if (!p && b) p = api.resolve_icall(b);
        return p;
    };

    api.cam_main = (fn_cam_main)try_icall(
        "UnityEngine.Camera::get_main()",
        "UnityEngine.Camera::get_main");
    if (!api.cam_main) { ESPWARN("icall: Camera::get_main miss"); return false; }

    api.cam_view = (fn_cam_matrix)try_icall(
        "UnityEngine.Camera::get_worldToCameraMatrix_Injected(UnityEngine.Matrix4x4&)",
        "UnityEngine.Camera::get_worldToCameraMatrix_Injected");
    if (!api.cam_view) { ESPWARN("icall: cam_view miss"); return false; }

    api.cam_proj = (fn_cam_matrix)try_icall(
        "UnityEngine.Camera::get_projectionMatrix_Injected(UnityEngine.Matrix4x4&)",
        "UnityEngine.Camera::get_projectionMatrix_Injected");
    if (!api.cam_proj) { ESPWARN("icall: cam_proj miss"); return false; }

    api.get_transform = (fn_get_transform)try_icall(
        "UnityEngine.Component::get_transform()",
        "UnityEngine.Component::get_transform");
    if (!api.get_transform) { ESPWARN("icall: get_transform miss"); return false; }

    api.get_position = (fn_get_position)try_icall(
        "UnityEngine.Transform::get_position_Injected(UnityEngine.Vector3&)",
        "UnityEngine.Transform::get_position_Injected");
    if (!api.get_position) { ESPWARN("icall: get_position miss"); return false; }

    ESPLOG("all icalls resolved");
    return true;
}

// ===================================================================
// Il2Cpp array helper
// ===================================================================
struct ArrayView {
    void** items;
    int32_t count;
};

static ArrayView read_array(void* arr) {
    ArrayView v{nullptr, 0};
    if (!arr || !ptr_ok(arr, 0x28)) return v;
    int32_t len;
    __builtin_memcpy(&len, (uint8_t*)arr + 0x18, 4);
    if (len <= 0 || len > 500) return v;
    void** elems = (void**)((uint8_t*)arr + 0x20);
    if (!ptr_ok(elems, (size_t)len * 8)) return v;
    v.items = elems;
    v.count = len;
    return v;
}

// ===================================================================
// FindObjectsOfType wrapper
// ===================================================================
typedef void* (*fn_fot2)(void*, bool);
typedef void* (*fn_fot1)(void*);
static fn_fot2 g_fot2 = nullptr;
static fn_fot1 g_fot1 = nullptr;
static bool    g_fot_tried = false;

static void init_fot() {
    if (g_fot_tried) return;
    g_fot_tried = true;

    g_fot2 = (fn_fot2)api.resolve_icall(
        "UnityEngine.Object::FindObjectsOfType(System.Type,System.Boolean)");
    if (g_fot2) { ESPLOG("FindObjectsOfType (2-arg) resolved"); return; }

    g_fot1 = (fn_fot1)api.resolve_icall(
        "UnityEngine.Object::FindObjectsOfType(System.Type)");
    if (g_fot1) { ESPLOG("FindObjectsOfType (1-arg) resolved"); return; }

    ESPWARN("FindObjectsOfType icall not found, will use runtime_invoke fallback");
}

static void* call_fot(void* klass) {
    void* il2type = api.class_get_type(klass);
    if (!il2type) return nullptr;
    void* managed_type = api.type_get_object(il2type);
    if (!managed_type) return nullptr;

    if (g_fot2) return g_fot2(managed_type, false);
    if (g_fot1) return g_fot1(managed_type);

    void* domain = api.domain_get();
    if (!domain) return nullptr;
    size_t asm_count = 0;
    void** asms = api.domain_get_assemblies(domain, &asm_count);
    for (size_t i = 0; i < asm_count; i++) {
        void* img = api.assembly_get_image(asms[i]);
        const char* iname = api.image_get_name(img);
        if (!iname) continue;
        if (!strstr(iname, "UnityEngine.CoreModule") && strcmp(iname, "UnityEngine.dll") != 0)
            continue;
        void* obj_class = api.class_from_name(img, "UnityEngine", "Object");
        if (!obj_class) continue;
        void* method = api.class_get_method_from_name(obj_class, "FindObjectsOfType", 2);
        if (!method) method = api.class_get_method_from_name(obj_class, "FindObjectsOfType", 1);
        if (!method) continue;
        void* exc = nullptr;
        uint8_t inactive = 0;
        void* params[2] = { managed_type, &inactive };
        return api.runtime_invoke(method, nullptr, params, &exc);
    }
    return nullptr;
}

// ===================================================================
// Player class discovery via fingerprinting
// ===================================================================
static bool is_mono_behaviour(void* klass) {
    void* cur = klass;
    for (int d = 0; d < 15 && cur; d++) {
        const char* n = api.class_get_name(cur);
        if (!n) break;
        if (strcmp(n, "MonoBehaviour") == 0) return true;
        cur = api.class_get_parent(cur);
    }
    return false;
}

struct FieldSlot {
    size_t offset;
    int    type_enum;
};

struct PlayerClassInfo {
    void*  klass        = nullptr;
    size_t off_is_local = 0;
    size_t off_hp       = 0;
    size_t off_team     = 0;
    size_t off_nickname = 0;
    bool   valid        = false;
};

static PlayerClassInfo g_pi;

static int score_class(void* klass,
                       FieldSlot* bools, int& nb,
                       FieldSlot* ints,  int& ni,
                       FieldSlot* strs,  int& ns) {
    nb = ni = ns = 0;
    int score = 0, class_refs = 0, total = 0;

    void* iter = nullptr;
    void* field;
    while ((field = api.class_get_fields(klass, &iter)) != nullptr) {
        if (api.field_get_flags) {
            int fl = api.field_get_flags(field);
            if (fl & 0x10) continue;
        }
        size_t off = api.field_get_offset(field);
        void* ft = api.field_get_type(field);
        int te = ft ? api.type_get_type(ft) : 0;
        total++;

        if (te == T_BOOL && nb < 8)               { bools[nb++] = {off, te}; }
        else if ((te == T_I4 || te == T_U4) && ni < 8) { ints[ni++] = {off, te}; }
        else if (te == T_STRING && ns < 4)         { strs[ns++] = {off, te}; }
        else if (te == T_CLASS || te == T_GENINS)  { class_refs++; }
    }

    if (nb >= 1) score += 3;
    if (ni >= 2) score += 3;
    if (ns >= 1) score += 2;
    if (class_refs >= 3) score += 2;
    if (total >= 8 && total <= 50) score += 1;

    const char* cname = api.class_get_name(klass);
    if (cname) {
        if (strstr(cname, "Manager") || strstr(cname, "Controller") ||
            strstr(cname, "Canvas")  || strstr(cname, "Button") ||
            strstr(cname, "Menu")    || strstr(cname, "Lobby") ||
            strstr(cname, "UI"))
            score -= 5;
    }
    const char* ns_name = api.class_get_namespace(klass);
    if (ns_name && (strstr(ns_name, "UI") || strstr(ns_name, "Editor")))
        score -= 3;

    return score;
}

static bool validate_player_class(void* klass,
                                  FieldSlot* bools, int nb,
                                  FieldSlot* ints,  int ni,
                                  FieldSlot* strs,  int ns,
                                  PlayerClassInfo& out) {
    void* arr = call_fot(klass);
    ArrayView av = read_array(arr);
    if (av.count < 2 || av.count > 30) return false;

    for (int bi = 0; bi < nb; bi++) {
        int local_count = 0;
        bool ok = true;
        for (int p = 0; p < av.count; p++) {
            void* obj = av.items[p];
            if (!obj || !ptr_ok(obj, bools[bi].offset + 1)) { ok = false; break; }
            uint8_t val;
            __builtin_memcpy(&val, (uint8_t*)obj + bools[bi].offset, 1);
            if (val == 1) local_count++;
        }
        if (!ok || local_count != 1) continue;

        for (int hi = 0; hi < ni; hi++) {
            bool hp_ok = true;
            for (int p = 0; p < av.count; p++) {
                void* obj = av.items[p];
                if (!ptr_ok(obj, ints[hi].offset + 4)) { hp_ok = false; break; }
                int32_t v;
                __builtin_memcpy(&v, (uint8_t*)obj + ints[hi].offset, 4);
                if (v < 0 || v > 200) { hp_ok = false; break; }
            }
            if (!hp_ok) continue;

            for (int ti = 0; ti < ni; ti++) {
                if (ti == hi) continue;
                bool team_ok = true;
                for (int p = 0; p < av.count; p++) {
                    void* obj = av.items[p];
                    if (!ptr_ok(obj, ints[ti].offset + 4)) { team_ok = false; break; }
                    int32_t v;
                    __builtin_memcpy(&v, (uint8_t*)obj + ints[ti].offset, 4);
                    if (v < 0 || v > 10) { team_ok = false; break; }
                }
                if (!team_ok) continue;

                out.klass        = klass;
                out.off_is_local = bools[bi].offset;
                out.off_hp       = ints[hi].offset;
                out.off_team     = ints[ti].offset;
                out.off_nickname = (ns > 0) ? strs[0].offset : 0;
                out.valid = true;

                ESPLOG("FOUND player class: %s  instances=%d",
                       api.class_get_name(klass), av.count);
                ESPLOG("  isLocal@0x%x  HP@0x%x  team@0x%x  nick@0x%x",
                       (unsigned)out.off_is_local, (unsigned)out.off_hp,
                       (unsigned)out.off_team, (unsigned)out.off_nickname);
                return true;
            }
        }
    }
    return false;
}

static bool discover_player_class() {
    if (g_pi.valid) return true;

    init_fot();

    void* domain = api.domain_get();
    if (!domain) return false;

    size_t asm_count = 0;
    void** asms = api.domain_get_assemblies(domain, &asm_count);

    struct Candidate { void* klass; int score; };
    Candidate best[16];
    int nbest = 0;

    for (size_t ai = 0; ai < asm_count; ai++) {
        void* img = api.assembly_get_image(asms[ai]);
        if (!img) continue;
        size_t cc = api.image_get_class_count(img);
        for (size_t ci = 0; ci < cc; ci++) {
            void* klass = api.image_get_class(img, ci);
            if (!klass) continue;
            if (!is_mono_behaviour(klass)) continue;

            FieldSlot bools[8], ints_arr[8], strs[4];
            int n_b, n_i, n_s;
            int sc = score_class(klass, bools, n_b, ints_arr, n_i, strs, n_s);
            if (sc < 6) continue;

            if (nbest < 16) {
                best[nbest++] = {klass, sc};
            } else {
                int worst_i = 0;
                for (int j = 1; j < 16; j++)
                    if (best[j].score < best[worst_i].score) worst_i = j;
                if (sc > best[worst_i].score)
                    best[worst_i] = {klass, sc};
            }
        }
    }

    for (int i = 0; i < nbest - 1; i++)
        for (int j = i + 1; j < nbest; j++)
            if (best[j].score > best[i].score) {
                Candidate tmp = best[i]; best[i] = best[j]; best[j] = tmp;
            }

    ESPLOG("fingerprint: %d candidates from %zu assemblies", nbest, asm_count);

    for (int i = 0; i < nbest; i++) {
        FieldSlot bools[8], ints_arr[8], strs[4];
        int n_b, n_i, n_s;
        score_class(best[i].klass, bools, n_b, ints_arr, n_i, strs, n_s);

        ESPLOG("  trying: %s (score=%d)", api.class_get_name(best[i].klass), best[i].score);

        if (validate_player_class(best[i].klass, bools, n_b, ints_arr, n_i, strs, n_s, g_pi))
            return true;
    }

    ESPWARN("player class not found (not in match?)");
    return false;
}

// ===================================================================
// Per-frame game data
// ===================================================================
struct PlayerFrame {
    void* object;
    Vec3  pos;
    int32_t hp;
    int32_t team;
    bool  is_local;
    char  nick[64];
    float dist;
};

static PlayerFrame  g_pf[32];
static int          g_pf_count = 0;
static int          g_local_team = -1;
static Vec3         g_local_pos{};
static Mat4         g_vp{};
static bool         g_vp_ok = false;
static int          g_frame = 0;

static bool get_camera_vp() {
    void* cam = api.cam_main();
    if (!cam || !ptr_ok(cam, 0x10)) return false;

    Mat4 view{}, proj{};
    api.cam_view(cam, &view);
    api.cam_proj(cam, &proj);

    float sum = 0.f;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            sum += fabsf(view.m[i][j]) + fabsf(proj.m[i][j]);
    if (sum < 0.01f) return false;

    g_vp = mat_mul(proj, view);
    return true;
}

static void refresh_players() {
    g_pf_count = 0;
    g_local_team = -1;

    if (!g_pi.valid) return;

    void* arr = call_fot(g_pi.klass);
    ArrayView av = read_array(arr);
    if (av.count <= 0) return;

    Vec3 local_pos{};
    bool found_local = false;

    for (int i = 0; i < av.count && g_pf_count < 32; i++) {
        void* obj = av.items[i];
        if (!obj || !ptr_ok(obj, g_pi.off_hp + 4)) continue;

        void* klass_ptr;
        __builtin_memcpy(&klass_ptr, obj, 8);
        if (klass_ptr != g_pi.klass) continue;

        uint8_t is_local;
        __builtin_memcpy(&is_local, (uint8_t*)obj + g_pi.off_is_local, 1);

        int32_t hp, team;
        __builtin_memcpy(&hp, (uint8_t*)obj + g_pi.off_hp, 4);
        __builtin_memcpy(&team, (uint8_t*)obj + g_pi.off_team, 4);

        if (hp <= 0) continue;

        void* transform = api.get_transform(obj);
        if (!transform || !ptr_ok(transform, 0x10)) continue;

        Vec3 pos{};
        api.get_position(transform, &pos);
        if (!isfinite(pos.x) || !isfinite(pos.y) || !isfinite(pos.z)) continue;
        if (fabsf(pos.x) > 50000.f || fabsf(pos.y) > 50000.f || fabsf(pos.z) > 50000.f) continue;

        if (is_local == 1) {
            g_local_team = team;
            g_local_pos = pos;
            found_local = true;
            continue;
        }

        PlayerFrame& pf = g_pf[g_pf_count];
        pf.object = obj;
        pf.pos = pos;
        pf.hp = hp;
        pf.team = team;
        pf.is_local = false;
        pf.nick[0] = 0;
        if (g_pi.off_nickname != 0) {
            void* str_ptr;
            if (safe_read((uint8_t*)obj + g_pi.off_nickname, str_ptr))
                read_il2cpp_string(str_ptr, pf.nick, sizeof(pf.nick));
        }
        g_pf_count++;
    }

    if (found_local) {
        for (int i = 0; i < g_pf_count; i++) {
            float dx = g_pf[i].pos.x - g_local_pos.x;
            float dy = g_pf[i].pos.y - g_local_pos.y;
            float dz = g_pf[i].pos.z - g_local_pos.z;
            g_pf[i].dist = sqrtf(dx*dx + dy*dy + dz*dz);
        }
    }
}

static void update_positions() {
    for (int i = 0; i < g_pf_count; i++) {
        PlayerFrame& pf = g_pf[i];
        if (!pf.object || !ptr_ok(pf.object, 0x10)) continue;

        void* klass_ptr;
        __builtin_memcpy(&klass_ptr, pf.object, 8);
        if (klass_ptr != g_pi.klass) { pf.hp = 0; continue; }

        __builtin_memcpy(&pf.hp, (uint8_t*)pf.object + g_pi.off_hp, 4);
        if (pf.hp <= 0) continue;

        void* transform = api.get_transform(pf.object);
        if (!transform) continue;

        Vec3 pos{};
        api.get_position(transform, &pos);
        if (isfinite(pos.x) && isfinite(pos.y) && isfinite(pos.z))
            pf.pos = pos;

        float dx = pf.pos.x - g_local_pos.x;
        float dy = pf.pos.y - g_local_pos.y;
        float dz = pf.pos.z - g_local_pos.z;
        pf.dist = sqrtf(dx*dx + dy*dy + dz*dz);
    }
}

// ===================================================================
// Drawing helpers
// ===================================================================
static void draw_text_outlined(ImDrawList* dl, ImFont* font, float sz,
                               const ImVec2& pos, ImU32 col, const char* text) {
    if (!font || !text || !*text) return;
    const ImU32 shadow = IM_COL32(0, 0, 0, (col >> 24) & 0xFF);
    for (int dx = -1; dx <= 1; dx++)
        for (int dy = -1; dy <= 1; dy++)
            if (dx || dy)
                dl->AddText(font, sz, ImVec2(pos.x + dx, pos.y + dy), shadow, text);
    dl->AddText(font, sz, pos, col, text);
}

static ImU32 iv4(const ImVec4& c, float a = 1.f) {
    return IM_COL32((int)(c.x*255), (int)(c.y*255), (int)(c.z*255), (int)(c.w*a*255));
}

static void draw_esp_box(ImDrawList* dl, const ImVec2& top, const ImVec2& bot, float alpha) {
    if (!cfg::esp::box) return;
    ImU32 col = iv4(cfg::esp::box_col, alpha);
    float w = (bot.x - top.x) * 0.5f;
    ImVec2 tl(top.x - w, top.y);
    ImVec2 br(top.x + w, bot.y);

    if (cfg::esp::box_type == 0) {
        dl->AddRect(tl, br, col, cfg::esp::box_rounding, 0, 1.5f);
    } else {
        float clen = (br.y - tl.y) * 0.25f;
        dl->AddLine({tl.x, tl.y}, {tl.x + clen, tl.y}, col, 1.5f);
        dl->AddLine({tl.x, tl.y}, {tl.x, tl.y + clen}, col, 1.5f);
        dl->AddLine({br.x, tl.y}, {br.x - clen, tl.y}, col, 1.5f);
        dl->AddLine({br.x, tl.y}, {br.x, tl.y + clen}, col, 1.5f);
        dl->AddLine({tl.x, br.y}, {tl.x + clen, br.y}, col, 1.5f);
        dl->AddLine({tl.x, br.y}, {tl.x, br.y - clen}, col, 1.5f);
        dl->AddLine({br.x, br.y}, {br.x - clen, br.y}, col, 1.5f);
        dl->AddLine({br.x, br.y}, {br.x, br.y - clen}, col, 1.5f);
    }
}

static void draw_healthbar(ImDrawList* dl, int hp, const ImVec2& top, const ImVec2& bot,
                            float alpha) {
    if (!cfg::esp::health) return;
    float w = (bot.x - top.x) * 0.5f;
    float box_h = bot.y - top.y;
    if (box_h < 10.f) return;

    float bar_w = 3.f;
    float x = top.x - w - bar_w - 2.f;
    float frac = (float)hp / 100.f;
    if (frac > 1.f) frac = 1.f;
    if (frac < 0.f) frac = 0.f;
    float bar_h = box_h * frac;

    dl->AddRectFilled({x, top.y}, {x + bar_w, top.y + box_h},
                      IM_COL32(0, 0, 0, (int)(180 * alpha)));
    dl->AddRectFilled({x, top.y + box_h - bar_h}, {x + bar_w, top.y + box_h},
                      iv4(cfg::esp::health_col, alpha));

    if (hp < 100 && espFont && box_h > 25.f) {
        char txt[8];
        snprintf(txt, sizeof(txt), "%d", hp);
        float fs = box_h * 0.12f;
        if (fs < 8.f) fs = 8.f;
        if (fs > 14.f) fs = 14.f;
        ImVec2 ts = espFont->CalcTextSizeA(fs, 1e9f, 0.f, txt);
        float tx = x + (bar_w - ts.x) * 0.5f;
        float ty = top.y + box_h - bar_h - ts.y - 2.f;
        draw_text_outlined(dl, espFont, fs, {tx, ty},
                           IM_COL32(255, 255, 255, (int)(255 * alpha)), txt);
    }
}

static void draw_nickname(ImDrawList* dl, const char* nick, float cx, float top_y,
                           float sz, float alpha) {
    if (!cfg::esp::name || !espFont || !nick || !*nick) return;
    ImVec2 ts = espFont->CalcTextSizeA(sz, 1e9f, 0.f, nick);
    draw_text_outlined(dl, espFont, sz, {cx - ts.x * 0.5f, top_y - ts.y - 2.f},
                       iv4(cfg::esp::name_col, alpha), nick);
}

static void draw_distance(ImDrawList* dl, float dist, float cx, float bot_y,
                            float sz, float alpha) {
    if (!cfg::esp::distance || !espFont) return;
    char txt[32];
    snprintf(txt, sizeof(txt), "%.0fm", dist);
    ImVec2 ts = espFont->CalcTextSizeA(sz, 1e9f, 0.f, txt);
    draw_text_outlined(dl, espFont, sz, {cx - ts.x * 0.5f, bot_y + 2.f},
                       iv4(cfg::esp::distance_col, alpha), txt);
}

// ===================================================================
// Main entry: visuals::draw()
// ===================================================================
enum Stage { ST_API, ST_ICALL, ST_DISCOVER, ST_ACTIVE };

static Stage g_stage = ST_API;
static int   g_retry_cd = 0;

void visuals::draw() {
    g_frame++;

    if (g_retry_cd > 0) { g_retry_cd--; return; }

    switch (g_stage) {
    case ST_API:
        if (!resolve_api()) { g_retry_cd = 60; return; }
        g_stage = ST_ICALL;
        // fall through
    case ST_ICALL:
        if (!resolve_icalls()) { g_retry_cd = 120; return; }
        g_stage = ST_DISCOVER;
        // fall through
    case ST_DISCOVER:
        if (!discover_player_class()) { g_retry_cd = 300; return; }
        g_stage = ST_ACTIVE;
        ESPLOG("ESP active");
        break;
    case ST_ACTIVE:
        break;
    }

    float sw = g_sw;
    float sh = g_sh;
    if (sw < 100.f || sh < 100.f) return;

    g_vp_ok = get_camera_vp();
    if (!g_vp_ok) return;

    // Full refresh every ~1.5s, position-only update every frame
    if (g_frame % 90 == 0 || g_pf_count == 0)
        refresh_players();
    else
        update_positions();

    if (g_pf_count == 0) return;

    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    if (!dl) return;

    for (int i = 0; i < g_pf_count; i++) {
        const PlayerFrame& pf = g_pf[i];
        if (pf.hp <= 0) continue;
        if (pf.team == g_local_team) continue;

        ImVec2 feet_s, head_s;
        if (!w2s(g_vp, pf.pos, sw, sh, feet_s)) continue;

        Vec3 head = { pf.pos.x, pf.pos.y + 1.7f, pf.pos.z };
        if (!w2s(g_vp, head, sw, sh, head_s)) continue;

        if (head_s.x < -200 || head_s.x > sw + 200) continue;
        if (head_s.y < -200 || head_s.y > sh + 200) continue;

        float box_h = feet_s.y - head_s.y;
        if (box_h < 5.f) continue;

        float alpha = 1.f;
        if (pf.dist > 150.f) alpha = 0.5f;
        if (pf.dist > 300.f) alpha = 0.3f;

        ImVec2 top_pt(head_s.x, head_s.y);
        ImVec2 bot_pt(feet_s.x, feet_s.y);

        float fsz = box_h * 0.14f;
        if (fsz < 9.f)  fsz = 9.f;
        if (fsz > 16.f) fsz = 16.f;

        draw_esp_box(dl, top_pt, bot_pt, alpha);
        draw_healthbar(dl, pf.hp, top_pt, bot_pt, alpha);
        draw_nickname(dl, pf.nick, top_pt.x, top_pt.y, fsz, alpha);
        draw_distance(dl, pf.dist, top_pt.x, bot_pt.y, fsz, alpha);
    }
}
