#include "dbg/log.hpp"

#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <time.h>
#include <ucontext.h>
#include <unistd.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace dbg {
namespace {

constexpr const char* kLogPaths[] = {"/sdcard/Download/rise_esp_log.txt", "/data/local/tmp/rise_esp_log.txt"};
constexpr const char* kDumpPaths[] = {"/sdcard/Download/rise_dump.bin", "/data/local/tmp/rise_dump.bin"};
constexpr int kCopies = 2;

int g_fd[kCopies] = {-1, -1};
double g_start = 0.0;

double monotonicSeconds() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) * 1e-9;
}

void writeAll(int fd, const void* data, size_t n) {
    const char* p = static_cast<const char*>(data);
    while (n > 0) {
        const ssize_t w = ::write(fd, p, n);
        if (w <= 0) return;
        p += w;
        n -= static_cast<size_t>(w);
    }
}

// Moves an existing file to <path>.prev and opens an empty one.
int openFresh(const char* path) {
    struct stat st{};
    if (::stat(path, &st) == 0) {
        const std::string prev = std::string(path) + ".prev";
        ::rename(path, prev.c_str());
    }
    return ::open(path, O_WRONLY | O_CREAT | O_TRUNC | O_APPEND, 0644);
}

const char* signalName(int sig) {
    switch (sig) {
        case SIGSEGV: return "SIGSEGV";
        case SIGBUS: return "SIGBUS";
        case SIGABRT: return "SIGABRT";
        case SIGFPE: return "SIGFPE";
        case SIGILL: return "SIGILL";
        default: return "signal";
    }
}

void onFatal(int sig, siginfo_t* info, void* ctx) {
    unsigned long long pc = 0;
#if defined(__aarch64__)
    if (ctx) pc = static_cast<ucontext_t*>(ctx)->uc_mcontext.pc;
#else
    (void)ctx;
#endif
    line("FATAL %s (%d): fault address %p, pc 0x%llx. The log ends here.", signalName(sig), sig,
         info ? info->si_addr : nullptr, pc);
    _exit(128 + sig);
}

}  // namespace

void begin(const char* build) {
    g_start = monotonicSeconds();
    for (int i = 0; i < kCopies; ++i) g_fd[i] = openFresh(kLogPaths[i]);
    line("rise_esp build %s", build);
    for (int i = 0; i < kCopies; ++i) {
        line("log file %s: %s", kLogPaths[i], g_fd[i] >= 0 ? "open" : "not writable");
    }
}

double uptime() { return monotonicSeconds() - g_start; }

void line(const char* fmt, ...) {
    char msg[1024];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    char out[1100];
    const int n = std::snprintf(out, sizeof(out), "[%9.3f] %s\n", uptime(), msg);
    if (n <= 0) return;
    const size_t len = std::min(static_cast<size_t>(n), sizeof(out) - 1);
    for (int i = 0; i < kCopies; ++i) {
        if (g_fd[i] >= 0) writeAll(g_fd[i], out, len);
    }
    std::fputs(out, stderr);
}

bool writeDump(const Block* blocks, size_t count) {
    std::vector<uint8_t> buf;
    auto put = [&buf](const void* src, size_t n) {
        const uint8_t* b = static_cast<const uint8_t*>(src);
        buf.insert(buf.end(), b, b + n);
    };
    // Layout: "RISEDMP1", u32 block count, u64 uptime in ms, then per block:
    // char name[32], u64 address, u32 size, u32 read ok (1/0), size bytes of data.
    const char magic[8] = {'R', 'I', 'S', 'E', 'D', 'M', 'P', '1'};
    put(magic, sizeof(magic));
    const uint32_t count32 = static_cast<uint32_t>(count);
    put(&count32, sizeof(count32));
    const uint64_t stampMs = static_cast<uint64_t>(uptime() * 1000.0);
    put(&stampMs, sizeof(stampMs));
    for (size_t i = 0; i < count; ++i) {
        char name[32] = {0};
        std::strncpy(name, blocks[i].name, sizeof(name) - 1);
        put(name, sizeof(name));
        const uint64_t addr = blocks[i].addr;
        put(&addr, sizeof(addr));
        const uint32_t size = static_cast<uint32_t>(blocks[i].size);
        put(&size, sizeof(size));
        const uint32_t ok = blocks[i].data ? 1u : 0u;
        put(&ok, sizeof(ok));
        if (blocks[i].data) {
            put(blocks[i].data, blocks[i].size);
        } else {
            const std::vector<uint8_t> zeros(blocks[i].size, 0);
            put(zeros.data(), zeros.size());
        }
    }
    bool any = false;
    for (int i = 0; i < kCopies; ++i) {
        const std::string tmp = std::string(kDumpPaths[i]) + ".tmp";
        const int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) continue;
        writeAll(fd, buf.data(), buf.size());
        ::close(fd);
        if (::rename(tmp.c_str(), kDumpPaths[i]) == 0) any = true;
    }
    return any;
}

void installCrashHandlers() {
    struct sigaction sa{};
    sa.sa_sigaction = onFatal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_SIGINFO | SA_RESETHAND;
    const int sigs[] = {SIGSEGV, SIGBUS, SIGABRT, SIGFPE, SIGILL};
    for (const int s : sigs) sigaction(s, &sa, nullptr);
}

}  // namespace dbg
