
// ================================================================================
// Сурс взят из бота @imguisource_bot
// Подпишись на ImGui Source: t.me/ImGui_Source_FIXNEY
// Подпишись на shit src 🧨: t.me/il2cpp_src
// ================================================================================

#pragma once

namespace ui::bar {
    inline bool g_open = false;
    inline float g_alpha = 0.f;
    inline float g_game_alpha = 1.f;

    void render();
    void set_game_alpha(float a);
    float game_alpha();
}
