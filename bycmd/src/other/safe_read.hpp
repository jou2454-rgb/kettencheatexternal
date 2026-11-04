
#pragma once

#include <signal.h>
#include <setjmp.h>
#include <cstring>
#include <cstdint>
#include "memory.hpp"

namespace safe {

inline thread_local sigjmp_buf g_jmp;
inline thread_local volatile sig_atomic_t g_active = 0;

inline void crash_handler(int sig) {
    if (g_active) {
        g_active = 0;
        siglongjmp(g_jmp, 1);
    }
    signal(sig, SIG_DFL);
    raise(sig);
}

inline void install() {
    struct sigaction sa{};
    sa.sa_handler = crash_handler;
    sa.sa_flags = SA_RESTART;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, nullptr);
    sigaction(SIGBUS, &sa, nullptr);
}

template<typename T>
inline T read(uint64_t address) {
    T result{};
    g_active = 1;
    if (sigsetjmp(g_jmp, 1) == 0) {
        result = rpm<T>(address);
        g_active = 0;
    } else {
        std::memset(&result, 0, sizeof(T));
        static long long last = 0;
        if (diag::every(last, 5000))
            diag::log("SAFE", "recovered from crash reading 0x%llx", (unsigned long long)address);
    }
    return result;
}

inline bool read_buf(uint64_t address, void* buf, size_t size) {
    g_active = 1;
    if (sigsetjmp(g_jmp, 1) == 0) {
        bool ok = mem_read(address, buf, size);
        g_active = 0;
        return ok;
    }
    std::memset(buf, 0, size);
    static long long last = 0;
    if (diag::every(last, 5000))
        diag::log("SAFE", "recovered from crash reading buf 0x%llx size=%zu",
                  (unsigned long long)address, size);
    return false;
}

}
