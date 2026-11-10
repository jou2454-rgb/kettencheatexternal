
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
#include "other/crash.hpp"
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
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/system_properties.h>
#include <fcntl.h>
#include <unistd.h>

// Process exit codes understood by the supervisor.
enum : int {
    exit_ok = 0,          // clean exit (menu Exit, SIGTERM)
    exit_duplicate = 2,   // another instance owns the lock: do not restart
    exit_display = 3,     // overlay surface / EGL lost: restart immediately
    exit_init = 4,        // display / GUI init failed: retry a few times
};

static std::atomic<bool> g_quit{false};
static pid_t g_child = 0;

static void on_signal(int) { g_quit = true; }
static void on_parent_signal(int sig) {
    g_quit = true;
    if (g_child > 0) ::kill(g_child, sig);
}

static void print_status(const char* status) {
    diag::log("STATUS", "%s", status);
}

struct options {
    bool diag_only = false;
    bool no_launch = false;
    bool skip_build_check = false;
    bool no_lock = false;
    bool no_supervisor = false;
    const char* log_path = "/data/local/tmp/bycmd.log";
    const char* crash_log_path = "/data/local/tmp/bycmd-crash.log";
};

// Shared between supervisor and overlay child (MAP_SHARED anonymous page).
struct heartbeat {
    std::atomic<long long> last_ms;
    std::atomic<unsigned long long> frame;
    std::atomic<int> phase;      // 0 = working, 1 = blocked in eglSwapBuffers
    std::atomic<int> first_frame;
};
static heartbeat* g_hb = nullptr;

static void usage() {
    std::printf("bycmd Standoff2 1.1.0 external ESP\n"
                "  --diag              probe overlay/EGL/process and exit\n"
                "  --no-launch         do not start the game package\n"
                "  --skip-build-check  run even if libunity build-id does not match the profile\n"
                "  --no-lock           ignore the single-instance lock\n"
                "  --no-supervisor     run the overlay in this process (no auto-restart)\n"
                "  --log <path>        log file (default /data/local/tmp/bycmd.log)\n"
                "  --crash-log <path>  crash record file (default /data/local/tmp/bycmd-crash.log)\n");
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

// ImGui assertion hook (imconfig.h): log instead of silently continuing.
void bycmd_imgui_assert_fail(const char* expr, const char* file, int line) {
    static long long last = 0;
    static unsigned count = 0;
    ++count;
    if (diag::every(last, 2000))
        diag::log("ERROR", "ImGui assertion failed: %s at %s:%d (count=%u stage=%s)", expr, file, line, count, crash::g_stage);
}

static void beat(int phase) {
    if (!g_hb) return;
    g_hb->last_ms.store(diag::now_ms());
    g_hb->phase.store(phase);
}

// ---------------------------------------------------------------------------
// The overlay itself. Runs in the child process under the supervisor (or in
// the main process with --no-supervisor / --diag).
// ---------------------------------------------------------------------------
static int run_overlay(const options& opts) {
    proc::skip_build_check = opts.skip_build_check;
    crash::install(opts.crash_log_path);
    crash::stage("startup");

    ::signal(SIGPIPE, SIG_IGN);
    ::signal(SIGINT, on_signal);
    ::signal(SIGTERM, on_signal);
    ::signal(SIGHUP, on_signal);

    diag::log("INFO", "bycmd Standoff2 1.1.0 external ESP starting profile=%s build=%s pid=%d", v100::profile_name, BYCMD_BUILD_ID, int(getpid()));
    diag::log("INFO", "device model=%s manufacturer=%s android=%s sdk=%s abi=%s pagesize=%ld uid=%d euid=%d",
              sys_prop("ro.product.model").c_str(), sys_prop("ro.product.manufacturer").c_str(),
              sys_prop("ro.build.version.release").c_str(), sys_prop("ro.build.version.sdk").c_str(),
              sys_prop("ro.product.cpu.abi").c_str(), ::sysconf(_SC_PAGESIZE), int(getuid()), int(geteuid()));
    if (getuid() != 0)
        diag::log("WARN", "not running as root: process_vm_readv on the game and the SurfaceFlinger overlay will most likely be refused");

    const int instance_lock = acquire_lock(opts);
    if (instance_lock == -2) return exit_duplicate;

    // ---- display ----
    crash::stage("display");
    for (int attempt = 0; attempt < 30; ++attempt) {
        screen_config();
        if (displayInfo.width > 0 && displayInfo.height > 0) break;
        usleep(100000);
    }
    if (displayInfo.width <= 0 || displayInfo.height <= 0) {
        diag::log("ERROR", "display info unavailable width=%d height=%d orientation=%u (SurfaceComposerClient display API failed on this Android build)",
                  displayInfo.width, displayInfo.height, unsigned(displayInfo.orientation));
        if (!opts.diag_only) return exit_init;
    }

    int max_size = (displayInfo.height > displayInfo.width ? displayInfo.height : displayInfo.width);
    int min_size = (displayInfo.height < displayInfo.width ? displayInfo.height : displayInfo.width);
    if (max_size < 2 || min_size < 2) {
        diag::log("ERROR", "invalid display size %dx%d", displayInfo.width, displayInfo.height);
        if (!opts.diag_only) return exit_init;
    }

    g_sw = static_cast<float>(max_size);
    g_sh = static_cast<float>(min_size);

    // Landscape-sized surface (e.g. 2720x1224) instead of the template's
    // max x max square (2720x2720): halves the pixels the GPU clears/blends
    // and SurfaceFlinger composites every frame. ESP only runs in landscape;
    // in portrait the right part of the overlay is simply off-screen.
    native_window_screen_x = max_size;
    native_window_screen_y = min_size;

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
        return gui ? exit_ok : exit_init;
    }

    crash::stage("gui-init");
    if (!initGUI_draw(native_window_screen_x, native_window_screen_y, true)) {
        diag::log("ERROR", "initGUI_draw failed (overlay surface / EGL / fonts)");
        return exit_init;
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
    double period_ms = 8.3, work_ms = 2.0, margin_ms = 2.5;
    long long last_swap_us = diag::now_us();
    unsigned frames = 0, missed = 0;
    long long last_pace_report = diag::now_ms();
    // Display period = median of the last 32 swap intervals (robust against
    // missed vsyncs and against a wrong initial guess on 60/90/144 Hz panels).
    double periods[32] = {0};
    unsigned period_index = 0;
    bool periods_full = false;
    unsigned swap_failures_seen = 0;

    int exit_code = exit_ok;
    while (!g_quit && !status::g_quit_request) {
        const long long frame_start_us = diag::now_us();
        crash::g_frame.fetch_add(1);
        beat(0);
        crash::stage("frame-begin");
        drawBegin();

        crash::stage("process");
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
                crash::stage("menu");
                ui::menu::render();
            }

            if (esp_allowed) {
                // Library health is independent from asynchronous registry discovery.
                game::check_lib(proc::lib);
                crash::stage("esp");
                visuals::draw();
            } else if (run && proc::build_checked && !proc::build_ok) {
                static long long mismatch_log = 0;
                if (diag::every(mismatch_log, 10000))
                    diag::log("ERROR", "ESP disabled: libunity build mismatch (%s). Use --skip-build-check only for diagnostics.", proc::build_id);
            }
        }

        crash::stage("render");
        const long long before_swap_us = diag::now_us();
        beat(1);
        drawEnd();
        beat(0);
        const long long after_swap_us = diag::now_us();
        if (g_hb && !g_hb->first_frame.load()) g_hb->first_frame.store(1);

        // Lost surface / context: the compositor can drop our layer when the
        // display configuration changes. Restart through the supervisor
        // instead of rendering into the void forever.
        if (g_swap_failures != swap_failures_seen) {
            swap_failures_seen = g_swap_failures;
            if (g_swap_consecutive_failures >= 120) {
                diag::log("ERROR", "eglSwapBuffers failed %u times in a row (last EGL error 0x%x); restarting overlay",
                          g_swap_consecutive_failures, g_swap_last_error);
                exit_code = exit_display;
                break;
            }
        }

        // eglSwapBuffers returns once the previous buffer was consumed at a
        // vsync, so the interval between returns is the display period
        // (8.3 ms at 120 Hz, 6.9 ms at 144 Hz). Sleep period - work - margin
        // and read the game as late as possible; the margin adapts to the
        // observed miss rate (a return after > 1.6 periods == missed vsync).
        work_ms = work_ms * 0.9 + double(before_swap_us - frame_start_us) / 1000.0 * 0.1;
        const double swap_period = double(after_swap_us - last_swap_us) / 1000.0;
        last_swap_us = after_swap_us;
        ++frames;
        if (swap_period > 2.0 && swap_period < 50.0) {
            if (swap_period > period_ms * 1.6 && run) ++missed;
            periods[period_index] = swap_period;
            period_index = (period_index + 1) % 32;
            if (period_index == 0) periods_full = true;
            if (periods_full && period_index % 8 == 0) {
                double sorted[32];
                std::memcpy(sorted, periods, sizeof(sorted));
                std::sort(sorted, sorted + 32);
                period_ms = sorted[15];
            }
        }
        if (diag::now_ms() - last_pace_report > 2000) {
            const double miss_rate = frames ? double(missed) / double(frames) : 0.0;
            if (miss_rate > 0.03) margin_ms = std::min(5.0, margin_ms + 0.5);
            else if (miss_rate < 0.005) margin_ms = std::max(1.5, margin_ms - 0.1);
            static long long pace_log = 0;
            if (run && diag::every(pace_log, 10000))
                diag::log("PACE", "period=%.2fms work=%.2fms margin=%.1fms missed=%u/%u", period_ms, work_ms, margin_ms, missed, frames);
            frames = missed = 0;
            last_pace_report = diag::now_ms();
        }
        crash::stage("sleep");
        double sleep_ms = period_ms - work_ms - margin_ms;
        if (ui::bar::g_open) sleep_ms = std::min(sleep_ms, 2.0);
        if (!run) sleep_ms = std::max(sleep_ms, 8.0);
        if (sleep_ms > 0.3) usleep(useconds_t(sleep_ms * 1000.0));
    }

    crash::stage("shutdown");
    diag::log("INFO", "shutting down (code=%d)", exit_code);
    scanner::stop();
    touch::shutdown();
    shutdown();
    if (instance_lock >= 0) ::close(instance_lock);
    return exit_code;
}

// ---------------------------------------------------------------------------
// Supervisor: forks the overlay, restarts it after a crash (signal), a lost
// display (exit_display) or an init failure (exit_init), and kills+restarts
// it when the heartbeat stops outside eglSwapBuffers. A clean exit or a
// duplicate-instance exit ends the supervisor too. Crash loops are capped.
// ---------------------------------------------------------------------------
static int supervise(const options& opts) {
    g_hb = static_cast<heartbeat*>(::mmap(nullptr, sizeof(heartbeat), PROT_READ | PROT_WRITE,
                                          MAP_SHARED | MAP_ANONYMOUS, -1, 0));
    if (g_hb == MAP_FAILED) {
        diag::log("WARN", "supervisor: shared heartbeat unavailable, running without supervisor");
        g_hb = nullptr;
        return run_overlay(opts);
    }
    ::signal(SIGINT, on_parent_signal);
    ::signal(SIGTERM, on_parent_signal);
    ::signal(SIGHUP, on_parent_signal);
    ::signal(SIGPIPE, SIG_IGN);

    long long restart_times[8] = {0};
    int restart_count = 0;
    int init_failures = 0;
    diag::log("INFO", "supervisor pid=%d build=%s", int(getpid()), BYCMD_BUILD_ID);

    while (!g_quit) {
        g_hb->last_ms.store(diag::now_ms());
        g_hb->frame.store(0);
        g_hb->phase.store(0);
        g_hb->first_frame.store(0);
        const pid_t child = ::fork();
        if (child < 0) {
            diag::log("ERROR", "supervisor: fork failed errno=%d; running inline", errno);
            return run_overlay(opts);
        }
        if (child == 0) {
            ::signal(SIGINT, SIG_DFL);
            ::signal(SIGTERM, SIG_DFL);
            ::signal(SIGHUP, SIG_DFL);
            const int code = run_overlay(opts);
            diag::close_log();
            // Skip static destructors: detached touch threads may still be
            // inside ImGui/io structures that destructors would free.
            _exit(code);
        }
        g_child = child;
        diag::log("INFO", "supervisor: overlay child pid=%d", int(child));

        int status = 0;
        bool exited = false;
        long long swap_block_logged = 0;
        while (!g_quit) {
            const pid_t r = ::waitpid(child, &status, WNOHANG);
            if (r == child) { exited = true; break; }
            if (r < 0 && errno != EINTR) { exited = true; status = 0; break; }
            usleep(200000);
            const long long age = diag::now_ms() - g_hb->last_ms.load();
            if (age > 30000 && g_hb->first_frame.load()) {
                if (g_hb->phase.load() == 1) {
                    // Blocked in eglSwapBuffers: screen off or compositor
                    // paused our layer. Not a hang; wait.
                    if (diag::every(swap_block_logged, 60000))
                        diag::log("WARN", "supervisor: overlay blocked in eglSwapBuffers for %lld ms (display off?)", age);
                    continue;
                }
                diag::log("ERROR", "supervisor: overlay hung for %lld ms (frame=%llu), killing", age, g_hb->frame.load());
                ::kill(child, SIGKILL);
                ::waitpid(child, &status, 0);
                exited = true;
                break;
            }
        }
        g_child = 0;
        if (g_quit && !exited) {
            ::kill(child, SIGTERM);
            for (int i = 0; i < 50 && ::waitpid(child, &status, WNOHANG) == 0; ++i) usleep(100000);
            if (::waitpid(child, &status, WNOHANG) == 0) ::kill(child, SIGKILL);
            break;
        }

        int delay_ms = 1000;
        if (WIFEXITED(status)) {
            const int code = WEXITSTATUS(status);
            if (code == exit_ok || code == exit_duplicate) {
                diag::log("INFO", "supervisor: overlay exited code=%d, done", code);
                return code;
            }
            if (code == exit_display) {
                diag::log("WARN", "supervisor: overlay lost its surface, restarting");
                delay_ms = 500;
            } else if (code == exit_init) {
                if (++init_failures > 10) {
                    diag::log("ERROR", "supervisor: overlay failed to initialise %d times, giving up", init_failures);
                    return code;
                }
                diag::log("WARN", "supervisor: overlay init failed (%d/10), retrying in 3 s", init_failures);
                delay_ms = 3000;
            } else {
                diag::log("WARN", "supervisor: overlay exited code=%d, restarting", code);
            }
        } else if (WIFSIGNALED(status)) {
            diag::log("ERROR", "supervisor: overlay died from signal %d%s, restarting", WTERMSIG(status),
                      WCOREDUMP(status) ? " (core)" : "");
        } else {
            diag::log("WARN", "supervisor: overlay ended (status=0x%x), restarting", status);
        }

        // Crash-loop guard: more than 8 restarts within 2 minutes means the
        // fault is deterministic and restarting only hides it.
        const long long now = diag::now_ms();
        restart_times[restart_count % 8] = now;
        ++restart_count;
        if (restart_count >= 8 && now - restart_times[restart_count % 8] < 120000) {
            diag::log("ERROR", "supervisor: %d restarts in 2 minutes, giving up; see %s", restart_count, opts.crash_log_path);
            return 5;
        }
        for (int waited = 0; waited < delay_ms && !g_quit; waited += 100) usleep(100000);
    }
    return exit_ok;
}

int main(int argc, char** argv) {
    options opts;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--diag")) opts.diag_only = true;
        else if (!std::strcmp(argv[i], "--no-launch")) opts.no_launch = true;
        else if (!std::strcmp(argv[i], "--skip-build-check")) opts.skip_build_check = true;
        else if (!std::strcmp(argv[i], "--no-lock")) opts.no_lock = true;
        else if (!std::strcmp(argv[i], "--no-supervisor")) opts.no_supervisor = true;
        else if (!std::strcmp(argv[i], "--log") && i + 1 < argc) opts.log_path = argv[++i];
        else if (!std::strcmp(argv[i], "--crash-log") && i + 1 < argc) opts.crash_log_path = argv[++i];
        else if (!std::strcmp(argv[i], "--help") || !std::strcmp(argv[i], "-h")) { usage(); return 0; }
        else { std::printf("unknown option %s\n", argv[i]); usage(); return 1; }
    }

    const int stdout_flags = ::fcntl(STDOUT_FILENO, F_GETFL, 0);
    if (stdout_flags >= 0) ::fcntl(STDOUT_FILENO, F_SETFL, stdout_flags | O_NONBLOCK);
    // The log is truncated once here; overlay children append to it, so a
    // crash record and the restart that follows are in the same file.
    if (!diag::open_log(opts.log_path, true) && !diag::open_log("./bycmd.log", true))
        diag::log("WARN", "log file unavailable (%s), stdout/logcat only", opts.log_path);
    else
        diag::log("INFO", "log file %s", diag::g_log_path);

    if (opts.diag_only || opts.no_supervisor) {
        const int code = run_overlay(opts);
        diag::close_log();
        _exit(code);
    }
    const int code = supervise(opts);
    diag::close_log();
    _exit(code);
}
