#include "dbg/log.hpp"

#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <time.h>
#include <ucontext.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

namespace dbg {
namespace {

constexpr const char* kLogPaths[] = {"/sdcard/Download/rise_esp_log.txt", "/data/local/tmp/rise_esp_log.txt"};
constexpr const char* kDumpPaths[] = {"/sdcard/Download/rise_dump.bin", "/data/local/tmp/rise_dump.bin"};
constexpr int kCopies = 2;

int g_fd[kCopies] = {-1, -1};
double g_start = 0.0;
bool g_threaded = false;   // set by begin(): lines and dumps go through the writer thread from then on

// The writer thread does the file work. Lines and dump contents wait in these queues.
std::mutex g_mutex;
std::condition_variable g_cv;
std::deque<std::string> g_lines;
std::deque<std::string> g_dumps;

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

// Writes formatted text to every log file and to stderr (the launcher's log).
void writeText(const char* text, size_t len) {
    for (int i = 0; i < kCopies; ++i) {
        if (g_fd[i] >= 0) writeAll(g_fd[i], text, len);
    }
    std::fwrite(text, 1, len, stderr);
}

// Replaces the dump file in every location (temp file, then rename).
void writeDumpFiles(const std::string& payload) {
    for (int i = 0; i < kCopies; ++i) {
        const std::string tmp = std::string(kDumpPaths[i]) + ".tmp";
        const int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) continue;
        writeAll(fd, payload.data(), payload.size());
        ::close(fd);
        ::rename(tmp.c_str(), kDumpPaths[i]);
    }
}

// Writer thread: takes whatever is queued and writes it, every 100 ms at most.
void writerLoop() {
    for (;;) {
        std::deque<std::string> lines;
        std::deque<std::string> dumps;
        {
            std::unique_lock<std::mutex> lock(g_mutex);
            g_cv.wait_for(lock, std::chrono::milliseconds(100),
                          [] { return !g_lines.empty() || !g_dumps.empty(); });
            lines.swap(g_lines);
            dumps.swap(g_dumps);
        }
        for (const std::string& s : lines) writeText(s.data(), s.size());
        for (const std::string& d : dumps) writeDumpFiles(d);
    }
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

// Fatal signal: the lines still queued go out first, then this one, synchronously.
void onFatal(int sig, siginfo_t* info, void* ctx) {
    unsigned long long pc = 0;
#if defined(__aarch64__)
    if (ctx) pc = static_cast<ucontext_t*>(ctx)->uc_mcontext.pc;
#else
    (void)ctx;
#endif
    char msg[256];
    std::snprintf(msg, sizeof(msg), "FATAL %s (%d): fault address %p, pc 0x%llx. The log ends here.",
                  signalName(sig), sig, info ? info->si_addr : nullptr, pc);
    char out[300];
    const int n = std::snprintf(out, sizeof(out), "[%9.3f] %s\n", uptime(), msg);
    if (g_mutex.try_lock()) {
        for (const std::string& s : g_lines) writeText(s.data(), s.size());
        g_lines.clear();
        g_mutex.unlock();
    }
    if (n > 0) writeText(out, std::min(static_cast<size_t>(n), sizeof(out) - 1));
    _exit(128 + sig);
}

}  // namespace

void begin(const char* build) {
    g_start = monotonicSeconds();
    for (int i = 0; i < kCopies; ++i) g_fd[i] = openFresh(kLogPaths[i]);
    g_threaded = true;
    std::thread(writerLoop).detach();
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
    if (!g_threaded) {
        writeText(out, len);
        return;
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    g_lines.emplace_back(out, len);
    g_cv.notify_one();
}

bool writeDump(const Block* blocks, size_t count) {
    // Layout: "RISEDMP1", u32 block count, u64 uptime in ms, then per block:
    // char name[32], u64 address, u32 size, u32 read ok (1/0), size bytes of data.
    std::string payload;
    auto put = [&payload](const void* src, size_t n) { payload.append(static_cast<const char*>(src), n); };
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
            payload.append(blocks[i].size, '\0');
        }
    }
    if (!g_threaded) {
        writeDumpFiles(payload);
        return true;
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    g_dumps.push_back(std::move(payload));
    g_cv.notify_one();
    return true;
}

void flush() {
    for (int i = 0; i < 300; ++i) {   // up to 3 s
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            if (g_lines.empty() && g_dumps.empty()) break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(150));   // the writer may hold the last batch
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
