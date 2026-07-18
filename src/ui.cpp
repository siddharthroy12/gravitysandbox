#include "ui.h"

#include "raymath.h"
#include <cmath>

static Font g_uiFont;
static bool g_uiFontLoaded = false;

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

void UISectionHeader(const char* label, float x, float y, float width) {
    (void)width;
    UIText(label, x, y, 18, UI_LABEL);
}

bool UIButton(Rectangle r, const char* label) {
    Vector2 m = GetMousePosition();
    bool hover = CheckCollisionPointRec(m, r);
    Color bg = hover ? UI_BTN_HOVER : UI_BTN_BG;
    if (hover && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) bg = UI_BTN_PRESS;
    DrawRectangleRec(r, bg);
    DrawRectangleLinesEx(r, 1, hover ? UI_BORDER_LIT : UI_BORDER);
    float tw = UITextWidth(label, 18);
    UIText(label, r.x + (r.width - tw) / 2, r.y + (r.height - 18) / 2, 18,
           hover ? UI_VALUE : UI_TEXT);
    return hover && IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
}

bool UIToggle(Rectangle r, const char* label, bool state) {
    Vector2 m = GetMousePosition();
    bool hover = CheckCollisionPointRec(m, r);
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
    return hover && IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
}

float UISliderLog(Rectangle r, float value, float minV, float maxV, bool* dragging) {
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
