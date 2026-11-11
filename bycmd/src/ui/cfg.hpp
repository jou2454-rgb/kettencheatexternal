
// ================================================================================
// Origin: @imguisource_bot
// Community:
//  - ImGui Source: @ImGui_Source_FIXNEY
//  - shit src 🧨: @il2cpp_src
// ================================================================================

#pragma once
#include "imgui.h"

namespace cfg {

namespace esp {
    inline bool box = false;        // reference look: skeleton + bar + name, no box
    inline bool name = true;        // nick above the head
    inline bool health = true;      // bar left of the figure
    inline bool distance = false;
    inline bool weapon = true;      // weapon icon under the feet (weapon.hpp)
    inline bool skeleton = true;    // bone lines from the player transform hierarchy (skeleton.hpp)
    inline ImVec4 skeleton_col = ImVec4(240/255.f, 64/255.f, 90/255.f, 1.f);
    inline int box_type = 0;
    inline float box_rounding = 0.f;
    // Interpolate enemy positions between game frames (one game frame of
    // delay, no stepping when the overlay runs faster than the game).
    inline bool smooth = true;
    // cloudhack default: red (240,64,90); the menu Color row drives box/health/distance/weapon.
    inline ImVec4 box_col = ImVec4(240/255.f, 64/255.f, 90/255.f, 1.f);
    inline ImVec4 name_col = ImVec4(1.f, 1.f, 1.f, 1.f);
    inline ImVec4 health_col = ImVec4(240/255.f, 64/255.f, 90/255.f, 1.f);
    inline ImVec4 distance_col = ImVec4(240/255.f, 64/255.f, 90/255.f, 1.f);
    inline ImVec4 weapon_col = ImVec4(240/255.f, 64/255.f, 90/255.f, 1.f);
}

}


