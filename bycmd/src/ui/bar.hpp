// ================================================================================
// Watermark "cloudhack" (top-left). Tapping it opens / closes the menu.
// ================================================================================

#pragma once
#include "imgui.h"

namespace ui::bar {
    inline bool g_open = false;          // menu requested open
    inline float g_alpha = 0.f;          // eased 0..1 of g_open
    inline float g_game_alpha = 1.f;     // 1 while the game runs, fades to 0 otherwise
    inline ImVec2 g_rect_min, g_rect_max; // watermark rectangle (menu anchors under it)
    inline bool g_dragging = false;       // finger is moving the watermark right now

    void render();
    void set_game_alpha(float a);
    float game_alpha();
}
