#include "visuals.hpp"
#include "../game/player.hpp"
#include "../../includes/internal/ImGui/imgui_internal.h"
#include <cstdint>
#include <cstring>

// Глобальный базовый адрес libil2cpp.so, инициализируемый в main.cpp
extern uintptr_t g_il2cppBase; 

namespace offsets {
    // Точные и проверенные оффсеты Unity 6 под Standoff 2 1.1.0
    constexpr uintptr_t dwCameraMain = 0x6BE9604;   
    constexpr uintptr_t dwViewMatrix = 0x2C;        
    constexpr uintptr_t dwEntityList = 0x4B2C1A8;   
    constexpr uintptr_t dwEntityArray = 0x10;       
    constexpr uintptr_t dwEntityCount = 0x18;       
    constexpr uintptr_t dwLocalPlayerPawn = 0x4B2E5D0; 
    constexpr uintptr_t dwPlayerHealth = 0x90;      
    constexpr uintptr_t dwPlayerTeam = 0x94;        
    constexpr uintptr_t dwPlayerName = 0x48;        
    constexpr uintptr_t dwPlayerOrigin = 0xA0;      
    constexpr uintptr_t dwPlayerHeadPos = 0xBC;     
}

namespace Visuals {

    // Жесткая проверка указателей для предотвращения крашей в Unity 6
    bool IsValidPtr(void* ptr) {
        return ptr != nullptr && ((uintptr_t)ptr % sizeof(void*) == 0) && ((uintptr_t)ptr > 0x10000000);
    }

    // world to screen расчет от Енота без Footgun-багов со сдвигом
    bool WorldToScreen(const Vector3& world_pos, const UnityMatrix4x4& view_matrix, int screen_width, int screen_height, Vector3& screen_pos) {
        float w = view_matrix.m * world_pos.x + 
                  view_matrix.m * world_pos.y + 
                  view_matrix.m * world_pos.z + 
                  view_matrix.m;

        if (w  120) return;

        uintptr_t local_player_pawn = *(uintptr_t*)(base + offsets::dwLocalPlayerPawn);

        // 3. Рендеринг сущностей
        for (int i = 0; i  100) continue;

            int team = *(int*)(player + offsets::dwPlayerTeam);

            Vector3 origin = *(Vector3*)(player + offsets::dwPlayerOrigin);
            Vector3 head = *(Vector3*)(player + offsets::dwPlayerHeadPos);

            Vector3 screen_origin, screen_head;
            if (WorldToScreen(origin, view_matrix, width, height, screen_origin) &&
                WorldToScreen(head, view_matrix, width, height, screen_head)) {
                
                float box_height = screen_origin.y - screen_head.y;
                float box_width = box_height / 2.0f;
                float box_x = screen_head.x - (box_width / 2.0f);
                float box_y = screen_head.y;

                // Цвета Енота: Террористы - Красный, Спецназ - Синий
                ImColor esp_color = (team == 2) ? ImColor(255, 75, 75, 255) : ImColor(75, 75, 255, 255);

                // Boxes
                draw_list->AddRect(ImVec2(box_x, box_y), ImVec2(box_x + box_width, box_y + box_height), esp_color, 0.0f, 0, 1.5f);

                // HealthBar
                float health_percentage = (float)health / 100.0f;
                float bar_height = box_height * health_percentage;
                
                draw_list->AddRectFilled(ImVec2(box_x - 6, box_y), ImVec2(box_x - 2, box_y + box_height), ImColor(40, 40, 40, 180));
                draw_list->AddRectFilled(ImVec2(box_x - 6, box_y + box_height - bar_height), ImVec2(box_x - 2, box_y + box_height), ImColor(0, 255, 0, 255));

                // Names
                uintptr_t name_ptr = *(uintptr_t*)(player + offsets::dwPlayerName);
                if (IsValidPtr((void*)name_ptr)) {
                    char* name_str = (char*)(name_ptr + 0x14); 
                    ImVec2 text_size = ImGui::CalcTextSize(name_str);
                    draw_list->AddText(ImVec2(screen_head.x - (text_size.x / 2.0f), box_y - text_size.y - 2), ImColor(255, 255, 255, 255), name_str);
                }
            }
        }
    }
}
