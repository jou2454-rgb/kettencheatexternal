#include "mem/memory.hpp"

#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>

namespace mem {

Process::~Process() {
    if (fd_ >= 0) ::close(fd_);
}

bool Process::attach(int pid) {
    if (fd_ >= 0) ::close(fd_);
    char path[64];
    std::snprintf(path, sizeof(path), "/proc/%d/mem", pid);
    fd_ = ::open(path, O_RDONLY);
    pid_ = fd_ >= 0 ? pid : -1;
    return fd_ >= 0;
}

bool Process::read(uint64_t addr, void* out, size_t n) const {
    if (fd_ < 0 || addr == 0 || addr >= (1ULL << 62)) return false;
    const ssize_t got = ::pread(fd_, out, n, static_cast<off_t>(addr));
    return got == static_cast<ssize_t>(n);
}

std::vector<std::pair<uint64_t, uint64_t>> Process::rwRegions() const {
    std::vector<std::pair<uint64_t, uint64_t>> out;
    if (pid_ < 0) return out;
    char path[64];
    std::snprintf(path, sizeof(path), "/proc/%d/maps", pid_);
    std::FILE* f = std::fopen(path, "r");
    if (!f) return out;
    char line[512];
    while (std::fgets(line, sizeof(line), f)) {
        unsigned long long start = 0, end = 0;
        char perms[8] = {0};
        if (std::sscanf(line, "%llx-%llx %7s", &start, &end, perms) != 3) continue;
        if (perms[0] == 'r' && perms[1] == 'w' && end - start <= (1ULL << 30))
            out.emplace_back(static_cast<uint64_t>(start), static_cast<uint64_t>(end));
    }
    std::fclose(f);
    return out;
}

uint64_t Process::moduleBase(const std::string& module) const {
    if (pid_ < 0) return 0;
    char path[64];
    std::snprintf(path, sizeof(path), "/proc/%d/maps", pid_);
    std::FILE* f = std::fopen(path, "r");
    if (!f) return 0;

    uint64_t base = 0;
    char line[512];
    while (std::fgets(line, sizeof(line), f)) {
        unsigned long long start = 0, end = 0, offset = 0, inode = 0;
        char perms[8] = {0}, dev[16] = {0}, name[256] = {0};
        const int n = std::sscanf(line, "%llx-%llx %7s %llx %15s %llu %255s",
                                  &start, &end, perms, &offset, dev, &inode, name);
        if (n < 7 || offset != 0) continue;
        const std::string nm(name);
        if (nm.size() >= module.size() &&
            nm.compare(nm.size() - module.size(), module.size(), module) == 0) {
            base = static_cast<uint64_t>(start);
            break;
        }
    }
    std::fclose(f);
    return base;
}

}  // namespace mem
