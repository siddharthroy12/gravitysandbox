#pragma once

#include "body.h"

enum CollisionMode {
    COLLIDE_NONE = 0,    // bodies pass through each other
    COLLIDE_MERGE,       // perfectly inelastic merge
    COLLIDE_DEBRIS       // merge, but part of the smaller body sprays out as fragments
};

void StepPhysics(std::vector<Body>& bodies, float dt, bool trailsOn, int collisionMode,
                 bool recordTrail, int trailLength);
