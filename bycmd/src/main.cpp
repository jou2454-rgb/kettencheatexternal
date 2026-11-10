
// ================================================================================
// Package distributed via @imguisource_bot
// Channel ImGui Source: @ImGui_Source_FIXNEY
// Channel shit src 🧨: @il2cpp_src
// ================================================================================

#include "Android_draw/draw.h"
#include "ui/theme/theme.hpp"
#include "ui/menu.hpp"
#include "ui/bar.hpp"
#include "other/memory.hpp"
#include "other/status.hpp"
#include "game/game.hpp"
#include "game/camera.hpp"
#include "func/visuals.hpp"
#include "protect/oxorany.hpp"
#include <cstdio>
#include <cstring>
#include <csignal>
#include <algorithm>
#include <string>
#include <atomic>
#include <thread>
#include <chrono>
#include <sys/file.h>
#include <sys/system_properties.h>
#include <fcntl.h>
#include <unistd.h>

static std::atomic<bool> g_quit{false};

static void on_signal(int) { g_quit = true; }

static void print_status(const char* status) {
    diag::log("STATUS", "%s", status);
}

struct options {
    bool diag_only = false;
    bool no_launch = false;
    bool skip_build_check = false;
    bool no_lock = false;
    const char* log_path = "/data/local/tmp/bycmd.log";
};

static void usage() {
    std::printf("bycmd Standoff2 1.1.0 external ESP\n"
                "  --diag              probe overlay/libgui/EGL/process and exit\n"
                "  --no-launch         do not start the game package\n"
                "  --skip-build-check  run even if libunity build-id does not match the profile\n"
                "  --no-lock           ignore the single-instance lock\n"
                "  --log <path>        log file (default /data/local/tmp/bycmd.log)\n");
}

static std::string sys_prop(const char* name) {
    char value[PROP_VALUE_MAX + 1] = {0};
    const int len = __system_property_get(name, value);
    return len > 0 ? std::string(value, size_t(len)) : std::string();
}

// Function that starts the game package.
static void launch_standoff() {
    int rc = system(oxorany("monkey -p com.axlebolt.standoff2 -c android.intent.category.LAUNCHER 1 >/dev/null 2>&1"));
    diag::log(rc == 0 ? "INFO" : "WARN", "game launch command exit=%d package=com.axlebolt.standoff2", rc);
}

static int acquire_lock(const options& opts) {
    if (opts.no_lock) return -1;
    const char* paths[] = {"/data/local/tmp/bycmd.lock", "./bycmd.lock", "/data/local/tmp/.bycmd.lock"};
    for (const char* path : paths) {
        const int fd = ::open(path, O_CREAT | O_RDWR | O_CLOEXEC, 0600);
        if (fd < 0) {
            diag::log("WARN", "lock file %s unavailable errno=%d", path, errno);
            continue;
        }
        if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
            diag::log("ERROR", "another bycmd instance already owns %s; refusing duplicate menu", path);
            ::close(fd);
            return -2;
        }
        diag::log("INFO", "instance lock %s", path);
        return fd;
    }
    diag::log("WARN", "no writable lock location; continuing without single-instance protection");
    return -1;
}

int main(int argc, char** argv) {
    options opts;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--diag")) opts.diag_only = true;
        else if (!std::strcmp(argv[i], "--no-launch")) opts.no_launch = true;
        else if (!std::strcmp(argv[i], "--skip-build-check")) opts.skip_build_check = true;
        else if (!std::strcmp(argv[i], "--no-lock")) opts.no_lock = true;
        else if (!std::strcmp(argv[i], "--log") && i + 1 < argc) opts.log_path = argv[++i];
        else if (!std::strcmp(argv[i], "--help") || !std::strcmp(argv[i], "-h")) { usage(); return 0; }
        else { std::printf("unknown option %s\n", argv[i]); usage(); return 1; }
    }
    proc::skip_build_check = opts.skip_build_check;

    const int stdout_flags = ::fcntl(STDOUT_FILENO, F_GETFL, 0);
    if (stdout_flags >= 0) ::fcntl(STDOUT_FILENO, F_SETFL, stdout_flags | O_NONBLOCK);
    if (!diag::open_log(opts.log_path) && !diag::open_log("./bycmd.log"))
        diag::log("WARN", "log file unavailable (%s), stdout/logcat only", opts.log_path);
    else
        diag::log("INFO", "log file %s", diag::g_log_path);

    ::signal(SIGPIPE, SIG_IGN);
    ::signal(SIGINT, on_signal);
    ::signal(SIGTERM, on_signal);
    ::signal(SIGHUP, on_signal);

    diag::log("INFO", "bycmd Standoff2 1.1.0 external ESP starting profile=%s", v100::profile_name);
    diag::log("INFO", "device model=%s manufacturer=%s android=%s sdk=%s abi=%s pagesize=%ld uid=%d euid=%d",
              sys_prop("ro.product.model").c_str(), sys_prop("ro.product.manufacturer").c_str(),
              sys_prop("ro.build.version.release").c_str(), sys_prop("ro.build.version.sdk").c_str(),
              sys_prop("ro.product.cpu.abi").c_str(), ::sysconf(_SC_PAGESIZE), int(getuid()), int(geteuid()));
    if (getuid() != 0)
        diag::log("WARN", "not running as root: process_vm_readv on the game and the SurfaceFlinger overlay will most likely be refused");

    const int instance_lock = acquire_lock(opts);
    if (instance_lock == -2) return 2;

    // ---- display ----
    for (int attempt = 0; attempt < 30; ++attempt) {
        screen_config();
        if (displayInfo.width > 0 && displayInfo.height > 0) break;
        usleep(100000);
    }
    if (displayInfo.width <= 0 || displayInfo.height <= 0) {
        diag::log("ERROR", "display info unavailable width=%d height=%d orientation=%u (SurfaceComposerClient display API failed on this Android build)",
                  displayInfo.width, displayInfo.height, unsigned(displayInfo.orientation));
        if (!opts.diag_only) return -1;
    }

    int max_size = (displayInfo.height > displayInfo.width ? displayInfo.height : displayInfo.width);
    int min_size = (displayInfo.height < displayInfo.width ? displayInfo.height : displayInfo.width);
    if (max_size < 2 || min_size < 2) {
        diag::log("ERROR", "invalid display size %dx%d", displayInfo.width, displayInfo.height);
        if (!opts.diag_only) return -1;
    }

    g_sw = static_cast<float>(max_size);
    g_sh = static_cast<float>(min_size);

    native_window_screen_x = max_size;
    native_window_screen_y = max_size;

    diag::log("INFO", "display width=%d height=%d orientation=%d overlay=%dx%d",
              displayInfo.width, displayInfo.height, displayInfo.orientation,
              native_window_screen_x, native_window_screen_y);

    if (opts.diag_only) {
        // Probe everything that "it does not open" can mean, then exit.
        const bool gui = displayInfo.width > 0 && initGUI_draw(native_window_screen_x, native_window_screen_y, true);
        diag::log(gui ? "INFO" : "ERROR", "overlay+EGL+ImGui init %s", gui ? "OK" : "FAILED");
        const bool touch_ok = touch::init(max_size, min_size, (uint8_t)displayInfo.orientation);
        diag::log(touch_ok ? "INFO" : "WARN", "touch devices %s", touch_ok ? "found" : "none readable in /dev/input");
        const int pid = get_pid();
        diag::log(pid > 0 ? "INFO" : "WARN", "game pid=%d", pid);
        if (pid > 0) {
            proc::pid = pid;
            init_mem();
            proc::lib = get_lib();
            diag::log(proc::lib ? "INFO" : "WARN", "libunity base=0x%llx", (unsigned long long)proc::lib);
            if (proc::lib) {
                for (int i = 0; i < 40 && !proc::build_checked; ++i) { game::verify_build(); usleep(100000); }
                diag::log(proc::build_ok ? "INFO" : "ERROR", "%s", status::build);
            }
        }
        if (gui) shutdown();
        diag::log("INFO", "diag done, log=%s", diag::g_log_path);
        return gui ? 0 : 3;
    }

    if (!initGUI_draw(native_window_screen_x, native_window_screen_y, true)) {
        diag::log("ERROR", "initGUI_draw failed (overlay surface / EGL / fonts)");
        return -1;
    }
    diag::log("INFO", "GUI initialized");

    if (!touch::init(max_size, min_size, (uint8_t)displayInfo.orientation))
        diag::log("WARN", "no touch device readable in /dev/input; menu cannot be operated");

    print_status(oxorany("Game detect ..."));
    if (!opts.no_launch && get_pid() <= 0) launch_standoff();

    game::init();
    scanner::start();

    static float alpha = 0.f;
    static bool prev = false;
    static bool game_started = false;

    // Frame pacing ("late latch"): eglSwapBuffers returns right after a
    // vsync in steady state. Sleeping most of the period and reading the
    // game memory as late as possible makes the boxes use coordinates that
    // are only a few ms old when SurfaceFlinger composes the frame.
    double period_ms = 16.6, work_ms = 4.0;
    long long last_swap = diag::now_ms();

    while (!g_quit) {
        const long long frame_start = diag::now_ms();
        drawBegin();

        bool run = game::valid();
        if (run && !proc::build_checked) game::verify_build();
        const bool esp_allowed = run && proc::lib != 0 && proc::build_ok;

#if defined(__x86_64__)
        bool is_landscape = (displayInfo.orientation == 0 || displayInfo.orientation == 2);
#else
        bool is_landscape = (displayInfo.orientation == 1 || displayInfo.orientation == 3);
#endif

        if (run && !prev) {
            if (!game_started) {
                print_status(oxorany("Game detect ✅"));
                print_status(oxorany("start cheat...."));
                game_started = true;
            } else {
                print_status(oxorany("Game detect ✅"));
            }
            prev = true;
        } else if (!run && prev) {
            print_status(oxorany("game closed"));
            prev = false;
            game_started = false;
            cam::reset();
            g_camera = 0;
        }

        if (is_landscape) {
            ImGuiIO& io = ImGui::GetIO();
            float dt = io.DeltaTime;
            if (dt <= 0.f || dt > 0.1f) dt = 0.016f;

            float target = run ? 1.f : 0.f;
            float spd = run ? 4.f : 6.f;

            if (alpha < target) {
                alpha += dt * spd;
                if (alpha > target) alpha = target;
            } else if (alpha > target) {
                alpha -= dt * spd;
                if (alpha < target) alpha = target;
            }

            ui::bar::set_game_alpha(alpha);

            if (alpha > 0.001f) {
                ui::menu::render();
            }

            if (esp_allowed) {
                // Library health is independent from asynchronous registry discovery.
                game::check_lib(proc::lib);
                visuals::draw();
            } else if (run && proc::build_checked && !proc::build_ok) {
                static long long mismatch_log = 0;
                if (diag::every(mismatch_log, 10000))
                    diag::log("ERROR", "ESP disabled: libunity build mismatch (%s). Use --skip-build-check only for diagnostics.", proc::build_id);
            }
        }

        const long long before_swap = diag::now_ms();
        drawEnd();
        const long long after_swap = diag::now_ms();

        work_ms = work_ms * 0.9 + double(before_swap - frame_start) * 0.1;
        const double swap_period = double(after_swap - last_swap);
        last_swap = after_swap;
        if (swap_period > 3.0 && swap_period < 50.0) period_ms = period_ms * 0.9 + swap_period * 0.1;
        double sleep_ms = period_ms - work_ms - 2.5;
        if (ui::bar::g_open) sleep_ms = std::min(sleep_ms, 2.0);
        if (!run) sleep_ms = std::max(sleep_ms, 8.0);
        if (sleep_ms > 0.5) usleep(useconds_t(sleep_ms * 1000.0));
    }

    diag::log("INFO", "shutting down");
    scanner::stop();
    touch::shutdown();
    shutdown();
    if (instance_lock >= 0) ::close(instance_lock);
    diag::close_log();
    return 0;
}
