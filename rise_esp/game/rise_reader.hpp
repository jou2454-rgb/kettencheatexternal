// Builds an esp::Snapshot for rise 1.0.0F5 from the game's memory (read-only).
#pragma once
#include <string>

#include "esp/esp.hpp"
#include "mem/memory.hpp"

namespace rise {

struct ReadResult {
    bool ready = false;          // false until every FILL item in rise_layout.hpp is set
    std::string missing;         // what is still needed (empty when ready)
    std::string info;            // one line when ready: player count, teams, health sample
    esp::Snapshot snapshot;      // valid when ready
};

// gameBase: base address of the il2cpp module (libil2cpp.so) in the target process.
ReadResult readFrame(const mem::Process& p, uint64_t gameBase, int screenW, int screenH);

}  // namespace rise
