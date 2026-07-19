#pragma once

#include "raylib.h"
#include <algorithm>
#include <cmath>
#include <deque>
#include <vector>

struct Body {
    Vector2 pos;
    Vector2 vel;
    float mass;
    Color color;
    std::deque<Vector2> trail;
    bool alive = true;
    int id = 0;          // stable identity; survives merges (the bigger body keeps its id)
};

inline int g_nextBodyId = 1;

inline constexpr float G = 1500.0f;
inline constexpr float SOFTENING2 = 400.0f;   // softening^2, avoids singularities at close range

// Bodies lighter than this render as glowing points ("dust") instead of
// circles, which keeps multi-thousand-body scenes cheap to draw.
inline constexpr float DUST_MASS_MAX = 5.0f;

inline bool IsDust(float mass) {
    return mass < DUST_MASS_MAX;
}

inline float MassToRadius(float mass) {
    return 3.0f + cbrtf(mass) * 1.6f;
}

// Radius used for collision tests. Dust renders as a small point, so it also
// collides as one; without this, dense dust fields merge away in seconds
// through contacts that are invisible on screen.
inline float CollisionRadius(float mass) {
    return IsDust(mass) ? 1.4f : MassToRadius(mass);
}

inline Color ColorForMass(float mass) {
    float hue = 220.0f - std::min(220.0f, log2f(mass + 1.0f) * 18.0f);
    return ColorFromHSV(hue, 0.75f, 1.0f);
}

inline void AddBody(std::vector<Body>& bodies, Vector2 pos, Vector2 vel, float mass) {
    Body b;
    b.pos = pos;
    b.vel = vel;
    b.mass = mass;
    b.color = ColorForMass(mass);
    b.id = g_nextBodyId++;
    bodies.push_back(b);
}
