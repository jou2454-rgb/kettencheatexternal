
// ================================================================================
// Telegram bot: t.me/imguisource_bot
// Telegram channel (ImGui Source): t.me/ImGui_Source_FIXNEY
// Telegram channel (shit src 🧨): t.me/il2cpp_src
// ================================================================================

#pragma once

namespace touch {
    bool init(int32_t _screen_w, int32_t _screen_h, uint8_t _orientation);
    void update(int32_t _screen_w, int32_t _screen_h, uint8_t _orientation);
    void updateOrientation(uint8_t _orientation);
    void shutdown();
}

