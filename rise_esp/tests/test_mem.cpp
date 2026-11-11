// Reads a known value out of this process through /proc/self/mem and checks the module lookup.
#include <cstdint>
#include <cstdio>
#include <unistd.h>

#include "mem/memory.hpp"

static volatile uint64_t g_magic = 0x1122334455667788ull;

int main() {
    mem::Process p;
    if (!p.attach(static_cast<int>(getpid()))) {
        std::printf("attach failed\n");
        return 1;
    }
    const uint64_t got = p.read<uint64_t>(reinterpret_cast<uint64_t>(&g_magic));
    const bool valueOk = got == 0x1122334455667788ull;
    const uint64_t base = p.moduleBase("test_mem");   // this binary is mapped under its own name
    const bool moduleOk = base != 0;
    const bool badOk = !p.read(0, nullptr, 0) && p.read<uint64_t>(0) == 0;
    std::printf("value %s, module %s, bad address %s\n", valueOk ? "ok" : "FAIL",
                moduleOk ? "ok" : "FAIL", badOk ? "ok" : "FAIL");
    return (valueOk && moduleOk && badOk) ? 0 : 1;
}
