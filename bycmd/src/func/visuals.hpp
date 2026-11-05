#ifndef VISUALS_HPP
#define VISUALS_HPP

#include <imgui.h>
#include "../other/vector3.h"

struct UnityMatrix4x4 {
    float m[16];
};

namespace Visuals {
    void DrawESP();
    bool WorldToScreen(const Vector3& world_pos, const UnityMatrix4x4& view_matrix, int screen_width, int screen_height, Vector3& screen_pos);
}

#endif // VISUALS_HPP
