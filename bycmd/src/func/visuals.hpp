
// ================================================================================
// -----------------[ TELEGRAM SOURCE ]-----------------
// * BOT:     t.me/imguisource_bot
// * CHANNEL: t.me/ImGui_Source_FIXNEY (ImGui Source)
// * CH 2:    t.me/il2cpp_src (shit src 🧨)
// -----------------------------------------------------
// ================================================================================

#pragma once

#include "imgui.h"

namespace visuals {
    void draw();
    void dbox(const ImVec2& t, const ImVec2& b, float a);
    void dhp(int hp, int max_hp, const ImVec2& t, const ImVec2& b, float box_h, float a);
    void dnick(const char* name, const ImVec2& box_max, float cx, float sz, float a);
    void ddist(float dist, float x, float y, float sz, float a);
    // Weapon icon glyph centred under the box (below the name when shown).
    void dweapon(char glyph, int id, float cx, float y, float sz, float max_w, float a);
    void draw_text_outlined(ImDrawList* dl, ImFont* font, float size, const ImVec2& pos, ImU32 color, const char* text);
}
