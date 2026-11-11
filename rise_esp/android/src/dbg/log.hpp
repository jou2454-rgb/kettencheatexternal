// Run log and memory dumps of the Rise overlay. Everything is written unbuffered, so the
// files are complete even when the process dies:
//   /sdcard/Download/rise_esp_log.txt   and   /data/local/tmp/rise_esp_log.txt
//   /sdcard/Download/rise_dump.bin      and   /data/local/tmp/rise_dump.bin
// The previous run's log is kept as rise_esp_log.txt.prev.
#pragma once
#include <cstddef>
#include <cstdint>

namespace dbg {

// Opens the log files. Call once, first thing in main().
void begin(const char* build);

// Seconds since begin().
double uptime();

// One timestamped line into every open log file and stderr.
void line(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

// A memory block for the dump file. data == nullptr records a failed read.
struct Block {
    const char* name;
    uint64_t addr;
    const void* data;
    size_t size;
};

// Replaces the dump file (written to a temp file, then renamed). Returns true when at
// least one copy was written.
bool writeDump(const Block* blocks, size_t count);

// Logs SIGSEGV, SIGBUS, SIGABRT, SIGFPE and SIGILL (signal, fault address, pc) before exit.
void installCrashHandlers();

}  // namespace dbg
