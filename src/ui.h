#pragma once

#include "raylib.h"

// ---------- theme (minimal dark, neutral grays) ----------

inline constexpr Color UI_BG = {13, 13, 15, 248};
inline constexpr Color UI_BORDER = {45, 45, 49, 255};
inline constexpr Color UI_BORDER_LIT = {80, 80, 86, 255};
inline constexpr Color UI_VALUE = {255, 255, 255, 255};
inline constexpr Color UI_TEXT = {228, 228, 232, 255};
inline constexpr Color UI_LABEL = {138, 138, 145, 255};
inline constexpr Color UI_BTN_BG = {25, 25, 28, 255};
inline constexpr Color UI_BTN_HOVER = {40, 40, 45, 255};
inline constexpr Color UI_BTN_PRESS = {55, 55, 61, 255};

// Load the bundled UI font (call once, after InitWindow). Falls back to
// raylib's default font if the asset is missing.
void UILoadFont();

// Build the widget click sound (call once, after the audio device is up).
// Widgets stay silent if this was never called or the device isn't ready.
void UIInitAudio();
void UICloseAudio();

void UIText(const char* text, float x, float y, float size, Color c);
float UITextWidth(const char* text, float size);

// Register a tooltip for this frame (widgets call it when hovered). It is
// drawn by UIDrawTooltip at the end of the UI pass, on top of everything.
void UITooltip(const char* text);
void UIDrawTooltip();

void DrawPanel(Rectangle r, Color border);
void UISectionHeader(const char* label, float x, float y, float width);
bool UIButton(Rectangle r, const char* label, const char* tip = nullptr);
bool UIToggle(Rectangle r, const char* label, bool state, const char* tip = nullptr);
float UISliderLog(Rectangle r, float value, float minV, float maxV, bool* dragging,
                  const char* tip = nullptr);
