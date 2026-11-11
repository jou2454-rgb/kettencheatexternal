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
constexpr uint64_t kPM_LocalPlayerAlt = 0x70;          // second PlayerController field of PlayerManager

ScanStats g_scan;   // PlayerManager search statistics, see managerScanStats()

// Pointers can carry tag bits in the top byte on this device: only the 48 address bits are used.
uint64_t untag(uint64_t v) { return v & 0x0000FFFFFFFFFFFFULL; }
// Heap range of the game on this device (heap addresses seen so far: 0x7b..., 0x7e...).
bool heapLike(uint64_t v) { return v >= 0x7000000000ULL && v < 0x8000000000ULL; }
uint64_t readPtr(const mem::Process& p, uint64_t addr) { return untag(p.read<uint64_t>(addr)); }

// Name of a class from its class structure (Il2CppClass::name at +0x10), or why it is unknown.
std::string className(const mem::Process& p, uint64_t klass) {
    if (!klass) return "(no class pointer)";
    const uint64_t name = readPtr(p, klass + 0x10);
    if (name < 0x1000) return "(bad name pointer)";
    char buf[32] = {0};
    if (!p.read(name, buf, sizeof(buf))) return "(name unreadable)";
    buf[sizeof(buf) - 1] = 0;
    std::string s;
    for (size_t i = 0; buf[i]; ++i) {
        const unsigned char c = static_cast<unsigned char>(buf[i]);
        if (c < 32 || c > 126) return "(not text)";
        s.push_back(buf[i]);
    }
    return s;
}

// Values of Dictionary<int, PlayerController> that are PlayerController objects (live entries).
// Entries whose object has another class are counted in skipped.
std::vector<uint64_t> dictValues(const mem::Process& p, uint64_t dict, uint64_t pcKlass, size_t& skipped) {
    std::vector<uint64_t> out;
    const uint64_t entries = readPtr(p, dict + kDict_Entries);
    const int32_t count = p.read<int32_t>(dict + kDict_Count);
    if (!heapLike(entries) || count <= 0 || count > 4096) return out;
    for (int32_t i = 0; i < count; ++i) {
        const uint64_t e = entries + kArrayVector + static_cast<uint64_t>(i) * kEntry_Size;
        if (p.read<int32_t>(e + kEntry_Next) < -1) continue;     // free slot
        const uint64_t value = readPtr(p, e + kEntry_Value);
        if (!value) continue;
        if (readPtr(p, value) != pcKlass) {
            ++skipped;
            continue;
        }
        out.push_back(value);
    }
    return out;
}

// State of the PlayerManager search that continues across frames.
struct ManagerScan {
    std::vector<std::pair<uint64_t, uint64_t>> regions;
    size_t region = 0;
    uint64_t addr = 0;
    bool active = false;
    int idle = 0;                // frames to wait before the next pass
};

// Result of checking a PlayerManager candidate. The layout is from dump.cs: monitor word (+0x8),
// a bool at +0x60, the Players dictionary at +0x20 and the local player at +0x68 or +0x70. The
// local player is accepted only when its object is a PlayerController, which rules out a match
// on the class pointer alone.
enum class Check { Ok, Monitor, Flag, Players, Local };

Check checkManager(const mem::Process& p, uint64_t obj, uint64_t pcKlass, uint64_t& localPc,
                   uint64_t& localOffset) {
    const uint64_t monitor = readPtr(p, obj + 8);
    if (monitor && !heapLike(monitor)) return Check::Monitor;
    if (p.read<uint8_t>(obj + 0x60) > 1) return Check::Flag;
    const uint64_t dict = readPtr(p, obj + kPM_Players);
    if (!heapLike(dict)) return Check::Players;
    const int32_t count = p.read<int32_t>(dict + kDict_Count);
    if (count < 0 || count > 4096) return Check::Players;
    for (const uint64_t off : {kPM_LocalPlayer, kPM_LocalPlayerAlt}) {
        const uint64_t pc = readPtr(p, obj + off);
        if (heapLike(pc) && readPtr(p, pc) == pcKlass) {
            localPc = pc;
            localOffset = off;
            return Check::Ok;
        }
    }
    return Check::Local;
}

void countCheck(Check c) {
    switch (c) {
        case Check::Monitor: ++g_scan.rejMonitor; break;
        case Check::Flag: ++g_scan.rejFlag; break;
        case Check::Players: ++g_scan.rejPlayers; break;
        case Check::Local: ++g_scan.rejLocal; break;
        case Check::Ok: break;
    }
}

// PlayerManager instance. Writable memory is searched for words equal to the PlayerManager class
// pointer (tag bits removed); each hit is checked by checkManager(). The search walks at most
// kScanBytesPerFrame per call and continues on the next frame, so a full pass never stalls the
// overlay loop. A pass without an accepted candidate is repeated after kRescanFrames. The accepted
// object is re-checked every frame and dropped when it no longer validates (match ended).
uint64_t findPlayerManager(const mem::Process& p, uint64_t pmKlass, uint64_t pcKlass, uint64_t& localPc,
                           uint64_t& localOffset) {
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

    if (cached) {
        if (checkManager(p, cached, pcKlass, localPc, localOffset) == Check::Ok) return cached;
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
        if (scan.region >= scan.regions.size()) {   // pass finished without an accepted candidate
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
            if (untag(v) != pmKlass) continue;
            ++g_scan.headerHits;
            const uint64_t obj = at + off;
            const Check c = checkManager(p, obj, pcKlass, localPc, localOffset);
            if (c == Check::Ok) {
                cached = obj;
                scan.active = false;
                ++g_scan.hits;
                publish();
                return obj;
            }
            countCheck(c);
        }
    }
    publish();
    return 0;
}

// World position of a player root: PlayerController -> MovementController -> translationData.
bool playerPosition(const mem::Process& p, uint64_t pc, esp::Vec3& out) {
    const uint64_t movement = readPtr(p, pc + kPC_Movement);
    const uint64_t translation = movement ? readPtr(p, movement + kMC_TranslationData) : 0;
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

std::string libBuildId(const mem::Process& p, uint64_t base) {
    if (!base) return "(module not mapped)";
    uint8_t eh[64];
    if (!p.read(base, eh, sizeof(eh)) || std::memcmp(eh, "\x7f" "ELF", 4) != 0) return "(no ELF header)";
    uint64_t phoff = 0;
    uint16_t phentsize = 0, phnum = 0;
    std::memcpy(&phoff, eh + 0x20, 8);
    std::memcpy(&phentsize, eh + 0x36, 2);
    std::memcpy(&phnum, eh + 0x38, 2);
    for (uint16_t i = 0; i < phnum && i < 64; ++i) {
        uint8_t ph[56];
        if (phentsize < 56 || !p.read(base + phoff + static_cast<uint64_t>(i) * phentsize, ph, sizeof(ph))) break;
        uint32_t type = 0;
        std::memcpy(&type, ph, 4);
        if (type != 4) continue;                      // PT_NOTE
        uint64_t vaddr = 0, filesz = 0;
        std::memcpy(&vaddr, ph + 16, 8);
        std::memcpy(&filesz, ph + 32, 8);
        uint8_t notes[256] = {0};
        const size_t n = static_cast<size_t>(std::min<uint64_t>(filesz, sizeof(notes)));
        if (!p.read(base + vaddr, notes, n)) break;
        size_t at = 0;
        while (at + 12 <= n) {
            uint32_t namesz = 0, descsz = 0, ntype = 0;
            std::memcpy(&namesz, notes + at, 4);
            std::memcpy(&descsz, notes + at + 4, 4);
            std::memcpy(&ntype, notes + at + 8, 4);
            const size_t nameAt = at + 12;
            const size_t descAt = nameAt + ((namesz + 3u) & ~3u);
            if (descAt + descsz > n) break;
            if (ntype == 3 && namesz == 4 && std::memcmp(notes + nameAt, "GNU", 4) == 0) {   // NT_GNU_BUILD_ID
                std::string hex;
                char b[3];
                for (uint32_t k = 0; k < descsz; ++k) {
                    std::snprintf(b, sizeof(b), "%02x", notes[descAt + k]);
                    hex += b;
                }
                return hex;
            }
            at = descAt + ((descsz + 3u) & ~3u);
        }
    }
    return "(no build id)";
}

ReadResult readFrame(const mem::Process& p, uint64_t gameBase, int screenW, int screenH) {
    ReadResult res;
    res.snapshot.screenW = static_cast<float>(screenW);
    res.snapshot.screenH = static_cast<float>(screenH);
    Diag& d = res.diag;
    if (!gameBase) {
        res.missing = "  libil2cpp.so is not mapped in the process\n";
        return res;
    }

    d.pmKlass = readPtr(p, gameBase + kTypeInfoPlayerManager);
    d.pcKlass = readPtr(p, gameBase + kTypeInfoPlayerController);
    d.pmName = className(p, d.pmKlass);
    d.pcName = className(p, d.pcKlass);
    if (!d.pmKlass || !d.pcKlass) {
        res.missing = "  class pointers of PlayerManager / PlayerController are not set\n";
        return res;
    }

    uint64_t localPc = 0;
    uint64_t localOffset = 0;
    d.manager = findPlayerManager(p, d.pmKlass, d.pcKlass, localPc, localOffset);
    if (!d.manager) {
        const ScanStats& s = g_scan;
        char msg[320];
        std::snprintf(msg, sizeof(msg),
                      "  PlayerManager instance not found yet: header matches %llu, rejected: monitor %llu, "
                      "flag %llu, players %llu, no local player %llu\n",
                      static_cast<unsigned long long>(s.headerHits), static_cast<unsigned long long>(s.rejMonitor),
                      static_cast<unsigned long long>(s.rejFlag), static_cast<unsigned long long>(s.rejPlayers),
                      static_cast<unsigned long long>(s.rejLocal));
        res.missing = msg;
        return res;
    }
    d.localPc = localPc;
    d.localOffset = localOffset;
    d.dumps.push_back({"manager", d.manager, 0x200});
    d.dumps.push_back({"local_pc", d.localPc, 0x400});

    d.movement = readPtr(p, d.localPc + kPC_Movement);
    d.translation = d.movement ? readPtr(p, d.movement + kMC_TranslationData) : 0;
    if (d.movement) d.dumps.push_back({"movement", d.movement, 0x200});
    if (d.translation) d.dumps.push_back({"translation", d.translation, 0x100});

    esp::Vec3 localFeet;
    if (!playerPosition(p, d.localPc, localFeet)) {
        res.missing = "  local player position not readable yet\n";
        return res;
    }
    d.localFeet = localFeet;
    d.haveLocalFeet = true;

    d.mainCam = readPtr(p, d.localPc + kPC_MainCamera);
    if (d.mainCam) d.dumps.push_back({"player_main_camera", d.mainCam, 0x100});
    d.camera = d.mainCam ? readPtr(p, d.mainCam + kPMC_Camera) : 0;
    if (d.camera) d.dumps.push_back({"camera_managed", d.camera, 0x100});
    d.nativeCam = d.camera ? readPtr(p, d.camera + kUnityCachedPtr) : 0;
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

    size_t skipped = 0;
    const std::vector<uint64_t> pcs =
        dictValues(p, readPtr(p, d.manager + kPM_Players), d.pcKlass, skipped);
    d.skippedEntries = skipped;
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
