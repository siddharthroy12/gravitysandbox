#include "raylib.h"
#include "raymath.h"

#include "body.h"
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

// ---------- app state (file scope so the web main-loop callback can reach it) ----------

static std::vector<Body> bodies;
static Camera2D camera = {0};

static float currentMass = 50.0f;
static bool paused = false;
static bool trailsOn = true;
static bool gridOn = true;
static int collisionMode = COLLIDE_MERGE;
static float trailTimer = 0.0f;
static const float trailInterval = 1.0f / 60.0f;   // trail sample rate, independent of FPS
static float trailLength = 240.0f;                  // in samples; /60 = seconds
static bool draggingTrailSlider = false;
static int pendingPattern = PAT_NONE;
static std::vector<Body> previewBodies;             // pattern preview, positions relative to cursor
static bool draggingBody = false;
static bool draggingSlider = false;
static int dragIndex = -1;
static Vector2 dragOffset = {0, 0};
static bool flicking = false;                       // dragging out a new dot's launch velocity
static Vector2 flickAnchor = {0, 0};
static const float flickScale = 2.0f;               // px of pull -> px/s of launch speed
static const float flickDeadzone = 6.0f;            // screen px; below this it's a plain click
static const float previewDt = 1.0f / 60.0f;        // trajectory preview integrator step
static const int previewSteps = 300;                // 300 * 1/60 = 5s of lookahead
static const float previewDotSpacing = 7.0f;        // screen px between preview dots
static bool vectorsOn = false;                      // per-body velocity arrows
static bool fieldOn = false;                        // gravity field visualization
static std::vector<Shockwave> shockwaves;
static float simSpeed = 1.0f;                       // time multiplier, 0.1x - 10x
static bool draggingSpeedSlider = false;
static int followId = -1;                           // body id the camera is locked onto
static double lastClickTime = 0;
static int lastClickBodyId = -1;
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
            potential -= G * scene[i].mass * scene[j].mass /
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
        DrawLineEx({x0, y0}, {x1, y1}, 1.5f, (Color){100, 210, 255, 255});
    }
}

static void UpdateDrawFrame() {
    float dt = GetFrameTime();
    int screenWidth = GetScreenWidth();
    int screenHeight = GetScreenHeight();
    camera.offset = {screenWidth / 2.0f, screenHeight / 2.0f};
    const Rectangle panel = {screenWidth - 262.0f, 10.0f, 252.0f, 718.0f};
    Vector2 mouseScreen = GetMousePosition();
    Vector2 mouseWorld = GetScreenToWorld2D(mouseScreen, camera);
    bool mouseOverUI = CheckCollisionPointRec(mouseScreen, panel) || draggingSlider ||
                       draggingTrailSlider || draggingSpeedSlider;

    // pan with right or middle mouse drag (manual pan breaks follow mode)
    if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT) || IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)) {
        Vector2 delta = GetMouseDelta();
        if (delta.x != 0 || delta.y != 0) followId = -1;
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

    // Esc cancels pending pattern placement, an in-progress flick, or follow mode
    if (IsKeyPressed(KEY_ESCAPE)) {
        pendingPattern = PAT_NONE;
        previewBodies.clear();
        flicking = false;
        followId = -1;
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
        bool hitBody = false;
        for (int i = (int)bodies.size() - 1; i >= 0; i--) {
            if (Vector2Distance(bodies[i].pos, mouseWorld) <= MassToRadius(bodies[i].mass)) {
                hitBody = true;
                double now = GetTime();
                if (now - lastClickTime < 0.35 && lastClickBodyId == bodies[i].id) {
                    followId = bodies[i].id;   // double-click: follow instead of drag
                } else {
                    draggingBody = true;
                    dragIndex = i;
                    dragOffset = Vector2Subtract(bodies[i].pos, mouseWorld);
                }
                lastClickTime = now;
                lastClickBodyId = bodies[i].id;
                break;
            }
        }
        if (!hitBody) {
            lastClickTime = GetTime();
            lastClickBodyId = -1;
        }
        if (!hitBody) {
            // empty space: start a flick; the dot spawns on release
            flicking = true;
            flickAnchor = mouseWorld;
        }
    }
    if (draggingBody && IsMouseButtonDown(MOUSE_BUTTON_LEFT) && dragIndex >= 0 &&
        dragIndex < (int)bodies.size()) {
        bodies[dragIndex].pos = Vector2Add(mouseWorld, dragOffset);
        bodies[dragIndex].vel = {0, 0};
    }
    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        if (flicking) {
            // slingshot: launch opposite the pull direction
            Vector2 pull = Vector2Subtract(flickAnchor, mouseWorld);
            Vector2 vel = (Vector2Length(pull) * camera.zoom < flickDeadzone)
                              ? Vector2{0, 0}
                              : Vector2Scale(pull, flickScale);
            AddBody(bodies, flickAnchor, vel, currentMass);
        }
        flicking = false;
        draggingBody = false;
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
    if (IsKeyPressed(KEY_V)) vectorsOn = !vectorsOn;
    if (IsKeyPressed(KEY_B)) fieldOn = !fieldOn;

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
        followId = -1;
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
            StepPhysics(bodies, stepDt / substeps, trailsOn, collisionMode,
                        recordTrail && s == substeps - 1, (int)trailLength, &impacts);
        }
        PlayImpactAudio(impacts);
        SampleEnergy(stepDt);
        for (const ImpactEvent& im : impacts) {
            float maxR = Clamp(im.radius * 2.0f + sqrtf(im.energy) * 0.02f, 30.0f, 240.0f);
            shockwaves.push_back({im.pos, 0.0f, maxR, im.radius});
        }
        for (Shockwave& sw : shockwaves) sw.age += stepDt;
        shockwaves.erase(std::remove_if(shockwaves.begin(), shockwaves.end(),
                                         [](const Shockwave& s) { return s.age >= SHOCKWAVE_LIFE; }),
                         shockwaves.end());
    }

    // follow mode: keep the camera locked on the followed body
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
    Camera2D camRender = camera;
    camRender.offset = {camera.offset.x * fbScale, camera.offset.y * fbScale};
    camRender.zoom = camera.zoom * fbScale;
    BeginMode2D(camRender);
    if (gridOn) DrawSpaceGrid(camera, screenWidth, screenHeight);

    if (fieldOn) {
        // gravity vector field sampled on a screen-space grid; arrow length
        // and brightness follow log-magnitude, so Lagrange regions show as gaps
        const float fieldSpacing = 48.0f;
        for (float sy = fieldSpacing / 2; sy < screenHeight; sy += fieldSpacing) {
            for (float sx = fieldSpacing / 2; sx < screenWidth; sx += fieldSpacing) {
                Vector2 wp = GetScreenToWorld2D({sx, sy}, camera);
                Vector2 gvec = GravityFieldAt(bodies, wp);
                float mag = Vector2Length(gvec);
                if (mag < 1.0f) continue;
                float t = Clamp(log10f(mag) / 4.0f, 0.0f, 1.0f);
                float len = (6.0f + 14.0f * t) / camera.zoom;
                Vector2 dir = Vector2Scale(gvec, 1.0f / mag);
                Vector2 tip = Vector2Add(wp, Vector2Scale(dir, len));
                Color fc = Fade((Color){140, 180, 255, 255}, 0.12f + 0.5f * t);
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
    for (auto& b : bodies) {
        if (trailsOn && (int)b.trail.size() > trailStride) {
            float trailWidth = std::max(2.0f, MassToRadius(b.mass) * 0.35f);
            for (size_t k = trailStride; k < b.trail.size(); k += trailStride) {
                float a = (float)k / b.trail.size();
                Color c = Fade(b.color, a * 0.6f);
                DrawLineEx(b.trail[k - trailStride], b.trail[k], trailWidth * a, c);
            }
        }
        DrawCircleV(b.pos, MassToRadius(b.mass), b.color);
    }

    // additive pass: hot halos on heavy bodies, then impact shockwaves
    BeginBlendMode(BLEND_ADDITIVE);
    for (const Body& b : bodies) {
        if (b.mass < 800.0f) continue;
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
                        Fade((Color){255, 240, 200, 255}, (0.25f - t) * 2.2f));
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
    if (flicking) {
        // ghost dot at the anchor, rubber band to the cursor, launch arrow opposite
        float r = MassToRadius(currentMass);
        DrawCircleV(flickAnchor, r, Fade(ColorForMass(currentMass), 0.5f));
        DrawCircleLinesV(flickAnchor, r, Fade(WHITE, 0.5f));
        Vector2 pull = Vector2Subtract(flickAnchor, mouseWorld);
        if (Vector2Length(pull) * camera.zoom >= flickDeadzone) {
            // trajectory preview: the candidate as a test particle in the frozen
            // field of the current bodies, ~5s ahead; stops at first impact.
            // Dots are placed at fixed screen-space spacing so the curve reads
            // as dotted at any speed or zoom.
            Color previewColor = ColorForMass(currentMass);
            float candidateR = MassToRadius(currentMass);
            Vector2 pos = flickAnchor;
            Vector2 vel = Vector2Scale(pull, flickScale);
            float sinceDot = previewDotSpacing;   // draw a dot on the first step
            for (int i = 0; i < previewSteps; i++) {
                Vector2 acc = GravityFieldAt(bodies, pos);
                vel = Vector2Add(vel, Vector2Scale(acc, previewDt));
                Vector2 next = Vector2Add(pos, Vector2Scale(vel, previewDt));
                bool hit = false;
                for (const Body& b : bodies) {
                    float minDist = candidateR + MassToRadius(b.mass);
                    if (Vector2DistanceSqr(next, b.pos) < minDist * minDist) {
                        hit = true;
                        break;
                    }
                }
                if (hit) break;
                pos = next;
                sinceDot += Vector2Length(vel) * previewDt * camera.zoom;
                if (sinceDot >= previewDotSpacing) {
                    sinceDot = 0.0f;
                    float fadeT = 1.0f - (float)i / previewSteps;
                    DrawCircleV(pos, 2.0f / camera.zoom,
                                Fade(previewColor, 0.15f + 0.6f * fadeT));
                }
            }
            DrawLineEx(flickAnchor, mouseWorld, 1.5f / camera.zoom, Fade(WHITE, 0.25f));
            Vector2 tip = Vector2Add(flickAnchor, pull);
            float thick = 2.5f / camera.zoom;
            DrawLineEx(flickAnchor, tip, thick, WHITE);
            Vector2 dir = Vector2Normalize(pull);
            float head = 12.0f / camera.zoom;
            DrawLineEx(tip, Vector2Add(tip, Vector2Scale(Vector2Rotate(dir, 150 * DEG2RAD), head)),
                       thick, WHITE);
            DrawLineEx(tip, Vector2Add(tip, Vector2Scale(Vector2Rotate(dir, -150 * DEG2RAD), head)),
                       thick, WHITE);
        }
    } else if (pendingPattern != PAT_NONE && !mouseOverUI) {
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

    // Screen-space UI is authored in logical (CSS) pixels. On the web the
    // high-DPI backing buffer is larger, so give this pass the same scale as
    // the world camera instead of drawing it at raw framebuffer coordinates.
    Camera2D uiRender = {};
    uiRender.zoom = fbScale;
    BeginMode2D(uiRender);

    // flick speed readout, in screen space next to the arrow tip
    if (flicking) {
        Vector2 pull = Vector2Subtract(flickAnchor, mouseWorld);
        if (Vector2Length(pull) * camera.zoom >= flickDeadzone) {
            Vector2 tipScreen = GetWorldToScreen2D(Vector2Add(flickAnchor, pull), camera);
            UIText(TextFormat("%.0f", Vector2Length(pull) * flickScale),
                   tipScreen.x + 10, tipScreen.y - 8, 16, UI_VALUE);
        }
    }

    // ---- UI panel ----
    DrawPanel(panel, UI_BORDER);

    float px = panel.x + 14, pw = panel.width - 28;
    float y = panel.y + 12;

    const char* massTxt = TextFormat("%.0f", currentMass);
    UISectionHeader("MASS", px, y, pw - UITextWidth(massTxt, 18) - 10);
    UIText(massTxt, panel.x + panel.width - 14 - UITextWidth(massTxt, 18), y, 18, UI_VALUE);
    y += 26;
    currentMass = UISliderLog({px, y, pw, 24}, currentMass, MASS_MIN, MASS_MAX, &draggingSlider);
    y += 34;

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
    y += 36;
    patternButton({px, y, colW, 32}, "Ring", PAT_RING);
    patternButton({col2, y, colW, 32}, "Galaxy", PAT_GALAXY);
    y += 36;
    patternButton({px, y, colW, 32}, "Grid", PAT_GRID);
    patternButton({col2, y, colW, 32}, "Cloud", PAT_CLOUD);
    y += 36;
    patternButton({px, y, colW, 32}, "Figure-8", PAT_FIGURE8);
    patternButton({col2, y, colW, 32}, "Moons", PAT_MOONS);
    y += 36;
    patternButton({px, y, colW, 32}, "Collision", PAT_COLLIDE);
    patternButton({col2, y, colW, 32}, "Comets", PAT_COMETS);
    y += 36;

    UISectionHeader("VIEW", px, y, pw);
    y += 26;
    float halfW = (pw - 8) / 2;
    if (UIToggle({px, y, halfW, 32}, "Trails (T)", trailsOn)) trailsOn = !trailsOn;
    if (UIToggle({px + halfW + 8, y, halfW, 32}, "Grid (G)", gridOn)) gridOn = !gridOn;
    y += 36;
    if (UIToggle({px, y, halfW, 32}, "Vectors (V)", vectorsOn)) vectorsOn = !vectorsOn;
    if (UIToggle({px + halfW + 8, y, halfW, 32}, "Field (B)", fieldOn)) fieldOn = !fieldOn;
    y += 36;
    UISectionHeader("COLLISION (M)", px, y, pw);
    y += 26;
    float w3 = (pw - 16) / 3;
    if (UIToggle({px, y, w3, 32}, "None", collisionMode == COLLIDE_NONE))
        collisionMode = COLLIDE_NONE;
    if (UIToggle({px + w3 + 8, y, w3, 32}, "Merge", collisionMode == COLLIDE_MERGE))
        collisionMode = COLLIDE_MERGE;
    if (UIToggle({px + 2 * (w3 + 8), y, w3, 32}, "Debris", collisionMode == COLLIDE_DEBRIS))
        collisionMode = COLLIDE_DEBRIS;
    y += 36;

    const char* trailTxt = TextFormat("%.1fs", trailLength / 60.0f);
    UISectionHeader("TRAIL LENGTH", px, y, pw - UITextWidth(trailTxt, 18) - 10);
    UIText(trailTxt, panel.x + panel.width - 14 - UITextWidth(trailTxt, 18), y, 18, UI_VALUE);
    y += 26;
    trailLength = UISliderLog({px, y, pw, 24}, trailLength, TRAIL_LEN_MIN, TRAIL_LEN_MAX,
                               &draggingTrailSlider);
    y += 34;

    const char* speedTxt = TextFormat("%.1fx", simSpeed);
    UISectionHeader("TIME", px, y, pw - UITextWidth(speedTxt, 18) - 10);
    UIText(speedTxt, panel.x + panel.width - 14 - UITextWidth(speedTxt, 18), y, 18, UI_VALUE);
    y += 26;
    simSpeed = UISliderLog({px, y, pw, 24}, simSpeed, 0.1f, 10.0f, &draggingSpeedSlider);
    y += 34;

    if (UIButton({px, y, pw, 32}, "Reset View (R)")) {
        camera.target = {0, 0};
        camera.zoom = 1.0f;
        followId = -1;
    }
    y += 36;
    if (UIButton({px, y, pw, 32}, "Center Bodies (H)")) centerOnBodies();
    y += 36;
    if (UIButton({px, y, pw, 32}, "Fullscreen (F)")) ToggleBorderlessWindowed();
    y += 44;
    if (UIButton({px, y, pw, 32}, "Clear Canvas (C)")) {
        bodies.clear();
        energyHistory.clear();
        energySampleTimer = 0.0f;
    }

    // ---- info card (top-left) ----
    Rectangle info = {10, 10, 210, 128};
    DrawPanel(info, UI_BORDER);
    UIText("FPS", info.x + 14, info.y + 13, 18, UI_LABEL);
    const char* fpsTxt = TextFormat("%d", GetFPS());
    UIText(fpsTxt, info.x + info.width - 14 - UITextWidth(fpsTxt, 18), info.y + 13, 18, UI_VALUE);
    UIText("Bodies", info.x + 14, info.y + 39, 18, UI_LABEL);
    const char* bodyTxt = TextFormat("%d", (int)bodies.size());
    UIText(bodyTxt, info.x + info.width - 14 - UITextWidth(bodyTxt, 18), info.y + 39, 18, UI_VALUE);
    UIText("TOTAL ENERGY", info.x + 14, info.y + 65, 14, UI_LABEL);
    if (!energyHistory.empty()) {
        float first = energyHistory.front();
        float change = (fabsf(first) > 1.0f) ? (energyHistory.back() - first) / fabsf(first) * 100.0f : 0.0f;
        const char* deltaTxt = TextFormat("%+.2f%%", change);
        UIText(deltaTxt, info.x + info.width - 14 - UITextWidth(deltaTxt, 14), info.y + 65, 14,
               fabsf(change) < 0.2f ? (Color){100, 210, 255, 255} : UI_VALUE);
    }
    DrawEnergyGraph({info.x + 14, info.y + 84, info.width - 28, 30});

    // ---- placement banner (top-center) ----
    if (pendingPattern != PAT_NONE) {
        const char* placeTxt = "Click to place pattern  -  ESC to cancel";
        float ptw = UITextWidth(placeTxt, 20);
        Rectangle banner = {(screenWidth - ptw) / 2.0f - 18, paused ? 58.0f : 10.0f,
                            ptw + 36.0f, 40};
        DrawPanel(banner, UI_BORDER_LIT);
        UIText(placeTxt, banner.x + 18, banner.y + 10, 20, UI_VALUE);
    }

    // ---- paused banner (top-center) ----
    if (paused) {
        const char* pauseTxt = "PAUSED  -  SPACE to resume  -  [.] to step";
        float ptw = UITextWidth(pauseTxt, 20);
        Rectangle banner = {(screenWidth - ptw) / 2.0f - 18, 10, ptw + 36.0f, 40};
        DrawPanel(banner, UI_BORDER_LIT);
        UIText(pauseTxt, banner.x + 18, banner.y + 10, 20, GOLD);
    }

    // ---- following banner (top-center, stacks under the others) ----
    if (followId != -1) {
        float fy = 10.0f;
        if (paused) fy += 48;
        if (pendingPattern != PAT_NONE) fy += 48;
        const char* followTxt = "FOLLOWING  -  ESC to stop";
        float ftw = UITextWidth(followTxt, 20);
        Rectangle banner = {(screenWidth - ftw) / 2.0f - 18, fy, ftw + 36.0f, 40};
        DrawPanel(banner, UI_BORDER_LIT);
        UIText(followTxt, banner.x + 18, banner.y + 10, 20, UI_VALUE);
    }

    // ---- hint bar (bottom-left) ----
    const char* hints = "Click: place dot     Drag: flick-launch     Double-click: follow     Right / middle drag: pan     Wheel: zoom     Space: pause";
    float htw = UITextWidth(hints, 16);
    Rectangle hintBar = {10, screenHeight - 44.0f, htw + 28.0f, 34};
    DrawPanel(hintBar, UI_BORDER);
    UIText(hints, hintBar.x + 14, hintBar.y + 9, 16, UI_LABEL);

    EndMode2D();
    EndDrawing();
}

int main() {
    unsigned int flags = FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE;
    flags |= FLAG_WINDOW_HIGHDPI;
    SetConfigFlags(flags);
    InitWindow(1280, 800, "Gravity Sandbox");
    SetWindowMinSize(800, 600);
    SetExitKey(KEY_NULL);   // Esc cancels pattern placement instead of quitting
    UILoadFont();
    InitSandboxAudio();
    UIInitAudio();

    camera.offset = {GetScreenWidth() / 2.0f, GetScreenHeight() / 2.0f};
    camera.target = {0, 0};
    camera.zoom = 1.0f;

#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop(UpdateDrawFrame, 0, 1);
#else
    while (!WindowShouldClose()) UpdateDrawFrame();
#endif

    UICloseAudio();
    CloseSandboxAudio();
    CloseWindow();
    return 0;
}
