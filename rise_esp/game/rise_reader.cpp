#include "rise_reader.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <set>
#include <vector>

#include "rise_layout.hpp"

namespace rise {
namespace {

constexpr float kHeadHeight = 1.8f;                    // standing eye height above the root, assumption
constexpr float kMaxCameraDistance = 2.f;              // head point must be this close to the camera (m)
constexpr uint64_t kCamScanBytes = 0x1000;             // native Camera object: bytes searched for matrices
constexpr uint64_t kScanBytesPerFrame = 4ull << 20;    // PlayerManager search budget per frame
constexpr int kRescanFrames = 600;                     // idle frames before the next full search (~5 s)
constexpr float kMaxHealth = 1000.f;                   // larger health values are not believed

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

// State of the PlayerManager search that continues across frames.
struct ManagerScan {
    std::vector<std::pair<uint64_t, uint64_t>> regions;
    size_t region = 0;
    uint64_t addr = 0;
    bool active = false;
    int idle = 0;                // frames to wait before the next pass
};

// PlayerManager instance. The class pointer is read from the TypeInfo slot, then the writable
// memory is searched for an object header that points to it. The search walks at most
// kScanBytesPerFrame per call and continues on the next frame, so a full pass never stalls the
// overlay loop. A pass without a match is repeated after kRescanFrames. The result is cached
// until the object stops pointing to the class (match ended).
uint64_t findPlayerManager(const mem::Process& p, uint64_t gameBase) {
    static uint64_t cached = 0;
    static ManagerScan scan;
    static std::vector<uint8_t> buf(1u << 20);

    const uint64_t klass = p.read<uint64_t>(gameBase + kTypeInfoPlayerManager);
    if (!klass) return 0;
    if (cached) {
        if (p.read<uint64_t>(cached) == klass) return cached;
        cached = 0;
        scan = ManagerScan{};
    }
    if (!scan.active) {
        if (scan.idle > 0) {
            --scan.idle;
            return 0;
        }
        scan.regions = p.rwRegions();
        scan.region = 0;
        scan.addr = scan.regions.empty() ? 0 : scan.regions[0].first;
        scan.active = !scan.regions.empty();
        if (!scan.active) {
            scan.idle = kRescanFrames;
            return 0;
        }
    }

    uint64_t budget = kScanBytesPerFrame;
    while (budget > 0) {
        if (scan.region >= scan.regions.size()) {          // pass finished without a match
            scan.active = false;
            scan.idle = kRescanFrames;
            return 0;
        }
        const uint64_t end = scan.regions[scan.region].second;
        if (scan.addr >= end) {
            if (++scan.region < scan.regions.size()) scan.addr = scan.regions[scan.region].first;
            continue;
        }
        const size_t n = static_cast<size_t>(std::min<uint64_t>(buf.size(), end - scan.addr));
        const uint64_t at = scan.addr;
        scan.addr += n;
        budget -= std::min<uint64_t>(budget, n);
        if (!p.read(at, buf.data(), n)) continue;
        for (size_t off = 0; off + 8 <= n; off += 8) {
            uint64_t v;
            std::memcpy(&v, &buf[off], 8);
            if (v != klass) continue;
            const uint64_t obj = at + off;
            const uint64_t dict = p.read<uint64_t>(obj + kPM_Players);
            if (!plausiblePtr(dict)) continue;
            const int32_t count = p.read<int32_t>(dict + kDict_Count);
            if (count < 0 || count > 4096) continue;
            cached = obj;
            scan.active = false;
            return obj;
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
// view: rigid candidate that maps the local head close to the origin (|V * head| < kMaxCameraDistance).
// proj: candidate that passes isProjection(). Both are checked against the screen aspect.
// closest: distance of the rigid candidate nearest to the head, reported in the status line.
bool scanCamera(const mem::Process& p, uint64_t nativeCam, esp::Vec3 head, float aspect,
                esp::Mat4& view, esp::Mat4& proj, float& closest) {
    closest = 1e30f;
    if (!nativeCam) return false;
    std::vector<uint8_t> buf(kCamScanBytes);
    if (!p.read(nativeCam, buf.data(), buf.size())) return false;
    bool haveView = false, haveProj = false;
    float bestDist = kMaxCameraDistance;
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
            closest = std::min(closest, d);
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
        res.missing = "  PlayerManager instance not found yet (match not loaded, or still searching)\n";
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
    float closest = 0.f;
    const float aspect = static_cast<float>(screenW) / static_cast<float>(screenH);
    if (!scanCamera(p, nativeCam, head, aspect, view, proj, closest)) {
        char msg[160];
        std::snprintf(msg, sizeof(msg),
                      "  camera matrices not found (nearest rigid candidate %.2f m, needs < %.1f m)\n",
                      closest, kMaxCameraDistance);
        res.missing = msg;
        return res;
    }
    res.snapshot.viewProj = esp::mul(proj, view);
    res.snapshot.cameraPos = cameraPosition(view);
    res.snapshot.localTeam = p.read<uint8_t>(localPc + kPC_Team);

    const std::vector<uint64_t> pcs = dictValues(p, p.read<uint64_t>(manager + kPM_Players));
    std::set<int> teams;
    int sampleHp = -1;
    for (const uint64_t pc : pcs) {
        esp::Player pl;
        pl.id = pc;
        pl.local = pc == localPc;
        pl.team = p.read<uint8_t>(pc + kPC_Team);
        esp::Vec3 feet;
        if (!playerPosition(p, pc, feet)) continue;
        pl.feet = feet;
        pl.head = esp::Vec3{feet.x, feet.y + kHeadHeight, feet.z};
        // Health candidate (+0x5C, float, unverified). An implausible value only hides the health
        // bar; the player stays on screen, so a wrong offset cannot empty the whole overlay.
        const float hpRaw = p.read<float>(pc + kPC_HealthCandidate);
        pl.hpKnown = std::isfinite(hpRaw) && hpRaw > 0.f && hpRaw <= kMaxHealth;
        pl.hp = pl.hpKnown ? static_cast<int>(hpRaw) : 0;
        pl.maxHp = kPcMaxHealthDefault;
        pl.alive = true;             // death is not read yet
        if (!pl.local) {
            teams.insert(pl.team);
            if (sampleHp < 0 && pl.hpKnown) sampleHp = pl.hp;
        }
        res.snapshot.players.push_back(pl);
    }

    // If no other player has a different team value, the team field does not split enemies
    // from teammates (or this is a free-for-all): draw everybody instead of an empty overlay.
    bool otherTeam = false;
    for (const auto& pl : res.snapshot.players) {
        if (!pl.local && pl.team != res.snapshot.localTeam) otherTeam = true;
    }
    res.snapshot.teamFilter = otherTeam;

    std::string teamList;
    for (const int t : teams) {
        if (!teamList.empty()) teamList += ",";
        teamList += std::to_string(t);
    }
    char line[200];
    std::snprintf(line, sizeof(line),
                  "players %zu  local team %d  other teams [%s]  hp sample %d  team filter %s",
                  res.snapshot.players.size(), int(res.snapshot.localTeam), teamList.c_str(), sampleHp,
                  otherTeam ? "on" : "off");
    res.info = line;
    res.ready = true;
    return res;
}

}  // namespace rise
