#include "raylib.h"
#include "raymath.h"

#include "body.h"
#include "patterns.h"
#include "physics.h"
#include "ui.h"

#include <algorithm>
#include <vector>

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

int main() {
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE | FLAG_WINDOW_HIGHDPI);
    InitWindow(1280, 800, "Gravity Sandbox");
    SetWindowMinSize(800, 600);
    SetExitKey(KEY_NULL);   // Esc cancels pattern placement instead of quitting
    UILoadFont();

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
    bool flicking = false;                       // dragging out a new dot's launch velocity
    Vector2 flickAnchor = {0, 0};
    const float flickScale = 2.0f;               // px of pull -> px/s of launch speed
    const float flickDeadzone = 6.0f;            // screen px; below this it's a plain click
    bool vectorsOn = false;                      // per-body velocity arrows
    float simSpeed = 1.0f;                       // time multiplier, 0.1x - 10x
    bool draggingSpeedSlider = false;
    int followId = -1;                           // body id the camera is locked onto
    double lastClickTime = 0;
    int lastClickBodyId = -1;

    while (!WindowShouldClose()) {
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
        if (IsKeyPressed(KEY_C)) bodies.clear();
        if (IsKeyPressed(KEY_T)) trailsOn = !trailsOn;
        if (IsKeyPressed(KEY_G)) gridOn = !gridOn;
        if (IsKeyPressed(KEY_F)) ToggleBorderlessWindowed();
        if (IsKeyPressed(KEY_M)) collisionMode = (collisionMode + 1) % 3;
        if (IsKeyPressed(KEY_V)) vectorsOn = !vectorsOn;

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

            const int substeps = 2;
            for (int s = 0; s < substeps; s++) {
                StepPhysics(bodies, stepDt / substeps, trailsOn, collisionMode,
                            recordTrail && s == substeps - 1, (int)trailLength);
            }
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
        if (UIToggle({px, y, pw, 32}, "Velocity Vectors (V)", vectorsOn)) vectorsOn = !vectorsOn;
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
        if (UIButton({px, y, pw, 32}, "Clear Canvas (C)")) bodies.clear();

        // ---- info card (top-left) ----
        Rectangle info = {10, 10, 170, 70};
        DrawPanel(info, UI_BORDER);
        UIText("FPS", info.x + 14, info.y + 13, 18, UI_LABEL);
        const char* fpsTxt = TextFormat("%d", GetFPS());
        UIText(fpsTxt, info.x + info.width - 14 - UITextWidth(fpsTxt, 18), info.y + 13, 18, UI_VALUE);
        UIText("Bodies", info.x + 14, info.y + 39, 18, UI_LABEL);
        const char* bodyTxt = TextFormat("%d", (int)bodies.size());
        UIText(bodyTxt, info.x + info.width - 14 - UITextWidth(bodyTxt, 18), info.y + 39, 18, UI_VALUE);

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

        EndDrawing();
    }

    CloseWindow();
    return 0;
}
