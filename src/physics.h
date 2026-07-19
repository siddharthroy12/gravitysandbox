#pragma once

#include "body.h"

enum CollisionMode {
    COLLIDE_NONE = 0,    // bodies pass through each other
    COLLIDE_MERGE,       // perfectly inelastic merge
    COLLIDE_DEBRIS       // merge, but part of the smaller body sprays out as fragments
};

struct ImpactEvent {
    Vector2 pos;
    float energy;    // 0.5 * reduced mass * impact speed^2
    float radius;    // radius of the merged body
};

// Advance the simulation. Uses a Barnes-Hut quadtree for forces and a uniform
// grid for collision pairs once the body count is large. If `impacts` is
// non-null, one event is appended per merge (for visual effects).
void StepPhysics(std::vector<Body>& bodies, float dt, bool trailsOn, int collisionMode,
                 bool recordTrail, int trailLength, std::vector<ImpactEvent>* impacts = nullptr);

// Net gravitational acceleration at a point (brute force; for field visualization).
Vector2 GravityFieldAt(const std::vector<Body>& bodies, Vector2 p);
