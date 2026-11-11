// Run log and memory dumps of the Rise overlay. The files are written by a background thread,
// so a slow file system never holds up the render loop; flush() waits for it at exit. A fatal
// signal is written synchronously before the process ends:
//   /sdcard/Download/rise_esp_log.txt   and   /data/local/tmp/rise_esp_log.txt
//   /sdcard/Download/rise_dump.bin      and   /data/local/tmp/rise_dump.bin
// The previous run's log is kept as rise_esp_log.txt.prev.
#pragma once
#include <cstddef>
#include <cstdint>

namespace dbg {

// Opens the log files and starts the writer thread. Call once, first thing in main().
void begin(const char* build);

// Seconds since begin().
double uptime();

// One timestamped line into every log file and stderr (queued for the writer thread).
void line(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

// A memory block for the dump file. data == nullptr records a failed read.
struct Block {
    const char* name;
    uint64_t addr;
    const void* data;
    size_t size;
};

// Queues the dump file (replaced in full by the writer thread). Returns true when queued.
bool writeDump(const Block* blocks, size_t count);

// Waits until the queued lines and dumps are written (call at exit).
void flush();

// Logs SIGSEGV, SIGBUS, SIGABRT, SIGFPE and SIGILL (signal, fault address, pc) before exit.
void installCrashHandlers();

}  // namespace dbg
