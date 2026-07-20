#include "ui.h"

#include "raymath.h"
#include <cmath>
#include <cstring>

static Font g_uiFont;
static bool g_uiFontLoaded = false;
static Sound g_click = {};
static bool g_clickReady = false;

// ---------- frosted-glass backdrop ----------

// separable 5-tap Gaussian with linear-sampling-optimized offsets
static const char* BLUR_FS_BODY =
    "uniform sampler2D texture0;\n"
    "uniform vec2 uTexel;\n"      // one texel step along the blur direction
    "void main()\n"
    "{\n"
    "    vec4 sum = TEX(texture0, fragTexCoord)*0.227027;\n"
    "    sum += TEX(texture0, fragTexCoord + uTexel*1.384615)*0.316216;\n"
    "    sum += TEX(texture0, fragTexCoord - uTexel*1.384615)*0.316216;\n"
    "    sum += TEX(texture0, fragTexCoord + uTexel*3.230769)*0.070270;\n"
    "    sum += TEX(texture0, fragTexCoord - uTexel*3.230769)*0.070270;\n"
    "    FRAG_OUT = vec4(sum.rgb, 1.0);\n"
    "}\n";

static Shader g_blurShader = {};
static int g_blurLocTexel = -1;
static RenderTexture2D g_blurA = {};
static RenderTexture2D g_blurB = {};
static int g_blurW = 0, g_blurH = 0;
static Texture2D g_backdrop = {};
static bool g_backdropValid = false;
static float g_bdLogicalW = 0, g_bdLogicalH = 0;

void UIBackdropInit() {
#ifdef __EMSCRIPTEN__
    const char* header =
        "#version 100\n"
        "precision mediump float;\n"
        "varying vec2 fragTexCoord;\n"
        "#define FRAG_OUT gl_FragColor\n"
        "#define TEX texture2D\n";
#else
    const char* header =
        "#version 330\n"
        "in vec2 fragTexCoord;\n"
        "out vec4 fragOut;\n"
        "#define FRAG_OUT fragOut\n"
        "#define TEX texture\n";
#endif
    char* source = (char*)MemAlloc((unsigned int)(strlen(header) + strlen(BLUR_FS_BODY) + 1));
    strcpy(source, header);
    strcat(source, BLUR_FS_BODY);
    g_blurShader = LoadShaderFromMemory(NULL, source);
    MemFree(source);
    g_blurLocTexel = GetShaderLocation(g_blurShader, "uTexel");
}

void UIBackdropProcess(Texture2D scene, int logicalW, int logicalH) {
    if (g_blurLocTexel == -1) return;   // shader failed to compile; keep opaque panels
    int bw = scene.width / 4, bh = scene.height / 4;
    if (bw < 1 || bh < 1) return;

    if (bw != g_blurW || bh != g_blurH) {
        if (g_blurA.id != 0) UnloadRenderTexture(g_blurA);
        if (g_blurB.id != 0) UnloadRenderTexture(g_blurB);
        g_blurA = LoadRenderTexture(bw, bh);
        g_blurB = LoadRenderTexture(bw, bh);
        SetTextureFilter(g_blurA.texture, TEXTURE_FILTER_BILINEAR);
        SetTextureFilter(g_blurB.texture, TEXTURE_FILTER_BILINEAR);
        g_blurW = bw;
        g_blurH = bh;
    }

    // downsample to quarter res, then one Gaussian pass per axis
    BeginTextureMode(g_blurA);
    DrawTexturePro(scene, {0, 0, (float)scene.width, -(float)scene.height},
                   {0, 0, (float)bw, (float)bh}, {0, 0}, 0, WHITE);
    EndTextureMode();

    float texelH[2] = {1.0f / bw, 0.0f};
    SetShaderValue(g_blurShader, g_blurLocTexel, texelH, SHADER_UNIFORM_VEC2);
    BeginTextureMode(g_blurB);
    BeginShaderMode(g_blurShader);
    DrawTexturePro(g_blurA.texture, {0, 0, (float)bw, -(float)bh},
                   {0, 0, (float)bw, (float)bh}, {0, 0}, 0, WHITE);
    EndShaderMode();
    EndTextureMode();

    float texelV[2] = {0.0f, 1.0f / bh};
    SetShaderValue(g_blurShader, g_blurLocTexel, texelV, SHADER_UNIFORM_VEC2);
    BeginTextureMode(g_blurA);
    BeginShaderMode(g_blurShader);
    DrawTexturePro(g_blurB.texture, {0, 0, (float)bw, -(float)bh},
                   {0, 0, (float)bw, (float)bh}, {0, 0}, 0, WHITE);
    EndShaderMode();
    EndTextureMode();

    g_backdrop = g_blurA.texture;
    g_backdropValid = true;
    g_bdLogicalW = (float)logicalW;
    g_bdLogicalH = (float)logicalH;
}

void UIBackdropUnload() {
    if (g_blurA.id != 0) UnloadRenderTexture(g_blurA);
    if (g_blurB.id != 0) UnloadRenderTexture(g_blurB);
    if (g_blurLocTexel != -1) UnloadShader(g_blurShader);
    g_backdropValid = false;
}

void UIInitAudio() {
    if (!IsAudioDeviceReady()) return;
    // short soft tick: descending sine with a sharp decay, built in memory
    constexpr int sampleRate = 22050;
    const unsigned int frames = (unsigned int)(0.045f * sampleRate);
    auto* samples = (short*)MemAlloc(frames * sizeof(short));
    for (unsigned int i = 0; i < frames; i++) {
        float t = (float)i / frames;
        float hz = 1250.0f - 550.0f * t;
        float env = (1.0f - t) * (1.0f - t) * (1.0f - t);
        float wave = sinf(2.0f * PI * hz * ((float)i / sampleRate));
        samples[i] = (short)(Clamp(wave * env * 0.3f, -1.0f, 1.0f) * 32767.0f);
    }
    Wave wave = {frames, sampleRate, 16, 1, samples};
    g_click = LoadSoundFromWave(wave);
    UnloadWave(wave);
    g_clickReady = true;
}

void UICloseAudio() {
    if (!g_clickReady) return;
    UnloadSound(g_click);
    g_clickReady = false;
}

static void PlayClick(float pitch) {
    if (!g_clickReady) return;
    SetSoundPitch(g_click, pitch);
    PlaySound(g_click);
}

void UILoadFont() {
    // atlas sized ~2x the largest UI text size so glyphs sample near 1:1 on retina
    int fontAtlasSize = (int)(20 * GetWindowScaleDPI().x) + 8;
    g_uiFont = LoadFontEx(TextFormat("%sassets/Inter.ttf", GetApplicationDirectory()),
                          fontAtlasSize, nullptr, 0);
    if (g_uiFont.texture.id != GetFontDefault().texture.id && g_uiFont.glyphCount > 0) {
        g_uiFontLoaded = true;
        SetTextureFilter(g_uiFont.texture, TEXTURE_FILTER_BILINEAR);
    }
}

void UIText(const char* text, float x, float y, float size, Color c) {
    if (g_uiFontLoaded) DrawTextEx(g_uiFont, text, {x, y}, size, 0, c);
    else DrawText(text, (int)x, (int)y, (int)size, c);
}

float UITextWidth(const char* text, float size) {
    if (g_uiFontLoaded) return MeasureTextEx(g_uiFont, text, size, 0).x;
    return (float)MeasureText(text, (int)size);
}

void DrawPanel(Rectangle r, Color border) {
    if (g_backdropValid) {
        // sample the blurred scene behind the panel (source y measured from the
        // texture bottom; negative height flips render-texture storage upright)
        float texW = (float)g_backdrop.width, texH = (float)g_backdrop.height;
        Rectangle src = {r.x / g_bdLogicalW * texW,
                         (g_bdLogicalH - r.y - r.height) / g_bdLogicalH * texH,
                         r.width / g_bdLogicalW * texW,
                         -(r.height / g_bdLogicalH * texH)};
        DrawTexturePro(g_backdrop, src, r, {0, 0}, 0, WHITE);
        DrawRectangleRec(r, CLITERAL(Color){13, 13, 15, 170});
    } else {
        DrawRectangleRec(r, UI_BG);
    }
    DrawRectangleLinesEx(r, 1, border);
}

// ---------- tooltips ----------

static const char* g_tooltip = nullptr;
static Vector2 g_tooltipPos = {0, 0};

void UITooltip(const char* text) {
    // suppress while any button is held: no tooltip mid-drag or mid-flick
    if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) || IsMouseButtonDown(MOUSE_BUTTON_RIGHT) ||
        IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)) return;
    g_tooltip = text;
    g_tooltipPos = GetMousePosition();
}

void UIDrawTooltip() {
    if (!g_tooltip) return;
    const float size = 15.0f, pad = 8.0f;
    float tw = UITextWidth(g_tooltip, size);
    Rectangle box = {g_tooltipPos.x + 14, g_tooltipPos.y + 18, tw + pad * 2, size + pad * 2};
    // keep the box on screen; flip above the cursor if it would clip the bottom
    if (box.x + box.width > GetScreenWidth() - 4) box.x = GetScreenWidth() - 4 - box.width;
    if (box.y + box.height > GetScreenHeight() - 4) box.y = g_tooltipPos.y - box.height - 6;
    DrawRectangleRec(box, UI_BG);
    DrawRectangleLinesEx(box, 1, UI_BORDER_LIT);
    UIText(g_tooltip, box.x + pad, box.y + pad, size, UI_TEXT);
    g_tooltip = nullptr;
}

void UISectionHeader(const char* label, float x, float y, float width) {
    (void)width;
    UIText(label, x, y, 18, UI_LABEL);
}

bool UIButton(Rectangle r, const char* label, const char* tip) {
    Vector2 m = GetMousePosition();
    bool hover = CheckCollisionPointRec(m, r);
    if (hover && tip) UITooltip(tip);
    Color bg = hover ? UI_BTN_HOVER : UI_BTN_BG;
    if (hover && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) bg = UI_BTN_PRESS;
    DrawRectangleRec(r, bg);
    DrawRectangleLinesEx(r, 1, hover ? UI_BORDER_LIT : UI_BORDER);
    float tw = UITextWidth(label, 18);
    UIText(label, r.x + (r.width - tw) / 2, r.y + (r.height - 18) / 2, 18,
           hover ? UI_VALUE : UI_TEXT);
    bool clicked = hover && IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
    if (clicked) PlayClick(1.0f);
    return clicked;
}

bool UIToggle(Rectangle r, const char* label, bool state, const char* tip) {
    Vector2 m = GetMousePosition();
    bool hover = CheckCollisionPointRec(m, r);
    if (hover && tip) UITooltip(tip);
    if (state) {
        // filled: white block with dark text, like a primary button
        Color bg = hover ? CLITERAL(Color){215, 215, 218, 255} : CLITERAL(Color){235, 235, 238, 255};
        DrawRectangleRec(r, bg);
        float tw = UITextWidth(label, 18);
        UIText(label, r.x + (r.width - tw) / 2, r.y + (r.height - 18) / 2, 18,
               CLITERAL(Color){18, 18, 20, 255});
    } else {
        Color bg = hover ? UI_BTN_HOVER : UI_BTN_BG;
        DrawRectangleRec(r, bg);
        DrawRectangleLinesEx(r, 1, hover ? UI_BORDER_LIT : UI_BORDER);
        float tw = UITextWidth(label, 18);
        UIText(label, r.x + (r.width - tw) / 2, r.y + (r.height - 18) / 2, 18, UI_LABEL);
    }
    bool clicked = hover && IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
    // turning on ticks slightly higher than turning off
    if (clicked) PlayClick(state ? 0.9f : 1.12f);
    return clicked;
}

float UISliderLog(Rectangle r, float value, float minV, float maxV, bool* dragging,
                  const char* tip) {
    Vector2 m = GetMousePosition();
    bool hover = CheckCollisionPointRec(m, r);
    if (hover && tip) UITooltip(tip);
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
    DrawRectangleRec({r.x, cy - 2, r.width, 4}, CLITERAL(Color){55, 55, 60, 255});
    DrawRectangleRec({r.x, cy - 2, kx - r.x, 4}, CLITERAL(Color){235, 235, 238, 255});
    // square knob
    float kh = (hover || *dragging) ? 14.0f : 12.0f;
    Rectangle knob = {kx - kh / 2, cy - kh / 2, kh, kh};
    DrawRectangleRec(knob, WHITE);
    return value;
}
