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
constexpr float kNearCameraDistance = 2.f;             // view candidate this close to the head: normal
constexpr float kFarCameraDistance = 8.f;              // up to this: accepted and flagged "far"
constexpr size_t kCamScanBytes = 0x4000;               // native Camera object: bytes searched for matrices
constexpr uint64_t kScanBytesPerFrame = 4ull << 20;    // PlayerManager search budget per frame
constexpr int kRescanFrames = 600;                     // idle frames before the next full search (~5 s)
constexpr float kMaxHealth = 1000.f;                   // larger health values are not believed
constexpr size_t kRemoteDumps = 2;                     // remote player objects dumped per read

ScanStats g_scan;   // PlayerManager search statistics, see managerScanStats()

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
    const auto publish = [&]() {
        g_scan.active = scan.active;
        g_scan.idle = scan.idle;
        g_scan.region = scan.region;
        g_scan.regions = scan.regions.size();
        g_scan.addr = scan.addr;
    };

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
            publish();
            return 0;
        }
        scan.regions = p.rwRegions();
        scan.region = 0;
        scan.addr = scan.regions.empty() ? 0 : scan.regions[0].first;
        scan.active = !scan.regions.empty();
        if (!scan.active) {
            scan.idle = kRescanFrames;
            publish();
            return 0;
        }
        ++g_scan.passes;
    }

    uint64_t budget = kScanBytesPerFrame;
    while (budget > 0) {
        if (scan.region >= scan.regions.size()) {   // pass finished without a match
            scan.active = false;
            scan.idle = kRescanFrames;
            publish();
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
        g_scan.bytesTotal += n;
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
            ++g_scan.hits;
            publish();
            return obj;
        }
    }
    publish();
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

// Rigid transform: last row (0,0,0,1), orthonormal rotation rows. Unity's view matrix may
// contain a Z flip, so the determinant is not checked.
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

// Perspective projection, structure only: P[11] = +-1, P[15] = 0, positive x and y scale.
bool isPerspective(const esp::Mat4& p) {
    if (std::fabs(std::fabs(p.at(3, 2)) - 1.f) > 1e-3f) return false;
    if (std::fabs(p.at(3, 3)) > 1e-3f || std::fabs(p.at(3, 0)) > 1e-3f || std::fabs(p.at(3, 1)) > 1e-3f) return false;
    return p.at(0, 0) > 0.f && p.at(1, 1) > 0.f;
}

// Aspect of a perspective projection, P[5] / P[0] (width / height of the rendered image).
float aspectOf(const esp::Mat4& p) { return p.at(1, 1) / p.at(0, 0); }

// What the native Camera object holds: the view (world -> camera) and the projection.
struct CamScan {
    esp::Mat4 view{};
    esp::Mat4 proj{};
    bool haveView = false;
    bool haveProj = false;
    bool projAspectMatch = false;  // proj has the screen aspect; otherwise the first perspective matrix
    float viewDist = 0.f;          // distance of the view candidate from the head point
    float closest = 0.f;           // nearest rigid candidate (0 when there is none)
    int rigid = 0;                 // rigid candidates in the scanned window
    int perspective = 0;           // perspective candidates in the scanned window
};

// Scans the first kCamScanBytes of the native Camera object.
// view: rigid candidate nearest to the head point, accepted up to kFarCameraDistance.
// proj: perspective candidate with the screen aspect, else the first perspective candidate.
CamScan scanCamera(const mem::Process& p, uint64_t nativeCam, esp::Vec3 head, float aspect) {
    CamScan s;
    if (!nativeCam) return s;
    std::vector<uint8_t> buf(kCamScanBytes);
    if (!p.read(nativeCam, buf.data(), buf.size())) return s;

    esp::Mat4 firstPerspective{};
    bool haveFirstPerspective = false;
    float bestDist = kFarCameraDistance;
    float nearest = 1e30f;
    for (size_t off = 0; off + 64 <= buf.size(); off += 4) {
        esp::Mat4 m;
        std::memcpy(m.m, &buf[off], 64);
        if (isPerspective(m)) {
            ++s.perspective;
            if (!haveFirstPerspective) {
                firstPerspective = m;
                haveFirstPerspective = true;
            }
            if (!s.projAspectMatch && std::fabs(aspectOf(m) - aspect) < 0.05f * aspect) {
                s.proj = m;
                s.haveProj = true;
                s.projAspectMatch = true;
            }
        }
        if (isRigid(m)) {
            ++s.rigid;
            const esp::Vec4 v = esp::mul(m, esp::Vec4{head.x, head.y, head.z, 1.f});
            const float d = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
            nearest = std::min(nearest, d);
            if (d < bestDist) {
                bestDist = d;
                s.view = m;
                s.haveView = true;
            }
        }
    }
    if (s.rigid > 0) s.closest = nearest;
    s.viewDist = bestDist;
    if (!s.projAspectMatch && haveFirstPerspective) {
        s.proj = firstPerspective;
        s.haveProj = true;
    }
    return s;
}

}  // namespace

ScanStats managerScanStats() { return g_scan; }

ReadResult readFrame(const mem::Process& p, uint64_t gameBase, int screenW, int screenH) {
    ReadResult res;
    res.snapshot.screenW = static_cast<float>(screenW);
    res.snapshot.screenH = static_cast<float>(screenH);
    Diag& d = res.diag;
    if (!gameBase) {
        res.missing = "  libil2cpp.so is not mapped in the process\n";
        return res;
    }

    d.manager = findPlayerManager(p, gameBase);
    if (!d.manager) {
        res.missing = "  PlayerManager instance not found yet (match not loaded, or still searching)\n";
        return res;
    }
    d.dumps.push_back({"manager", d.manager, 0x200});

    d.localPc = p.read<uint64_t>(d.manager + kPM_LocalPlayer);
    if (!d.localPc) {
        res.missing = "  local player not set yet\n";
        return res;
    }
    d.dumps.push_back({"local_pc", d.localPc, 0x400});

    d.movement = p.read<uint64_t>(d.localPc + kPC_Movement);
    d.translation = d.movement ? p.read<uint64_t>(d.movement + kMC_TranslationData) : 0;
    if (d.movement) d.dumps.push_back({"movement", d.movement, 0x200});
    if (d.translation) d.dumps.push_back({"translation", d.translation, 0x100});

    esp::Vec3 localFeet;
    if (!playerPosition(p, d.localPc, localFeet)) {
        res.missing = "  local player position not readable yet\n";
        return res;
    }
    d.localFeet = localFeet;
    d.haveLocalFeet = true;

    d.mainCam = p.read<uint64_t>(d.localPc + kPC_MainCamera);
    if (d.mainCam) d.dumps.push_back({"player_main_camera", d.mainCam, 0x100});
    d.camera = d.mainCam ? p.read<uint64_t>(d.mainCam + kPMC_Camera) : 0;
    if (d.camera) d.dumps.push_back({"camera_managed", d.camera, 0x100});
    d.nativeCam = d.camera ? p.read<uint64_t>(d.camera + kUnityCachedPtr) : 0;
    if (d.nativeCam) d.dumps.push_back({"native_cam", d.nativeCam, kCamScanBytes});

    const esp::Vec3 head{localFeet.x, localFeet.y + kHeadHeight, localFeet.z};
    const float aspect = static_cast<float>(screenW) / static_cast<float>(screenH);
    const CamScan cam = scanCamera(p, d.nativeCam, head, aspect);
    d.perspective = cam.perspective;
    d.rigid = cam.rigid;
    d.nearestRigid = cam.closest;
    d.viewDist = cam.viewDist;
    d.viewFar = cam.haveView && cam.viewDist > kNearCameraDistance;
    d.projAspectMatch = cam.projAspectMatch;
    d.projAspect = cam.haveProj ? aspectOf(cam.proj) : 0.f;
    d.view = cam.view;
    d.proj = cam.proj;
    if (!cam.haveView || !cam.haveProj) {
        char msg[256];
        std::snprintf(msg, sizeof(msg),
                      "  camera matrices not found: native 0x%llx, perspective %d, rigid %d, nearest rigid %.2f m\n",
                      static_cast<unsigned long long>(d.nativeCam), cam.perspective, cam.rigid, cam.closest);
        res.missing = msg;
        return res;
    }
    d.haveMatrices = true;
    res.snapshot.viewProj = esp::mul(cam.proj, cam.view);
    res.snapshot.cameraPos = cameraPosition(cam.view);
    res.snapshot.localTeam = p.read<uint8_t>(d.localPc + kPC_Team);

    const std::vector<uint64_t> pcs = dictValues(p, p.read<uint64_t>(d.manager + kPM_Players));
    static const char* const kRemoteNames[kRemoteDumps] = {"remote_pc0", "remote_pc1"};
    std::set<int> teams;
    int sampleHp = -1;
    size_t remoteDumps = 0;
    for (const uint64_t pc : pcs) {
        esp::Player pl;
        pl.id = pc;
        pl.local = pc == d.localPc;
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
            d.otherTeams.push_back(pl.team);
            d.otherHp.push_back(hpRaw);
            if (remoteDumps < kRemoteDumps) {
                d.dumps.push_back({kRemoteNames[remoteDumps], pc, 0x400});
                ++remoteDumps;
            }
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
    char line[256];
    std::snprintf(line, sizeof(line),
                  "players %zu  local team %d  other teams [%s]  hp sample %d  team filter %s  "
                  "view %.1f m%s  aspect %.2f (screen %.2f)",
                  res.snapshot.players.size(), int(res.snapshot.localTeam), teamList.c_str(), sampleHp,
                  otherTeam ? "on" : "off", cam.viewDist, cam.viewDist > kNearCameraDistance ? " far" : "",
                  aspectOf(cam.proj), aspect);
    res.info = line;
    res.ready = true;
    return res;
}

}  // namespace rise
