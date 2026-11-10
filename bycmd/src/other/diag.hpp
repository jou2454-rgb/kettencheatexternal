
// ================================================================================
// Source code from @imguisource_bot
// Primary: t.me/ImGui_Source_FIXNEY (ImGui Source)
// Secondary: t.me/il2cpp_src (shit src 🧨)
// ================================================================================

#pragma once

#include <cstdarg>
#include <cstdio>
#include <chrono>
#include <algorithm>
#include <unistd.h>
#if defined(__ANDROID__)
#include <android/log.h>
#endif

namespace diag {
inline long long now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

inline bool every(long long& last, long long interval_ms) {
    const long long now = now_ms();
    if (now - last < interval_ms) return false;
    last = now;
    return true;
}

inline void log(const char* level, const char* format, ...) {
    // A disconnected/slow adb shell must never block the overlay thread.
    // STDOUT is configured O_NONBLOCK by main(); drop records when full.
    char line[1536];
    int used = std::snprintf(line, sizeof(line), "[bycmd][%s] ", level);
    if (used < 0) return;
    if (used >= static_cast<int>(sizeof(line))) used = sizeof(line) - 1;
    va_list args;
    va_start(args, format);
    const int added = std::vsnprintf(line + used, sizeof(line) - size_t(used), format, args);
    va_end(args);
    if (added > 0) used += std::min<int>(added, int(sizeof(line) - size_t(used) - 1));
    if (used < static_cast<int>(sizeof(line)) - 1) line[used++] = '\n';
    line[used] = '\0';
    (void)::write(STDOUT_FILENO, line, size_t(used));
#if defined(__ANDROID__)
    // The overlay is usually started from adb/MT Manager, so stdout is not
    // visible in logcat. Mirror diagnostics there as well.
    __android_log_print(ANDROID_LOG_INFO, "bycmd", "%s", line);
#endif
}
}
