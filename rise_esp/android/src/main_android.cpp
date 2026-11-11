// Rise ESP overlay: attaches to the game, reads one frame per loop and draws the
// ESP primitives on the overlay window. The cloudhack watermark and menu (from the
// bycmd source) sit on top. The overlay writes its log to
// /sdcard/Download/rise_esp_log.txt (copy in /data/local/tmp) and the game memory it
// reads for calibration to /sdcard/Download/rise_dump.bin (copy in /data/local/tmp).
// Usage (as root): rise_esp [package name]
// Without an argument the StandRise package is used.
#include <dirent.h>
#include <signal.h>
#include <sys/system_properties.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <cfloat>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "dbg/log.hpp"
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

#ifndef RISE_BUILD
#define RISE_BUILD "dev"
#endif

#define ULL(x) static_cast<unsigned long long>(x)

namespace {

constexpr const char* kDefaultPackage = "com.standrise.game";
constexpr double kSummaryPeriod = 1.0;       // SUMMARY line every second
constexpr double kDetailPeriod = 5.0;        // DETAIL lines (camera values, players) every 5 s
constexpr double kDumpPeriodMissing = 20.0;  // memory dump while something is missing
constexpr double kDumpPeriodReady = 60.0;    // memory dump sample while ready

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

// Step name of a "missing" message: the numbers in it change every frame.
std::string stepOf(const std::string& missing) {
    const size_t start = missing.find_first_not_of(" \n");
    if (start == std::string::npos) return std::string();
    const size_t end = missing.find_first_of(":(\n", start);
    return missing.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

std::string oneLine(std::string s) {
    std::replace(s.begin(), s.end(), '\n', ' ');
    return s;
}

std::string fmtMatrix(const esp::Mat4& mat) {
    std::string s;
    char buf[32];
    for (int i = 0; i < 16; ++i) {
        std::snprintf(buf, sizeof(buf), "%s%.3f", i ? " " : "", mat.m[i]);
        s += buf;
    }
    return s;
}

std::string fmtTeams(const std::vector<int>& v) {
    std::string s = "[";
    for (size_t i = 0; i < v.size() && i < 8; ++i) {
        if (i) s += ",";
        s += std::to_string(v[i]);
    }
    if (v.size() > 8) s += ",...";
    return s + "]";
}

std::string fmtHp(const std::vector<float>& v) {
    std::string s = "[";
    char buf[32];
    for (size_t i = 0; i < v.size() && i < 8; ++i) {
        std::snprintf(buf, sizeof(buf), "%s%.1f", i ? "," : "", v[i]);
        s += buf;
    }
    if (v.size() > 8) s += ",...";
    return s + "]";
}

// Device model and Android version, once.
void logDevice() {
    char model[96] = {0}, release[96] = {0}, sdk[96] = {0}, abi[96] = {0};
    __system_property_get("ro.product.model", model);
    __system_property_get("ro.build.version.release", release);
    __system_property_get("ro.build.version.sdk", sdk);
    __system_property_get("ro.product.cpu.abi", abi);
    dbg::line("device %s, android %s (sdk %s), abi %s", model, release, sdk, abi);
}

// Writes the memory blocks of one read to the dump file.
void dumpBlocks(const mem::Process& proc, const rise::Diag& d, const char* reason) {
    std::vector<std::vector<uint8_t>> store;
    std::vector<dbg::Block> blocks;
    store.reserve(d.dumps.size());
    blocks.reserve(d.dumps.size());
    std::string summary;
    for (const rise::DumpBlock& b : d.dumps) {
        if (!b.addr || !b.size) continue;
        store.emplace_back(b.size, static_cast<uint8_t>(0));
        const bool ok = proc.read(b.addr, store.back().data(), b.size);
        blocks.push_back(dbg::Block{b.name, b.addr, ok ? store.back().data() : nullptr, b.size});
        char item[128];
        std::snprintf(item, sizeof(item), "%s%s@0x%llx/%zu%s", summary.empty() ? "" : ", ", b.name, ULL(b.addr),
                      b.size, ok ? "" : " (read failed)");
        summary += item;
    }
    const bool written = dbg::writeDump(blocks.data(), blocks.size());
    dbg::line("DUMP (%s): %zu block(s) %s: %s", reason, blocks.size(), written ? "written" : "NOT written",
              summary.c_str());
}

}  // namespace

int main(int argc, char** argv) {
    dbg::begin(RISE_BUILD);
    dbg::installCrashHandlers();
    const char* package = argc >= 2 ? argv[1] : kDefaultPackage;
    dbg::line("package %s, overlay pid %d, uid %d", package, static_cast<int>(getpid()), static_cast<int>(getuid()));
    logDevice();

    const auto info = overlay::displayInfo();
    dbg::line("display %dx%d, orientation %u", info.width, info.height, static_cast<unsigned>(info.orientation));
    const int W = std::max(info.width, info.height);
    const int H = std::min(info.width, info.height);
    if (W < 2 || H < 2) {
        dbg::line("display size unknown: exit");
        return 2;
    }
    if (!overlay::init(W, H)) {
        dbg::line("overlay init failed: exit");
        return 3;
    }
    dbg::line("overlay %dx%d ready", W, H);
    // The menu and the watermark size themselves from the landscape overlay (style::k() reads g_sh).
    g_sw = static_cast<float>(W);
    g_sh = static_cast<float>(H);

    // Display rotation: touch mapping follows it (as in bycmd); the overlay itself stays W x H.
    uint32_t orientation = info.orientation;
    const bool touchOk = touch::init(W, H, static_cast<uint8_t>(orientation));
    if (!touchOk) dbg::line("touch: no device readable in /dev/input, the menu cannot be operated");

    mem::Process proc;
    int pid = 0;
    uint64_t base = 0;
    int retry = 0;
    float alpha = 0.f;   // watermark and menu fade with the game, as in the bycmd overlay
    unsigned frame = 0;

    // Logging state.
    double lastFrame = dbg::uptime();
    double lastSummary = lastFrame;
    double lastDetail = -1e9;
    double lastDump = -1e9;
    double frameSum = 0.0;
    double frameMax = 0.0;
    unsigned framesInPeriod = 0;
    unsigned touchDowns = 0;
    bool touchPrevDown = false;
    float touchX = 0.f;
    float touchY = 0.f;
    int loggedPid = 0;
    uint64_t loggedBase = 0;
    uint64_t loggedManager = 0;
    uint64_t loggedLocalPc = 0;
    uint64_t loggedNative = 0;
    std::string loggedStep = "start";
    int drawnPrims = 0;

    while (!status::g_quit_request.load()) {
        const double now = dbg::uptime();
        const double frameMs = (now - lastFrame) * 1000.0;
        lastFrame = now;
        frameSum += frameMs;
        frameMax = std::max(frameMax, frameMs);
        ++framesInPeriod;

        // Rotation is read every 16 frames (about 0.1 s); a change switches the touch mapping.
        if (frame % 16 == 0) {
            const auto nowInfo = overlay::displayInfo();
            const uint32_t current = static_cast<uint32_t>(nowInfo.orientation);
            if (nowInfo.width > 0 && nowInfo.height > 0 && current != orientation) {
                dbg::line("EVENT display orientation %u -> %u (display %dx%d)", orientation, current,
                          nowInfo.width, nowInfo.height);
                orientation = current;
                touch::updateOrientation(static_cast<uint8_t>(orientation));
            }
        }
        const bool landscape = orientation == 1 || orientation == 3;   // rotation 90 / 270

        // The game is looked up once a second: /proc scans are slow.
        if (pid > 0 && kill(pid, 0) != 0) {
            pid = 0;
            base = 0;
        }
        if (pid <= 0 && frame % 60 == 0) {
            pid = findPid(package);
            base = 0;
            retry = 0;
            if (pid > 0 && !proc.attach(pid)) {
                dbg::line("EVENT attach to pid %d failed", pid);
                pid = 0;
            }
        }
        if (pid > 0 && base == 0 && (retry++ % 60) == 0) base = proc.moduleBase("libil2cpp.so");
        if (pid != loggedPid) {
            if (pid > 0) dbg::line("EVENT game process found, pid %d", pid);
            else dbg::line("EVENT game process gone (was pid %d)", loggedPid);
            loggedPid = pid;
        }
        if (base != loggedBase) {
            if (base) dbg::line("EVENT libil2cpp.so mapped at 0x%llx", ULL(base));
            else dbg::line("EVENT libil2cpp.so not mapped");
            loggedBase = base;
        }
        ++frame;

        overlay::beginFrame();
        ImGuiIO& io = ImGui::GetIO();
        {
            float dt = io.DeltaTime;
            if (dt <= 0.f || dt > 0.1f) dt = 0.016f;
            const bool running = landscape && pid > 0;
            const float target = running ? 1.f : 0.f;
            const float speed = running ? 4.f : 6.f;
            if (alpha < target) alpha = std::min(target, alpha + dt * speed);
            else if (alpha > target) alpha = std::max(target, alpha - dt * speed);
            ui::bar::set_game_alpha(alpha);
            if (alpha > 0.001f) ui::menu::render();
        }
        {
            // Taps for the log: counted on the press edge, last position while pressed.
            const bool down = io.MouseDown[0];
            if (down && !touchPrevDown) ++touchDowns;
            if (down) {
                touchX = io.MousePos.x;
                touchY = io.MousePos.y;
            }
            touchPrevDown = down;
        }

        rise::ReadResult res;
        std::string statusText;
        bool didRead = false;
        drawnPrims = 0;
        if (!landscape) {
            statusText = "rotate the phone to landscape (display orientation " + std::to_string(orientation) + ")";
        } else if (pid <= 0) {
            statusText = std::string("waiting for ") + package;
        } else if (base == 0) {
            statusText = "libil2cpp.so is not mapped yet";
        } else {
            res = rise::readFrame(proc, base, W, H);
            didRead = true;
            if (res.ready) {
                esp::DrawList out;
                esp::buildEsp(res.snapshot, espConfigFromMenu(), out);
                drawPrimitives(out);
                drawnPrims = static_cast<int>(out.size());
                statusText = res.info;
            } else {
                statusText = "not ready:\n" + res.missing;
            }
        }

        if (didRead) {
            const rise::Diag& d = res.diag;
            if (d.manager != loggedManager) {
                if (d.manager) dbg::line("EVENT PlayerManager found at 0x%llx", ULL(d.manager));
                else dbg::line("EVENT PlayerManager lost (was 0x%llx)", ULL(loggedManager));
                loggedManager = d.manager;
            }
            if (d.localPc != loggedLocalPc) {
                if (d.localPc) dbg::line("EVENT local player at 0x%llx", ULL(d.localPc));
                else dbg::line("EVENT local player lost (was 0x%llx)", ULL(loggedLocalPc));
                loggedLocalPc = d.localPc;
            }
            if (d.nativeCam != loggedNative) {
                if (d.nativeCam) dbg::line("EVENT native camera at 0x%llx", ULL(d.nativeCam));
                else dbg::line("EVENT native camera lost (was 0x%llx)", ULL(loggedNative));
                loggedNative = d.nativeCam;
            }
            const std::string step = res.ready ? std::string("ready") : stepOf(res.missing);
            if (step != loggedStep) {
                if (res.ready) dbg::line("EVENT ready: %s", res.info.c_str());
                else dbg::line("EVENT missing: %s", oneLine(res.missing).c_str());
                loggedStep = step;
                lastDump = -1e9;   // a new step is dumped at once
            }
            const double dumpPeriod = res.ready ? kDumpPeriodReady : kDumpPeriodMissing;
            if (now - lastDump >= dumpPeriod) {
                dumpBlocks(proc, d, res.ready ? "ready sample" : "missing");
                lastDump = now;
            }
            if (now - lastDetail >= kDetailPeriod) {
                lastDetail = now;
                dbg::line("DETAIL chain: mgr 0x%llx lpc 0x%llx movement 0x%llx translation 0x%llx "
                          "mainCam 0x%llx camera 0x%llx native 0x%llx",
                          ULL(d.manager), ULL(d.localPc), ULL(d.movement), ULL(d.translation), ULL(d.mainCam),
                          ULL(d.camera), ULL(d.nativeCam));
                dbg::line("DETAIL local feet (%.3f %.3f %.3f), valid %d", d.localFeet.x, d.localFeet.y,
                          d.localFeet.z, d.haveLocalFeet ? 1 : 0);
                dbg::line("DETAIL camera scan: perspective %d, rigid %d, nearest rigid %.3f m, view %.3f m%s, "
                          "projection aspect %.4f (match %d), screen aspect %.4f",
                          d.perspective, d.rigid, d.nearestRigid, d.viewDist, d.viewFar ? " (far)" : "",
                          d.projAspect, d.projAspectMatch ? 1 : 0, static_cast<double>(W) / H);
                if (d.haveMatrices) {
                    dbg::line("DETAIL view %s", fmtMatrix(d.view).c_str());
                    dbg::line("DETAIL proj %s", fmtMatrix(d.proj).c_str());
                }
                dbg::line("DETAIL other players: teams %s, hp raw %s", fmtTeams(d.otherTeams).c_str(),
                          fmtHp(d.otherHp).c_str());
                if (res.ready) {
                    // Why each of the first three other players is or is not drawn: distance, screen
                    // position (in front of the camera or not) and the primitives the ESP builds for it.
                    const esp::Vec3 cp = res.snapshot.cameraPos;
                    const float fw = static_cast<float>(W);
                    const float fh = static_cast<float>(H);
                    dbg::line("DETAIL camera position (%.3f %.3f %.3f), overlay %dx%d, players %zu", cp.x, cp.y,
                              cp.z, W, H, res.snapshot.players.size());
                    size_t shown = 0;
                    for (const esp::Player& pl : res.snapshot.players) {
                        if (pl.local) continue;
                        if (shown == 3) break;
                        ++shown;
                        esp::Snapshot one = res.snapshot;
                        one.players.assign(1, pl);
                        esp::DrawList tmp;
                        esp::buildEsp(one, espConfigFromMenu(), tmp);
                        esp::Vec2 headS{};
                        esp::Vec2 feetS{};
                        const bool headFront = esp::worldToScreen(res.snapshot.viewProj, pl.head, fw, fh, headS, true);
                        const bool feetFront = esp::worldToScreen(res.snapshot.viewProj, pl.feet, fw, fh, feetS, true);
                        dbg::line("DETAIL player 0x%llx: team %d (team filter %s), hp %d (known %d), %.1f m away, "
                                  "head (%.0f %.0f) %s, feet (%.0f %.0f) %s, primitives %zu",
                                  ULL(pl.id), pl.team, res.snapshot.teamFilter ? "on" : "off", pl.hp,
                                  pl.hpKnown ? 1 : 0, static_cast<double>(esp::length(pl.feet - cp)),
                                  static_cast<double>(headS.x), static_cast<double>(headS.y),
                                  headFront ? "in front" : "behind camera", static_cast<double>(feetS.x),
                                  static_cast<double>(feetS.y), feetFront ? "in front" : "behind camera", tmp.size());
                    }
                }
            }
        }

        if (now - lastSummary >= kSummaryPeriod) {
            const double span = now - lastSummary;
            const rise::ScanStats scan = rise::managerScanStats();
            char scanText[160];
            if (scan.active) {
                std::snprintf(scanText, sizeof(scanText), "scan pass %llu, region %zu/%zu, %.1f MB read",
                              ULL(scan.passes), scan.region, scan.regions,
                              static_cast<double>(scan.bytesTotal) / 1048576.0);
            } else {
                std::snprintf(scanText, sizeof(scanText), "scan idle (next pass in %d frames), %llu pass(es), %llu hit(s)",
                              scan.idle, ULL(scan.passes), ULL(scan.hits));
            }
            std::string state = "?";
            if (!landscape) state = "rotated";
            else if (pid <= 0) state = "no game";
            else if (base == 0) state = "no libil2cpp";
            else if (didRead) state = res.ready ? "ready" : oneLine(res.missing);
            const overlay::SwapStats sw = overlay::swapStats();
            const rise::Diag& d = res.diag;
            dbg::line("SUMMARY fps %.0f, frame %.1f/%.1f ms, orient %u, landscape %d, pid %d, base 0x%llx, %s, "
                      "mgr 0x%llx, lpc 0x%llx, feet (%.2f %.2f %.2f), native 0x%llx, drawn %d, "
                      "touch downs %u last (%.0f %.0f), alpha %.2f, menu %d, swap fail %u (egl 0x%x), "
                      "gl err %u (0x%x), state: %s",
                      static_cast<double>(framesInPeriod) / span, frameSum / framesInPeriod, frameMax, orientation,
                      landscape ? 1 : 0, pid, ULL(base), scanText, ULL(d.manager), ULL(d.localPc),
                      d.localFeet.x, d.localFeet.y, d.localFeet.z, ULL(d.nativeCam), drawnPrims, touchDowns,
                      static_cast<double>(touchX), static_cast<double>(touchY), static_cast<double>(alpha),
                      ui::bar::g_open ? 1 : 0, sw.swapFailures, sw.lastEglError, sw.glErrors, sw.lastGlError,
                      state.c_str());
            lastSummary = now;
            frameSum = 0.0;
            frameMax = 0.0;
            framesInPeriod = 0;
            touchDowns = 0;
        }

        if (!touchOk) statusText += "\ntouch: no device in /dev/input, menu disabled";
        if (!statusText.empty()) drawStatus(statusText, static_cast<float>(H));
        overlay::endFrame();
        usleep(8000);
    }

    dbg::line("EXIT quit requested");
    touch::shutdown();
    overlay::shutdown();
    dbg::line("EXIT clean");
    return 0;
}
