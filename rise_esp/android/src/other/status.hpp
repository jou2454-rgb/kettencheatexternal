// ================================================================================
// Quit request shared between the menu and the main loop.
// ================================================================================

#pragma once
#include <atomic>

namespace status {
    // Set by the menu's unload button. The main loop leaves its frame loop, then
    // touch and the overlay are shut down (exit() from inside a frame races the
    // touch thread and ImGui teardown).
    inline std::atomic<bool> g_quit_request{false};
}
