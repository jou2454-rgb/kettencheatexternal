// ================================================================================
// Triggerbot: taps the fire button while the screen centre sits on an enemy's
// projected box (same projection and team check as the ESP).
//
// Game memory is only read. The shot is a touch injected through a uinput virtual
// touchscreen that uses the raw range of the real panel (touch::overlay_to_raw).
// Fire button: set from the menu ("set fire button", then tap the button in game
// while the menu is open). Saved to /data/local/tmp/bycmd_fire.txt.
// ================================================================================

#pragma once

#include <cstdint>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <thread>
#include <unistd.h>
#include "Android_touch/Touch.hpp"
#include "../ui/cfg.hpp"
#include "../other/diag.hpp"

namespace triggerbot {
    inline constexpr const char* kFireFile = "/data/local/tmp/bycmd_fire.txt";
    inline constexpr long long kCooldownUs = 120000;   // at most one shot per 120 ms

    inline std::atomic<bool> g_pending{false};
    inline std::atomic<bool> g_worker_started{false};
    inline std::atomic<float> g_req_x{0.f}, g_req_y{0.f};
    inline std::atomic<int> g_uinput_state{0};         // 0 untried, 1 device up, -1 failed
    inline std::atomic<int> g_uinput_errno{0};
    inline long long g_first_us = 0;                   // crosshair landed on the current target
    inline long long g_last_shot_us = 0;
    inline bool g_loaded = false;

    inline void emit(int fd, int type, int code, int value) {
        input_event ev{};
        ev.type = static_cast<__u16>(type);
        ev.code = static_cast<__u16>(code);
        ev.value = value;
        (void) write(fd, &ev, sizeof(ev));
    }

    // Creates the virtual touchscreen. Ranges match the real panel so the game
    // maps injected taps exactly like finger taps.
    inline int open_device(int32_t max_x, int32_t max_y) {
        int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
        if (fd < 0) fd = open("/dev/input/uinput", O_WRONLY | O_NONBLOCK);
        if (fd < 0) { g_uinput_errno = errno; return -1; }
        auto fail = [&]() -> int { g_uinput_errno = errno; close(fd); return -1; };
        if (ioctl(fd, UI_SET_EVBIT, EV_KEY) < 0 || ioctl(fd, UI_SET_KEYBIT, BTN_TOUCH) < 0 ||
            ioctl(fd, UI_SET_EVBIT, EV_ABS) < 0 || ioctl(fd, UI_SET_PROPBIT, INPUT_PROP_DIRECT) < 0)
            return fail();
        const struct { int code; int max; } axes[] = {
            {ABS_MT_SLOT, 9}, {ABS_MT_TRACKING_ID, 65535},
            {ABS_MT_POSITION_X, max_x}, {ABS_MT_POSITION_Y, max_y},
        };
        for (const auto& ax : axes) {
            uinput_abs_setup s{};
            s.code = static_cast<__u16>(ax.code);
            s.absinfo.minimum = 0;
            s.absinfo.maximum = ax.max;
            if (ioctl(fd, UI_ABS_SETUP, &s) < 0) return fail();
        }
        uinput_setup us{};
        us.id.bustype = BUS_VIRTUAL;
        us.id.vendor = 0x1d6b;
        us.id.product = 0x0104;
        std::strncpy(us.name, "bycmd-touch", sizeof(us.name) - 1);
        if (ioctl(fd, UI_DEV_SETUP, &us) < 0 || ioctl(fd, UI_DEV_CREATE) < 0) return fail();
        usleep(300000);   // let the input core publish the node before the first event
        g_uinput_state = 1;
        return fd;
    }

    inline void tap(int fd, int32_t x, int32_t y) {
        static int tracking = 1;
        emit(fd, EV_ABS, ABS_MT_SLOT, 0);
        emit(fd, EV_ABS, ABS_MT_TRACKING_ID, tracking);
        emit(fd, EV_ABS, ABS_MT_POSITION_X, x);
        emit(fd, EV_ABS, ABS_MT_POSITION_Y, y);
        emit(fd, EV_KEY, BTN_TOUCH, 1);
        emit(fd, EV_SYN, SYN_REPORT, 0);
        usleep(35000);
        emit(fd, EV_ABS, ABS_MT_SLOT, 0);
        emit(fd, EV_ABS, ABS_MT_TRACKING_ID, -1);
        emit(fd, EV_KEY, BTN_TOUCH, 0);
        emit(fd, EV_SYN, SYN_REPORT, 0);
        tracking = tracking % 60000 + 1;
    }

    inline void worker() {
        int fd = -1;
        for (;;) {
            if (!g_pending.exchange(false)) { usleep(1000); continue; }
            int32_t rx = 0, ry = 0, mx = 0, my = 0;
            if (!touch::overlay_to_raw(g_req_x.load(), g_req_y.load(), rx, ry, mx, my)) {
                g_uinput_state = -1;
                continue;
            }
            if (fd < 0) {
                fd = open_device(mx, my);
                if (fd < 0) {
                    static long long last_log = 0;
                    if (diag::every(last_log, 5000))
                        diag::log("TRIGGER", "uinput unavailable errno=%d", g_uinput_errno.load());
                    continue;
                }
            }
            tap(fd, rx, ry);
        }
    }

    inline void request(float x, float y) {
        g_req_x = x;
        g_req_y = y;
        g_pending = true;
        if (!g_worker_started.exchange(true)) std::thread(worker).detach();
    }

    // Once per overlay frame. on_target: the crosshair is inside an enemy's box.
    inline void frame(bool on_target, long long now_us) {
        if (!cfg::trigger::enabled || !cfg::trigger::fire_set || !on_target) { g_first_us = 0; return; }
        if (g_first_us == 0) g_first_us = now_us;
        if (now_us - g_first_us < static_cast<long long>(cfg::trigger::delay_ms) * 1000) return;
        if (now_us - g_last_shot_us < kCooldownUs) return;
        g_last_shot_us = now_us;
        request(cfg::trigger::fire_x, cfg::trigger::fire_y);
    }

    inline void save_fire(float x, float y) {
        if (FILE* f = std::fopen(kFireFile, "w")) {
            std::fprintf(f, "%.1f %.1f\n", x, y);
            std::fclose(f);
        }
    }

    inline void load_once() {
        if (g_loaded) return;
        g_loaded = true;
        if (FILE* f = std::fopen(kFireFile, "r")) {
            float x = 0.f, y = 0.f;
            if (std::fscanf(f, "%f %f", &x, &y) == 2) {
                cfg::trigger::fire_x = x;
                cfg::trigger::fire_y = y;
                cfg::trigger::fire_set = true;
            }
            std::fclose(f);
        }
    }
}
