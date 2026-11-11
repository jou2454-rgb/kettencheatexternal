// Plain vector math. No dependencies, no game knowledge.
#pragma once
#include <cmath>

namespace esp {

struct Vec2 {
    float x = 0.f, y = 0.f;
};

struct Vec3 {
    float x = 0.f, y = 0.f, z = 0.f;
};

struct Vec4 {
    float x = 0.f, y = 0.f, z = 0.f, w = 0.f;
};

inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float length(Vec3 a) { return std::sqrt(dot(a, a)); }
inline Vec3 normalize(Vec3 a) {
    const float l = length(a);
    return l > 1e-6f ? a * (1.f / l) : Vec3{};
}

}  // namespace esp
