#include "raylib.h"
#include "raymath.h"

#include "body.h"
#include "curvature.h"
#include "patterns.h"
#include "physics.h"
#include "ui.h"

#include <algorithm>
#include <cmath>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

struct Shockwave {
    Vector2 pos;
    float age;      // sim seconds since impact
    float maxR;
    float baseR;
};
static const float SHOCKWAVE_LIFE = 0.6f;

static const float TRAIL_LEN_MIN = 10.0f;
static const float TRAIL_LEN_MAX = 2000.0f;
static const float MASS_MIN = 1.0f;
static const float MASS_MAX = 20000.0f;

// Deep-space backdrop: vertical gradient, faint nebula tints, and a twinkling
// starfield. Stars and nebulae shift with a small parallax factor when the
// camera pans (but not when it zooms), so they read as infinitely far away.
static void DrawSpaceBackground(int w, int h, Vector2 camTarget) {
    DrawRectangleGradientV(0, 0, w, h, CLITERAL(Color){12, 14, 30, 255},
                           CLITERAL(Color){3, 4, 10, 255});

    float t = (float)GetTime();
    BeginBlendMode(BLEND_ADDITIVE);

    // nebulae: huge soft radial tints, wrapped across a tile larger than the
    // screen so panning always keeps some in view
    const float nebPar = 0.03f;
    float tileW = w * 1.6f, tileH = h * 1.6f;
    struct Neb { float u, v, r; Color c; };
    const Neb nebs[3] = {
        {0.70f, 0.25f, 0.55f, {80, 60, 160, 255}},    // violet
        {0.15f, 0.75f, 0.50f, {40, 110, 130, 255}},   // teal
        {0.45f, 0.55f, 0.65f, {70, 50, 120, 255}},    // dim violet
    };
    for (const Neb& n : nebs) {
        float nx = fmodf(n.u * tileW - camTarget.x * nebPar, tileW);
        float ny = fmodf(n.v * tileH - camTarget.y * nebPar, tileH);
        if (nx < 0) nx += tileW;
        if (ny < 0) ny += tileH;
        DrawCircleGradient({nx - (tileW - w) / 2, ny - (tileH - h) / 2}, w * n.r,
                           Fade(n.c, 0.055f), Fade(n.c, 0.0f));
    }

    // starfield: positions and character from a per-index hash, so it's stable
    // frame to frame without storing anything
    const float starPar = 0.07f;
    int count = (int)Clamp(w * h / 11000.0f, 60.0f, 220.0f);
    for (int i = 0; i < count; i++) {
        unsigned int h1 = (unsigned int)i * 2654435761u + 1013904223u;
        float sx = ((h1 >> 8) & 0xffff) / 65535.0f * w;
        float sy = ((h1 >> 15) & 0xffff) / 65535.0f * h;
        float px = fmodf(sx - camTarget.x * starPar, (float)w);
        float py = fmodf(sy - camTarget.y * starPar, (float)h);
        if (px < 0) px += w;
        if (py < 0) py += h;

        float bright = 0.25f + ((h1 >> 3) & 255) / 255.0f * 0.5f;
        float phase = ((h1 >> 11) & 255) / 255.0f * 2.0f * PI;
        float speed = 0.4f + ((h1 >> 19) & 255) / 255.0f * 1.2f;
        float twinkle = 0.75f + 0.25f * sinf(t * speed + phase);
        float size = 1.0f + ((h1 >> 23) & 3) * 0.5f;
        // a few stars get a subtle warm or cool tint
        Color c = WHITE;
        int tint = (h1 >> 27) & 7;
        if (tint == 0) c = CLITERAL(Color){200, 215, 255, 255};
        else if (tint == 1) c = CLITERAL(Color){255, 230, 200, 255};
        DrawRectangleRec({px - size / 2, py - size / 2, size, size},
                         Fade(c, bright * twinkle));
    }
    EndBlendMode();
}

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

// ---------- black / white hole rendering ----------

struct HolePalette {
    Color bright, mid, deep;   // disk gradient, inner to outer
    Color ringCore;            // hottest ring highlight
    Color core;                // event horizon fill
};
static const HolePalette BH_PALETTE = {{215, 185, 255, 255}, {150, 100, 255, 255},
                                       {100, 55, 220, 255},  {225, 205, 255, 255},
                                       {0, 0, 0, 255}};
static const HolePalette WH_PALETTE = {{255, 252, 240, 255}, {255, 214, 120, 255},
                                       {255, 160, 60, 255},  {255, 255, 250, 255},
                                       {255, 255, 255, 255}};

static Color BHLerpColor(Color a, Color b, float t) {
    return {(unsigned char)(a.r + (b.r - a.r) * t), (unsigned char)(a.g + (b.g - a.g) * t),
            (unsigned char)(a.b + (b.b - a.b) * t), 255};
}

// One half of the flattened accretion disk (back = upper arcs, drawn behind the
// horizon; front = lower arcs, drawn over it). Strands are static ellipses with
// a traveling brightness wave, which reads as swirling gas without arc clipping.
static void DrawHoleDiskHalf(Vector2 c, float r, float time, bool back, const HolePalette& pal) {
    const int strands = 18;
    float a0 = back ? 180.0f : 0.0f;

    for (int i = 0; i < strands; i++) {
        unsigned int h = (unsigned int)i * 1664525u + 1013904223u;
        float rnd1 = ((h >> 8) & 1023) / 1023.0f;
        float rnd2 = ((h >> 18) & 1023) / 1023.0f;
        float t01 = (i + rnd1 * 0.7f) / strands;
        float ax = r * (1.50f + 3.2f * t01 * t01);          // semi-major, out to ~4.7r
        float ay = ax * (0.30f + 0.05f * rnd2);             // squashed: seen nearly edge-on
        float speed = 140.0f / (0.4f + t01);                // inner gas swirls faster
        float phase = time * speed + rnd1 * 360.0f;
        int freq = 2 + (int)(rnd2 * 2.99f);
        Color col = (t01 < 0.5f) ? BHLerpColor(pal.bright, pal.mid, t01 * 2.0f)
                                 : BHLerpColor(pal.mid, pal.deep, t01 * 2.0f - 1.0f);
        float baseAlpha = (0.30f - 0.22f * t01) * (0.7f + 0.3f * rnd2);
        float thick = fmaxf(r * (0.055f - 0.030f * t01), 1.2f);

        const float step = 10.0f;
        Vector2 prev = {c.x + cosf(DEG2RAD * a0) * ax, c.y + sinf(DEG2RAD * a0) * ay};
        for (float ang = a0 + step; ang <= a0 + 180.0f + 0.1f; ang += step) {
            Vector2 pt = {c.x + cosf(DEG2RAD * ang) * ax, c.y + sinf(DEG2RAD * ang) * ay};
            float wave = 0.5f + 0.5f * sinf(DEG2RAD * (ang * freq + phase));
            DrawLineEx(prev, pt, thick, Fade(col, baseAlpha * (0.35f + 0.65f * wave)));
            prev = pt;
        }
    }
}

static void DrawHoleFX(Vector2 p, float r, float time, const HolePalette& pal) {
    BeginBlendMode(BLEND_ADDITIVE);
    DrawCircleV(p, r * 4.6f, Fade(pal.deep, 0.05f));        // ambient haze
    DrawCircleV(p, r * 2.4f, Fade(pal.mid, 0.07f));
    DrawHoleDiskHalf(p, r, time, true, pal);                // far side of the disk
    EndBlendMode();

    DrawCircleV(p, r * 1.02f, pal.core);                    // horizon (black) / core (white)

    BeginBlendMode(BLEND_ADDITIVE);
    DrawRing(p, r * 1.42f, r * 1.47f, 0, 360, 72, Fade(pal.mid, 0.12f));   // lensing shell
    DrawRing(p, r * 1.04f, r * 1.18f, 0, 360, 72, Fade(pal.mid, 0.50f));   // photon ring
    DrawRing(p, r * 1.05f, r * 1.12f, 0, 360, 72, Fade(pal.ringCore, 0.85f));
    DrawRing(p, r * 1.18f, r * 1.34f, 0, 360, 72, Fade(pal.mid, 0.16f));   // ring bloom
    DrawHoleDiskHalf(p, r, time, false, pal);               // near side crosses the horizon
    EndBlendMode();
}

// ---------- app state (file scope so the web main-loop callback can reach it) ----------

static std::vector<Body> bodies;
static Camera2D camera = {0};
static RenderTexture2D sceneRT = {};                // world rendered here, then blurred for panels
static int sceneRTW = 0, sceneRTH = 0;

static float currentMass = 50.0f;
static bool paused = false;
static bool trailsOn = true;
static bool gridOn = true;
static int collisionMode = COLLIDE_MERGE;
static bool tidalDestruction = true;                // Roche-like pull-apart near heavy bodies
static float trailTimer = 0.0f;
static const float trailInterval = 1.0f / 60.0f;   // trail sample rate, independent of FPS
static float trailLength = 240.0f;                  // in samples; /60 = seconds
static bool draggingTrailSlider = false;
static int pendingPattern = PAT_NONE;
static std::vector<Body> previewBodies;             // pattern preview, positions relative to cursor
static bool patternFlicking = false;                // dragging out a pending pattern's launch velocity
static Vector2 patternAnchor = {0, 0};              // world pos where the pattern flick started
static float patternMass = 0.0f;                    // mass the pending pattern preview was built with
static bool draggingBody = false;
static bool dragEngaged = false;                    // mouse moved past the deadzone: really dragging
static bool draggingSlider = false;
static int dragIndex = -1;
static Vector2 dragOffset = {0, 0};
static Vector2 dragAnchor = {0, 0};                 // mouse world pos at press, for the deadzone
static const float flickScale = 2.0f;               // px of pull -> px/s of launch speed
static const float flickDeadzone = 6.0f;            // screen px; below this it's a plain click
static const float previewDt = 1.0f / 60.0f;        // trajectory preview integrator step
static const int previewSteps = 300;                // 300 * 1/60 = 5s of lookahead
static const float previewDotSpacing = 7.0f;        // screen px between preview dots
static bool vectorsOn = false;                      // per-body velocity arrows
static bool fieldOn = false;                        // gravity field visualization
static bool curvatureOn = false;                    // shader-warped spacetime grid
static std::vector<Shockwave> shockwaves;
static float simSpeed = 1.0f;                       // time multiplier, 0.1x - 10x
static bool draggingSpeedSlider = false;
static int followId = -1;                           // body id the camera is locked onto
static bool followCenter = false;                   // camera tracks the barycenter of all bodies
static double lastClickTime = 0;
static int lastClickBodyId = -1;
static bool showControls = false;                   // keyboard/mouse controls overlay
static bool leftCollapsed = false;                  // left panel folded to a corner tab
static bool rightCollapsed = false;                 // right panel folded to a corner tab
static std::vector<float> energyHistory;
static float energySampleTimer = 0.0f;
static constexpr float ENERGY_SAMPLE_INTERVAL = 0.125f;
static constexpr size_t ENERGY_HISTORY_LENGTH = 160;

struct AudioState {
    Sound merge[4] = {};
    int mergeVoice = 0;
    bool ready = false;
    float mergeCooldown = 0.0f;
};

static AudioState audio;

// Build tiny one-shot sounds in memory. This works in native raylib and in the
// web build, without asking the browser to fetch separate audio files.
static Sound MakeTone(float seconds, float startHz, float endHz, float volume) {
    constexpr int sampleRate = 22050;
    const unsigned int frames = (unsigned int)(seconds * sampleRate);
    auto* samples = (short*)MemAlloc(frames * sizeof(short));
    for (unsigned int i = 0; i < frames; i++) {
        float t = (float)i / frames;
        float hz = startHz + (endHz - startHz) * t;
        float env = (1.0f - t) * (1.0f - t);
        float phase = 2.0f * PI * hz * ((float)i / sampleRate);
        float wave = sinf(phase);
        samples[i] = (short)(Clamp(wave * env * volume, -1.0f, 1.0f) * 32767.0f);
    }
    Wave wave = {frames, sampleRate, 16, 1, samples};
    Sound sound = LoadSoundFromWave(wave);
    UnloadWave(wave);
    return sound;
}

static void InitSandboxAudio() {
    InitAudioDevice();
    if (!IsAudioDeviceReady()) return;
    audio.merge[0] = MakeTone(0.13f, 190.0f, 105.0f, 0.34f);
    for (int i = 1; i < 4; i++) audio.merge[i] = LoadSoundAlias(audio.merge[0]);
    audio.ready = true;
}

static void PlayImpactAudio(const std::vector<ImpactEvent>& impacts) {
    if (!audio.ready) return;
    audio.mergeCooldown = fmaxf(0.0f, audio.mergeCooldown - GetFrameTime());

    for (const ImpactEvent& impact : impacts) {
        if (audio.mergeCooldown <= 0.0f) {
            float pitch = Clamp(0.68f + log2f(impact.mass / 50.0f) * 0.13f, 0.65f, 1.6f);
            Sound sound = audio.merge[audio.mergeVoice++ % 4];
            SetSoundPitch(sound, pitch);
            SetSoundVolume(sound, 1.0f);
            PlaySound(sound);
            audio.mergeCooldown = 0.045f;
        }
    }

}

static void CloseSandboxAudio() {
    if (!audio.ready) return;
    for (int i = 1; i < 4; i++) UnloadSoundAlias(audio.merge[i]);
    UnloadSound(audio.merge[0]);
    CloseAudioDevice();
    audio.ready = false;
}

static float TotalEnergy(const std::vector<Body>& scene) {
    float kinetic = 0.0f;
    float potential = 0.0f;
    for (size_t i = 0; i < scene.size(); i++) {
        kinetic += 0.5f * scene[i].mass * Vector2LengthSqr(scene[i].vel);
        for (size_t j = i + 1; j < scene.size(); j++) {
            Vector2 delta = Vector2Subtract(scene[j].pos, scene[i].pos);
            // signed masses: a white hole pair's repulsive potential is positive
            potential -= G * GravMass(scene[i]) * GravMass(scene[j]) /
                         sqrtf(Vector2LengthSqr(delta) + SOFTENING2);
        }
    }
    return kinetic + potential;
}

static void SampleEnergy(float simDt) {
    energySampleTimer += simDt;
    if (energySampleTimer < ENERGY_SAMPLE_INTERVAL) return;
    energySampleTimer = 0.0f;
    energyHistory.push_back(TotalEnergy(bodies));
    if (energyHistory.size() > ENERGY_HISTORY_LENGTH) energyHistory.erase(energyHistory.begin());
}

static void DrawEnergyGraph(Rectangle area) {
    DrawRectangleRec(area, Fade(BLACK, 0.45f));
    DrawRectangleLinesEx(area, 1.0f, UI_BORDER);
    if (energyHistory.size() < 2) return;

    float low = *std::min_element(energyHistory.begin(), energyHistory.end());
    float high = *std::max_element(energyHistory.begin(), energyHistory.end());
    float span = high - low;
    // Avoid magnifying harmless integration drift into a dramatic spike.
    float minimumSpan = fmaxf(1.0f, fabsf(energyHistory.back()) * 0.002f);
    if (span < minimumSpan) {
        float mid = (high + low) * 0.5f;
        low = mid - minimumSpan * 0.5f;
        high = mid + minimumSpan * 0.5f;
        span = minimumSpan;
    }
    for (size_t i = 1; i < energyHistory.size(); i++) {
        float x0 = area.x + area.width * (float)(i - 1) / (ENERGY_HISTORY_LENGTH - 1);
        float x1 = area.x + area.width * (float)i / (ENERGY_HISTORY_LENGTH - 1);
        float y0 = area.y + area.height * (1.0f - (energyHistory[i - 1] - low) / span);
        float y1 = area.y + area.height * (1.0f - (energyHistory[i] - low) / span);
        DrawLineEx({x0, y0}, {x1, y1}, 1.5f, CLITERAL(Color){100, 210, 255, 255});
    }
}

static void UpdateDrawFrame() {
    float dt = GetFrameTime();
    int screenWidth = GetScreenWidth();
    int screenHeight = GetScreenHeight();
    camera.offset = {screenWidth / 2.0f, screenHeight / 2.0f};
    // Full-height panels flush with the screen edges; either can collapse to a
    // small corner tab.
    const Rectangle panel = {screenWidth - 252.0f, 0.0f, 252.0f, (float)screenHeight};
    const Rectangle leftPanel = {0.0f, 0.0f, 252.0f, (float)screenHeight};
    const Rectangle leftTab = {6.0f, 6.0f, 32.0f, 32.0f};
    const Rectangle rightTab = {screenWidth - 38.0f, 6.0f, 32.0f, 32.0f};
    Vector2 mouseScreen = GetMousePosition();
    Vector2 mouseWorld = GetScreenToWorld2D(mouseScreen, camera);
    // controls overlay pops out to the left of the right panel, bottom-aligned
    const Rectangle controlsPanel = {panel.x - 488.0f, screenHeight - 397.0f, 480.0f, 397.0f};
    bool mouseOverUI = (rightCollapsed ? CheckCollisionPointRec(mouseScreen, rightTab)
                                       : CheckCollisionPointRec(mouseScreen, panel)) ||
                       (leftCollapsed ? CheckCollisionPointRec(mouseScreen, leftTab)
                                      : CheckCollisionPointRec(mouseScreen, leftPanel)) ||
                       (showControls && !rightCollapsed &&
                        CheckCollisionPointRec(mouseScreen, controlsPanel)) ||
                       draggingSlider || draggingTrailSlider || draggingSpeedSlider;

    // pan with right or middle mouse drag (manual pan breaks follow mode)
    if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT) || IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)) {
        Vector2 delta = GetMouseDelta();
        if (delta.x != 0 || delta.y != 0) { followId = -1; followCenter = false; }
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

    // the single-body patterns are sized by the mass selection: rebuild the
    // pending preview whenever the mass changes so the ghost stays honest
    if ((pendingPattern == PAT_PLANET || pendingPattern == PAT_BLACKHOLE ||
         pendingPattern == PAT_WHITEHOLE) &&
        currentMass != patternMass) {
        previewBodies = MakePattern(pendingPattern, {0, 0}, currentMass);
        patternMass = currentMass;
    }

    // Esc cancels an in-progress launch, follow mode, or the controls overlay;
    // the armed pattern stays
    if (IsKeyPressed(KEY_ESCAPE)) {
        patternFlicking = false;
        followId = -1;
        followCenter = false;
        showControls = false;
    }

    // left press: grab an existing body on a hit (double-click follows); on
    // empty space, start a pattern flick — the pattern lands on release, at
    // rest for a plain click or launched along the pull direction
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !mouseOverUI) {
        draggingBody = false;
        dragIndex = -1;
        bool hitBody = false;
        for (int i = (int)bodies.size() - 1; i >= 0; i--) {
            if (Vector2Distance(bodies[i].pos, mouseWorld) <= MassToRadius(bodies[i].mass)) {
                hitBody = true;
                double now = GetTime();
                if (now - lastClickTime < 0.35 && lastClickBodyId == bodies[i].id) {
                    followId = bodies[i].id;   // double-click: follow instead of drag
                    followCenter = false;
                } else {
                    draggingBody = true;
                    dragEngaged = false;
                    dragIndex = i;
                    dragOffset = Vector2Subtract(bodies[i].pos, mouseWorld);
                    dragAnchor = mouseWorld;
                }
                lastClickTime = now;
                lastClickBodyId = bodies[i].id;
                break;
            }
        }
        if (!hitBody) {
            lastClickTime = GetTime();
            lastClickBodyId = -1;
            patternFlicking = true;
            patternAnchor = mouseWorld;
        }
    }
    if (draggingBody && IsMouseButtonDown(MOUSE_BUTTON_LEFT) && dragIndex >= 0 &&
        dragIndex < (int)bodies.size()) {
        if (!dragEngaged) {
            // deadzone: a plain click (including the first click of a
            // double-click) must not disturb the body; only real mouse
            // motion starts a drag
            Vector2 dragDelta = Vector2Subtract(mouseWorld, dragAnchor);
            if (Vector2Length(dragDelta) * camera.zoom >= flickDeadzone) {
                dragEngaged = true;
                dragOffset = Vector2Subtract(bodies[dragIndex].pos, mouseWorld);
            }
        }
        if (dragEngaged) {
            bodies[dragIndex].pos = Vector2Add(mouseWorld, dragOffset);
            bodies[dragIndex].vel = {0, 0};
        }
    }
    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        if (patternFlicking) {
            // slingshot the whole pattern: every body gets the launch velocity
            Vector2 pull = Vector2Subtract(patternAnchor, mouseWorld);
            Vector2 launchVel = (Vector2Length(pull) * camera.zoom < flickDeadzone)
                                    ? Vector2{0, 0}
                                    : Vector2Scale(pull, flickScale);
            for (Body b : previewBodies) {
                b.pos = Vector2Add(b.pos, patternAnchor);
                b.vel = Vector2Add(b.vel, launchVel);
                bodies.push_back(b);
            }
            // the pattern stays armed for the next placement
        }
        patternFlicking = false;
        draggingBody = false;
        dragEngaged = false;
        dragIndex = -1;
    }

    if (IsKeyPressed(KEY_SPACE)) paused = !paused;
    if (IsKeyPressed(KEY_C)) {
        bodies.clear();
        energyHistory.clear();
        energySampleTimer = 0.0f;
    }
    if (IsKeyPressed(KEY_T)) trailsOn = !trailsOn;
    if (IsKeyPressed(KEY_G)) gridOn = !gridOn;
    if (IsKeyPressed(KEY_F)) ToggleBorderlessWindowed();
    if (IsKeyPressed(KEY_M)) collisionMode = (collisionMode + 1) % 3;
    if (IsKeyPressed(KEY_D)) tidalDestruction = !tidalDestruction;
    if (IsKeyPressed(KEY_V)) vectorsOn = !vectorsOn;
    if (IsKeyPressed(KEY_B)) fieldOn = !fieldOn;
    if (IsKeyPressed(KEY_W)) curvatureOn = !curvatureOn;
    // Tab: collapse both panels for a clean view, or bring both back
    if (IsKeyPressed(KEY_TAB)) {
        bool anyOpen = !leftCollapsed || !rightCollapsed;
        leftCollapsed = anyOpen;
        rightCollapsed = anyOpen;
    }

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
    if (IsKeyPressed(KEY_J)) {
        followCenter = !followCenter;
        if (followCenter) followId = -1;
    }
    if (IsKeyPressed(KEY_R)) {
        camera.target = {0, 0};
        camera.zoom = 1.0f;
        followId = -1;
        followCenter = false;
    }

    // sim time: scaled by simSpeed; '.' advances one frame while paused
    bool doStep = !paused;
    float stepDt = dt * simSpeed;
    if (paused && IsKeyPressed(KEY_PERIOD)) {
        doStep = true;
        stepDt = (1.0f / 60.0f) * simSpeed;
    }
    if (doStep) {
        trailTimer += stepDt;
        bool recordTrail = trailTimer >= trailInterval;
        if (recordTrail) trailTimer = 0.0f;

        std::vector<ImpactEvent> impacts;
        const int substeps = 2;
        for (int s = 0; s < substeps; s++) {
            StepPhysics(bodies, stepDt / substeps, trailsOn, collisionMode, tidalDestruction,
                        recordTrail && s == substeps - 1, (int)trailLength, &impacts);
        }
        PlayImpactAudio(impacts);
        SampleEnergy(stepDt);
        for (const ImpactEvent& im : impacts) {
            if (im.isBlackHole) continue;   // black holes swallow silently: no ripple
            float maxR = Clamp(im.radius * 2.0f + sqrtf(im.energy) * 0.02f, 30.0f, 240.0f);
            shockwaves.push_back({im.pos, 0.0f, maxR, im.radius});
        }
        for (Shockwave& sw : shockwaves) sw.age += stepDt;
        shockwaves.erase(std::remove_if(shockwaves.begin(), shockwaves.end(),
                                         [](const Shockwave& s) { return s.age >= SHOCKWAVE_LIFE; }),
                         shockwaves.end());
    }

    // follow mode: keep the camera locked on the followed body / mass center
    if (followCenter) centerOnBodies();
    if (followId != -1) {
        bool found = false;
        for (const Body& b : bodies) {
            if (b.id == followId) {
                camera.target = b.pos;
                found = true;
                break;
            }
        }
        if (!found) followId = -1;   // body merged away or was cleared
    }

    BeginDrawing();
    ClearBackground(BLACK);

    // raylib's BeginMode2D doesn't apply the HiDPI screen scale (screen-space
    // drawing does), so render the world through a DPI-adjusted camera while
    // keeping `camera` in logical units for all input math. Derive the scale
    // from the actual framebuffer ratio: on web GetWindowScaleDPI() reports
    // devicePixelRatio even though the framebuffer is logical-sized.
    float fbScale = (float)GetRenderWidth() / (float)screenWidth;

    // the world renders offscreen so the UI can blur what's behind its panels
    if (GetRenderWidth() != sceneRTW || GetRenderHeight() != sceneRTH) {
        if (sceneRT.id != 0) UnloadRenderTexture(sceneRT);
        sceneRTW = GetRenderWidth();
        sceneRTH = GetRenderHeight();
        sceneRT = LoadRenderTexture(sceneRTW, sceneRTH);
        SetTextureFilter(sceneRT.texture, TEXTURE_FILTER_BILINEAR);
    }
    BeginTextureMode(sceneRT);
    ClearBackground(BLACK);

    // backdrop first, in logical pixels, so everything else layers over it
    {
        Camera2D bgCam = {};
        bgCam.zoom = fbScale;
        BeginMode2D(bgCam);
        DrawSpaceBackground(screenWidth, screenHeight, camera.target);
        EndMode2D();
    }

    // shader-warped spacetime grid replaces the flat grid while active
    bool curvedGrid = curvatureOn && CurvatureReady();
    if (curvedGrid) CurvatureDraw(bodies, camera, screenWidth, screenHeight, fbScale);

    Camera2D camRender = camera;
    camRender.offset = {camera.offset.x * fbScale, camera.offset.y * fbScale};
    camRender.zoom = camera.zoom * fbScale;
    BeginMode2D(camRender);
    if (gridOn && !curvedGrid) DrawSpaceGrid(camera, screenWidth, screenHeight);

    if (fieldOn) {
        // gravity vector field sampled on a screen-space grid; arrow length
        // and brightness follow log-magnitude, so Lagrange regions show as gaps.
        // One Barnes-Hut tree per frame keeps each sample O(log n).
        BHField field;
        field.Build(bodies);
        const float fieldSpacing = 48.0f;
        for (float sy = fieldSpacing / 2; sy < screenHeight; sy += fieldSpacing) {
            for (float sx = fieldSpacing / 2; sx < screenWidth; sx += fieldSpacing) {
                Vector2 wp = GetScreenToWorld2D({sx, sy}, camera);
                Vector2 gvec = field.AccelAt(wp);
                float mag = Vector2Length(gvec);
                if (mag < 1.0f) continue;
                float t = Clamp(log10f(mag) / 4.0f, 0.0f, 1.0f);
                float len = (6.0f + 14.0f * t) / camera.zoom;
                Vector2 dir = Vector2Scale(gvec, 1.0f / mag);
                Vector2 tip = Vector2Add(wp, Vector2Scale(dir, len));
                Color fc = Fade(CLITERAL(Color){140, 180, 255, 255}, 0.12f + 0.5f * t);
                float thick = 1.2f / camera.zoom;
                DrawLineEx(wp, tip, thick, fc);
                float head = 4.0f / camera.zoom;
                DrawLineEx(tip, Vector2Add(tip, Vector2Scale(Vector2Rotate(dir, 150 * DEG2RAD), head)), thick, fc);
                DrawLineEx(tip, Vector2Add(tip, Vector2Scale(Vector2Rotate(dir, -150 * DEG2RAD), head)), thick, fc);
            }
        }
    }

    // stride trail points when crowded so draw cost stays bounded
    int trailStride = 1 + (int)(bodies.size() / 300);
    bool dustTrails = bodies.size() < 500;   // in dense scenes dust reads better without trails
    float dustSize = fmaxf(2.4f, 2.0f / camera.zoom);   // never shrinks below ~2 screen px
    for (auto& b : bodies) {
        // holes are never dust: tiny ones still render as horizon + disk,
        // and the bright dust quad must not bleed through the event horizon
        bool dust = IsDust(b.mass) && !b.isBlackHole && !b.isWhiteHole;
        if (trailsOn && (dustTrails || !dust) && (int)b.trail.size() > trailStride) {
            float trailWidth = std::max(2.0f, MassToRadius(b.mass) * 0.35f);
            for (size_t k = trailStride; k < b.trail.size(); k += trailStride) {
                float a = (float)k / b.trail.size();
                Color c = Fade(b.color, a * 0.6f);
                DrawLineEx(b.trail[k - trailStride], b.trail[k], trailWidth * a, c);
            }
        }
        if (dust) {
            // bright quad core; the soft halo comes from the additive pass below
            Color core = {(unsigned char)((b.color.r + 255) / 2),
                          (unsigned char)((b.color.g + 255) / 2),
                          (unsigned char)((b.color.b + 255) / 2), 255};
            DrawRectangleRec({b.pos.x - dustSize / 2, b.pos.y - dustSize / 2, dustSize, dustSize},
                             core);
        } else if (!b.isBlackHole && !b.isWhiteHole) {
            DrawCircleV(b.pos, MassToRadius(b.mass), b.color);
        }
        // black and white holes render in their own layered pass below
    }

    // holes: far disk behind the horizon, photon ring + near disk over it
    float bhTime = (float)GetTime();
    for (const Body& b : bodies) {
        if (b.isBlackHole) DrawHoleFX(b.pos, MassToRadius(b.mass), bhTime, BH_PALETTE);
        else if (b.isWhiteHole) DrawHoleFX(b.pos, MassToRadius(b.mass), bhTime, WH_PALETTE);
    }

    // additive pass: dust glow, hot halos on heavy bodies, then impact shockwaves
    BeginBlendMode(BLEND_ADDITIVE);
    float dustGlow = dustSize * 2.6f;
    for (const Body& b : bodies) {
        if (!IsDust(b.mass) || b.isBlackHole || b.isWhiteHole) continue;
        DrawRectangleRec({b.pos.x - dustGlow / 2, b.pos.y - dustGlow / 2, dustGlow, dustGlow},
                         Fade(b.color, 0.22f));
    }
    for (const Body& b : bodies) {
        if (b.mass < 800.0f || b.isBlackHole || b.isWhiteHole) continue;
        float r = MassToRadius(b.mass);
        float halo = r * (1.8f + std::min(b.mass / 10000.0f, 1.2f));
        DrawCircleV(b.pos, halo, Fade(b.color, 0.10f));
        DrawCircleV(b.pos, halo * 0.55f, Fade(b.color, 0.16f));
        DrawCircleV(b.pos, halo * 0.30f, Fade(b.color, 0.25f));
    }
    for (const Shockwave& sw : shockwaves) {
        float t = sw.age / SHOCKWAVE_LIFE;
        float ease = 1.0f - (1.0f - t) * (1.0f - t);
        float ringR = sw.baseR + (sw.maxR - sw.baseR) * ease;
        Color warm = {255, 220, 150, 255};
        DrawRing(sw.pos, ringR - 3.0f / camera.zoom, ringR, 0, 360, 48,
                 Fade(warm, (1.0f - t) * 0.55f));
        if (t < 0.25f) {
            DrawCircleV(sw.pos, sw.baseR * (1.0f + t * 2.0f),
                        Fade(CLITERAL(Color){255, 240, 200, 255}, (0.25f - t) * 2.2f));
        }
    }
    EndBlendMode();
    if (vectorsOn) {
        // velocity arrows: tip = position 0.35s from now at current velocity
        for (const Body& b : bodies) {
            float sp = Vector2Length(b.vel);
            if (sp < 1.0f) continue;
            Vector2 tip = Vector2Add(b.pos, Vector2Scale(b.vel, 0.35f));
            float thick = 1.5f / camera.zoom;
            Color vc = Fade(WHITE, 0.65f);
            DrawLineEx(b.pos, tip, thick, vc);
            Vector2 dir = Vector2Scale(b.vel, 1.0f / sp);
            float head = 8.0f / camera.zoom;
            DrawLineEx(tip, Vector2Add(tip, Vector2Scale(Vector2Rotate(dir, 150 * DEG2RAD), head)),
                       thick, vc);
            DrawLineEx(tip, Vector2Add(tip, Vector2Scale(Vector2Rotate(dir, -150 * DEG2RAD), head)),
                       thick, vc);
        }
    }
    if (followId != -1) {
        // highlight ring on the followed body
        for (const Body& b : bodies) {
            if (b.id == followId) {
                DrawCircleLinesV(b.pos, MassToRadius(b.mass) + 5.0f / camera.zoom,
                                 Fade(WHITE, 0.8f));
                break;
            }
        }
    }
    if (pendingPattern != PAT_NONE && !mouseOverUI) {
        // ghost preview of the armed pattern: follows the cursor, pinned to
        // the anchor while a launch is being dragged out
        Vector2 anchor = patternFlicking ? patternAnchor : mouseWorld;
        float ghostDust = fmaxf(2.4f, 2.0f / camera.zoom);
        for (const Body& b : previewBodies) {
            Vector2 p = Vector2Add(b.pos, anchor);
            if (IsDust(b.mass) && !b.isBlackHole && !b.isWhiteHole) {
                DrawRectangleRec({p.x - ghostDust / 2, p.y - ghostDust / 2, ghostDust, ghostDust},
                                 Fade(b.color, 0.5f));
            } else {
                DrawCircleV(p, MassToRadius(b.mass), Fade(b.color, 0.4f));
                DrawCircleLinesV(p, MassToRadius(b.mass), Fade(WHITE, 0.25f));
            }
        }
        DrawCircleLinesV(anchor, 6.0f / camera.zoom, Fade(WHITE, 0.6f));
        if (patternFlicking) {
            Vector2 pull = Vector2Subtract(patternAnchor, mouseWorld);
            if (Vector2Length(pull) * camera.zoom >= flickDeadzone) {
                // trajectory preview for the single-body patterns (planet,
                // black hole): the candidate as a test particle in the frozen
                // field of the current bodies, ~5s ahead. The path runs
                // through bodies it would collide with, so the full arc stays
                // visible. Dots are placed at fixed screen-space spacing so
                // the curve reads as dotted at any speed or zoom.
                if (previewBodies.size() == 1) {
                    Color previewColor = previewBodies[0].color;
                    // a white hole candidate is repelled by the field, not pulled
                    float fieldSign = previewBodies[0].isWhiteHole ? -1.0f : 1.0f;
                    Vector2 pos = patternAnchor;
                    Vector2 vel = Vector2Scale(pull, flickScale);
                    float sinceDot = previewDotSpacing;   // draw a dot on the first step
                    for (int i = 0; i < previewSteps; i++) {
                        Vector2 acc = Vector2Scale(GravityFieldAt(bodies, pos), fieldSign);
                        vel = Vector2Add(vel, Vector2Scale(acc, previewDt));
                        pos = Vector2Add(pos, Vector2Scale(vel, previewDt));
                        sinceDot += Vector2Length(vel) * previewDt * camera.zoom;
                        if (sinceDot >= previewDotSpacing) {
                            sinceDot = 0.0f;
                            float fadeT = 1.0f - (float)i / previewSteps;
                            DrawCircleV(pos, 2.0f / camera.zoom,
                                        Fade(previewColor, 0.15f + 0.6f * fadeT));
                        }
                    }
                }
                DrawLineEx(patternAnchor, mouseWorld, 1.5f / camera.zoom, Fade(WHITE, 0.25f));
                Vector2 tip = Vector2Add(patternAnchor, pull);
                float thick = 2.5f / camera.zoom;
                DrawLineEx(patternAnchor, tip, thick, WHITE);
                Vector2 dir = Vector2Normalize(pull);
                float head = 12.0f / camera.zoom;
                DrawLineEx(tip, Vector2Add(tip, Vector2Scale(Vector2Rotate(dir, 150 * DEG2RAD), head)),
                           thick, WHITE);
                DrawLineEx(tip, Vector2Add(tip, Vector2Scale(Vector2Rotate(dir, -150 * DEG2RAD), head)),
                           thick, WHITE);
            }
        }
    }
    EndMode2D();
    EndTextureMode();

    UIBackdropProcess(sceneRT.texture, screenWidth, screenHeight);

    // Screen-space UI is authored in logical (CSS) pixels. On the web the
    // high-DPI backing buffer is larger, so give this pass the same scale as
    // the world camera instead of drawing it at raw framebuffer coordinates.
    Camera2D uiRender = {};
    uiRender.zoom = fbScale;
    BeginMode2D(uiRender);

    // present the offscreen scene (negative source height: render textures
    // store their content vertically flipped)
    DrawTexturePro(sceneRT.texture, {0, 0, (float)sceneRTW, -(float)sceneRTH},
                   {0, 0, (float)screenWidth, (float)screenHeight}, {0, 0}, 0, WHITE);

    // launch speed readout, in screen space next to the arrow tip
    if (patternFlicking) {
        Vector2 pull = Vector2Subtract(patternAnchor, mouseWorld);
        if (Vector2Length(pull) * camera.zoom >= flickDeadzone) {
            Vector2 tipScreen = GetWorldToScreen2D(Vector2Add(patternAnchor, pull), camera);
            UIText(TextFormat("%.0f", Vector2Length(pull) * flickScale),
                   tipScreen.x + 10, tipScreen.y - 8, 16, UI_VALUE);
        }
    }

    // ---- Left UI panel (Mass and Patterns) ----
    if (leftCollapsed) {
        if (UIChevron(leftTab, false, true, "Expand the patterns panel")) leftCollapsed = false;
    } else {
    DrawPanel(leftPanel, BLANK);
    if (UIChevron({leftPanel.width - 34.0f, 6.0f, 28.0f, 28.0f}, true, false,
                  "Collapse the panel"))
        leftCollapsed = true;

    float lpx = leftPanel.x + 14, lpw = leftPanel.width - 28;
    float ly = leftPanel.y + 12;

    const char* massTxt = TextFormat("%.0f", currentMass);
    UISectionHeader("MASS", lpx, ly, lpw - UITextWidth(massTxt, 18) - 10);
    // right margin leaves room for the collapse chevron above
    UIText(massTxt, leftPanel.x + leftPanel.width - 44 - UITextWidth(massTxt, 18), ly, 18, UI_VALUE);
    ly += 26;
    currentMass = UISliderLog({lpx, ly, lpw, 24}, currentMass, MASS_MIN, MASS_MAX, &draggingSlider,
                              "Mass of newly placed dots (log scale; Up/Down keys work too)");
    ly += 34;

    UISectionHeader("PATTERNS", lpx, ly, lpw);
    ly += 26;

    auto patternButton = [&](Rectangle r, const char* label, int type, const char* tip) {
        // one pattern is always armed, so clicking the active one is a no-op
        if (UIToggle(r, label, pendingPattern == type, tip) && pendingPattern != type) {
            pendingPattern = type;
            previewBodies = MakePattern(type, {0, 0}, currentMass);
            patternMass = currentMass;
        }
    };
    float colW = (lpw - 8) / 2;
    float col2 = lpx + colW + 8;
    patternButton({lpx, ly, colW, 32}, "Solar Sys", PAT_SOLAR, "A star with six orbiting planets");
    patternButton({col2, ly, colW, 32}, "Binary", PAT_BINARY, "Two stars orbiting their shared center");
    ly += 36;
    patternButton({lpx, ly, colW, 32}, "Ring", PAT_RING, "A star with a ring of small bodies");
    patternButton({col2, ly, colW, 32}, "Galaxy", PAT_GALAXY,
                  "A spiral galaxy orbiting a central black hole");
    ly += 36;
    patternButton({lpx, ly, colW, 32}, "Grid", PAT_GRID, "A grid of dots at rest that collapses into clumps");
    patternButton({col2, ly, colW, 32}, "Cloud", PAT_CLOUD, "A random cloud at rest that collapses into clumps");
    ly += 36;
    patternButton({lpx, ly, colW, 32}, "Figure-8", PAT_FIGURE8, "Three equal bodies in a stable figure-8 orbit");
    patternButton({col2, ly, colW, 32}, "Moons", PAT_MOONS, "A star with two planets, each with its own moon");
    ly += 36;
    patternButton({lpx, ly, colW, 32}, "Collision", PAT_COLLIDE,
                  "Two galaxies with black hole cores drifting into each other");
    patternButton({col2, ly, colW, 32}, "Comets", PAT_COMETS, "Comets on long elliptical orbits around a star");
    ly += 36;
    patternButton({lpx, ly, colW, 32}, "Planet", PAT_PLANET,
                  "A single planet sized by the MASS slider");
    patternButton({col2, ly, colW, 32}, "Black Hole", PAT_BLACKHOLE,
                  "A black hole with a swirling accretion disk; sized by the MASS slider");
    ly += 36;
    patternButton({lpx, ly, colW, 32}, "White Hole", PAT_WHITEHOLE,
                  "A white hole that repels matter and can't be entered; sized by the MASS slider");
    }   // end left panel

    // ---- Right UI panel ----
    if (rightCollapsed) {
        if (UIChevron(rightTab, true, true, "Expand the controls panel")) rightCollapsed = false;
    } else {
    DrawPanel(panel, BLANK);
    if (UIChevron({panel.x + panel.width - 34.0f, 6.0f, 28.0f, 28.0f}, false, false,
                  "Collapse the panel"))
        rightCollapsed = true;

    float px = panel.x + 14, pw = panel.width - 28;
    float y = panel.y + 12;

    UISectionHeader("VIEW", px, y, pw);
    y += 26;
    float halfW = (pw - 8) / 2;
    if (UIToggle({px, y, halfW, 32}, "Trails (T)", trailsOn, "Fading orbit trails behind each dot"))
        trailsOn = !trailsOn;
    if (UIToggle({px + halfW + 8, y, halfW, 32}, "Grid (G)", gridOn,
                 "Reference grid and origin axes")) gridOn = !gridOn;
    y += 36;
    if (UIToggle({px, y, halfW, 32}, "Vectors (V)", vectorsOn, "Velocity arrow on each dot"))
        vectorsOn = !vectorsOn;
    if (UIToggle({px + halfW + 8, y, halfW, 32}, "Field (B)", fieldOn,
                 "Gravity field arrows; gaps mark Lagrange regions")) fieldOn = !fieldOn;
    y += 36;
    if (UIToggle({px, y, pw, 32}, "Space Curvature (W)", curvatureOn,
                 "Grid bends into gravity wells (GPU shader)")) curvatureOn = !curvatureOn;
    y += 36;
    UISectionHeader("COLLISION (M)", px, y, pw);
    y += 26;
    float w3 = (pw - 16) / 3;
    if (UIToggle({px, y, w3, 32}, "None", collisionMode == COLLIDE_NONE,
                 "Dots pass through each other"))
        collisionMode = COLLIDE_NONE;
    if (UIToggle({px + w3 + 8, y, w3, 32}, "Merge", collisionMode == COLLIDE_MERGE,
                 "Colliding dots combine, conserving mass and momentum"))
        collisionMode = COLLIDE_MERGE;
    if (UIToggle({px + 2 * (w3 + 8), y, w3, 32}, "Debris", collisionMode == COLLIDE_DEBRIS,
                 "Merge, spraying part of the smaller dot out as fragments"))
        collisionMode = COLLIDE_DEBRIS;
    y += 36;
    if (UIToggle({px, y, pw, 32}, "Tidal Destruction (D)", tidalDestruction,
                 "Heavy dots pull small dots apart on close passes"))
        tidalDestruction = !tidalDestruction;
    y += 36;

    const char* trailTxt = TextFormat("%.1fs", trailLength / 60.0f);
    UISectionHeader("TRAIL LENGTH", px, y, pw - UITextWidth(trailTxt, 18) - 10);
    UIText(trailTxt, panel.x + panel.width - 14 - UITextWidth(trailTxt, 18), y, 18, UI_VALUE);
    y += 26;
    trailLength = UISliderLog({px, y, pw, 24}, trailLength, TRAIL_LEN_MIN, TRAIL_LEN_MAX,
                               &draggingTrailSlider, "How long trails persist, in seconds");
    y += 34;

    const char* speedTxt = TextFormat("%.1fx", simSpeed);
    UISectionHeader("TIME", px, y, pw - UITextWidth(speedTxt, 18) - 10);
    UIText(speedTxt, panel.x + panel.width - 14 - UITextWidth(speedTxt, 18), y, 18, UI_VALUE);
    y += 26;
    simSpeed = UISliderLog({px, y, pw, 24}, simSpeed, 0.1f, 10.0f, &draggingSpeedSlider,
                           "Simulation speed multiplier");
    y += 34;

    if (UIButton({px, y, pw, 32}, "Reset View (R)", "Return the camera to the origin")) {
        camera.target = {0, 0};
        camera.zoom = 1.0f;
        followId = -1;
        followCenter = false;
    }
    y += 36;
    if (UIButton({px, y, halfW, 32}, "Center (H)",
                 "Move the camera to the mass center of all dots")) centerOnBodies();
    if (UIToggle({px + halfW + 8, y, halfW, 32}, "Follow (J)", followCenter,
                 "Keep the camera locked on the mass center of all dots")) {
        followCenter = !followCenter;
        if (followCenter) followId = -1;
    }
    y += 36;
    if (UIButton({px, y, pw, 32}, "Fullscreen (F)", "Toggle borderless fullscreen"))
        ToggleBorderlessWindowed();
    y += 36;
    if (UIButton({px, y, pw, 32}, "Clear Canvas (C)", "Remove every dot from the scene")) {
        bodies.clear();
        energyHistory.clear();
        energySampleTimer = 0.0f;
    }
    y += 36;

    // ---- stats (FPS / bodies / energy + graph) ----
    UISectionHeader("STATS", px, y, pw);
    y += 26;
    UIText("FPS", px, y, 15, UI_LABEL);
    UIText(TextFormat("%d", GetFPS()), px + UITextWidth("FPS", 15) + 6, y, 15, UI_VALUE);
    float bodiesX = px + pw / 2;
    UIText("BODIES", bodiesX, y, 15, UI_LABEL);
    UIText(TextFormat("%d", (int)bodies.size()), bodiesX + UITextWidth("BODIES", 15) + 6, y, 15,
           UI_VALUE);
    y += 22;
    UIText("ENERGY", px, y, 15, UI_LABEL);
    const char* deltaTxt = "0.00%";
    Color energyCol = UI_VALUE;
    if (!energyHistory.empty()) {
        float first = energyHistory.front();
        float change = (fabsf(first) > 1.0f) ? (energyHistory.back() - first) / fabsf(first) * 100.0f : 0.0f;
        deltaTxt = TextFormat("%+.2f%%", change);
        if (fabsf(change) < 0.2f) {
            energyCol = CLITERAL(Color){100, 210, 255, 255};
        }
    }
    UIText(deltaTxt, px + UITextWidth("ENERGY", 15) + 6, y, 15, energyCol);
    if (CheckCollisionPointRec(mouseScreen, {px, y, pw / 2, 18})) {
        UITooltip("Kinetic + potential energy of the whole system");
    }
    y += 22;
    DrawEnergyGraph({px, y, pw, 22});
    y += 30;

    if (UIToggle({px, y, pw, 32}, "View Controls", showControls,
                 "Every mouse and keyboard control"))
        showControls = !showControls;
    }   // end right panel

    // ---- paused banner (top-center) ----
    if (paused) {
        const char* pauseTxt = "PAUSED  -  SPACE to resume  -  [.] to step";
        float ptw = UITextWidth(pauseTxt, 20);
        Rectangle banner = {(screenWidth - ptw) / 2.0f - 18, 10, ptw + 36.0f, 40};
        DrawPanel(banner, UI_BORDER_LIT);
        UIText(pauseTxt, banner.x + 18, banner.y + 10, 20, GOLD);
    }

    // ---- following banner (top-center, stacks under the paused banner) ----
    if (followId != -1 || followCenter) {
        float fy = 10.0f;
        if (paused) fy += 48;
        const char* followTxt = followCenter ? "FOLLOWING CENTER  -  ESC to stop"
                                             : "FOLLOWING  -  ESC to stop";
        float ftw = UITextWidth(followTxt, 20);
        Rectangle banner = {(screenWidth - ftw) / 2.0f - 18, fy, ftw + 36.0f, 40};
        DrawPanel(banner, UI_BORDER_LIT);
        UIText(followTxt, banner.x + 18, banner.y + 10, 20, UI_VALUE);
    }

    // ---- controls overlay (left of the right panel) ----
    if (showControls && !rightCollapsed) {
        DrawPanel(controlsPanel, BLANK);
        float cx = controlsPanel.x + 14, cw = controlsPanel.width - 28;
        float yy = controlsPanel.y + 12;
        auto row = [&](float x, float y, float keyW, const char* key, const char* action) {
            UIText(key, x, y, 15, UI_VALUE);
            UIText(action, x + keyW, y, 15, UI_LABEL);
        };
        UISectionHeader("MOUSE", cx, yy, cw);
        yy += 24;
        row(cx, yy, 160, "Click", "Place the armed pattern"); yy += 21;
        row(cx, yy, 160, "Drag", "Flick-launch the pattern"); yy += 21;
        row(cx, yy, 160, "Drag a dot", "Move it"); yy += 21;
        row(cx, yy, 160, "Double-click a dot", "Follow it"); yy += 21;
        row(cx, yy, 160, "Right / middle drag", "Pan"); yy += 21;
        row(cx, yy, 160, "Wheel", "Zoom"); yy += 21;
        yy += 10;
        UISectionHeader("KEYBOARD", cx, yy, cw);
        yy += 24;
        static const struct { const char* key; const char* action; } shortcuts[] = {
            {"Space", "Pause"},          {".", "Step one frame"},
            {"Up / Down", "Adjust mass"},{"T", "Trails"},
            {"G", "Grid"},               {"V", "Velocity vectors"},
            {"B", "Gravity field"},      {"W", "Space curvature"},
            {"M", "Collision mode"},     {"D", "Tidal destruction"},
            {"H", "Center on dots"},     {"J", "Follow center"},
            {"R", "Reset view"},         {"F", "Fullscreen"},
            {"C", "Clear canvas"},       {"Esc", "Cancel / stop follow"},
            {"Tab", "Collapse both panels"},
        };
        float colW2 = cw / 2;
        for (int i = 0; i < (int)(sizeof(shortcuts) / sizeof(shortcuts[0])); i++) {
            row(cx + (i % 2) * colW2, yy, 90, shortcuts[i].key, shortcuts[i].action);
            if (i % 2 == 1) yy += 21;
        }
    }

    // tooltips registered by hovered widgets this frame, drawn on top of all UI
    UIDrawTooltip();

    EndMode2D();
    EndDrawing();
}

int main() {
    unsigned int flags = FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE;
    flags |= FLAG_WINDOW_HIGHDPI;
    SetConfigFlags(flags);
    InitWindow(1280, 850, "Gravity Sandbox");
    SetWindowMinSize(800, 600);
    Image icon = LoadImage(TextFormat("%sassets/icon.png", GetApplicationDirectory()));
    if (icon.data) {
        SetWindowIcon(icon);
        UnloadImage(icon);
    }
    SetExitKey(KEY_NULL);   // Esc cancels pattern placement instead of quitting
    UILoadFont();
    InitSandboxAudio();
    UIInitAudio();
    CurvatureLoad();
    UIBackdropInit();

    camera.offset = {GetScreenWidth() / 2.0f, GetScreenHeight() / 2.0f};
    camera.target = {0, 0};
    camera.zoom = 1.0f;

    // a pattern is always armed; start on the plain planet
    pendingPattern = PAT_PLANET;
    previewBodies = MakePattern(PAT_PLANET, {0, 0}, currentMass);
    patternMass = currentMass;

#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop(UpdateDrawFrame, 0, 1);
#else
    while (!WindowShouldClose()) UpdateDrawFrame();
#endif

    UIBackdropUnload();
    if (sceneRT.id != 0) UnloadRenderTexture(sceneRT);
    CurvatureUnload();
    UICloseAudio();
    CloseSandboxAudio();
    CloseWindow();
    return 0;
}
