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

static Font g_uiFont;
static bool g_uiFontLoaded = false;

static void UIText(const char* text, float x, float y, float size, Color c) {
    if (g_uiFontLoaded) DrawTextEx(g_uiFont, text, {x, y}, size, 0, c);
    else DrawText(text, (int)x, (int)y, (int)size, c);
}

static float UITextWidth(const char* text, float size) {
    if (g_uiFontLoaded) return MeasureTextEx(g_uiFont, text, size, 0).x;
    return (float)MeasureText(text, (int)size);
}

static void DrawPanel(Rectangle r, Color border) {
    DrawRectangleRec(r, UI_BG);
    DrawRectangleLinesEx(r, 1, border);
}

static void UISectionHeader(const char* label, float x, float y, float width) {
    (void)width;
    UIText(label, (int)x, (int)y, 18, UI_LABEL);
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

static void SpawnFigure8(std::vector<Body>& bodies, Vector2 c) {
    // Chenciner-Montgomery three-body figure-eight choreography, scaled from
    // normalized units (G=1, m=1, L=1) to our G and pixel scale
    float L = 300.0f, m = 500.0f;
    float vs = sqrtf(G * m / L);
    Vector2 p1 = {0.97000436f * L, -0.24308753f * L};
    Vector2 v3 = {-0.93240737f * vs, -0.86473146f * vs};
    Vector2 v12 = {-v3.x / 2, -v3.y / 2};
    AddBody(bodies, {c.x + p1.x, c.y + p1.y}, v12, m);
    AddBody(bodies, {c.x - p1.x, c.y - p1.y}, v12, m);
    AddBody(bodies, c, v3, m);
}

static void SpawnMoons(std::vector<Body>& bodies, Vector2 c) {
    float starMass = 8000.0f;
    AddBody(bodies, c, {0, 0}, starMass);
    // two planets, each with an orbiting moon inside its Hill sphere
    float planetR[2] = {320.0f, 560.0f};
    for (int i = 0; i < 2; i++) {
        float ang = GetRandomValue(0, 359) * DEG2RAD;
        float planetMass = 250.0f;
        Vector2 pPos = {c.x + cosf(ang) * planetR[i], c.y + sinf(ang) * planetR[i]};
        Vector2 pVel = OrbitVelocity(c, pPos, starMass);
        AddBody(bodies, pPos, pVel, planetMass);

        float moonR = 45.0f;
        float mAng = GetRandomValue(0, 359) * DEG2RAD;
        Vector2 mPos = {pPos.x + cosf(mAng) * moonR, pPos.y + sinf(mAng) * moonR};
        Vector2 mVel = Vector2Add(pVel, OrbitVelocity(pPos, mPos, planetMass));
        AddBody(bodies, mPos, mVel, 4.0f);
    }
}

static void SpawnCollision(std::vector<Body>& bodies, Vector2 c) {
    // two small galaxies drifting into each other
    for (int g = 0; g < 2; g++) {
        float side = (g == 0) ? -1.0f : 1.0f;
        Vector2 core = {c.x + side * 480.0f, c.y + side * 120.0f};
        Vector2 drift = {-side * 35.0f, -side * 8.0f};
        float coreMass = 6000.0f;
        AddBody(bodies, core, drift, coreMass);
        for (int i = 0; i < 60; i++) {
            float r = 60.0f + powf((float)GetRandomValue(0, 1000) / 1000.0f, 1.5f) * 220.0f;
            float ang = GetRandomValue(0, 359) * DEG2RAD;
            Vector2 pos = {core.x + cosf(ang) * r, core.y + sinf(ang) * r};
            Vector2 vel = Vector2Add(drift, OrbitVelocity(core, pos, coreMass));
            AddBody(bodies, pos, vel, (float)GetRandomValue(1, 3));
        }
    }
}

static void SpawnComets(std::vector<Body>& bodies, Vector2 c) {
    float starMass = 8000.0f;
    AddBody(bodies, c, {0, 0}, starMass);
    // launched near perihelion faster than circular -> long elliptical orbits
    for (int i = 0; i < 8; i++) {
        float r = 100.0f + GetRandomValue(0, 60);
        float ang = GetRandomValue(0, 359) * DEG2RAD;
        Vector2 pos = {c.x + cosf(ang) * r, c.y + sinf(ang) * r};
        Vector2 vel = Vector2Scale(OrbitVelocity(c, pos, starMass),
                                    1.20f + GetRandomValue(0, 15) / 100.0f);
        AddBody(bodies, pos, vel, 2.0f);
    }
}

enum PatternType {
    PAT_NONE = -1,
    PAT_SOLAR,
    PAT_BINARY,
    PAT_RING,
    PAT_GALAXY,
    PAT_GRID,
    PAT_CLOUD,
    PAT_FIGURE8,
    PAT_MOONS,
    PAT_COLLIDE,
    PAT_COMETS
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
        case PAT_FIGURE8: SpawnFigure8(v, c); break;
        case PAT_MOONS: SpawnMoons(v, c); break;
        case PAT_COLLIDE: SpawnCollision(v, c); break;
        case PAT_COMETS: SpawnComets(v, c); break;
    }
    return v;
}

// ---------- physics ----------

enum CollisionMode {
    COLLIDE_NONE = 0,    // bodies pass through each other
    COLLIDE_MERGE,       // perfectly inelastic merge
    COLLIDE_DEBRIS       // merge, but part of the smaller body sprays out as fragments
};

static void StepPhysics(std::vector<Body>& bodies, float dt, bool trailsOn, int collisionMode,
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

    // resolve overlapping bodies
    if (collisionMode == COLLIDE_NONE) return;
    std::vector<Body> debris;
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
                float impactSpeed = Vector2Length(Vector2Subtract(big.vel, small.vel));

                // perfectly inelastic merge, conserves momentum
                big.vel = Vector2Scale(Vector2Add(Vector2Scale(big.vel, big.mass),
                                                   Vector2Scale(small.vel, small.mass)),
                                        1.0f / totalMass);
                big.pos = Vector2Scale(Vector2Add(Vector2Scale(big.pos, big.mass),
                                                   Vector2Scale(small.pos, small.mass)),
                                        1.0f / totalMass);
                big.mass = totalMass;
                small.alive = false;

                // spray part of the smaller body back out as fragments
                float debrisMass = 0.25f * std::min(m1, m2);
                if (collisionMode == COLLIDE_DEBRIS && debrisMass >= 4.0f) {
                    int count = (int)Clamp(debrisMass / 3.0f, 3.0f, 8.0f);
                    float fragMass = debrisMass / count;
                    big.mass = totalMass - debrisMass;
                    float kick = fmaxf(impactSpeed * 0.5f, 40.0f);
                    float spawnR = MassToRadius(big.mass) + MassToRadius(fragMass) + 6.0f;
                    float baseAng = GetRandomValue(0, 359) * DEG2RAD;
                    // even angular spread keeps the net fragment momentum near zero
                    for (int k = 0; k < count; k++) {
                        float a = baseAng + 2.0f * PI * k / count;
                        Vector2 dir = {cosf(a), sinf(a)};
                        Body d;
                        d.pos = Vector2Add(big.pos, Vector2Scale(dir, spawnR));
                        d.vel = Vector2Add(big.vel,
                                            Vector2Scale(dir, kick * (0.8f + GetRandomValue(0, 40) / 100.0f)));
                        d.mass = fragMass;
                        d.color = ColorForMass(fragMass);
                        debris.push_back(d);
                    }
                }
                big.color = ColorForMass(big.mass);
            }
        }
    }

    bodies.erase(std::remove_if(bodies.begin(), bodies.end(),
                                 [](const Body& b) { return !b.alive; }),
                 bodies.end());
    bodies.insert(bodies.end(), debris.begin(), debris.end());
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
    DrawRectangleRec(r, bg);
    DrawRectangleLinesEx(r, 1, hover ? UI_BORDER_LIT : UI_BORDER);
    float tw = UITextWidth(label, 18);
    UIText(label, (int)(r.x + (r.width - tw) / 2), (int)(r.y + (r.height - 18) / 2), 18,
             hover ? UI_VALUE : UI_TEXT);
    return hover && IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
}

static bool UIToggle(Rectangle r, const char* label, bool state) {
    Vector2 m = GetMousePosition();
    bool hover = CheckCollisionPointRec(m, r);
    if (state) {
        // filled: white block with dark text, like a primary button
        Color bg = hover ? (Color){215, 215, 218, 255} : (Color){235, 235, 238, 255};
        DrawRectangleRec(r, bg);
        float tw = UITextWidth(label, 18);
        UIText(label, (int)(r.x + (r.width - tw) / 2), (int)(r.y + (r.height - 18) / 2), 18,
                 (Color){18, 18, 20, 255});
    } else {
        Color bg = hover ? UI_BTN_HOVER : UI_BTN_BG;
        DrawRectangleRec(r, bg);
        DrawRectangleLinesEx(r, 1, hover ? UI_BORDER_LIT : UI_BORDER);
        float tw = UITextWidth(label, 18);
        UIText(label, (int)(r.x + (r.width - tw) / 2), (int)(r.y + (r.height - 18) / 2), 18,
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
    DrawRectangleRec({r.x, cy - 2, r.width, 4}, (Color){55, 55, 60, 255});
    DrawRectangleRec({r.x, cy - 2, kx - r.x, 4}, (Color){235, 235, 238, 255});
    // square knob
    float kh = (hover || *dragging) ? 20.0f : 18.0f;
    Rectangle knob = {kx - kh / 2, cy - kh / 2, kh, kh};
    DrawRectangleRec(knob, WHITE);
    DrawRectangleLinesEx(knob, 1, (Color){120, 120, 126, 255});
    return value;
}

int main() {
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE | FLAG_WINDOW_HIGHDPI);
    InitWindow(1280, 800, "Gravity Sandbox");
    SetWindowMinSize(800, 600);
    SetExitKey(KEY_NULL);   // Esc cancels pattern placement instead of quitting

    // atlas sized ~2x the largest UI text size so glyphs sample near 1:1 on retina
    int fontAtlasSize = (int)(20 * GetWindowScaleDPI().x) + 8;
    g_uiFont = LoadFontEx(TextFormat("%sassets/Inter.ttf", GetApplicationDirectory()),
                          fontAtlasSize, NULL, 0);
    if (g_uiFont.texture.id != GetFontDefault().texture.id && g_uiFont.glyphCount > 0) {
        g_uiFontLoaded = true;
        SetTextureFilter(g_uiFont.texture, TEXTURE_FILTER_BILINEAR);
    }

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
    int collisionMode = COLLIDE_MERGE;
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
        const Rectangle panel = {screenWidth - 262.0f, 10.0f, 252.0f, 718.0f};
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
        if (IsKeyPressed(KEY_M)) collisionMode = (collisionMode + 1) % 3;

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
                StepPhysics(bodies, dt / substeps, trailsOn, collisionMode,
                            recordTrail && s == substeps - 1, (int)trailLength);
            }
        }

        BeginDrawing();
        ClearBackground(BLACK);

        // raylib's BeginMode2D doesn't apply the HiDPI screen scale (screen-space
        // drawing does), so render the world through a DPI-adjusted camera while
        // keeping `camera` in logical units for all input math
        Vector2 dpi = GetWindowScaleDPI();
        Camera2D camRender = camera;
        camRender.offset = {camera.offset.x * dpi.x, camera.offset.y * dpi.y};
        camRender.zoom = camera.zoom * dpi.x;
        BeginMode2D(camRender);
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
        UISectionHeader("MASS", px, y, pw - UITextWidth(massTxt, 18) - 10);
        UIText(massTxt, (int)(panel.x + panel.width - 14 - UITextWidth(massTxt, 18)), (int)y, 18, UI_VALUE);
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
        float colW = (pw - 8) / 2;
        float col2 = px + colW + 8;
        patternButton({px, y, colW, 32}, "Solar Sys", PAT_SOLAR);
        patternButton({col2, y, colW, 32}, "Binary", PAT_BINARY);
        y += 40;
        patternButton({px, y, colW, 32}, "Ring", PAT_RING);
        patternButton({col2, y, colW, 32}, "Galaxy", PAT_GALAXY);
        y += 40;
        patternButton({px, y, colW, 32}, "Grid", PAT_GRID);
        patternButton({col2, y, colW, 32}, "Cloud", PAT_CLOUD);
        y += 40;
        patternButton({px, y, colW, 32}, "Figure-8", PAT_FIGURE8);
        patternButton({col2, y, colW, 32}, "Moons", PAT_MOONS);
        y += 40;
        patternButton({px, y, colW, 32}, "Collision", PAT_COLLIDE);
        patternButton({col2, y, colW, 32}, "Comets", PAT_COMETS);
        y += 44;

        UISectionHeader("VIEW", px, y, pw);
        y += 26;
        float halfW = (pw - 8) / 2;
        if (UIToggle({px, y, halfW, 32}, "Trails (T)", trailsOn)) trailsOn = !trailsOn;
        if (UIToggle({px + halfW + 8, y, halfW, 32}, "Grid (G)", gridOn)) gridOn = !gridOn;
        y += 40;
        UISectionHeader("COLLISION (M)", px, y, pw);
        y += 26;
        float w3 = (pw - 16) / 3;
        if (UIToggle({px, y, w3, 32}, "None", collisionMode == COLLIDE_NONE))
            collisionMode = COLLIDE_NONE;
        if (UIToggle({px + w3 + 8, y, w3, 32}, "Merge", collisionMode == COLLIDE_MERGE))
            collisionMode = COLLIDE_MERGE;
        if (UIToggle({px + 2 * (w3 + 8), y, w3, 32}, "Debris", collisionMode == COLLIDE_DEBRIS))
            collisionMode = COLLIDE_DEBRIS;
        y += 40;

        const char* trailTxt = TextFormat("%.1fs", trailLength / 60.0f);
        UISectionHeader("TRAIL LENGTH", px, y, pw - UITextWidth(trailTxt, 18) - 10);
        UIText(trailTxt, (int)(panel.x + panel.width - 14 - UITextWidth(trailTxt, 18)), (int)y, 18, UI_VALUE);
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
        UIText("FPS", (int)info.x + 14, (int)info.y + 13, 18, UI_LABEL);
        const char* fpsTxt = TextFormat("%d", GetFPS());
        UIText(fpsTxt, (int)(info.x + info.width - 14 - UITextWidth(fpsTxt, 18)),
                 (int)info.y + 13, 18, UI_VALUE);
        UIText("Bodies", (int)info.x + 14, (int)info.y + 39, 18, UI_LABEL);
        const char* bodyTxt = TextFormat("%d", (int)bodies.size());
        UIText(bodyTxt, (int)(info.x + info.width - 14 - UITextWidth(bodyTxt, 18)),
                 (int)info.y + 39, 18, UI_VALUE);

        // ---- placement banner (top-center) ----
        if (pendingPattern != PAT_NONE) {
            const char* placeTxt = "Click to place pattern  -  ESC to cancel";
            float ptw = UITextWidth(placeTxt, 20);
            Rectangle banner = {(screenWidth - ptw) / 2.0f - 18, paused ? 58.0f : 10.0f,
                                ptw + 36.0f, 40};
            DrawPanel(banner, UI_BORDER_LIT);
            UIText(placeTxt, (int)(banner.x + 18), (int)(banner.y + 10), 20, UI_VALUE);
        }

        // ---- paused banner (top-center) ----
        if (paused) {
            const char* pauseTxt = "PAUSED  -  SPACE to resume";
            float ptw = UITextWidth(pauseTxt, 20);
            Rectangle banner = {(screenWidth - ptw) / 2.0f - 18, 10, ptw + 36.0f, 40};
            DrawPanel(banner, UI_BORDER_LIT);
            UIText(pauseTxt, (int)(banner.x + 18), (int)(banner.y + 10), 20, GOLD);
        }

        // ---- hint bar (bottom-left) ----
        const char* hints = "Left click: place / drag     Right / middle drag: pan     Wheel: zoom     Space: pause";
        float htw = UITextWidth(hints, 16);
        Rectangle hintBar = {10, screenHeight - 44.0f, htw + 28.0f, 34};
        DrawPanel(hintBar, UI_BORDER);
        UIText(hints, (int)(hintBar.x + 14), (int)(hintBar.y + 9), 16, UI_LABEL);

        EndDrawing();
    }

    CloseWindow();
    return 0;
}
