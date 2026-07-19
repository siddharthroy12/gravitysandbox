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
    float mass;      // mass after the merge/debris split
    bool isBlackHole = false;   // survivor is a black hole: no shockwave ring
};

// Advance the simulation. Uses a Barnes-Hut quadtree for forces and a uniform
// grid for collision pairs once the body count is large. If `impacts` is
// non-null, one event is appended per merge (for visual effects).
// `tidalDestruction` toggles the Roche-like pull-apart of small bodies
// passing deep into a heavy body's gravity.
void StepPhysics(std::vector<Body>& bodies, float dt, bool trailsOn, int collisionMode,
                 bool tidalDestruction, bool recordTrail, int trailLength, std::vector<ImpactEvent>* impacts = nullptr);

// Reusable Barnes-Hut quadtree over a set of bodies: build once, then query
// gravitational acceleration at any point in O(log n). StepPhysics uses this
// for its many-body force pass; the field overlay rebuilds one per frame.
struct BHField {
    struct Node {
        float cx, cy, half;              // square bounds
        float mass = 0;
        Vector2 com = {0, 0};
        int child[4] = {-1, -1, -1, -1};
        int body = -1;                   // >=0: leaf with that body; -2: aggregated leaf
        int count = 0;
    };

    std::vector<Node> nodes;

    void Build(const std::vector<Body>& bodies);
    // Net acceleration at p; pass a body index as `self` to skip its own pull.
    Vector2 AccelAt(Vector2 p, int self = -1);

private:
    std::vector<int> stack;              // traversal scratch, reused across queries
};

// Net gravitational acceleration at a point (brute force; the flick
// trajectory preview samples step-by-step, where a tree would not pay off).
Vector2 GravityFieldAt(const std::vector<Body>& bodies, Vector2 p);
