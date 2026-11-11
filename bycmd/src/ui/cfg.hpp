
// ================================================================================
// Origin: @imguisource_bot
// Community:
//  - ImGui Source: @ImGui_Source_FIXNEY
//  - shit src 🧨: @il2cpp_src
// ================================================================================

#pragma once
#include "imgui.h"

namespace cfg {

namespace menu {
    inline float scale_x = 1.f;                // panel width scale, set by the grip in the bottom-right corner
    inline float scale_y = 1.f;                // panel height scale
}

namespace esp {
    inline bool box = true;
    inline bool name = false;
    inline bool health = true;
    inline bool weapon = true;
    inline int box_type = 0;
    inline float box_rounding = 0.f;
    // Interpolate enemy positions between game frames (one game frame of
    // delay, no stepping when the overlay runs faster than the game).
    inline bool smooth = true;
    // cloudhack default: red (240,64,90); the menu Color row drives box/health.
    inline ImVec4 box_col = ImVec4(240/255.f, 64/255.f, 90/255.f, 1.f);
    inline ImVec4 name_col = ImVec4(1.f, 1.f, 1.f, 1.f);
    inline ImVec4 health_col = ImVec4(240/255.f, 64/255.f, 90/255.f, 1.f);
}

}


