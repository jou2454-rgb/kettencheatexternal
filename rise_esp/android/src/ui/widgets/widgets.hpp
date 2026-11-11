
// ================================================================================
// Official Telegram Bot: t.me/imguisource_bot
// Official Channels:
// 1. ImGui Source -> t.me/ImGui_Source_FIXNEY
// 2. shit src 🧨 -> t.me/il2cpp_src
// ================================================================================

#pragma once
#include "../theme/theme.hpp"
#include <vector>
#include <functional>

namespace ui::widgets {

bool checkbox(const char* name, bool* v, float a);
bool slider(const char* name, float* v, float mn, float mx, float a, const char* fmt = "%.1f");
bool combo(const char* name, int* v, const std::vector<const char*>& items, float a);
void colorpick(const char* name, ImVec4* v, float a);
void separator(float a);
bool button(const char* name, float a, std::function<void()> on_click = nullptr);

}
