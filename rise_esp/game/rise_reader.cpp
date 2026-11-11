#include "rise_reader.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "rise_layout.hpp"

namespace rise {
namespace {

constexpr float kHeadHeight = 1.8f;             // standing eye height above the root, assumption
constexpr uint64_t kCamScanBytes = 0x1000;      // native Camera object: bytes searched for matrices

// Values of Dictionary<int, PlayerController> (live entries only).
std::vector<uint64_t> dictValues(const mem::Process& p, uint64_t dict) {
    std::vector<uint64_t> out;
    const uint64_t entries = p.read<uint64_t>(dict + kDict_Entries);
    const int32_t count = p.read<int32_t>(dict + kDict_Count);
    if (!entries || count <= 0 || count > 4096) return out;
    for (int32_t i = 0; i < count; ++i) {
        const uint64_t e = entries + kArrayVector + static_cast<uint64_t>(i) * kEntry_Size;
        if (p.read<int32_t>(e + kEntry_Next) < -1) continue;     // free slot
        const uint64_t value = p.read<uint64_t>(e + kEntry_Value);
        if (value) out.push_back(value);
    }
    return out;
}

bool plausiblePtr(uint64_t v) { return v >= 0x1000 && v < (1ULL << 47); }

// PlayerManager instance. The class pointer is read from the TypeInfo slot, then the
// writable memory is searched for an object header that points to it. Cached per process.
uint64_t findPlayerManager(const mem::Process& p, uint64_t gameBase) {
    static uint64_t cached = 0;
    const uint64_t klass = p.read<uint64_t>(gameBase + kTypeInfoPlayerManager);
    if (!klass) return 0;
    if (cached && p.read<uint64_t>(cached) == klass) return cached;
    cached = 0;
    std::vector<uint8_t> buf(1u << 20);
    for (const auto& region : p.rwRegions()) {
        for (uint64_t a = region.first; a < region.second; a += buf.size()) {
            const size_t n = static_cast<size_t>(std::min<uint64_t>(buf.size(), region.second - a));
            if (!p.read(a, buf.data(), n)) continue;
            for (size_t off = 0; off + 8 <= n; off += 8) {
                uint64_t v;
                std::memcpy(&v, &buf[off], 8);
                if (v != klass) continue;
                const uint64_t obj = a + off;
                const uint64_t dict = p.read<uint64_t>(obj + kPM_Players);
                if (!plausiblePtr(dict)) continue;
                const int32_t count = p.read<int32_t>(dict + kDict_Count);
                if (count < 0 || count > 4096) continue;
                cached = obj;
                return obj;
            }
        }
    }
    return 0;
}

// World position of a player root: PlayerController -> MovementController -> translationData.
bool playerPosition(const mem::Process& p, uint64_t pc, esp::Vec3& out) {
    const uint64_t movement = p.read<uint64_t>(pc + kPC_Movement);
    const uint64_t translation = movement ? p.read<uint64_t>(movement + kMC_TranslationData) : 0;
    return translation && p.read(translation + kTD_Position, &out, sizeof(out));
}

// Camera position from a world-to-camera matrix: -R^T * t.
esp::Vec3 cameraPosition(const esp::Mat4& v) {
    return {
        -(v.at(0, 0) * v.at(0, 3) + v.at(1, 0) * v.at(1, 3) + v.at(2, 0) * v.at(2, 3)),
        -(v.at(0, 1) * v.at(0, 3) + v.at(1, 1) * v.at(1, 3) + v.at(2, 1) * v.at(2, 3)),
        -(v.at(0, 2) * v.at(0, 3) + v.at(1, 2) * v.at(1, 3) + v.at(2, 2) * v.at(2, 3)),
    };
}

// Rigid transform: last row (0,0,0,1), orthonormal rotation rows.
bool isRigid(const esp::Mat4& m) {
    if (std::fabs(m.at(3, 0)) > 1e-3f || std::fabs(m.at(3, 1)) > 1e-3f ||
        std::fabs(m.at(3, 2)) > 1e-3f || std::fabs(m.at(3, 3) - 1.f) > 1e-3f) return false;
    for (int r = 0; r < 3; ++r) {
        const float l = m.at(r, 0) * m.at(r, 0) + m.at(r, 1) * m.at(r, 1) + m.at(r, 2) * m.at(r, 2);
        if (std::fabs(l - 1.f) > 1e-2f) return false;
    }
    for (int a = 0; a < 3; ++a)
        for (int b = a + 1; b < 3; ++b) {
            const float d = m.at(a, 0) * m.at(b, 0) + m.at(a, 1) * m.at(b, 1) + m.at(a, 2) * m.at(b, 2);
            if (std::fabs(d) > 1e-2f) return false;
        }
    return true;
}

// Perspective projection with the expected aspect ratio (P[5] / P[0] == aspect).
bool isProjection(const esp::Mat4& p, float aspect) {
    if (std::fabs(std::fabs(p.at(3, 2)) - 1.f) > 1e-3f) return false;
    if (std::fabs(p.at(3, 3)) > 1e-3f || std::fabs(p.at(3, 0)) > 1e-3f || std::fabs(p.at(3, 1)) > 1e-3f) return false;
    if (p.at(0, 0) <= 0.f || p.at(1, 1) <= 0.f) return false;
    return std::fabs(p.at(1, 1) / p.at(0, 0) - aspect) < 0.05f * aspect;
}

// Finds the view and projection matrices inside the native Camera object.
// view: rigid candidate that maps the local head close to the origin (|V * head| < 2 m).
// proj: candidate that passes isProjection(). Both are checked against the screen aspect.
bool scanCamera(const mem::Process& p, uint64_t nativeCam, esp::Vec3 head, float aspect,
                esp::Mat4& view, esp::Mat4& proj) {
    if (!nativeCam) return false;
    std::vector<uint8_t> buf(kCamScanBytes);
    if (!p.read(nativeCam, buf.data(), buf.size())) return false;
    bool haveView = false, haveProj = false;
    float bestDist = 2.f;
    for (size_t off = 0; off + 64 <= buf.size(); off += 4) {
        esp::Mat4 m;
        std::memcpy(m.m, &buf[off], 64);
        if (!haveProj && isProjection(m, aspect)) {
            proj = m;
            haveProj = true;
        }
        if (isRigid(m)) {
            const esp::Vec4 v = esp::mul(m, esp::Vec4{head.x, head.y, head.z, 1.f});
            const float d = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
            if (d < bestDist) {
                bestDist = d;
                view = m;
                haveView = true;
            }
        }
    }
    return haveView && haveProj;
}

}  // namespace

ReadResult readFrame(const mem::Process& p, uint64_t gameBase, int screenW, int screenH) {
    ReadResult res;
    res.snapshot.screenW = static_cast<float>(screenW);
    res.snapshot.screenH = static_cast<float>(screenH);
    if (!gameBase) {
        res.missing = "  libil2cpp.so is not mapped in the process\n";
        return res;
    }

    const uint64_t manager = findPlayerManager(p, gameBase);
    if (!manager) {
        res.missing = "  PlayerManager instance not found (match not loaded?)\n";
        return res;
    }
    const uint64_t localPc = p.read<uint64_t>(manager + kPM_LocalPlayer);
    if (!localPc) {
        res.missing = "  local player not set yet\n";
        return res;
    }
    esp::Vec3 localFeet;
    if (!playerPosition(p, localPc, localFeet)) {
        res.missing = "  local player position not readable yet\n";
        return res;
    }

    const uint64_t mainCam = p.read<uint64_t>(localPc + kPC_MainCamera);
    const uint64_t cam = mainCam ? p.read<uint64_t>(mainCam + kPMC_Camera) : 0;
    const uint64_t nativeCam = cam ? p.read<uint64_t>(cam + kUnityCachedPtr) : 0;
    const esp::Vec3 head{localFeet.x, localFeet.y + kHeadHeight, localFeet.z};
    esp::Mat4 view, proj;
    const float aspect = static_cast<float>(screenW) / static_cast<float>(screenH);
    if (!scanCamera(p, nativeCam, head, aspect, view, proj)) {
        res.missing = "  camera matrices not found in the native camera\n";
        return res;
    }
    res.snapshot.viewProj = esp::mul(proj, view);
    res.snapshot.cameraPos = cameraPosition(view);
    res.snapshot.localTeam = p.read<uint8_t>(localPc + kPC_Team);

    const std::vector<uint64_t> pcs = dictValues(p, p.read<uint64_t>(manager + kPM_Players));
    for (const uint64_t pc : pcs) {
        esp::Player pl;
        pl.id = pc;
        pl.local = pc == localPc;
        pl.team = p.read<uint8_t>(pc + kPC_Team);
        esp::Vec3 feet;
        if (!playerPosition(p, pc, feet)) continue;
        pl.feet = feet;
        pl.head = esp::Vec3{feet.x, feet.y + kHeadHeight, feet.z};
        pl.hp = static_cast<int>(p.read<float>(pc + kPC_HealthCandidate));
        pl.maxHp = kPcMaxHealthDefault;
        pl.alive = pl.hp > 0;
        res.snapshot.players.push_back(pl);
    }

    res.ready = true;
    return res;
}

}  // namespace rise
