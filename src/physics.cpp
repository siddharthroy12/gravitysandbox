#include "physics.h"

#include "raymath.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>

// Above this body count, forces switch from brute force to Barnes-Hut and
// collision pairing switches from all-pairs to a uniform grid.
static const size_t ACCEL_TREE_THRESHOLD = 200;

// ---------- Barnes-Hut quadtree ----------

namespace {

constexpr float BH_THETA = 0.7f;
constexpr int BH_MAX_DEPTH = 24;

int NewNode(std::vector<BHField::Node>& nodes, float cx, float cy, float half) {
    BHField::Node n;
    n.cx = cx;
    n.cy = cy;
    n.half = half;
    nodes.push_back(n);
    return (int)nodes.size() - 1;
}

void Insert(std::vector<BHField::Node>& nodes, int idx, const std::vector<Body>& bodies,
            int bi, int depth);

void InsertIntoChild(std::vector<BHField::Node>& nodes, int idx,
                     const std::vector<Body>& bodies, int bi, int depth) {
    // NewNode may reallocate `nodes`; only use indices across that call
    float cx = nodes[idx].cx, cy = nodes[idx].cy, half = nodes[idx].half;
    int q = (bodies[bi].pos.x >= cx ? 1 : 0) | (bodies[bi].pos.y >= cy ? 2 : 0);
    if (nodes[idx].child[q] == -1) {
        float h = half / 2;
        int c = NewNode(nodes, cx + ((q & 1) ? h : -h), cy + ((q & 2) ? h : -h), h);
        nodes[idx].child[q] = c;
    }
    Insert(nodes, nodes[idx].child[q], bodies, bi, depth + 1);
}

void Insert(std::vector<BHField::Node>& nodes, int idx, const std::vector<Body>& bodies,
            int bi, int depth) {
    {
        BHField::Node& n = nodes[idx];
        float gm = GravMass(bodies[bi]);
        float tm = n.mass + gm;
        // signed masses can cancel to ~0; leave the com alone then (the
        // node's force contribution is ~0 anyway)
        if (fabsf(tm) > 1e-4f) {
            n.com.x = (n.com.x * n.mass + bodies[bi].pos.x * gm) / tm;
            n.com.y = (n.com.y * n.mass + bodies[bi].pos.y * gm) / tm;
        }
        n.mass = tm;
        n.count++;
    }
    if (nodes[idx].count == 1) {
        nodes[idx].body = bi;
        return;
    }
    if (depth >= BH_MAX_DEPTH) {
        nodes[idx].body = -2;   // coincident points: aggregate instead of subdividing forever
        return;
    }
    if (nodes[idx].body >= 0) {
        int old = nodes[idx].body;
        nodes[idx].body = -1;
        InsertIntoChild(nodes, idx, bodies, old, depth);
    }
    InsertIntoChild(nodes, idx, bodies, bi, depth);
}

}   // namespace

void BHField::Build(const std::vector<Body>& bodies) {
    nodes.clear();
    if (bodies.empty()) return;
    nodes.reserve(2 * bodies.size());
    float minX = 1e30f, minY = 1e30f, maxX = -1e30f, maxY = -1e30f;
    for (const Body& b : bodies) {
        minX = fminf(minX, b.pos.x);
        maxX = fmaxf(maxX, b.pos.x);
        minY = fminf(minY, b.pos.y);
        maxY = fmaxf(maxY, b.pos.y);
    }
    float half = fmaxf(maxX - minX, maxY - minY) / 2 * 1.01f + 1.0f;
    NewNode(nodes, (minX + maxX) / 2, (minY + maxY) / 2, half);
    for (int i = 0; i < (int)bodies.size(); i++) Insert(nodes, 0, bodies, i, 0);
}

Vector2 BHField::AccelAt(Vector2 p, int self) {
    Vector2 acc = {0, 0};
    if (nodes.empty()) return acc;
    stack.clear();
    stack.push_back(0);
    while (!stack.empty()) {
        int idx = stack.back();
        stack.pop_back();
        const Node& n = nodes[idx];
        if (n.count == 0) continue;
        if (n.body == self && n.count == 1) continue;   // the leaf that is exactly us

        Vector2 d = {n.com.x - p.x, n.com.y - p.y};
        float dist2 = d.x * d.x + d.y * d.y;
        bool leaf = (n.body != -1) ||
                    (n.child[0] == -1 && n.child[1] == -1 && n.child[2] == -1 && n.child[3] == -1);
        float s = n.half * 2;
        if (leaf || s * s < BH_THETA * BH_THETA * dist2) {
            float inv = 1.0f / sqrtf(dist2 + SOFTENING2);
            float inv3 = inv * inv * inv;
            acc.x += G * n.mass * d.x * inv3;
            acc.y += G * n.mass * d.y * inv3;
        } else {
            for (int k = 0; k < 4; k++) {
                if (n.child[k] != -1) stack.push_back(n.child[k]);
            }
        }
    }
    return acc;
}

Vector2 GravityFieldAt(const std::vector<Body>& bodies, Vector2 p) {
    Vector2 acc = {0, 0};
    for (const Body& b : bodies) {
        Vector2 d = Vector2Subtract(b.pos, p);
        float inv = 1.0f / sqrtf(d.x * d.x + d.y * d.y + SOFTENING2);
        float inv3 = inv * inv * inv;
        acc.x += G * GravMass(b) * d.x * inv3;
        acc.y += G * GravMass(b) * d.y * inv3;
    }
    return acc;
}

// ---------- integration + collisions ----------

void StepPhysics(std::vector<Body>& bodies, float dt, bool trailsOn, int collisionMode,
                 bool tidalDestruction, bool recordTrail, int trailLength, std::vector<ImpactEvent>* impacts) {
    size_t n = bodies.size();
    std::vector<Vector2> accel(n, {0, 0});

    if (n >= ACCEL_TREE_THRESHOLD) {
        // Barnes-Hut: O(n log n)
        BHField field;
        field.Build(bodies);
        for (size_t i = 0; i < n; i++) {
            accel[i] = field.AccelAt(bodies[i].pos, (int)i);
            // a white hole's own response to the field is inverted too
            if (bodies[i].isWhiteHole) accel[i] = Vector2Scale(accel[i], -1.0f);
        }
    } else {
        // brute force: O(n^2), slightly more accurate for small scenes
        for (size_t i = 0; i < n; i++) {
            for (size_t j = i + 1; j < n; j++) {
                Vector2 d = Vector2Subtract(bodies[j].pos, bodies[i].pos);
                float dist2 = d.x * d.x + d.y * d.y + SOFTENING2;
                float invDist = 1.0f / sqrtf(dist2);
                float invDist3 = invDist * invDist * invDist;

                Vector2 forceDir = Vector2Scale(d, invDist3);
                float si = bodies[i].isWhiteHole ? -1.0f : 1.0f;
                float sj = bodies[j].isWhiteHole ? -1.0f : 1.0f;
                accel[i] = Vector2Add(accel[i], Vector2Scale(forceDir, G * si * GravMass(bodies[j])));
                accel[j] = Vector2Subtract(accel[j], Vector2Scale(forceDir, G * sj * GravMass(bodies[i])));
            }
        }
    }

    std::vector<Vector2> oldPos(n);
    for (size_t i = 0; i < n; i++) {
        oldPos[i] = bodies[i].pos;
        bodies[i].vel = Vector2Add(bodies[i].vel, Vector2Scale(accel[i], dt));
        bodies[i].pos = Vector2Add(bodies[i].pos, Vector2Scale(bodies[i].vel, dt));

        if (trailsOn) {
            if (recordTrail) bodies[i].trail.push_back(bodies[i].pos);
            while ((int)bodies[i].trail.size() > trailLength) bodies[i].trail.pop_front();
        } else if (!bodies[i].trail.empty()) {
            bodies[i].trail.clear();
        }
    }

    // A small body can be pulled apart before physical contact when it enters
    // the tidal region of a much heavier body. Keep this small-scene only: the
    // collision grid deliberately avoids all-pairs work for crowded scenes.
    // Toggleable at runtime (tidalDestruction).
    std::vector<Body> debris;
    if (tidalDestruction && n < ACCEL_TREE_THRESHOLD) {
        for (size_t i = 0; i < n; i++) {
            for (size_t j = i + 1; j < n; j++) {
                if (!bodies[i].alive || !bodies[j].alive) continue;
                Body& heavy = (bodies[i].mass >= bodies[j].mass) ? bodies[i] : bodies[j];
                Body& small = (bodies[i].mass >= bodies[j].mass) ? bodies[j] : bodies[i];
                if (small.isBlackHole || small.isWhiteHole) continue;   // holes are never torn apart
                if (heavy.isWhiteHole) continue;   // repulsion, not tides: nothing to tear with
                float massRatio = heavy.mass / small.mass;
                if (heavy.mass < 800.0f || massRatio < 12.0f) continue;

                Vector2 toSmall = Vector2Subtract(small.pos, heavy.pos);
                float distance = Vector2Length(toSmall);
                float contact = CollisionRadius(heavy.mass) + CollisionRadius(small.mass);
                // A gameplay-tuned Roche-like limit: it grows with the cube
                // root of mass ratio but remains close enough to feel like a
                // deep pass rather than a distant gravitational pull.
                float rocheLimit = MassToRadius(heavy.mass) *
                                   (1.4f + 0.5f * cbrtf(massRatio));
                if (distance <= contact || distance >= rocheLimit) continue;

                Vector2 relVel = Vector2Subtract(small.vel, heavy.vel);
                if (Vector2DotProduct(toSmall, relVel) >= 0.0f) continue; // only inbound passes

                int count = (int)Clamp(3.0f + cbrtf(massRatio), 3.0f, 8.0f);
                float fragmentMass = small.mass / count;
                float tidalKick = fmaxf(35.0f, 0.18f * sqrtf(G * heavy.mass / distance));
                float offset = MassToRadius(small.mass) * 0.7f + MassToRadius(fragmentMass);
                float baseAngle = GetRandomValue(0, 359) * DEG2RAD;
                for (int k = 0; k < count; k++) {
                    float angle = baseAngle + 2.0f * PI * k / count;
                    Vector2 dir = {cosf(angle), sinf(angle)};
                    Body fragment;
                    fragment.pos = Vector2Add(small.pos, Vector2Scale(dir, offset));
                    fragment.vel = Vector2Add(small.vel, Vector2Scale(dir, tidalKick));
                    fragment.mass = fragmentMass;
                    fragment.color = ColorForMass(fragmentMass);
                    fragment.id = g_nextBodyId++;
                    debris.push_back(fragment);
                }
                small.alive = false;
            }
        }
    }

    // resolve overlapping bodies
    if (collisionMode == COLLIDE_NONE) {
        bodies.erase(std::remove_if(bodies.begin(), bodies.end(),
                                    [](const Body& b) { return !b.alive; }),
                    bodies.end());
        bodies.insert(bodies.end(), debris.begin(), debris.end());
        return;
    }

    // closest approach of the pair's *relative* motion over this step, so fast
    // bodies can't tunnel through each other between sampled positions
    auto pairMinDist = [&](size_t i, size_t j) {
        Vector2 a = Vector2Subtract(oldPos[i], oldPos[j]);
        Vector2 b = Vector2Subtract(bodies[i].pos, bodies[j].pos);
        Vector2 ab = Vector2Subtract(b, a);
        float len2 = ab.x * ab.x + ab.y * ab.y;
        float t = (len2 > 0) ? Clamp(-(a.x * ab.x + a.y * ab.y) / len2, 0.0f, 1.0f) : 0.0f;
        return Vector2Length(Vector2Add(a, Vector2Scale(ab, t)));
    };

    auto tryCollide = [&](size_t i, size_t j) {
        if (!bodies[i].alive || !bodies[j].alive) return;
        float dist = pairMinDist(i, j);
        float minDist = CollisionRadius(bodies[i].mass) + CollisionRadius(bodies[j].mass);
        if (dist >= minDist) return;

        // elastic bounce: everything rebounds off everything, except a black
        // hole still swallows whatever touches it (fall through to the merge)
        if (collisionMode == COLLIDE_BOUNCE && !bodies[i].isBlackHole &&
            !bodies[j].isBlackHole) {
            Vector2 delta = Vector2Subtract(bodies[j].pos, bodies[i].pos);
            float len = Vector2Length(delta);
            Vector2 nrm = (len > 1e-5f) ? Vector2Scale(delta, 1.0f / len) : Vector2{1.0f, 0.0f};
            float ma = bodies[i].mass, mb = bodies[j].mass;

            // push overlapping bodies apart along the normal, lighter one more,
            // so stacked bodies don't sink into each other under gravity
            float overlap = (CollisionRadius(ma) + CollisionRadius(mb)) - len;
            if (overlap > 0) {
                bodies[i].pos = Vector2Subtract(bodies[i].pos,
                                                Vector2Scale(nrm, overlap * mb / (ma + mb)));
                bodies[j].pos = Vector2Add(bodies[j].pos,
                                           Vector2Scale(nrm, overlap * ma / (ma + mb)));
            }

            // impulse along the normal, restitution 1; only while approaching,
            // so duplicate pair tests from the grid can't double-bounce
            float relN = Vector2DotProduct(Vector2Subtract(bodies[j].vel, bodies[i].vel), nrm);
            if (relN < 0) {
                float imp = -2.0f * relN / (1.0f / ma + 1.0f / mb);
                bodies[i].vel = Vector2Subtract(bodies[i].vel, Vector2Scale(nrm, imp / ma));
                bodies[j].vel = Vector2Add(bodies[j].vel, Vector2Scale(nrm, imp / mb));
            }
            return;
        }

        // matter cannot enter a white hole: it only merges with another white
        // hole (they attract each other) or gets swallowed by a black hole
        bool iWH = bodies[i].isWhiteHole, jWH = bodies[j].isWhiteHole;
        if ((iWH != jWH) && !bodies[i].isBlackHole && !bodies[j].isBlackHole) return;

        float m1 = bodies[i].mass, m2 = bodies[j].mass;
        float totalMass = m1 + m2;
        // a black hole always wins: it absorbs the other body regardless of mass
        bool iIsBig = (bodies[i].isBlackHole == bodies[j].isBlackHole)
                          ? (m1 >= m2)
                          : bodies[i].isBlackHole;
        Body& big = iIsBig ? bodies[i] : bodies[j];
        Body& small = iIsBig ? bodies[j] : bodies[i];
        float impactSpeed = Vector2Length(Vector2Subtract(big.vel, small.vel));
        // impact axis: from the bigger body toward the smaller one, captured
        // before the merge moves big.pos to the barycenter
        Vector2 toSmall = Vector2Subtract(small.pos, big.pos);
        Vector2 impactDir = (Vector2LengthSqr(toSmall) > 1e-6f)
                                ? Vector2Normalize(toSmall)
                                : Vector2{1.0f, 0.0f};

        // perfectly inelastic merge, conserves momentum
        big.vel = Vector2Scale(Vector2Add(Vector2Scale(big.vel, big.mass),
                                           Vector2Scale(small.vel, small.mass)),
                                1.0f / totalMass);
        big.pos = Vector2Scale(Vector2Add(Vector2Scale(big.pos, big.mass),
                                           Vector2Scale(small.pos, small.mass)),
                                1.0f / totalMass);
        big.mass = totalMass;
        big.isBlackHole = big.isBlackHole || small.isBlackHole;   // merged holes stay holes
        small.alive = false;

        // spray part of the smaller body back out as fragments; a black hole
        // swallows everything, so no debris escapes the merge
        float debrisMass = 0.25f * std::min(m1, m2);
        if (collisionMode == COLLIDE_DEBRIS && debrisMass >= 4.0f && !big.isBlackHole &&
            !big.isWhiteHole) {
            // small dust-sized fragments, capped so huge impacts stay bounded
            int count = (int)Clamp(ceilf(debrisMass / 2.0f), 4.0f, 16.0f);
            float fragMass = debrisMass / count;
            big.mass = totalMass - debrisMass;
            float spawnR = MassToRadius(big.mass) + MassToRadius(fragMass) + 6.0f;
            // escape velocity from spawnR, with a small margin, so the
            // fragments actually get away instead of raining back down
            float escV = sqrtf(2.0f * G * big.mass / spawnR);
            // two fans perpendicular to the impact axis, one on each side of
            // the contact point; alternating sides keeps net momentum ~0
            Vector2 perp = {-impactDir.y, impactDir.x};
            float baseAng = atan2f(perp.y, perp.x);
            const float coneHalf = 45.0f * DEG2RAD;
            for (int k = 0; k < count; k++) {
                float side = (k % 2 == 0) ? 0.0f : PI;
                float a = baseAng + side + (GetRandomValue(-100, 100) / 100.0f) * coneHalf;
                Vector2 dir = {cosf(a), sinf(a)};
                Body d;
                d.pos = Vector2Add(big.pos, Vector2Scale(dir, spawnR));
                d.vel = Vector2Add(big.vel,
                                    Vector2Scale(dir, escV * (1.05f + GetRandomValue(0, 30) / 100.0f)));
                d.mass = fragMass;
                d.color = ColorForMass(fragMass);
                d.id = g_nextBodyId++;
                debris.push_back(d);
            }
        }
        if (big.isBlackHole) big.color = {168, 120, 255, 255};
        else if (big.isWhiteHole) big.color = WHITEHOLE_COLOR;
        else big.color = ColorForMass(big.mass);

        if (impacts) {
            float mu = m1 * m2 / totalMass;   // reduced mass
            impacts->push_back({big.pos, 0.5f * mu * impactSpeed * impactSpeed,
                                MassToRadius(big.mass), big.mass, big.isBlackHole});
        }
    };

    if (n < ACCEL_TREE_THRESHOLD) {
        for (size_t i = 0; i < n; i++) {
            for (size_t j = i + 1; j < n; j++) tryCollide(i, j);
        }
    } else {
        // uniform grid over each body's swept AABB; duplicate pair tests are
        // harmless (the alive checks make the second test a no-op)
        float maxR = 0;
        for (size_t i = 0; i < n; i++) maxR = fmaxf(maxR, CollisionRadius(bodies[i].mass));
        float cell = fmaxf(64.0f, 2.5f * maxR);
        std::unordered_map<long long, std::vector<int>> grid;
        auto cellKey = [](int ix, int iy) {
            return ((long long)ix << 32) ^ (unsigned int)iy;
        };
        for (size_t i = 0; i < n; i++) {
            float r = CollisionRadius(bodies[i].mass);
            float minX = fminf(oldPos[i].x, bodies[i].pos.x) - r;
            float maxX = fmaxf(oldPos[i].x, bodies[i].pos.x) + r;
            float minY = fminf(oldPos[i].y, bodies[i].pos.y) - r;
            float maxY = fmaxf(oldPos[i].y, bodies[i].pos.y) + r;
            int x0 = (int)floorf(minX / cell), x1 = (int)floorf(maxX / cell);
            int y0 = (int)floorf(minY / cell), y1 = (int)floorf(maxY / cell);
            x1 = std::min(x1, x0 + 7);   // cap span for absurdly fast bodies
            y1 = std::min(y1, y0 + 7);
            for (int ix = x0; ix <= x1; ix++) {
                for (int iy = y0; iy <= y1; iy++) {
                    grid[cellKey(ix, iy)].push_back((int)i);
                }
            }
        }
        for (auto& kv : grid) {
            std::vector<int>& v = kv.second;
            for (size_t a = 0; a < v.size(); a++) {
                for (size_t b = a + 1; b < v.size(); b++) tryCollide(v[a], v[b]);
            }
        }
    }

    bodies.erase(std::remove_if(bodies.begin(), bodies.end(),
                                 [](const Body& b) { return !b.alive; }),
                 bodies.end());
    bodies.insert(bodies.end(), debris.begin(), debris.end());
}
