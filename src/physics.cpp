#include "physics.h"

#include "raymath.h"
#include <algorithm>

void StepPhysics(std::vector<Body>& bodies, float dt, bool trailsOn, int collisionMode,
                 bool recordTrail, int trailLength) {
    size_t n = bodies.size();
    std::vector<Vector2> accel(n, {0, 0});

    for (size_t i = 0; i < n; i++) {
        if (!bodies[i].alive) continue;
        for (size_t j = i + 1; j < n; j++) {
            if (!bodies[j].alive) continue;
            Vector2 d = Vector2Subtract(bodies[j].pos, bodies[i].pos);
            float dist2 = d.x * d.x + d.y * d.y + SOFTENING2;
            float invDist = 1.0f / sqrtf(dist2);
            float invDist3 = invDist * invDist * invDist;

            Vector2 forceDir = Vector2Scale(d, invDist3);
            accel[i] = Vector2Add(accel[i], Vector2Scale(forceDir, G * bodies[j].mass));
            accel[j] = Vector2Subtract(accel[j], Vector2Scale(forceDir, G * bodies[i].mass));
        }
    }

    std::vector<Vector2> oldPos(n);
    for (size_t i = 0; i < n; i++) {
        if (!bodies[i].alive) continue;
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

    // resolve overlapping bodies
    if (collisionMode == COLLIDE_NONE) return;

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

    std::vector<Body> debris;
    for (size_t i = 0; i < n; i++) {
        if (!bodies[i].alive) continue;
        for (size_t j = i + 1; j < n; j++) {
            if (!bodies[j].alive) continue;
            float dist = pairMinDist(i, j);
            float minDist = MassToRadius(bodies[i].mass) + MassToRadius(bodies[j].mass);
            if (dist < minDist) {
                float m1 = bodies[i].mass, m2 = bodies[j].mass;
                float totalMass = m1 + m2;
                Body& big = (m1 >= m2) ? bodies[i] : bodies[j];
                Body& small = (m1 >= m2) ? bodies[j] : bodies[i];
                float impactSpeed = Vector2Length(Vector2Subtract(big.vel, small.vel));

                // perfectly inelastic merge, conserves momentum
                big.vel = Vector2Scale(Vector2Add(Vector2Scale(big.vel, big.mass),
                                                   Vector2Scale(small.vel, small.mass)),
                                        1.0f / totalMass);
                big.pos = Vector2Scale(Vector2Add(Vector2Scale(big.pos, big.mass),
                                                   Vector2Scale(small.pos, small.mass)),
                                        1.0f / totalMass);
                big.mass = totalMass;
                small.alive = false;

                // spray part of the smaller body back out as fragments
                float debrisMass = 0.25f * std::min(m1, m2);
                if (collisionMode == COLLIDE_DEBRIS && debrisMass >= 4.0f) {
                    int count = (int)Clamp(debrisMass / 3.0f, 3.0f, 8.0f);
                    float fragMass = debrisMass / count;
                    big.mass = totalMass - debrisMass;
                    float kick = fmaxf(impactSpeed * 0.5f, 40.0f);
                    float spawnR = MassToRadius(big.mass) + MassToRadius(fragMass) + 6.0f;
                    float baseAng = GetRandomValue(0, 359) * DEG2RAD;
                    // even angular spread keeps the net fragment momentum near zero
                    for (int k = 0; k < count; k++) {
                        float a = baseAng + 2.0f * PI * k / count;
                        Vector2 dir = {cosf(a), sinf(a)};
                        Body d;
                        d.pos = Vector2Add(big.pos, Vector2Scale(dir, spawnR));
                        d.vel = Vector2Add(big.vel,
                                            Vector2Scale(dir, kick * (0.8f + GetRandomValue(0, 40) / 100.0f)));
                        d.mass = fragMass;
                        d.color = ColorForMass(fragMass);
                        d.id = g_nextBodyId++;
                        debris.push_back(d);
                    }
                }
                big.color = ColorForMass(big.mass);
            }
        }
    }

    bodies.erase(std::remove_if(bodies.begin(), bodies.end(),
                                 [](const Body& b) { return !b.alive; }),
                 bodies.end());
    bodies.insert(bodies.end(), debris.begin(), debris.end());
}
