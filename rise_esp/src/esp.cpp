#include "esp/esp.hpp"

#include <algorithm>
#include <cmath>

namespace esp {
namespace {

constexpr float kBoxWidthPerHeight = 0.42f;   // standing body, width / height
constexpr float kHealthGap = 6.f;             // px between box and health bar
constexpr float kHealthWidth = 3.f;           // px
constexpr float kNameLift = 8.f;              // px above the box

}  // namespace

void buildEsp(const Snapshot& s, const Config& c, DrawList& out) {
    for (const Player& p : s.players) {
        if (p.local || !p.alive || p.team == s.localTeam) continue;
        if (length(p.feet - s.cameraPos) > c.maxDistance) continue;

        Vec2 headS, feetS;
        if (!worldToScreen(s.viewProj, p.head, s.screenW, s.screenH, headS, true)) continue;
        if (!worldToScreen(s.viewProj, p.feet, s.screenW, s.screenH, feetS, true)) continue;

        const float top = std::min(headS.y, feetS.y);
        const float bot = std::max(headS.y, feetS.y);
        const float height = bot - top;
        if (height < 2.f || height > s.screenH * 2.f) continue;

        const float cx = (headS.x + feetS.x) * 0.5f;
        const float half = height * kBoxWidthPerHeight * 0.5f;
        if (cx + half < 0.f || cx - half > s.screenW || bot < 0.f || top > s.screenH) continue;

        if (c.box) {
            Prim r;
            r.kind = Kind::Rect;
            r.x1 = cx - half; r.y1 = top; r.x2 = cx + half; r.y2 = bot;
            r.color = c.boxColor;
            out.push_back(r);
        }
        if (c.health) {
            const float frac = p.maxHp > 0
                ? std::clamp(static_cast<float>(p.hp) / static_cast<float>(p.maxHp), 0.f, 1.f)
                : 0.f;
            const float bx = cx - half - kHealthGap;
            Prim b;
            b.kind = Kind::Bar;
            b.x1 = bx; b.y1 = top; b.x2 = bx + kHealthWidth; b.y2 = bot;
            b.fraction = frac;
            b.color = c.healthColor;
            out.push_back(b);
        }
        if (c.name && !p.name.empty()) {
            Prim t;
            t.kind = Kind::Text;
            t.x1 = cx; t.y1 = top - kNameLift;
            t.color = c.nameColor;
            t.text = p.name;
            out.push_back(t);
        }
        if (c.skeleton) {
            for (const auto& pair : c.bonePairs) {
                const int a = pair.first, b = pair.second;
                if (a < 0 || b < 0 || a >= static_cast<int>(p.bones.size()) ||
                    b >= static_cast<int>(p.bones.size())) continue;
                Vec2 pa, pb;
                if (!worldToScreen(s.viewProj, p.bones[a], s.screenW, s.screenH, pa, true)) continue;
                if (!worldToScreen(s.viewProj, p.bones[b], s.screenW, s.screenH, pb, true)) continue;
                Prim l;
                l.kind = Kind::Line;
                l.x1 = pa.x; l.y1 = pa.y; l.x2 = pb.x; l.y2 = pb.y;
                l.color = c.skeletonColor;
                out.push_back(l);
            }
        }
    }
}

}  // namespace esp
