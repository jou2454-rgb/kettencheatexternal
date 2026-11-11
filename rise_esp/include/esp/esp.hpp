// ESP: turns a snapshot of players into 2D draw primitives. Pure function,
// no memory access, no drawing API. The platform layer draws the list.
#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>
#include "mat.hpp"

namespace esp {

struct Player {
    uint64_t id = 0;
    std::string name;
    int team = 0;
    bool local = false;
    bool alive = false;
    int hp = 0;
    int maxHp = 100;
    Vec3 feet{};                 // world space, ground point
    Vec3 head{};                 // world space, head point
    std::vector<Vec3> bones;     // world space, optional
};

struct Snapshot {
    Mat4 viewProj{};             // projection * view, same convention as worldToScreen
    Vec3 cameraPos{};
    float screenW = 1920.f;
    float screenH = 1080.f;
    int localTeam = 0;
    std::vector<Player> players;
};

struct Config {
    bool box = true;
    bool health = true;
    bool name = true;
    bool skeleton = false;
    float maxDistance = 300.f;
    uint32_t boxColor = 0xFFF04060;        // 0xAARRGGBB
    uint32_t healthColor = 0xFF40E060;
    uint32_t nameColor = 0xFFFFFFFF;
    uint32_t skeletonColor = 0xFFF04060;
    std::vector<std::pair<int, int>> bonePairs;   // indices into Player::bones
};

enum class Kind : uint8_t { Rect, Line, Text, Bar };

// Rect: (x1,y1)-(x2,y2) box.      Line: (x1,y1)-(x2,y2).
// Bar:  vertical bar at x1, from y1 (top) to y2 (bottom), fill = fraction.
// Text: text anchored at (x1,y1), centred horizontally.
struct Prim {
    Kind kind = Kind::Rect;
    float x1 = 0.f, y1 = 0.f, x2 = 0.f, y2 = 0.f;
    float fraction = 0.f;
    uint32_t color = 0;
    std::string text;
};

using DrawList = std::vector<Prim>;

void buildEsp(const Snapshot& s, const Config& c, DrawList& out);

}  // namespace esp
