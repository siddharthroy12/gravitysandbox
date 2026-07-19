#include "ui.h"

#include "raymath.h"
#include <cmath>

static Font g_uiFont;
static bool g_uiFontLoaded = false;
static Sound g_click = {};
static bool g_clickReady = false;

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
    DrawRectangleRec(r, UI_BG);
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
        Color bg = hover ? (Color){215, 215, 218, 255} : (Color){235, 235, 238, 255};
        DrawRectangleRec(r, bg);
        float tw = UITextWidth(label, 18);
        UIText(label, r.x + (r.width - tw) / 2, r.y + (r.height - 18) / 2, 18,
               (Color){18, 18, 20, 255});
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
    DrawRectangleRec({r.x, cy - 2, r.width, 4}, (Color){55, 55, 60, 255});
    DrawRectangleRec({r.x, cy - 2, kx - r.x, 4}, (Color){235, 235, 238, 255});
    // square knob
    float kh = (hover || *dragging) ? 20.0f : 18.0f;
    Rectangle knob = {kx - kh / 2, cy - kh / 2, kh, kh};
    DrawRectangleRec(knob, WHITE);
    DrawRectangleLinesEx(knob, 1, (Color){120, 120, 126, 255});
    return value;
}
