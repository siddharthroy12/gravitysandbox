#include "patterns.h"

#include "raymath.h"

static Vector2 OrbitVelocity(Vector2 center, Vector2 pos, float centralMass) {
    Vector2 d = Vector2Subtract(pos, center);
    float r = Vector2Length(d);
    if (r < 1.0f) return {0, 0};
    float v = sqrtf(G * centralMass / r);
    Vector2 tangent = {-d.y / r, d.x / r};
    return Vector2Scale(tangent, v);
}

static void SpawnSolarSystem(std::vector<Body>& bodies, Vector2 c) {
    float starMass = 8000.0f;
    AddBody(bodies, c, {0, 0}, starMass);
    int planets = 6;
    for (int i = 0; i < planets; i++) {
        float r = 160.0f + i * 90.0f + GetRandomValue(-15, 15);
        float ang = GetRandomValue(0, 359) * DEG2RAD;
        Vector2 pos = {c.x + cosf(ang) * r, c.y + sinf(ang) * r};
        AddBody(bodies, pos, OrbitVelocity(c, pos, starMass), (float)GetRandomValue(5, 40));
    }
}

static void SpawnBinaryStars(std::vector<Body>& bodies, Vector2 c) {
    float m = 4000.0f;
    float d = 300.0f;
    float v = sqrtf(G * m / (2.0f * d));
    AddBody(bodies, {c.x - d / 2, c.y}, {0, -v}, m);
    AddBody(bodies, {c.x + d / 2, c.y}, {0, v}, m);
}

static void SpawnPlanetRing(std::vector<Body>& bodies, Vector2 c) {
    float starMass = 6000.0f;
    AddBody(bodies, c, {0, 0}, starMass);
    int count = 40;
    float r = 300.0f;
    for (int i = 0; i < count; i++) {
        float ang = (2.0f * PI * i) / count;
        Vector2 pos = {c.x + cosf(ang) * r, c.y + sinf(ang) * r};
        AddBody(bodies, pos, OrbitVelocity(c, pos, starMass), 3.0f);
    }
}

static void SpawnGalaxy(std::vector<Body>& bodies, Vector2 c) {
    float coreMass = 12000.0f;
    AddBody(bodies, c, {0, 0}, coreMass);
    int count = 2500;
    for (int i = 0; i < count; i++) {
        // sqrt gives uniform surface density; a core-heavy disk merges itself
        // away in seconds once dust collisions are point-sized
        float r = 100.0f + sqrtf((float)GetRandomValue(0, 1000) / 1000.0f) * 450.0f;
        float ang = GetRandomValue(0, 359) * DEG2RAD;
        Vector2 pos = {c.x + cosf(ang) * r, c.y + sinf(ang) * r};
        Vector2 vel = OrbitVelocity(c, pos, coreMass);
        vel = Vector2Scale(vel, 0.95f + GetRandomValue(0, 100) / 1000.0f);
        AddBody(bodies, pos, vel, (float)GetRandomValue(1, 4));
    }
}

static void SpawnGrid(std::vector<Body>& bodies, Vector2 c) {
    int side = 6;
    float spacing = 120.0f;
    float half = (side - 1) * spacing / 2.0f;
    for (int y = 0; y < side; y++) {
        for (int x = 0; x < side; x++) {
            AddBody(bodies, {c.x - half + x * spacing, c.y - half + y * spacing}, {0, 0}, 30.0f);
        }
    }
}

static void SpawnCloud(std::vector<Body>& bodies, Vector2 c) {
    int count = 60;
    for (int i = 0; i < count; i++) {
        float r = sqrtf((float)GetRandomValue(0, 1000) / 1000.0f) * 400.0f;
        float ang = GetRandomValue(0, 359) * DEG2RAD;
        AddBody(bodies, {c.x + cosf(ang) * r, c.y + sinf(ang) * r}, {0, 0},
                (float)GetRandomValue(5, 50));
    }
}

static void SpawnFigure8(std::vector<Body>& bodies, Vector2 c) {
    // Chenciner-Montgomery three-body figure-eight choreography, scaled from
    // normalized units (G=1, m=1, L=1) to our G and pixel scale
    float L = 300.0f, m = 500.0f;
    float vs = sqrtf(G * m / L);
    Vector2 p1 = {0.97000436f * L, -0.24308753f * L};
    Vector2 v3 = {-0.93240737f * vs, -0.86473146f * vs};
    Vector2 v12 = {-v3.x / 2, -v3.y / 2};
    AddBody(bodies, {c.x + p1.x, c.y + p1.y}, v12, m);
    AddBody(bodies, {c.x - p1.x, c.y - p1.y}, v12, m);
    AddBody(bodies, c, v3, m);
}

static void SpawnMoons(std::vector<Body>& bodies, Vector2 c) {
    float starMass = 8000.0f;
    AddBody(bodies, c, {0, 0}, starMass);
    // two planets, each with an orbiting moon inside its Hill sphere
    float planetR[2] = {320.0f, 560.0f};
    for (int i = 0; i < 2; i++) {
        float ang = GetRandomValue(0, 359) * DEG2RAD;
        float planetMass = 250.0f;
        Vector2 pPos = {c.x + cosf(ang) * planetR[i], c.y + sinf(ang) * planetR[i]};
        Vector2 pVel = OrbitVelocity(c, pPos, starMass);
        AddBody(bodies, pPos, pVel, planetMass);

        float moonR = 45.0f;
        float mAng = GetRandomValue(0, 359) * DEG2RAD;
        Vector2 mPos = {pPos.x + cosf(mAng) * moonR, pPos.y + sinf(mAng) * moonR};
        Vector2 mVel = Vector2Add(pVel, OrbitVelocity(pPos, mPos, planetMass));
        AddBody(bodies, mPos, mVel, 4.0f);
    }
}

static void SpawnCollision(std::vector<Body>& bodies, Vector2 c) {
    // two small galaxies drifting into each other
    for (int g = 0; g < 2; g++) {
        float side = (g == 0) ? -1.0f : 1.0f;
        Vector2 core = {c.x + side * 480.0f, c.y + side * 120.0f};
        Vector2 drift = {-side * 35.0f, -side * 8.0f};
        float coreMass = 6000.0f;
        AddBody(bodies, core, drift, coreMass);
        for (int i = 0; i < 800; i++) {
            float r = 60.0f + powf((float)GetRandomValue(0, 1000) / 1000.0f, 1.5f) * 220.0f;
            float ang = GetRandomValue(0, 359) * DEG2RAD;
            Vector2 pos = {core.x + cosf(ang) * r, core.y + sinf(ang) * r};
            Vector2 vel = Vector2Add(drift, OrbitVelocity(core, pos, coreMass));
            AddBody(bodies, pos, vel, (float)GetRandomValue(1, 3));
        }
    }
}

static void SpawnComets(std::vector<Body>& bodies, Vector2 c) {
    float starMass = 8000.0f;
    AddBody(bodies, c, {0, 0}, starMass);
    // launched near perihelion faster than circular -> long elliptical orbits
    for (int i = 0; i < 8; i++) {
        float r = 100.0f + GetRandomValue(0, 60);
        float ang = GetRandomValue(0, 359) * DEG2RAD;
        Vector2 pos = {c.x + cosf(ang) * r, c.y + sinf(ang) * r};
        Vector2 vel = Vector2Scale(OrbitVelocity(c, pos, starMass),
                                    1.20f + GetRandomValue(0, 15) / 100.0f);
        AddBody(bodies, pos, vel, 2.0f);
    }
}

static void SpawnBlackHole(std::vector<Body>& bodies, Vector2 c) {
    AddBody(bodies, c, {0, 0}, 40000.0f);
    Body& bh = bodies.back();
    bh.isBlackHole = true;
    bh.color = {255, 170, 90, 255};   // accretion orange, used by trails
}

std::vector<Body> MakePattern(int type, Vector2 c) {
    std::vector<Body> v;
    switch (type) {
        case PAT_SOLAR: SpawnSolarSystem(v, c); break;
        case PAT_BINARY: SpawnBinaryStars(v, c); break;
        case PAT_RING: SpawnPlanetRing(v, c); break;
        case PAT_GALAXY: SpawnGalaxy(v, c); break;
        case PAT_GRID: SpawnGrid(v, c); break;
        case PAT_CLOUD: SpawnCloud(v, c); break;
        case PAT_FIGURE8: SpawnFigure8(v, c); break;
        case PAT_MOONS: SpawnMoons(v, c); break;
        case PAT_COLLIDE: SpawnCollision(v, c); break;
        case PAT_COMETS: SpawnComets(v, c); break;
        case PAT_BLACKHOLE: SpawnBlackHole(v, c); break;
    }
    return v;
}
