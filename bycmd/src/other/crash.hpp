
// ================================================================================
// Crash reporting for the overlay process.
//
// On SIGSEGV / SIGBUS / SIGFPE / SIGILL / SIGABRT / SIGTRAP the handler writes
// one record to stdout, the main log and /data/local/tmp/bycmd-crash.log:
// signal, fault address, PC/LR/SP, PC and LR as offsets inside eeeww, the
// frame counter, the last breadcrumb (crash::stage) and a best-effort
// backtrace. Then it exits with 128+signal so the supervisor in main.cpp
// can restart the overlay. Offsets are symbolised offline against the
// unstripped build artifact (eeeww-arm64-unstripped):
//   llvm-symbolizer --obj=eeeww 0x<offset>
// ================================================================================

#pragma once

#include "diag.hpp"
#include <signal.h>
#include <ucontext.h>
#include <unistd.h>
#include <fcntl.h>
#include <link.h>
#include <unwind.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <atomic>

#ifndef BYCMD_BUILD_ID
#define BYCMD_BUILD_ID "dev"
#endif

namespace crash {
    inline const char* volatile g_stage = "init";
    inline std::atomic<unsigned long long> g_frame{0};
    inline uintptr_t g_exe_base = 0;
    inline uintptr_t g_exe_end = 0;
    inline int g_crash_fd = -1;
    inline bool g_installed = false;

    inline void stage(const char* s) { g_stage = s; }

    inline void emit(const char* text, size_t len) {
        (void)::write(STDOUT_FILENO, text, len);
        if (diag::g_log_fd >= 0) (void)::write(diag::g_log_fd, text, len);
        if (g_crash_fd >= 0) (void)::write(g_crash_fd, text, len);
#if defined(__ANDROID__)
        __android_log_write(ANDROID_LOG_ERROR, "bycmd", text);
#endif
    }

    inline uintptr_t rva(uintptr_t address) {
        if (g_exe_base && address >= g_exe_base && address < g_exe_end) return address - g_exe_base;
        return 0;
    }

    struct backtrace_state { uintptr_t* frames; int count; int max; };

    inline _Unwind_Reason_Code backtrace_callback(_Unwind_Context* context, void* arg) {
        backtrace_state* state = static_cast<backtrace_state*>(arg);
        const uintptr_t pc = _Unwind_GetIP(context);
        if (pc && state->count < state->max) state->frames[state->count++] = pc;
        return state->count >= state->max ? _URC_END_OF_STACK : _URC_NO_REASON;
    }

    inline const char* signal_name(int sig) {
        switch (sig) {
            case SIGSEGV: return "SIGSEGV";
            case SIGBUS: return "SIGBUS";
            case SIGFPE: return "SIGFPE";
            case SIGILL: return "SIGILL";
            case SIGABRT: return "SIGABRT";
            case SIGTRAP: return "SIGTRAP";
            default: return "SIG?";
        }
    }

    inline void handler(int sig, siginfo_t* info, void* context_pointer) {
        // A second fault inside the handler must kill the process, not loop.
        ::signal(sig, SIG_DFL);
        uintptr_t pc = 0, lr = 0, sp = 0;
        const ucontext_t* uc = static_cast<const ucontext_t*>(context_pointer);
        if (uc) {
#if defined(__aarch64__)
            pc = uintptr_t(uc->uc_mcontext.pc);
            lr = uintptr_t(uc->uc_mcontext.regs[30]);
            sp = uintptr_t(uc->uc_mcontext.sp);
#elif defined(__x86_64__)
            pc = uintptr_t(uc->uc_mcontext.gregs[REG_RIP]);
            sp = uintptr_t(uc->uc_mcontext.gregs[REG_RSP]);
#endif
        }
        const uintptr_t fault = info ? uintptr_t(info->si_addr) : 0;
        char line[512];
        int n = std::snprintf(line, sizeof(line),
            "[bycmd][CRASH] %s(%d) code=%d fault=0x%lx pc=0x%lx (eeeww+0x%lx) lr=0x%lx (eeeww+0x%lx) sp=0x%lx stage=%s frame=%llu build=%s pid=%d\n",
            signal_name(sig), sig, info ? info->si_code : 0, (unsigned long)fault,
            (unsigned long)pc, (unsigned long)rva(pc), (unsigned long)lr, (unsigned long)rva(lr),
            (unsigned long)sp, g_stage ? g_stage : "?", g_frame.load(), BYCMD_BUILD_ID, int(getpid()));
        if (n > 0) emit(line, size_t(n));

        // Best effort: the unwinder is not async-signal-safe, but a crash
        // record without a stack is worth less than the small risk.
        uintptr_t frames[32];
        backtrace_state state{frames, 0, 32};
        _Unwind_Backtrace(backtrace_callback, &state);
        for (int i = 0; i < state.count; ++i) {
            n = std::snprintf(line, sizeof(line), "[bycmd][CRASH]   #%02d pc 0x%lx eeeww+0x%lx\n",
                              i, (unsigned long)frames[i], (unsigned long)rva(frames[i]));
            if (n > 0) emit(line, size_t(n));
        }
        n = std::snprintf(line, sizeof(line), "[bycmd][CRASH] end, exiting %d\n", 128 + sig);
        if (n > 0) emit(line, size_t(n));
        _exit(128 + sig);
    }

    inline int locate_executable(dl_phdr_info* info, size_t, void* data) {
        // The first entry of dl_iterate_phdr is the main executable (name "" or the path).
        if (g_exe_base) return 1;
        uintptr_t lo = ~uintptr_t(0), hi = 0;
        for (int i = 0; i < info->dlpi_phnum; ++i) {
            const ElfW(Phdr)& ph = info->dlpi_phdr[i];
            if (ph.p_type != PT_LOAD) continue;
            const uintptr_t begin = uintptr_t(info->dlpi_addr + ph.p_vaddr);
            const uintptr_t end = begin + uintptr_t(ph.p_memsz);
            if (begin < lo) lo = begin;
            if (end > hi) hi = end;
        }
        if (hi > lo) {
            g_exe_base = uintptr_t(info->dlpi_addr);
            g_exe_end = hi;
        }
        (void)data;
        return 1;
    }

    inline void install(const char* crash_log_path) {
        if (g_installed) return;
        g_installed = true;
        dl_iterate_phdr(locate_executable, nullptr);
        if (crash_log_path && *crash_log_path)
            g_crash_fd = ::open(crash_log_path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);

        static uint8_t alt_stack[128 * 1024];
        stack_t ss{};
        ss.ss_sp = alt_stack;
        ss.ss_size = sizeof(alt_stack);
        ss.ss_flags = 0;
        ::sigaltstack(&ss, nullptr);

        struct sigaction action{};
        action.sa_sigaction = handler;
        action.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESETHAND;
        ::sigemptyset(&action.sa_mask);
        for (int sig : {SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT, SIGTRAP})
            ::sigaction(sig, &action, nullptr);
        diag::log("INFO", "crash handler installed exe=0x%lx..0x%lx build=%s crashlog=%s",
                  (unsigned long)g_exe_base, (unsigned long)g_exe_end, BYCMD_BUILD_ID,
                  g_crash_fd >= 0 ? crash_log_path : "none");
    }
}
