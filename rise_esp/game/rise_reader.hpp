// Builds an esp::Snapshot for rise 1.0.0F5 from the game's memory (read-only).
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "esp/esp.hpp"
#include "mem/memory.hpp"

namespace rise {

// A block of game memory the platform layer may dump for offline analysis.
struct DumpBlock {
    const char* name;
    uint64_t addr;
    size_t size;
};

// Values seen during one read, for the log. Valid as far as the read got.
struct Diag {
    uint64_t pmKlass = 0;          // PlayerManager class pointer (TypeInfo slot)
    uint64_t pcKlass = 0;          // PlayerController class pointer (TypeInfo slot)
    std::string pmName;            // class name read from the class structure, to confirm the pointer
    std::string pcName;
    uint64_t manager = 0;          // PlayerManager instance (validated)
    uint64_t localPc = 0;          // local PlayerController
    uint64_t localOffset = 0;      // manager field that holds the local player (0x68 or 0x70)
    size_t skippedEntries = 0;     // Players entries that are not PlayerController objects
    uint64_t movement = 0;         // MovementController of the local player
    uint64_t translation = 0;      // its translationData
    uint64_t mainCam = 0;          // PlayerMainCamera
    uint64_t camera = 0;           // managed Camera
    uint64_t nativeCam = 0;        // native Camera (m_CachedPtr)
    esp::Vec3 localFeet{};
    bool haveLocalFeet = false;
    int perspective = 0;           // perspective matrices in the scanned native camera window
    int rigid = 0;                 // rigid matrices in the window
    float nearestRigid = 0.f;      // nearest rigid matrix to the head point, m (0 when none)
    float viewDist = 0.f;          // distance of the chosen view to the head point, m
    bool viewFar = false;          // view accepted beyond the normal distance
    bool projAspectMatch = false;  // projection has the screen aspect
    float projAspect = 0.f;        // aspect of the chosen projection
    esp::Mat4 view{};              // chosen view (world -> camera)
    esp::Mat4 proj{};              // chosen projection
    bool haveMatrices = false;
    std::vector<int> otherTeams;   // team byte of every other player
    std::vector<float> otherHp;    // raw health candidate of every other player
    std::vector<DumpBlock> dumps;  // memory worth dumping for this read
};

struct ReadResult {
    bool ready = false;          // false until the camera and the players can be read
    std::string missing;         // what is still missing (empty when ready)
    std::string info;            // one line when ready: player count, teams, health sample
    esp::Snapshot snapshot;      // valid when ready
    Diag diag;                   // values for the log
};

// State of the PlayerManager search, for the log.
struct ScanStats {
    bool active = false;         // a pass is running
    int idle = 0;                // frames until the next pass
    size_t region = 0;           // current memory region of the pass
    size_t regions = 0;          // regions in the pass
    uint64_t addr = 0;           // next address to read
    uint64_t bytesTotal = 0;     // bytes read by the search since start
    uint64_t passes = 0;         // passes started
    uint64_t hits = 0;           // PlayerManager instances accepted
    uint64_t headerHits = 0;     // words equal to the PlayerManager class pointer
    uint64_t rejMonitor = 0;     // rejected: monitor word is not a heap pointer
    uint64_t rejFlag = 0;        // rejected: bool field at 0x60 is not 0 or 1
    uint64_t rejPlayers = 0;     // rejected: Players dictionary is not plausible
    uint64_t rejLocal = 0;       // rejected: no local player that is a PlayerController
};
ScanStats managerScanStats();

// Build id of a module (libil2cpp.so) read from its ELF notes in the process, as hex.
std::string libBuildId(const mem::Process& p, uint64_t base);

// gameBase: base address of the il2cpp module (libil2cpp.so) in the target process.
ReadResult readFrame(const mem::Process& p, uint64_t gameBase, int screenW, int screenH);

}  // namespace rise
