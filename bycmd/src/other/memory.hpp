
// ================================================================================
// [TG] @imguisource_bot -> @ImGui_Source_FIXNEY (ImGui Source) -> @il2cpp_src (shit src 🧨)
// ================================================================================

#pragma once

#include <cstdint>
#include <cstring>
#include <cstdio>
#include <vector>
#include <string>
#include <algorithm>
#include <sys/uio.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include "../protect/oxorany.hpp"
#include "diag.hpp"

#include <signal.h>

namespace proc {
    inline int pid = -1;
    inline uint64_t lib = 0;
    inline int fd = -1;
    inline int pm = -1;
    // Build identity of the mapped libunity.so (GNU build-id note, hex).
    // ESP stays disabled until it matches the profile, see game::verify_build().
    inline bool build_checked = false;
    inline bool build_ok = false;
    inline bool skip_build_check = false; // --skip-build-check
    inline char build_id[48] = {0};
}

namespace sys {
    #if defined(__aarch64__)
    static constexpr long SYS_openat = 56;
    static constexpr long SYS_close = 57;
    static constexpr long SYS_read = 63;
    static constexpr long SYS_getdents64 = 61;
    static constexpr long SYS_vm_read = 270;
    #elif defined(__x86_64__)
    static constexpr long SYS_openat = 257;
    static constexpr long SYS_close = 3;
    static constexpr long SYS_read = 0;
    static constexpr long SYS_getdents64 = 217;
    static constexpr long SYS_vm_read = 310;
    #endif

    #ifndef AT_FDCWD
    #define AT_FDCWD -100
    #endif

    static inline long sc6(long n, long a1, long a2, long a3, long a4, long a5, long a6) {
        long r;
        #if defined(__aarch64__)
        register long x8 __asm__("x8") = n;
        register long x0 __asm__("x0") = a1;
        register long x1 __asm__("x1") = a2;
        register long x2 __asm__("x2") = a3;
        register long x3 __asm__("x3") = a4;
        register long x4 __asm__("x4") = a5;
        register long x5 __asm__("x5") = a6;
        __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5) : "memory", "cc");
        r = x0;
        #elif defined(__x86_64__)
        register long rax asm("rax") = n;
        register long rdi asm("rdi") = a1;
        register long rsi asm("rsi") = a2;
        register long rdx asm("rdx") = a3;
        register long r10 asm("r10") = a4;
        register long r8 asm("r8") = a5;
        register long r9 asm("r9") = a6;
        asm volatile("syscall" : "=a"(r) : "a"(rax), "D"(rdi), "S"(rsi), "d"(rdx), "r"(r10), "r"(r8), "r"(r9) : "rcx", "r11", "memory");
        #else
        r = syscall(n, a1, a2, a3, a4, a5, a6);
        #endif
        return r;
    }

    static inline int openat(int fd, const char* p, int f) {
        return static_cast<int>(sc6(SYS_openat, fd, reinterpret_cast<long>(p), f, 0, 0, 0));
    }

    static inline int close(int fd) {
        return static_cast<int>(sc6(SYS_close, fd, 0, 0, 0, 0, 0));
    }

    static inline long read(int fd, void* b, size_t c) {
        return sc6(SYS_read, fd, reinterpret_cast<long>(b), c, 0, 0, 0);
    }

    static inline long getdents64(int fd, void* d, size_t c) {
        return sc6(SYS_getdents64, fd, reinterpret_cast<long>(d), c, 0, 0, 0);
    }

    static inline long vm_read(int pid, struct iovec* l, unsigned long lc, struct iovec* r, unsigned long rc) {
        return sc6(SYS_vm_read, pid, reinterpret_cast<long>(l), lc, reinterpret_cast<long>(r), rc, 0);
    }

}

struct linux_dirent64 {
    uint64_t d_ino;
    int64_t d_off;
    unsigned short d_reclen;
    unsigned char d_type;
    char d_name[];
};

inline bool addr_valid(uint64_t a) {
    return a >= 0x1000;
}

inline bool mem_read(uint64_t a, void* b, size_t l) {
    if (proc::pid < 0 || a == 0 || l == 0 || b == nullptr) {
        return false;
    }

    if (a < 0x1000) {
        return false;
    }

    struct iovec loc[1];
    struct iovec rem[1];

    loc[0].iov_base = b;
    loc[0].iov_len = l;
    rem[0].iov_base = reinterpret_cast<void*>(a);
    rem[0].iov_len = l;

    ssize_t result = sys::vm_read(proc::pid, loc, 1, rem, 1);

    if (result != static_cast<long>(l) && proc::pm >= 0) {
        result = ::pread(proc::pm, b, l, static_cast<off_t>(a));
    }

    if (result != static_cast<long>(l)) {
        static long long last_log = 0;
        if (diag::every(last_log, 10000))
            diag::log("ERROR", "remote read failed pid=%d addr=0x%llx size=%zu result=%lld errno=%d",
                      proc::pid, (unsigned long long)a, l, (long long)result, errno);
        memset(b, 0, l);
        return false;
    }

    return true;
}

// Read-only memory access. Write helpers are deliberately absent: the overlay never writes to the game.
template<typename T>
inline T rpm(uint64_t a) {
    T v{};
    mem_read(a, &v, sizeof(T));
    return v;
}

inline int get_pid() {
    int pfd = sys::openat(AT_FDCWD, oxorany("/proc"), O_RDONLY | O_DIRECTORY);
    if (pfd < 0) return -1;

    char buf[4096];

    while (true) {
        long nr = sys::getdents64(pfd, buf, sizeof(buf));
        if (nr <= 0) break;

        char* p = buf;
        while (p < buf + nr) {
            auto* e = reinterpret_cast<linux_dirent64*>(p);

            if (e->d_type == 4) {
                char* n = e->d_name;
                bool in = true;
                for (char* c = n; *c; c++) {
                    if (*c < '0' || *c > '9') { in = false; break; }
                }

                if (in && *n != '\0') {
                    int pid = 0;
                    for (char* c = n; *c; c++) pid = pid * 10 + (*c - '0');

                    char cp[256];
                    int ps = 0;

                    const char* ps_ = oxorany("/proc/");
                    while (*ps_) cp[ps++] = *ps_++;
                    for (char* c = n; *c; c++) cp[ps++] = *c;
                    const char* cs = oxorany("/cmdline");
                    while (*cs) cp[ps++] = *cs++;
                    cp[ps] = '\0';

                    int cfd = sys::openat(AT_FDCWD, cp, O_RDONLY);
                    if (cfd >= 0) {
                        char cmd[256] = {0};
                        long b = sys::read(cfd, cmd, sizeof(cmd) - 1);
                        sys::close(cfd);

                        if (b > 0) {
                            cmd[b] = '\0';

                            const char* tg = oxorany("com.axlebolt.standoff2");
                            int tl = 0;
                            while (tg[tl]) tl++;

                            bool exact = b >= tl;
                            for (int j = 0; exact && j < tl; ++j) exact = cmd[j] == tg[j];
                            exact = exact && cmd[tl] == '\0';
                            if (exact) {
                                sys::close(pfd);
                                return pid;
                            }
                        }
                    }
                }
            }

            p += e->d_reclen;
        }
    }

    sys::close(pfd);
    return -1;
}

inline bool elf_is_aarch64(uint64_t base) {
    if (base < 0x10000 || base > 0x0001000000000000ULL) return false;
    uint32_t magic = 0;
    uint16_t type = 0, machine = 0;
    if (!mem_read(base, &magic, sizeof(magic)) || magic != 0x464c457fU) return false;
    if (!mem_read(base + 16, &type, sizeof(type)) || type != 3) return false;       // ET_DYN
    if (!mem_read(base + 18, &machine, sizeof(machine)) || machine != 0xB7) return false; // EM_AARCH64
    return true;
}

// Unity 6 can map libunity.so directly from base.apk. In that case the maps
// pathname is only "base.apk" and the file offset is the offset inside the
// APK, so neither the filename nor begin-file_offset identifies the load bias.
// Resolve it from the ELF's DT_SONAME instead, which is stable across APK
// splits, page alignment and Unity upgrades.
inline bool elf_soname_is_libunity(uint64_t base) {
    unsigned char ehdr[64]{};
    if (!mem_read(base, ehdr, sizeof(ehdr))) return false;
    if (ehdr[0] != 0x7f || ehdr[1] != 'E' || ehdr[2] != 'L' || ehdr[3] != 'F') return false;
    if (ehdr[4] != 2 || ehdr[5] != 1) return false; // ELFCLASS64, little endian
    uint16_t type = 0;
    uint16_t machine = 0;
    std::memcpy(&type, ehdr + 16, sizeof(type));
    std::memcpy(&machine, ehdr + 18, sizeof(machine));
    if (type != 3 || machine != 0xB7) return false;

    uint64_t phoff = 0;
    uint16_t phentsize = 0, phnum = 0;
    std::memcpy(&phoff, ehdr + 0x20, sizeof(phoff));
    std::memcpy(&phentsize, ehdr + 0x36, sizeof(phentsize));
    std::memcpy(&phnum, ehdr + 0x38, sizeof(phnum));
    if (phentsize < 56 || phnum == 0 || phnum > 128 || phoff > 0x10000) return false;

    uint64_t dynamic_vaddr = 0, dynamic_size = 0;
    for (uint16_t i = 0; i < phnum; ++i) {
        unsigned char ph[56]{};
        if (!mem_read(base + phoff + uint64_t(i) * phentsize, ph, sizeof(ph))) return false;
        uint32_t p_type = 0;
        uint64_t p_vaddr = 0, p_filesz = 0;
        std::memcpy(&p_type, ph, sizeof(p_type));
        std::memcpy(&p_vaddr, ph + 16, sizeof(p_vaddr));
        std::memcpy(&p_filesz, ph + 32, sizeof(p_filesz));
        if (p_type == 2 && p_filesz >= 16) { // PT_DYNAMIC
            dynamic_vaddr = p_vaddr;
            dynamic_size = p_filesz;
            break;
        }
    }
    if (!dynamic_vaddr || dynamic_vaddr > 0x10000000ULL || dynamic_size > 0x100000ULL) return false;

    uint64_t strtab = 0, soname = 0;
    const uint64_t entries = std::min<uint64_t>(dynamic_size / 16, 512);
    for (uint64_t i = 0; i < entries; ++i) {
        uint64_t tag = 0, value = 0;
        if (!mem_read(base + dynamic_vaddr + i * 16, &tag, sizeof(tag)) ||
            !mem_read(base + dynamic_vaddr + i * 16 + 8, &value, sizeof(value))) return false;
        if (tag == 5) strtab = value;       // DT_STRTAB
        else if (tag == 14) soname = value; // DT_SONAME
        if (tag == 0) break;                // DT_NULL
    }
    if (!strtab || strtab > 0x40000000ULL || soname > 0x10000ULL) return false;

    auto name_matches = [&](uint64_t address) {
        char value[160]{};
        if (!mem_read(address, value, sizeof(value) - 1)) return false;
        return std::strstr(value, "libunity.so") != nullptr;
    };
    // DT_STRTAB is normally a virtual address (load_bias + d_ptr), but some
    // Android linkers expose it already relocated. Accept both forms.
    if (name_matches(base + strtab + soname)) return true;
    if (strtab > 0x10000ULL && name_matches(strtab + soname)) return true;
    return false;
}

// Read the NT_GNU_BUILD_ID note of a mapped ELF and format it as lowercase hex.
// Returns false when the ELF has no readable PT_NOTE / build-id note.
inline bool elf_build_id(uint64_t base, char* out, size_t out_size) {
    if (!out || out_size < 3) return false;
    out[0] = '\0';
    unsigned char ehdr[64]{};
    if (!mem_read(base, ehdr, sizeof(ehdr))) return false;
    if (ehdr[0] != 0x7f || ehdr[1] != 'E' || ehdr[2] != 'L' || ehdr[3] != 'F' || ehdr[4] != 2) return false;
    uint64_t phoff = 0;
    uint16_t phentsize = 0, phnum = 0;
    std::memcpy(&phoff, ehdr + 0x20, sizeof(phoff));
    std::memcpy(&phentsize, ehdr + 0x36, sizeof(phentsize));
    std::memcpy(&phnum, ehdr + 0x38, sizeof(phnum));
    if (phentsize < 56 || phnum == 0 || phnum > 128 || phoff > 0x10000) return false;
    for (uint16_t i = 0; i < phnum; ++i) {
        unsigned char ph[56]{};
        if (!mem_read(base + phoff + uint64_t(i) * phentsize, ph, sizeof(ph))) return false;
        uint32_t p_type = 0;
        uint64_t p_vaddr = 0, p_filesz = 0;
        std::memcpy(&p_type, ph, sizeof(p_type));
        std::memcpy(&p_vaddr, ph + 16, sizeof(p_vaddr));
        std::memcpy(&p_filesz, ph + 32, sizeof(p_filesz));
        if (p_type != 4 /* PT_NOTE */ || p_filesz < 16 || p_filesz > 0x4000) continue;
        std::vector<unsigned char> notes(static_cast<size_t>(p_filesz), 0);
        if (!mem_read(base + p_vaddr, notes.data(), notes.size())) continue;
        size_t off = 0;
        while (off + 12 <= notes.size()) {
            uint32_t namesz = 0, descsz = 0, type = 0;
            std::memcpy(&namesz, notes.data() + off, 4);
            std::memcpy(&descsz, notes.data() + off + 4, 4);
            std::memcpy(&type, notes.data() + off + 8, 4);
            const size_t name_off = off + 12;
            const size_t desc_off = name_off + ((namesz + 3) & ~3u);
            const size_t next = desc_off + ((descsz + 3) & ~3u);
            if (namesz > 64 || descsz > 64 || next > notes.size()) break;
            if (type == 3 /* NT_GNU_BUILD_ID */ && namesz >= 4 &&
                std::memcmp(notes.data() + name_off, "GNU", 3) == 0 && descsz > 0) {
                static const char hex[] = "0123456789abcdef";
                size_t w = 0;
                for (uint32_t b = 0; b < descsz && w + 2 < out_size; ++b) {
                    const unsigned char v = notes[desc_off + b];
                    out[w++] = hex[v >> 4];
                    out[w++] = hex[v & 15];
                }
                out[w] = '\0';
                return w > 0;
            }
            off = next;
        }
    }
    return false;
}

inline bool process_alive(int pid) {
    if (pid <= 0) return false;
    return ::kill(pid, 0) == 0 || errno == EPERM;
}

inline uint64_t get_lib() {
    if (proc::pid <= 0) return 0;
    static long long last_report = 0;
    const bool report = diag::every(last_report, 2000);
    char path[64];
    std::snprintf(path, sizeof(path), "/proc/%d/maps", proc::pid);
    FILE* file = std::fopen(path, "r");
    if (!file) {
        diag::log("ERROR", "cannot open %s for libunity discovery errno=%d", path, errno);
        return 0;
    }

    struct Segment {
        uint64_t begin, end, file_offset;
        bool named_unity;
    };
    std::vector<Segment> segments;
    char line[1024];
    while (std::fgets(line, sizeof(line), file)) {
        unsigned long long begin = 0, end = 0, file_offset = 0;
        char perms[5]{};
        if (std::sscanf(line, "%llx-%llx %4s %llx", &begin, &end, perms, &file_offset) != 4 ||
            end <= begin || perms[0] != 'r') continue;
        segments.push_back({uint64_t(begin), uint64_t(end), uint64_t(file_offset),
                            std::strstr(line, "libunity.so") != nullptr});
    }
    std::fclose(file);

    std::vector<uint64_t> candidates;
    auto add_candidate = [&candidates](uint64_t base) {
        if (!elf_is_aarch64(base)) return;
        if (std::find(candidates.begin(), candidates.end(), base) == candidates.end())
            candidates.push_back(base);
    };
    for (const Segment& segment : segments) {
        // Normal .so mappings: first LOAD starts at the load bias.
        add_candidate(segment.begin);
        // Some linkers expose only a later LOAD segment at offset 0, or a
        // mapping whose file offset must be subtracted. Check both forms.
        if (segment.begin >= segment.file_offset) add_candidate(segment.begin - segment.file_offset);
    }

    // DT_SONAME is authoritative and covers split-APK mappings with no
    // libunity.so pathname. Keep the old path-based hint as a fallback for
    // stripped/packed ELFs whose dynamic section is unreadable.
    for (uint64_t base : candidates) {
        if (elf_soname_is_libunity(base)) {
            diag::log("INFO", "libunity.so load bias=0x%llx (ELF SONAME, %zu candidates)",
                      (unsigned long long)base, candidates.size());
            return base;
        }
    }
    for (const Segment& segment : segments) {
        if (!segment.named_unity) continue;
        for (uint64_t base : {segment.begin, segment.begin >= segment.file_offset
                                   ? segment.begin - segment.file_offset : segment.begin}) {
            if (elf_is_aarch64(base)) {
                diag::log("WARN", "libunity.so load bias=0x%llx (pathname fallback, no SONAME)",
                          (unsigned long long)base);
                return base;
            }
        }
    }

    if (report)
        diag::log("ERROR", "libunity.so ELF not found; readable mappings=%zu candidates=%zu",
                  segments.size(), candidates.size());
    return 0;
}

inline void init_mem() {
    if (proc::pid <= 0) return;
    if (proc::pm >= 0) sys::close(proc::pm);
    proc::pm = -1;

    char mp[64];
    int ps = 0;

    const char* ps_ = oxorany("/proc/");
    while (*ps_) mp[ps++] = *ps_++;

    int pid = proc::pid;
    char pds[16];
    int pl = 0;
    int tm = pid;
    while (tm > 0) { pds[pl++] = '0' + (tm % 10); tm /= 10; }
    for (int i = pl - 1; i >= 0; i--) mp[ps++] = pds[i];

    const char* ms = oxorany("/mem");
    while (*ms) mp[ps++] = *ms++;
    mp[ps] = '\0';

    proc::pm = sys::openat(AT_FDCWD, mp, O_RDONLY);
    diag::log(proc::pm >= 0 ? "INFO" : "WARN", "remote memory fallback %s path=%s fd=%d",
              proc::pm >= 0 ? "ready" : "unavailable", mp, proc::pm);
}

namespace game {
    inline int tick = 0;
    inline int fail_count = 0;
    inline int check_tick = 0;

    inline bool valid() {
        if (proc::pid <= 0) {
            proc::pid = get_pid();
            if (proc::pid <= 0) {
                static long long last_pid_log = 0;
                if (diag::every(last_pid_log, 2000)) diag::log("WAIT", "process com.axlebolt.standoff2 not found");
                return false;
            }
            diag::log("INFO", "game process found pid=%d", proc::pid);
            init_mem();
        }

        if (proc::lib == 0) {
            if (proc::pm < 0) init_mem();
            proc::lib = get_lib();
            if (proc::lib != 0) {
                diag::log("INFO", "libunity.so base=0x%llx", (unsigned long long)proc::lib);
            } else {
                static long long last_lib_log = 0;
                if (diag::every(last_lib_log, 2000)) diag::log("WAIT", "libunity.so mapping not ready for pid=%d", proc::pid);
            }
            return false;
        }

        // kill(pid, 0) costs one cheap syscall; a full /proc walk every 60
        // frames was a periodic hitch on the render thread.
        if (!process_alive(proc::pid)) {
            if (proc::pm >= 0) { sys::close(proc::pm); proc::pm = -1; }
            diag::log("WARN", "game process pid=%d exited; resetting discovery", proc::pid);
            proc::pid = -1;
            proc::lib = 0;
            proc::build_checked = false;
            proc::build_ok = false;
            proc::build_id[0] = '\0';
            tick = 0;
            return false;
        }
        tick++;
        if (tick >= 600) {
            // The package can be restarted under a new pid while the old one
            // is still tearing down; re-resolve the pid by cmdline now and then.
            tick = 0;
            const int p = get_pid();
            if (p > 0 && p != proc::pid) {
                if (proc::pm >= 0) { sys::close(proc::pm); proc::pm = -1; }
                proc::pid = p;
                proc::lib = 0;
                proc::build_checked = false;
                proc::build_ok = false;
                proc::build_id[0] = '\0';
                diag::log("WARN", "game process changed, new pid=%d; resetting library discovery", p);
                return false;
            }
        }

        return true;
    }

    inline void check_lib(uint64_t pm) {
        check_tick++;
        if (check_tick >= 60) {
            if (pm == 0) {
                fail_count++;
                if (fail_count >= 5) {
                    uint64_t lib = get_lib();
                    if (lib != 0 && lib != proc::lib) {
                        proc::lib = lib;
                        if (proc::pm >= 0) { sys::close(proc::pm); proc::pm = -1; }
                        init_mem();
                    }
                    fail_count = 3;
                }
            } else {
                fail_count = 0;
            }
            check_tick = 0;
        }
    }

    inline void init() {
        proc::pid = get_pid();
        diag::log(proc::pid > 0 ? "INFO" : "WAIT", "initial process lookup pid=%d", proc::pid);
        if (proc::pid > 0) {
            init_mem();
            proc::lib = get_lib();
            if (proc::lib != 0) {
                diag::log("INFO", "initial libunity.so base=0x%llx", (unsigned long long)proc::lib);
            }
        }
    }
}
