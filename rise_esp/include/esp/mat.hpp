// 4x4 matrices and the world -> screen projection.
//
// Layout: column-major, m[col * 4 + row], the same memory layout as
// UnityEngine.Matrix4x4 (m[0..3] is column 0). Vectors are column vectors:
// clip = P * V * (x, y, z, 1).
#pragma once
#include "vec.hpp"

namespace esp {

struct Mat4 {
    float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

    float at(int row, int col) const { return m[col * 4 + row]; }
    float& at(int row, int col) { return m[col * 4 + row]; }
};

inline Mat4 mul(const Mat4& a, const Mat4& b) {
    Mat4 r{};
    for (int c = 0; c < 4; ++c)
        for (int row = 0; row < 4; ++row) {
            float s = 0.f;
            for (int k = 0; k < 4; ++k) s += a.at(row, k) * b.at(k, c);
            r.at(row, c) = s;
        }
    return r;
}

inline Vec4 mul(const Mat4& a, Vec4 v) {
    return {
        a.at(0, 0) * v.x + a.at(0, 1) * v.y + a.at(0, 2) * v.z + a.at(0, 3) * v.w,
        a.at(1, 0) * v.x + a.at(1, 1) * v.y + a.at(1, 2) * v.z + a.at(1, 3) * v.w,
        a.at(2, 0) * v.x + a.at(2, 1) * v.y + a.at(2, 2) * v.z + a.at(2, 3) * v.w,
        a.at(3, 0) * v.x + a.at(3, 1) * v.y + a.at(3, 2) * v.z + a.at(3, 3) * v.w,
    };
}

// Right-handed perspective, camera looks down -Z, clip z in [-w, w] (OpenGL style).
// fovY in radians.
inline Mat4 perspective(float fovY, float aspect, float zNear, float zFar) {
    Mat4 r{};
    const float f = 1.f / std::tan(fovY * 0.5f);
    r.at(0, 0) = f / aspect;
    r.at(1, 1) = f;
    r.at(2, 2) = (zFar + zNear) / (zNear - zFar);
    r.at(3, 2) = -1.f;                                    // w = -z_view
    r.at(2, 3) = (2.f * zFar * zNear) / (zNear - zFar);
    r.at(3, 3) = 0.f;                                     // Mat4 starts as identity
    return r;
}

// World -> view for a camera at `eye` looking at `target`.
inline Mat4 lookAt(Vec3 eye, Vec3 target, Vec3 up) {
    const Vec3 f = normalize(target - eye);   // forward
    const Vec3 s = normalize(cross(f, up));   // right
    const Vec3 u = cross(s, f);               // true up
    Mat4 r{};
    r.at(0, 0) = s.x;  r.at(0, 1) = s.y;  r.at(0, 2) = s.z;
    r.at(1, 0) = u.x;  r.at(1, 1) = u.y;  r.at(1, 2) = u.z;
    r.at(2, 0) = -f.x; r.at(2, 1) = -f.y; r.at(2, 2) = -f.z;
    r.at(0, 3) = -dot(s, eye);
    r.at(1, 3) = -dot(u, eye);
    r.at(2, 3) = dot(f, eye);
    r.at(3, 3) = 1.f;
    return r;
}

// Projects a world point to screen pixels, origin top-left, y grows down.
// Returns false when the point is behind the camera, or outside the screen
// unless allowOffscreen is set.
inline bool worldToScreen(const Mat4& viewProj, Vec3 p, float screenW, float screenH,
                          Vec2& out, bool allowOffscreen = false) {
    const Vec4 c = mul(viewProj, Vec4{p.x, p.y, p.z, 1.f});
    if (c.w <= 1e-4f) return false;
    const float nx = c.x / c.w;
    const float ny = c.y / c.w;
    if (!allowOffscreen && (nx < -1.f || nx > 1.f || ny < -1.f || ny > 1.f)) return false;
    out.x = (nx * 0.5f + 0.5f) * screenW;
    out.y = (1.f - (ny * 0.5f + 0.5f)) * screenH;
    return allowOffscreen || (out.x >= 0.f && out.x <= screenW && out.y >= 0.f && out.y <= screenH);
}

}  // namespace esp
