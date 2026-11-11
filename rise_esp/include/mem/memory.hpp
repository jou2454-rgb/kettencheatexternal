// Read-only access to another process on Linux/Android (needs root or ptrace rights).
// Reads go through /proc/<pid>/mem with pread. Nothing is ever written.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace mem {

class Process {
public:
    Process() = default;
    Process(const Process&) = delete;
    Process& operator=(const Process&) = delete;
    ~Process();

    bool attach(int pid);
    bool valid() const { return fd_ >= 0; }
    bool read(uint64_t addr, void* out, size_t n) const;

    template <class T>
    T read(uint64_t addr) const {
        T v{};
        read(addr, &v, sizeof(T));
        return v;
    }

    // Readable, writable mappings [begin, end) of the process (heap, stacks, .data).
    std::vector<std::pair<uint64_t, uint64_t>> rwRegions() const;

    // Start address of the first mapping of `module` with file offset 0, e.g. "libil2cpp.so".
    // Returns 0 when the module is not mapped.
    uint64_t moduleBase(const std::string& module) const;

private:
    int pid_ = -1;
    int fd_ = -1;
};

}  // namespace mem
