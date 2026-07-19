#pragma once

#include "body.h"

enum PatternType {
    PAT_NONE = -1,
    PAT_SOLAR,
    PAT_BINARY,
    PAT_RING,
    PAT_GALAXY,
    PAT_GRID,
    PAT_CLOUD,
    PAT_FIGURE8,
    PAT_MOONS,
    PAT_COLLIDE,
    PAT_COMETS,
    PAT_BLACKHOLE,
    PAT_PLANET
};

// Build a pattern's bodies centered on `c` (pass {0,0} for cursor-relative
// previews). `mass` scales the single-body patterns (planet, black hole);
// the rest ignore it.
std::vector<Body> MakePattern(int type, Vector2 c, float mass);
