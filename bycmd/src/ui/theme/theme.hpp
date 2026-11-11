// ================================================================================
// cloudhack theme: dark red-black panel, red accent, light text.
// All menu geometry is expressed in "base pixels" for a 1224 px tall landscape
// screen (Infinix GT 30 Pro, 2720x1224) and multiplied by style::k() at draw
// time, so the same layout keeps its physical size on other panels.
// ================================================================================

#pragma once
#include "imgui.h"
#include "imgui_internal.h"
#include <string>
#include <unordered_map>

inline float g_sw, g_sh;

namespace ui {

namespace clr {
    inline ImVec4 bg           = ImVec4(22/255.f, 10/255.f, 12/255.f, 0.94f);   // panel
    inline ImVec4 bg_two       = ImVec4(30/255.f, 14/255.f, 17/255.f, 1.f);
    inline ImVec4 sidebar      = ImVec4(18/255.f, 8/255.f, 10/255.f, 1.f);
    inline ImVec4 panel        = ImVec4(34/255.f, 16/255.f, 20/255.f, 1.f);
    inline ImVec4 widget       = ImVec4(46/255.f, 22/255.f, 27/255.f, 1.f);     // toggle track (off)
    inline ImVec4 track_on     = ImVec4(74/255.f, 30/255.f, 38/255.f, 1.f);     // toggle track (on)
    inline ImVec4 knob_off     = ImVec4(122/255.f, 62/255.f, 72/255.f, 1.f);
    inline ImVec4 accent       = ImVec4(240/255.f, 64/255.f, 90/255.f, 1.f);    // knob / "hack" / button
    inline ImVec4 accent_light = ImVec4(247/255.f, 96/255.f, 118/255.f, 1.f);
    inline ImVec4 accent_dark  = ImVec4(214/255.f, 40/255.f, 76/255.f, 1.f);    // "unload" button
    inline ImVec4 text         = ImVec4(242/255.f, 233/255.f, 235/255.f, 1.f);
    inline ImVec4 text_light   = ImVec4(255/255.f, 255/255.f, 255/255.f, 1.f);
    inline ImVec4 text_dim     = ImVec4(150/255.f, 120/255.f, 126/255.f, 1.f);
    inline ImVec4 border       = ImVec4(255/255.f, 255/255.f, 255/255.f, 0.07f);
    inline ImVec4 border_light = ImVec4(255/255.f, 255/255.f, 255/255.f, 0.14f);
    inline ImVec4 border_dark  = ImVec4(0/255.f, 0/255.f, 0/255.f, 0.55f);
    inline ImVec4 subtab_bg    = ImVec4(20/255.f, 10/255.f, 12/255.f, 1.f);
    inline ImVec4 dot          = ImVec4(255/255.f, 255/255.f, 255/255.f, 0.045f); // panel dot grid
}

namespace style {
    inline std::unordered_map<std::string, float> anims;
    inline std::unordered_map<std::string, ImVec4> anim_colors;
    inline float content_w = 0.f;
    inline float content_alpha = 1.f;
    inline bool popup_open = false;
    inline std::string active_popup = "";
    inline constexpr float S = 2.5f;   // legacy widgets.cpp scale, unused by the cloudhack menu

    // Base layout height is 1224 px (Infinix GT 30 Pro). g_sh is the short
    // side of the display in landscape, set by main() before the first frame.
    inline float k() {
        const float s = g_sh > 1.f ? g_sh / 1224.f : 1.f;
        return ImClamp(s, 0.5f, 3.f);
    }

    void tick();
    float anim(const std::string& id, float tgt, float spd = 12.f);
    ImVec4 anim_col(const std::string& id, const ImVec4& tgt, float spd = 12.f);
    ImU32 col(const ImVec4& c, float a = 1.f);
    bool popup();
    void close();
    void popups();
}

}
