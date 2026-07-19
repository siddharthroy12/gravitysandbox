#pragma once

#include "body.h"

// Shader-based "rubber sheet" grid: lines bend toward heavy bodies like a
// spacetime lattice diagram. Load after InitWindow; Ready() reports whether
// the shader compiled on this GPU (callers fall back to the flat grid).
void CurvatureLoad();
void CurvatureUnload();
bool CurvatureReady();

// Fullscreen pass in screen space — call between BeginDrawing and BeginMode2D.
void CurvatureDraw(const std::vector<Body>& bodies, const Camera2D& camera,
                   int screenWidth, int screenHeight, float fbScale);
