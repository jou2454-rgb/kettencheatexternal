
// ================================================================================
// Runtime status shared between the game layer and the menu. Plain strings,
// written by the render thread only.
// ================================================================================

#pragma once

#include <cstdio>
#include <cstdarg>
#include <atomic>

namespace status {
    inline char build[96] = "build: not checked";
    inline char camera[96] = "camera: none";
    inline char scan[96] = "scan: idle";
    // Set by the menu's Exit button; the main loop shuts down cleanly
    // (exit() from inside a frame raced the touch threads and ImGui teardown).
    inline std::atomic<bool> g_quit_request{false};

    inline void set(char* slot, size_t size, const char* format, ...) {
        va_list args;
        va_start(args, format);
        std::vsnprintf(slot, size, format, args);
        va_end(args);
    }
}

#define STATUS_SET(slot, ...) ::status::set(::status::slot, sizeof(::status::slot), __VA_ARGS__)
