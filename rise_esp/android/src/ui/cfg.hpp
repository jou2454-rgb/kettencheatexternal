// ================================================================================
// Menu settings of the Rise overlay. The cloudhack menu writes these; the main
// loop copies them into esp::Config every frame.
// ================================================================================

#pragma once
#include "imgui.h"

namespace cfg {

namespace menu {
    inline float scale_x = 1.f;   // panel width scale, set by the grip in the bottom-right corner
    inline float scale_y = 1.f;   // panel height scale
}

namespace esp {
    inline bool box = true;
    inline bool name = true;      // the Rise build has always drawn names
    inline bool health = true;
    // The menu's Color row drives the box and the health bar (one ESP colour).
    inline ImVec4 box_col = ImVec4(240/255.f, 64/255.f, 90/255.f, 1.f);
    inline ImVec4 health_col = ImVec4(240/255.f, 64/255.f, 90/255.f, 1.f);
}

}
