// Rise ESP overlay: attaches to the game, reads one frame per loop and draws the
// ESP primitives on the overlay window. The cloudhack watermark and menu (ported
// from the bycmd source) sit on top; they fade in while the game runs.
// Usage (as root): rise_esp [package name]
// Without an argument the StandRise package is used.
#include <dirent.h>
#include <signal.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <cfloat>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "draw.h"
#include "esp/esp.hpp"
#include "game/rise_reader.hpp"
#include "mem/memory.hpp"
#include "other/status.hpp"
#include "touch/Touch.hpp"
#include "ui/bar.hpp"
#include "ui/cfg.hpp"
#include "ui/menu.hpp"
#include "ui/theme/theme.hpp"

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

// Menu colour (floats 0..1) to 0xAARRGGBB.
uint32_t toArgb(const ImVec4& c) {
    auto channel = [](float v) {
        return static_cast<uint32_t>(std::min(1.f, std::max(0.f, v)) * 255.f + 0.5f);
    };
    return (channel(c.w) << 24) | (channel(c.x) << 16) | (channel(c.y) << 8) | channel(c.z);
}

// ESP settings from the cloudhack menu. The skeleton stays off: the reader does not fill bones yet.
esp::Config espConfigFromMenu() {
    esp::Config c;
    c.box = cfg::esp::box;
    c.name = cfg::esp::name;
    c.health = cfg::esp::health;
    c.skeleton = false;
    c.boxColor = toArgb(cfg::esp::box_col);
    c.healthColor = toArgb(cfg::esp::health_col);
    c.skeletonColor = c.boxColor;
    return c;
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

// One line per '\n', bottom-left corner: the watermark and the menu use the top-left.
void drawStatus(const std::string& text, float screenH) {
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    ImFont* f = overlay::espFont;
    const float lineH = f->FontSize + 2.f;
    const float lines = static_cast<float>(std::count(text.begin(), text.end(), '\n') + 1);
    float y = screenH - 20.f - lines * lineH;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t end = text.find('\n', start);
        const std::string line = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!line.empty()) dl->AddText(f, f->FontSize, ImVec2(20.f, y), IM_COL32(255, 255, 255, 255), line.c_str());
        y += lineH;
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
    // The menu and the watermark size themselves from the landscape overlay (style::k() reads g_sh).
    g_sw = static_cast<float>(W);
    g_sh = static_cast<float>(H);
    if (!touch::init(W, H, static_cast<uint8_t>(info.orientation)))
        std::fprintf(stderr, "no touch device readable in /dev/input; the menu cannot be operated\n");

    mem::Process proc;
    int pid = 0;
    uint64_t base = 0;
    int retry = 0;
    float alpha = 0.f;   // watermark and menu fade with the game, as in the bycmd overlay
    while (!status::g_quit_request.load()) {
        if (pid <= 0 || kill(pid, 0) != 0) {
            pid = findPid(package);
            base = 0;
            retry = 0;
            if (pid > 0 && !proc.attach(pid)) pid = 0;
        }
        if (pid > 0 && base == 0 && (retry++ % 60) == 0) base = proc.moduleBase("libil2cpp.so");

        overlay::beginFrame();
        {
            ImGuiIO& io = ImGui::GetIO();
            float dt = io.DeltaTime;
            if (dt <= 0.f || dt > 0.1f) dt = 0.016f;
            const float target = pid > 0 ? 1.f : 0.f;
            const float speed = pid > 0 ? 4.f : 6.f;
            if (alpha < target) alpha = std::min(target, alpha + dt * speed);
            else if (alpha > target) alpha = std::max(target, alpha - dt * speed);
            ui::bar::set_game_alpha(alpha);
            if (alpha > 0.001f) ui::menu::render();
        }

        std::string statusText;
        if (pid <= 0) {
            statusText = std::string("waiting for ") + package;
        } else if (base == 0) {
            statusText = "libil2cpp.so is not mapped yet";
        } else {
            const rise::ReadResult frame = rise::readFrame(proc, base, W, H);
            if (frame.ready) {
                esp::DrawList out;
                esp::buildEsp(frame.snapshot, espConfigFromMenu(), out);
                drawPrimitives(out);
            } else {
                statusText = "not ready:\n" + frame.missing;
            }
        }
        if (!statusText.empty()) drawStatus(statusText, static_cast<float>(H));
        overlay::endFrame();
        usleep(8000);
    }

    touch::shutdown();
    overlay::shutdown();
    return 0;
}
