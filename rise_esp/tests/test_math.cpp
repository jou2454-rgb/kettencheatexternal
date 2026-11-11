// Host tests for the math and the ESP builder. Run: make test
#include <cmath>
#include <cstdio>

#include "esp/esp.hpp"

using namespace esp;

static int failures = 0;

#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);  \
            ++failures;                                                  \
        }                                                                \
    } while (0)

static bool near(float a, float b, float eps = 1e-2f) { return std::fabs(a - b) <= eps; }

static Mat4 makeViewProj(float W, float H) {
    const Mat4 view = lookAt(Vec3{0, 0, 10}, Vec3{0, 0, 0}, Vec3{0, 1, 0});
    const Mat4 proj = perspective(60.f * 3.14159265f / 180.f, W / H, 0.1f, 1000.f);
    return mul(proj, view);
}

static void testProjection() {
    const float W = 1920.f, H = 1080.f;
    const Mat4 vp = makeViewProj(W, H);
    Vec2 s;

    CHECK(worldToScreen(vp, Vec3{0, 0, 0}, W, H, s));
    CHECK(near(s.x, W * 0.5f) && near(s.y, H * 0.5f));          // target is the centre

    CHECK(worldToScreen(vp, Vec3{0, 1, 0}, W, H, s));
    CHECK(s.y < H * 0.5f);                                       // up in world -> up on screen

    CHECK(worldToScreen(vp, Vec3{1, 0, 0}, W, H, s));
    CHECK(s.x > W * 0.5f);                                       // right in world -> right on screen

    CHECK(!worldToScreen(vp, Vec3{0, 0, 20}, W, H, s));         // behind the camera
    CHECK(!worldToScreen(vp, Vec3{200, 0, 0}, W, H, s));         // far outside the view
    CHECK(worldToScreen(vp, Vec3{200, 0, 0}, W, H, s, true));    // ...but allowed when asked
    CHECK(s.x > W);
}

static void testPerspectiveFov() {
    // A point at the top edge of a 60 degree vertical fov, 10 units in front, must land on the top edge.
    const float W = 1000.f, H = 1000.f;
    const Mat4 vp = makeViewProj(W, H);
    const float halfH = 10.f * std::tan(30.f * 3.14159265f / 180.f);
    Vec2 s;
    CHECK(worldToScreen(vp, Vec3{0, halfH, 0}, W, H, s));
    CHECK(near(s.y, 0.f, 0.5f));
}

static void testBuilder() {
    const float W = 1920.f, H = 1080.f;
    Snapshot snap;
    snap.viewProj = makeViewProj(W, H);
    snap.cameraPos = Vec3{0, 0, 10};
    snap.screenW = W;
    snap.screenH = H;
    snap.localTeam = 1;

    Player enemy;
    enemy.id = 1; enemy.name = "enemy"; enemy.team = 2; enemy.alive = true;
    enemy.hp = 50; enemy.maxHp = 100;
    enemy.feet = Vec3{0, -0.9f, 0};
    enemy.head = Vec3{0, 0.9f, 0};

    Player mate = enemy;  mate.id = 2; mate.team = 1;             // same team: skipped
    Player dead = enemy;  dead.id = 3; dead.alive = false;        // dead: skipped
    Player far  = enemy;  far.id = 4; far.feet.z = 500.f; far.head.z = 500.f;   // too far
    Player me   = enemy;  me.id = 5; me.local = true;             // local: skipped
    snap.players = {enemy, mate, dead, far, me};

    Config cfg;
    cfg.skeleton = false;
    DrawList out;
    buildEsp(snap, cfg, out);

    int rects = 0, bars = 0, texts = 0;
    for (const Prim& p : out) {
        if (p.kind == Kind::Rect) ++rects;
        if (p.kind == Kind::Bar) ++bars;
        if (p.kind == Kind::Text) ++texts;
    }
    CHECK(rects == 1);
    CHECK(bars == 1);
    CHECK(texts == 1);

    for (const Prim& p : out) {
        if (p.kind == Kind::Bar) CHECK(near(p.fraction, 0.5f, 1e-4f));
        if (p.kind == Kind::Rect) {
            CHECK(p.x1 < p.x2 && p.y1 < p.y2);
            const float cx = 0.5f * (p.x1 + p.x2);
            CHECK(near(cx, W * 0.5f, 1.f));                       // enemy is dead centre
            const float h = p.y2 - p.y1;
            CHECK(near(p.x2 - p.x1, h * 0.42f, 1.f));             // width = 0.42 * height
        }
    }
}

static void testSkeleton() {
    const float W = 1920.f, H = 1080.f;
    Snapshot snap;
    snap.viewProj = makeViewProj(W, H);
    snap.cameraPos = Vec3{0, 0, 10};
    snap.screenW = W;
    snap.screenH = H;
    snap.localTeam = 1;

    Player enemy;
    enemy.team = 2; enemy.alive = true; enemy.hp = 100; enemy.maxHp = 100;
    enemy.feet = Vec3{0, -0.9f, 0}; enemy.head = Vec3{0, 0.9f, 0};
    enemy.bones = {Vec3{0, 0.5f, 0}, Vec3{0, 0.0f, 0}, Vec3{0, -0.5f, 0}};
    snap.players = {enemy};

    Config cfg;
    cfg.box = false; cfg.health = false; cfg.name = false; cfg.skeleton = true;
    cfg.bonePairs = {{0, 1}, {1, 2}, {0, 7}};                    // last pair is out of range
    DrawList out;
    buildEsp(snap, cfg, out);
    int lines = 0;
    for (const Prim& p : out) if (p.kind == Kind::Line) ++lines;
    CHECK(lines == 2);
}

int main() {
    testProjection();
    testPerspectiveFov();
    testBuilder();
    testSkeleton();
    if (failures == 0) std::printf("all tests passed\n");
    else std::printf("%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
