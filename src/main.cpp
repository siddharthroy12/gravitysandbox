#include "raylib.h"
#include "raymath.h"
#include <vector>
#include <deque>
#include <cmath>
#include <algorithm>

struct Body {
    Vector2 pos;
    Vector2 vel;
    float mass;
    Color color;
    std::deque<Vector2> trail;
    bool alive = true;
};

static const float G = 1500.0f;
static const float SOFTENING2 = 400.0f;   // softening^2, avoids singularities at close range
static const float TRAIL_LEN_MIN = 10.0f;
static const float TRAIL_LEN_MAX = 2000.0f;
static const float MASS_MIN = 1.0f;
static const float MASS_MAX = 20000.0f;

static float MassToRadius(float mass) {
    return 3.0f + cbrtf(mass) * 1.6f;
}

static Color ColorForMass(float mass) {
    float hue = 220.0f - std::min(220.0f, log2f(mass + 1.0f) * 18.0f);
    return ColorFromHSV(hue, 0.75f, 1.0f);
}

// ---------- UI theme (minimal dark, neutral grays) ----------

static const Color UI_BG = {13, 13, 15, 248};
static const Color UI_BORDER = {45, 45, 49, 255};
static const Color UI_BORDER_LIT = {80, 80, 86, 255};
static const Color UI_VALUE = {255, 255, 255, 255};
static const Color UI_TEXT = {228, 228, 232, 255};
static const Color UI_LABEL = {138, 138, 145, 255};
static const Color UI_BTN_BG = {25, 25, 28, 255};
static const Color UI_BTN_HOVER = {40, 40, 45, 255};
static const Color UI_BTN_PRESS = {55, 55, 61, 255};

static void DrawPanel(Rectangle r, Color border) {
    float round = fminf(24.0f / fminf(r.width, r.height), 1.0f);   // ~12px corner radius
    DrawRectangleRounded(r, round, 8, UI_BG);
    DrawRectangleRoundedLinesEx(r, round, 8, 1, border);
}

static void UISectionHeader(const char* label, float x, float y, float width) {
    (void)width;
    DrawText(label, (int)x, (int)y, 18, UI_LABEL);
}

static void AddBody(std::vector<Body>& bodies, Vector2 pos, Vector2 vel, float mass) {
    Body b;
    b.pos = pos;
    b.vel = vel;
    b.mass = mass;
    b.color = ColorForMass(mass);
    bodies.push_back(b);
}

// ---------- patterns ----------

static Vector2 OrbitVelocity(Vector2 center, Vector2 pos, float centralMass) {
    Vector2 d = Vector2Subtract(pos, center);
    float r = Vector2Length(d);
    if (r < 1.0f) return {0, 0};
    float v = sqrtf(G * centralMass / r);
    Vector2 tangent = {-d.y / r, d.x / r};
    return Vector2Scale(tangent, v);
}

static void SpawnSolarSystem(std::vector<Body>& bodies, Vector2 c) {
    float starMass = 8000.0f;
    AddBody(bodies, c, {0, 0}, starMass);
    int planets = 6;
    for (int i = 0; i < planets; i++) {
        float r = 160.0f + i * 90.0f + GetRandomValue(-15, 15);
        float ang = GetRandomValue(0, 359) * DEG2RAD;
        Vector2 pos = {c.x + cosf(ang) * r, c.y + sinf(ang) * r};
        AddBody(bodies, pos, OrbitVelocity(c, pos, starMass), (float)GetRandomValue(5, 40));
    }
}

static void SpawnBinaryStars(std::vector<Body>& bodies, Vector2 c) {
    float m = 4000.0f;
    float d = 300.0f;
    float v = sqrtf(G * m / (2.0f * d));
    AddBody(bodies, {c.x - d / 2, c.y}, {0, -v}, m);
    AddBody(bodies, {c.x + d / 2, c.y}, {0, v}, m);
}

static void SpawnPlanetRing(std::vector<Body>& bodies, Vector2 c) {
    float starMass = 6000.0f;
    AddBody(bodies, c, {0, 0}, starMass);
    int count = 40;
    float r = 300.0f;
    for (int i = 0; i < count; i++) {
        float ang = (2.0f * PI * i) / count;
        Vector2 pos = {c.x + cosf(ang) * r, c.y + sinf(ang) * r};
        AddBody(bodies, pos, OrbitVelocity(c, pos, starMass), 3.0f);
    }
}

static void SpawnGalaxy(std::vector<Body>& bodies, Vector2 c) {
    float coreMass = 12000.0f;
    AddBody(bodies, c, {0, 0}, coreMass);
    int count = 150;
    for (int i = 0; i < count; i++) {
        float r = 100.0f + powf((float)GetRandomValue(0, 1000) / 1000.0f, 1.5f) * 450.0f;
        float ang = GetRandomValue(0, 359) * DEG2RAD;
        Vector2 pos = {c.x + cosf(ang) * r, c.y + sinf(ang) * r};
        Vector2 vel = OrbitVelocity(c, pos, coreMass);
        vel = Vector2Scale(vel, 0.95f + GetRandomValue(0, 100) / 1000.0f);
        AddBody(bodies, pos, vel, (float)GetRandomValue(1, 4));
    }
}

static void SpawnGrid(std::vector<Body>& bodies, Vector2 c) {
    int side = 6;
    float spacing = 120.0f;
    float half = (side - 1) * spacing / 2.0f;
    for (int y = 0; y < side; y++) {
        for (int x = 0; x < side; x++) {
            AddBody(bodies, {c.x - half + x * spacing, c.y - half + y * spacing}, {0, 0}, 30.0f);
        }
    }
}

static void SpawnCloud(std::vector<Body>& bodies, Vector2 c) {
    int count = 60;
    for (int i = 0; i < count; i++) {
        float r = sqrtf((float)GetRandomValue(0, 1000) / 1000.0f) * 400.0f;
        float ang = GetRandomValue(0, 359) * DEG2RAD;
        AddBody(bodies, {c.x + cosf(ang) * r, c.y + sinf(ang) * r}, {0, 0},
                (float)GetRandomValue(5, 50));
    }
}

enum PatternType {
    PAT_NONE = -1,
    PAT_SOLAR,
    PAT_BINARY,
    PAT_RING,
    PAT_GALAXY,
    PAT_GRID,
    PAT_CLOUD
};

static std::vector<Body> MakePattern(int type, Vector2 c) {
    std::vector<Body> v;
    switch (type) {
        case PAT_SOLAR: SpawnSolarSystem(v, c); break;
        case PAT_BINARY: SpawnBinaryStars(v, c); break;
        case PAT_RING: SpawnPlanetRing(v, c); break;
        case PAT_GALAXY: SpawnGalaxy(v, c); break;
        case PAT_GRID: SpawnGrid(v, c); break;
        case PAT_CLOUD: SpawnCloud(v, c); break;
    }
    return v;
}

// ---------- physics ----------

static void StepPhysics(std::vector<Body>& bodies, float dt, bool trailsOn, bool mergeOn,
                        bool recordTrail, int trailLength) {
    size_t n = bodies.size();
    std::vector<Vector2> accel(n, {0, 0});

    for (size_t i = 0; i < n; i++) {
        if (!bodies[i].alive) continue;
        for (size_t j = i + 1; j < n; j++) {
            if (!bodies[j].alive) continue;
            Vector2 d = Vector2Subtract(bodies[j].pos, bodies[i].pos);
            float dist2 = d.x * d.x + d.y * d.y + SOFTENING2;
            float invDist = 1.0f / sqrtf(dist2);
            float invDist3 = invDist * invDist * invDist;

            Vector2 forceDir = Vector2Scale(d, invDist3);
            accel[i] = Vector2Add(accel[i], Vector2Scale(forceDir, G * bodies[j].mass));
            accel[j] = Vector2Subtract(accel[j], Vector2Scale(forceDir, G * bodies[i].mass));
        }
    }

    for (size_t i = 0; i < n; i++) {
        if (!bodies[i].alive) continue;
        bodies[i].vel = Vector2Add(bodies[i].vel, Vector2Scale(accel[i], dt));
        bodies[i].pos = Vector2Add(bodies[i].pos, Vector2Scale(bodies[i].vel, dt));

        if (trailsOn) {
            if (recordTrail) bodies[i].trail.push_back(bodies[i].pos);
            while ((int)bodies[i].trail.size() > trailLength) bodies[i].trail.pop_front();
        } else if (!bodies[i].trail.empty()) {
            bodies[i].trail.clear();
        }
    }

    // merge overlapping bodies (perfectly inelastic collision, conserves momentum)
    if (!mergeOn) return;
    for (size_t i = 0; i < n; i++) {
        if (!bodies[i].alive) continue;
        for (size_t j = i + 1; j < n; j++) {
            if (!bodies[j].alive) continue;
            float dist = Vector2Distance(bodies[i].pos, bodies[j].pos);
            float minDist = MassToRadius(bodies[i].mass) + MassToRadius(bodies[j].mass);
            if (dist < minDist * 0.6f) {
                float m1 = bodies[i].mass, m2 = bodies[j].mass;
                float totalMass = m1 + m2;
                Body& big = (m1 >= m2) ? bodies[i] : bodies[j];
                Body& small = (m1 >= m2) ? bodies[j] : bodies[i];

                big.vel = Vector2Scale(Vector2Add(Vector2Scale(big.vel, big.mass),
                                                   Vector2Scale(small.vel, small.mass)),
                                        1.0f / totalMass);
                big.pos = Vector2Scale(Vector2Add(Vector2Scale(big.pos, big.mass),
                                                   Vector2Scale(small.pos, small.mass)),
                                        1.0f / totalMass);
                big.mass = totalMass;
                big.color = ColorForMass(totalMass);
                small.alive = false;
            }
        }
    }

    bodies.erase(std::remove_if(bodies.begin(), bodies.end(),
                                 [](const Body& b) { return !b.alive; }),
                 bodies.end());
}

// ---------- grid ----------

static void DrawSpaceGrid(const Camera2D& camera, int screenWidth, int screenHeight) {
    Vector2 topLeft = GetScreenToWorld2D({0, 0}, camera);
    Vector2 bottomRight = GetScreenToWorld2D({(float)screenWidth, (float)screenHeight}, camera);

    // pick a grid spacing that stays between ~20 and ~80 px on screen
    float spacing = 50.0f;
    while (spacing * camera.zoom < 20.0f) spacing *= 4.0f;
    while (spacing * camera.zoom > 80.0f) spacing /= 4.0f;

    Color minorColor = Fade(WHITE, 0.06f);
    Color majorColor = Fade(WHITE, 0.14f);

    float startX = floorf(topLeft.x / spacing) * spacing;
    float startY = floorf(topLeft.y / spacing) * spacing;

    for (float x = startX; x <= bottomRight.x; x += spacing) {
        bool major = fmodf(fabsf(x), spacing * 4.0f) < spacing * 0.5f;
        DrawLineV({x, topLeft.y}, {x, bottomRight.y}, major ? majorColor : minorColor);
    }
    for (float y = startY; y <= bottomRight.y; y += spacing) {
        bool major = fmodf(fabsf(y), spacing * 4.0f) < spacing * 0.5f;
        DrawLineV({topLeft.x, y}, {bottomRight.x, y}, major ? majorColor : minorColor);
    }

    // axes through the origin, slightly brighter
    DrawLineEx({0, topLeft.y}, {0, bottomRight.y}, 1.5f / camera.zoom, Fade(WHITE, 0.18f));
    DrawLineEx({topLeft.x, 0}, {bottomRight.x, 0}, 1.5f / camera.zoom, Fade(WHITE, 0.18f));
}

// ---------- immediate-mode UI ----------

static bool UIButton(Rectangle r, const char* label) {
    Vector2 m = GetMousePosition();
    bool hover = CheckCollisionPointRec(m, r);
    Color bg = hover ? UI_BTN_HOVER : UI_BTN_BG;
    if (hover && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) bg = UI_BTN_PRESS;
    float round = fminf(16.0f / fminf(r.width, r.height), 1.0f);
    DrawRectangleRounded(r, round, 8, bg);
    DrawRectangleRoundedLinesEx(r, round, 8, 1, hover ? UI_BORDER_LIT : UI_BORDER);
    int tw = MeasureText(label, 18);
    DrawText(label, (int)(r.x + (r.width - tw) / 2), (int)(r.y + (r.height - 18) / 2), 18,
             hover ? UI_VALUE : UI_TEXT);
    return hover && IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
}

static bool UIToggle(Rectangle r, const char* label, bool state) {
    Vector2 m = GetMousePosition();
    bool hover = CheckCollisionPointRec(m, r);
    float round = fminf(16.0f / fminf(r.width, r.height), 1.0f);
    if (state) {
        // filled: white pill with dark text, like a primary button
        Color bg = hover ? (Color){215, 215, 218, 255} : (Color){235, 235, 238, 255};
        DrawRectangleRounded(r, round, 8, bg);
        int tw = MeasureText(label, 18);
        DrawText(label, (int)(r.x + (r.width - tw) / 2), (int)(r.y + (r.height - 18) / 2), 18,
                 (Color){18, 18, 20, 255});
    } else {
        Color bg = hover ? UI_BTN_HOVER : UI_BTN_BG;
        DrawRectangleRounded(r, round, 8, bg);
        DrawRectangleRoundedLinesEx(r, round, 8, 1, hover ? UI_BORDER_LIT : UI_BORDER);
        int tw = MeasureText(label, 18);
        DrawText(label, (int)(r.x + (r.width - tw) / 2), (int)(r.y + (r.height - 18) / 2), 18,
                 UI_LABEL);
    }
    return hover && IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
}

static float UISliderLog(Rectangle r, float value, float minV, float maxV, bool* dragging) {
    Vector2 m = GetMousePosition();
    bool hover = CheckCollisionPointRec(m, r);
    if (hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) *dragging = true;
    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) *dragging = false;

    float logMin = logf(minV), logMax = logf(maxV);
    float t = (logf(value) - logMin) / (logMax - logMin);
    if (*dragging) {
        t = Clamp((m.x - r.x) / r.width, 0.0f, 1.0f);
        value = expf(logMin + t * (logMax - logMin));
    }

    float cy = r.y + r.height / 2;
    float kx = r.x + r.width * t;
    DrawRectangleRounded({r.x, cy - 2, r.width, 4}, 1.0f, 4, (Color){55, 55, 60, 255});
    DrawRectangleRounded({r.x, cy - 2, kx - r.x, 4}, 1.0f, 4, (Color){235, 235, 238, 255});
    // square knob
    float kh = (hover || *dragging) ? 20.0f : 18.0f;
    Rectangle knob = {kx - 7, cy - kh / 2, 14, kh};
    DrawRectangleRounded(knob, 0.35f, 4, WHITE);
    DrawRectangleRoundedLinesEx(knob, 0.35f, 4, 1, (Color){120, 120, 126, 255});
    return value;
}

int main() {
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(1280, 800, "Gravity Sandbox");
    SetWindowMinSize(800, 600);
    SetExitKey(KEY_NULL);   // Esc cancels pattern placement instead of quitting

    std::vector<Body> bodies;

    Camera2D camera = {0};
    camera.offset = {GetScreenWidth() / 2.0f, GetScreenHeight() / 2.0f};
    camera.target = {0, 0};
    camera.zoom = 1.0f;
    camera.rotation = 0.0f;

    float currentMass = 50.0f;
    bool paused = false;
    bool trailsOn = true;
    bool gridOn = true;
    bool mergeOn = true;
    float trailTimer = 0.0f;
    const float trailInterval = 1.0f / 60.0f;   // trail sample rate, independent of FPS
    float trailLength = 240.0f;                  // in samples; /60 = seconds
    bool draggingTrailSlider = false;
    int pendingPattern = PAT_NONE;
    std::vector<Body> previewBodies;             // pattern preview, positions relative to cursor
    bool draggingBody = false;
    bool draggingSlider = false;
    int dragIndex = -1;
    Vector2 dragOffset = {0, 0};

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        int screenWidth = GetScreenWidth();
        int screenHeight = GetScreenHeight();
        camera.offset = {screenWidth / 2.0f, screenHeight / 2.0f};
        const Rectangle panel = {screenWidth - 262.0f, 10.0f, 252.0f, 732.0f};
        Vector2 mouseScreen = GetMousePosition();
        Vector2 mouseWorld = GetScreenToWorld2D(mouseScreen, camera);
        bool mouseOverUI = CheckCollisionPointRec(mouseScreen, panel) || draggingSlider ||
                           draggingTrailSlider;

        // pan with right or middle mouse drag
        if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT) || IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)) {
            Vector2 delta = GetMouseDelta();
            delta = Vector2Scale(delta, -1.0f / camera.zoom);
            camera.target = Vector2Add(camera.target, delta);
        }

        // zoom with wheel, centered on mouse
        float wheel = GetMouseWheelMove();
        if (wheel != 0 && !mouseOverUI) {
            Vector2 worldBeforeZoom = GetScreenToWorld2D(mouseScreen, camera);
            camera.zoom = Clamp(camera.zoom * (1.0f + wheel * 0.1f), 0.05f, 8.0f);
            Vector2 worldAfterZoom = GetScreenToWorld2D(mouseScreen, camera);
            camera.target = Vector2Add(camera.target,
                                        Vector2Subtract(worldBeforeZoom, worldAfterZoom));
        }

        // mass control (keys still work alongside slider)
        if (IsKeyDown(KEY_UP)) currentMass *= 1.03f;
        if (IsKeyDown(KEY_DOWN)) currentMass *= 0.97f;
        currentMass = Clamp(currentMass, MASS_MIN, MASS_MAX);

        // Esc cancels pending pattern placement
        if (IsKeyPressed(KEY_ESCAPE)) {
            pendingPattern = PAT_NONE;
            previewBodies.clear();
        }

        // left click: place pending pattern, grab existing body, or place a new dot
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !mouseOverUI && pendingPattern != PAT_NONE) {
            for (Body b : previewBodies) {
                b.pos = Vector2Add(b.pos, mouseWorld);
                bodies.push_back(b);
            }
            pendingPattern = PAT_NONE;
            previewBodies.clear();
        } else if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !mouseOverUI) {
            draggingBody = false;
            dragIndex = -1;
            for (int i = (int)bodies.size() - 1; i >= 0; i--) {
                if (Vector2Distance(bodies[i].pos, mouseWorld) <= MassToRadius(bodies[i].mass)) {
                    draggingBody = true;
                    dragIndex = i;
                    dragOffset = Vector2Subtract(bodies[i].pos, mouseWorld);
                    break;
                }
            }
            if (!draggingBody) {
                AddBody(bodies, mouseWorld, {0, 0}, currentMass);
            }
        }
        if (draggingBody && IsMouseButtonDown(MOUSE_BUTTON_LEFT) && dragIndex >= 0 &&
            dragIndex < (int)bodies.size()) {
            bodies[dragIndex].pos = Vector2Add(mouseWorld, dragOffset);
            bodies[dragIndex].vel = {0, 0};
        }
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            draggingBody = false;
            dragIndex = -1;
        }

        if (IsKeyPressed(KEY_SPACE)) paused = !paused;
        if (IsKeyPressed(KEY_C)) bodies.clear();
        if (IsKeyPressed(KEY_T)) trailsOn = !trailsOn;
        if (IsKeyPressed(KEY_G)) gridOn = !gridOn;
        if (IsKeyPressed(KEY_F)) ToggleBorderlessWindowed();
        if (IsKeyPressed(KEY_M)) mergeOn = !mergeOn;

        auto centerOnBodies = [&]() {
            if (bodies.empty()) return;
            Vector2 com = {0, 0};
            float totalMass = 0;
            for (const Body& b : bodies) {
                com = Vector2Add(com, Vector2Scale(b.pos, b.mass));
                totalMass += b.mass;
            }
            camera.target = Vector2Scale(com, 1.0f / totalMass);
        };
        if (IsKeyPressed(KEY_H)) centerOnBodies();
        if (IsKeyPressed(KEY_R)) {
            camera.target = {0, 0};
            camera.zoom = 1.0f;
        }

        if (!paused) {
            trailTimer += dt;
            bool recordTrail = trailTimer >= trailInterval;
            if (recordTrail) trailTimer = 0.0f;

            const int substeps = 2;
            for (int s = 0; s < substeps; s++) {
                StepPhysics(bodies, dt / substeps, trailsOn, mergeOn,
                            recordTrail && s == substeps - 1, (int)trailLength);
            }
        }

        BeginDrawing();
        ClearBackground(BLACK);

        BeginMode2D(camera);
        if (gridOn) DrawSpaceGrid(camera, screenWidth, screenHeight);
        for (auto& b : bodies) {
            if (trailsOn && b.trail.size() > 1) {
                float trailWidth = std::max(2.0f, MassToRadius(b.mass) * 0.35f);
                for (size_t k = 1; k < b.trail.size(); k++) {
                    float a = (float)k / b.trail.size();
                    Color c = Fade(b.color, a * 0.6f);
                    DrawLineEx(b.trail[k - 1], b.trail[k], trailWidth * a, c);
                }
            }
            DrawCircleV(b.pos, MassToRadius(b.mass), b.color);
        }
        if (pendingPattern != PAT_NONE && !mouseOverUI) {
            // ghost preview of the pattern, centered on the cursor
            for (const Body& b : previewBodies) {
                Vector2 p = Vector2Add(b.pos, mouseWorld);
                DrawCircleV(p, MassToRadius(b.mass), Fade(b.color, 0.4f));
                DrawCircleLinesV(p, MassToRadius(b.mass), Fade(WHITE, 0.25f));
            }
            DrawCircleLinesV(mouseWorld, 6.0f / camera.zoom, Fade(WHITE, 0.6f));
        } else if (!draggingBody && !mouseOverUI && pendingPattern == PAT_NONE) {
            DrawCircleLinesV(mouseWorld, MassToRadius(currentMass), Fade(WHITE, 0.5f));
        }
        EndMode2D();

        // ---- UI panel ----
        DrawPanel(panel, UI_BORDER);

        float px = panel.x + 14, pw = panel.width - 28;
        float y = panel.y + 12;

        const char* massTxt = TextFormat("%.0f", currentMass);
        UISectionHeader("MASS", px, y, pw - MeasureText(massTxt, 18) - 10);
        DrawText(massTxt, (int)(panel.x + panel.width - 14 - MeasureText(massTxt, 18)), (int)y, 18, UI_VALUE);
        y += 26;
        currentMass = UISliderLog({px, y, pw, 24}, currentMass, MASS_MIN, MASS_MAX, &draggingSlider);
        y += 38;

        UISectionHeader("PATTERNS", px, y, pw);
        y += 26;

        auto patternButton = [&](Rectangle r, const char* label, int type) {
            if (UIToggle(r, label, pendingPattern == type)) {
                if (pendingPattern == type) {
                    pendingPattern = PAT_NONE;
                    previewBodies.clear();
                } else {
                    pendingPattern = type;
                    previewBodies = MakePattern(type, {0, 0});
                }
            }
        };
        patternButton({px, y, pw, 32}, "Solar System", PAT_SOLAR);
        y += 40;
        patternButton({px, y, pw, 32}, "Binary Stars", PAT_BINARY);
        y += 40;
        patternButton({px, y, pw, 32}, "Planet + Ring", PAT_RING);
        y += 40;
        patternButton({px, y, pw, 32}, "Spiral Galaxy", PAT_GALAXY);
        y += 40;
        patternButton({px, y, pw, 32}, "Grid Collapse", PAT_GRID);
        y += 40;
        patternButton({px, y, pw, 32}, "Random Cloud", PAT_CLOUD);
        y += 44;

        UISectionHeader("VIEW", px, y, pw);
        y += 26;
        float halfW = (pw - 8) / 2;
        if (UIToggle({px, y, halfW, 32}, "Trails (T)", trailsOn)) trailsOn = !trailsOn;
        if (UIToggle({px + halfW + 8, y, halfW, 32}, "Grid (G)", gridOn)) gridOn = !gridOn;
        y += 40;
        if (UIToggle({px, y, pw, 32}, "Merge on collision (M)", mergeOn)) mergeOn = !mergeOn;
        y += 40;

        const char* trailTxt = TextFormat("%.1fs", trailLength / 60.0f);
        UISectionHeader("TRAIL LENGTH", px, y, pw - MeasureText(trailTxt, 18) - 10);
        DrawText(trailTxt, (int)(panel.x + panel.width - 14 - MeasureText(trailTxt, 18)), (int)y, 18, UI_VALUE);
        y += 26;
        trailLength = UISliderLog({px, y, pw, 24}, trailLength, TRAIL_LEN_MIN, TRAIL_LEN_MAX,
                                   &draggingTrailSlider);
        y += 38;
        if (UIButton({px, y, pw, 32}, "Reset View (R)")) {
            camera.target = {0, 0};
            camera.zoom = 1.0f;
        }
        y += 40;
        if (UIButton({px, y, pw, 32}, "Center Bodies (H)")) centerOnBodies();
        y += 40;
        if (UIButton({px, y, pw, 32}, "Fullscreen (F)")) ToggleBorderlessWindowed();
        y += 48;
        if (UIButton({px, y, pw, 32}, "Clear Canvas (C)")) bodies.clear();

        // ---- info card (top-left) ----
        Rectangle info = {10, 10, 170, 70};
        DrawPanel(info, UI_BORDER);
        DrawText("FPS", (int)info.x + 14, (int)info.y + 13, 18, UI_LABEL);
        const char* fpsTxt = TextFormat("%d", GetFPS());
        DrawText(fpsTxt, (int)(info.x + info.width - 14 - MeasureText(fpsTxt, 18)),
                 (int)info.y + 13, 18, UI_VALUE);
        DrawText("Bodies", (int)info.x + 14, (int)info.y + 39, 18, UI_LABEL);
        const char* bodyTxt = TextFormat("%d", (int)bodies.size());
        DrawText(bodyTxt, (int)(info.x + info.width - 14 - MeasureText(bodyTxt, 18)),
                 (int)info.y + 39, 18, UI_VALUE);

        // ---- placement banner (top-center) ----
        if (pendingPattern != PAT_NONE) {
            const char* placeTxt = "Click to place pattern  -  ESC to cancel";
            int ptw = MeasureText(placeTxt, 20);
            Rectangle banner = {(screenWidth - ptw) / 2.0f - 18, paused ? 58.0f : 10.0f,
                                ptw + 36.0f, 40};
            DrawPanel(banner, UI_BORDER_LIT);
            DrawText(placeTxt, (int)(banner.x + 18), (int)(banner.y + 10), 20, UI_VALUE);
        }

        // ---- paused banner (top-center) ----
        if (paused) {
            const char* pauseTxt = "PAUSED  -  SPACE to resume";
            int ptw = MeasureText(pauseTxt, 20);
            Rectangle banner = {(screenWidth - ptw) / 2.0f - 18, 10, ptw + 36.0f, 40};
            DrawPanel(banner, UI_BORDER_LIT);
            DrawText(pauseTxt, (int)(banner.x + 18), (int)(banner.y + 10), 20, GOLD);
        }

        // ---- hint bar (bottom-left) ----
        const char* hints = "Left click: place / drag     Right / middle drag: pan     Wheel: zoom     Space: pause";
        int htw = MeasureText(hints, 16);
        Rectangle hintBar = {10, screenHeight - 44.0f, htw + 28.0f, 34};
        DrawPanel(hintBar, UI_BORDER);
        DrawText(hints, (int)(hintBar.x + 14), (int)(hintBar.y + 9), 16, UI_LABEL);

        EndDrawing();
    }

    CloseWindow();
    return 0;
}
