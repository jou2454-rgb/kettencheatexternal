// Prints one ESP frame for a running rise process: the draw primitives per enemy.
// Usage: rise_esp <pid> [module=libil2cpp.so] [screenW=1920] [screenH=1080]
#include <cstdio>
#include <cstdlib>

#include "esp/esp.hpp"
#include "game/rise_reader.hpp"

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: rise_esp <pid> [module=libil2cpp.so] [screenW] [screenH]\n");
        return 1;
    }
    const int pid = std::atoi(argv[1]);
    const char* module = argc > 2 ? argv[2] : "libil2cpp.so";
    const int W = argc > 3 ? std::atoi(argv[3]) : 1920;
    const int H = argc > 4 ? std::atoi(argv[4]) : 1080;

    mem::Process proc;
    if (!proc.attach(pid)) {
        std::printf("cannot open /proc/%d/mem (root needed)\n", pid);
        return 1;
    }
    const uint64_t base = proc.moduleBase(module);

    const rise::ReadResult frame = rise::readFrame(proc, base, W, H);
    if (!frame.ready) {
        std::printf("not ready:\n%s", frame.missing.c_str());
        return 2;
    }

    esp::Config cfg;
    cfg.skeleton = false;
    esp::DrawList out;
    esp::buildEsp(frame.snapshot, cfg, out);
    std::printf("players=%zu primitives=%zu\n", frame.snapshot.players.size(), out.size());
    for (const esp::Prim& p : out)
        std::printf("kind=%d  %.1f %.1f %.1f %.1f  fill=%.2f  %s\n", static_cast<int>(p.kind),
                    p.x1, p.y1, p.x2, p.y2, p.fraction, p.text.c_str());
    return 0;
}
