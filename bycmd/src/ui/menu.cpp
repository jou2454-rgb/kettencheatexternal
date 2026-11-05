#include "menu.hpp"
#include "../func/visuals.hpp"
#include <imgui.h>

void DrawMenu() {
    // Вызываем отрисовку ESP Енота каждый кадр на заднем плане
    Visuals::DrawESP();

    // Дальше идет твой стандартный код отрисовки меню
    static bool g_MenuOpen = true; 
    if (g_MenuOpen) {
        ImGui::Begin("Enot Reversed ESP", &g_MenuOpen);
        ImGui::Text("ESP успешно перенесено и адаптировано под Unity 6.");
        ImGui::End();
    }
}
