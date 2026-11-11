// Rise ESP overlay: attaches to the game, reads one frame per loop and draws the
// ESP primitives on the overlay window. Usage (as root): rise_esp [package name]
// Without an argument the StandRise package is used.
#include <dirent.h>
#include <signal.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "draw.h"
#include "esp/esp.hpp"
#include "game/rise_reader.hpp"
#include "mem/memory.hpp"

namespace {

constexpr const char* kDefaultPackage = "com.standrise.game";

// Pid of the process whose cmdline equals the package name, 0 if not running.
int findPid(const char* package) {
    DIR* dir = opendir("/proc");
    if (!dir) return 0;
    int found = 0;
    while (dirent* e = readdir(dir)) {
        const int pid = std::atoi(e->d_name);
        if (pid <= 0) continue;
        char path[64];
        std::snprintf(path, sizeof(path), "/proc/%d/cmdline", pid);
        std::FILE* f = std::fopen(path, "r");
        if (!f) continue;
        char cmd[256] = {0};
        std::fread(cmd, 1, sizeof(cmd) - 1, f);
        std::fclose(f);
        if (std::strcmp(cmd, package) == 0) {
            found = pid;
            break;
        }
    }
    closedir(dir);
    return found;
}

ImU32 toImColor(uint32_t argb) {
    return IM_COL32((argb >> 16) & 0xFF, (argb >> 8) & 0xFF, argb & 0xFF, (argb >> 24) & 0xFF);
}

void drawPrimitives(const esp::DrawList& list) {
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    for (const esp::Prim& p : list) {
        const ImU32 col = toImColor(p.color);
        switch (p.kind) {
            case esp::Kind::Rect:
                dl->AddRect(ImVec2(p.x1, p.y1), ImVec2(p.x2, p.y2), col, 0.f, 0, 1.5f);
                break;
            case esp::Kind::Line:
                dl->AddLine(ImVec2(p.x1, p.y1), ImVec2(p.x2, p.y2), col, 1.5f);
                break;
            case esp::Kind::Bar: {
                dl->AddRectFilled(ImVec2(p.x1, p.y1), ImVec2(p.x2, p.y2), IM_COL32(0, 0, 0, 160));
                const float h = (p.y2 - p.y1) * p.fraction;
                dl->AddRectFilled(ImVec2(p.x1, p.y2 - h), ImVec2(p.x2, p.y2), col);
                break;
            }
            case esp::Kind::Text: {
                ImFont* f = overlay::espFont;
                const ImVec2 ts = f->CalcTextSizeA(f->FontSize, FLT_MAX, 0.f, p.text.c_str());
                dl->AddText(f, f->FontSize, ImVec2(p.x1 - ts.x * 0.5f, p.y1 - ts.y), col, p.text.c_str());
                break;
            }
        }
    }
}

// One line per '\n' in the text, top-left corner.
void drawStatus(const std::string& text) {
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    ImFont* f = overlay::espFont;
    float y = 20.f;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t end = text.find('\n', start);
        const std::string line = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!line.empty()) dl->AddText(f, f->FontSize, ImVec2(20.f, y), IM_COL32(255, 255, 255, 255), line.c_str());
        y += f->FontSize + 2.f;
        if (end == std::string::npos) break;
        start = end + 1;
    }
}

}  // namespace

int main(int argc, char** argv) {
    const char* package = argc >= 2 ? argv[1] : kDefaultPackage;

    const auto info = overlay::displayInfo();
    const int W = std::max(info.width, info.height);
    const int H = std::min(info.width, info.height);
    if (W < 2 || H < 2) {
        std::fprintf(stderr, "display size unknown\n");
        return 2;
    }
    if (!overlay::init(W, H)) {
        std::fprintf(stderr, "overlay init failed\n");
        return 3;
    }

    esp::Config cfg;
    cfg.skeleton = false;
    mem::Process proc;
    int pid = 0;
    uint64_t base = 0;
    int retry = 0;
    for (;;) {
        if (pid <= 0 || kill(pid, 0) != 0) {
            pid = findPid(package);
            base = 0;
            retry = 0;
            if (pid > 0 && !proc.attach(pid)) pid = 0;
        }
        if (pid > 0 && base == 0 && (retry++ % 60) == 0) base = proc.moduleBase("libil2cpp.so");

        overlay::beginFrame();
        std::string status;
        if (pid <= 0) {
            status = std::string("waiting for ") + package;
        } else if (base == 0) {
            status = "libil2cpp.so is not mapped yet";
        } else {
            const rise::ReadResult frame = rise::readFrame(proc, base, W, H);
            if (frame.ready) {
                esp::DrawList out;
                esp::buildEsp(frame.snapshot, cfg, out);
                drawPrimitives(out);
            } else {
                status = "not ready:\n" + frame.missing;
            }
        }
        if (!status.empty()) drawStatus(status);
        overlay::endFrame();
        usleep(8000);
    }
}
