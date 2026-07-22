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
    Body& core = bodies.back();
    core.isBlackHole = true;
    core.color = {168, 120, 255, 255};   // accretion violet, used by trails
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
        Body& bh = bodies.back();
        bh.isBlackHole = true;
        bh.color = {168, 120, 255, 255};   // accretion violet, used by trails
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

static void SpawnPinwheel(std::vector<Body>& bodies, Vector2 c) {
    // spiral-armed galaxy around a bright stellar core (no black hole)
    float coreMass = 10000.0f;
    AddBody(bodies, c, {0, 0}, coreMass);
    const int arms = 3;
    const int perArm = 520;
    for (int a = 0; a < arms; a++) {
        for (int i = 0; i < perArm; i++) {
            float t = (float)GetRandomValue(0, 1000) / 1000.0f;
            float r = 110.0f + t * 420.0f;
            // winding angle grows with radius; jitter thickens the arm
            float ang = a * 2.0f * PI / arms + r * 0.011f +
                        GetRandomValue(-120, 120) / 1000.0f;
            Vector2 pos = {c.x + cosf(ang) * r, c.y + sinf(ang) * r};
            AddBody(bodies, pos, OrbitVelocity(c, pos, coreMass), (float)GetRandomValue(1, 3));
        }
    }
}

static void SpawnCluster(std::vector<Body>& bodies, Vector2 c) {
    // globular cluster: isotropic velocities near virial balance, so it
    // churns indefinitely instead of collapsing or flying apart
    const int count = 350;
    const float R = 250.0f;
    float totalMass = 0;
    for (int i = 0; i < count; i++) totalMass += 5.0f;   // avg of the 2..8 draw
    float sigma = 0.6f * sqrtf(G * totalMass / R);
    for (int i = 0; i < count; i++) {
        float u = (float)GetRandomValue(0, 1000) / 1000.0f;
        float r = R * powf(u, 0.7f);
        float ang = GetRandomValue(0, 359) * DEG2RAD;
        float vAng = GetRandomValue(0, 359) * DEG2RAD;
        float v = sigma * (float)GetRandomValue(200, 1200) / 1000.0f;
        AddBody(bodies, {c.x + cosf(ang) * r, c.y + sinf(ang) * r},
                {cosf(vAng) * v, sinf(vAng) * v}, (float)GetRandomValue(2, 8));
    }
}

static void SpawnSaturn(std::vector<Body>& bodies, Vector2 c) {
    float planetMass = 3000.0f;
    AddBody(bodies, c, {0, 0}, planetMass);
    // three ring bands with gaps, a shepherd moon in the outer gap, one big moon
    const struct { float r0, r1; int count; } bands[] = {
        {110.0f, 132.0f, 120}, {150.0f, 176.0f, 140}, {196.0f, 216.0f, 110},
    };
    for (const auto& band : bands) {
        for (int i = 0; i < band.count; i++) {
            float r = band.r0 + (band.r1 - band.r0) * GetRandomValue(0, 1000) / 1000.0f;
            float ang = GetRandomValue(0, 359) * DEG2RAD;
            Vector2 pos = {c.x + cosf(ang) * r, c.y + sinf(ang) * r};
            AddBody(bodies, pos, OrbitVelocity(c, pos, planetMass), 1.5f);
        }
    }
    float shepAng = GetRandomValue(0, 359) * DEG2RAD;
    Vector2 shepPos = {c.x + cosf(shepAng) * 186.0f, c.y + sinf(shepAng) * 186.0f};
    AddBody(bodies, shepPos, OrbitVelocity(c, shepPos, planetMass), 8.0f);
    float moonAng = GetRandomValue(0, 359) * DEG2RAD;
    Vector2 moonPos = {c.x + cosf(moonAng) * 320.0f, c.y + sinf(moonAng) * 320.0f};
    AddBody(bodies, moonPos, OrbitVelocity(c, moonPos, planetMass), 60.0f);
}

static void SpawnRosette(std::vector<Body>& bodies, Vector2 c) {
    // Klemperer rosette: alternating heavy/light bodies on a hexagon. Famously
    // unstable: it holds for a few orbits, then shears apart beautifully.
    const float r = 260.0f;
    const float masses[6] = {1200.0f, 300.0f, 1200.0f, 300.0f, 1200.0f, 300.0f};
    float total = 4500.0f;
    float v = sqrtf(G * total * 0.75f / r);
    for (int i = 0; i < 6; i++) {
        float ang = i * 60.0f * DEG2RAD;
        Vector2 pos = {c.x + cosf(ang) * r, c.y + sinf(ang) * r};
        Vector2 tangent = {-sinf(ang), cosf(ang)};
        AddBody(bodies, pos, Vector2Scale(tangent, v), masses[i]);
    }
}

static void SpawnTatooine(std::vector<Body>& bodies, Vector2 c) {
    // circumbinary planets: two suns orbiting each other, planets around both
    float m = 3000.0f;
    float d = 160.0f;
    float v = sqrtf(G * m / (2.0f * d));
    AddBody(bodies, {c.x - d / 2, c.y}, {0, -v}, m);
    AddBody(bodies, {c.x + d / 2, c.y}, {0, v}, m);
    float planetR[2] = {520.0f, 760.0f};
    float planetM[2] = {25.0f, 12.0f};
    for (int i = 0; i < 2; i++) {
        float ang = GetRandomValue(0, 359) * DEG2RAD;
        Vector2 pos = {c.x + cosf(ang) * planetR[i], c.y + sinf(ang) * planetR[i]};
        AddBody(bodies, pos, OrbitVelocity(c, pos, 2.0f * m), planetM[i]);
    }
}

static void SpawnSupernova(std::vector<Body>& bodies, Vector2 c) {
    // collapsed core at rest plus a shell of ejecta racing outward; some of it
    // escapes, some rains back down. Gorgeous with trails on.
    AddBody(bodies, c, {0, 0}, 2500.0f);
    const int count = 450;
    for (int i = 0; i < count; i++) {
        float ang = GetRandomValue(0, 359) * DEG2RAD + GetRandomValue(0, 100) / 5000.0f;
        float r = 15.0f + GetRandomValue(0, 1000) / 1000.0f * 50.0f;
        Vector2 dir = {cosf(ang), sinf(ang)};
        float speed = 280.0f + GetRandomValue(0, 1000) / 1000.0f * 180.0f;
        Vector2 tangent = {-dir.y, dir.x};
        Vector2 vel = Vector2Add(Vector2Scale(dir, speed),
                                 Vector2Scale(tangent, (float)GetRandomValue(-30, 30)));
        AddBody(bodies, Vector2Add(c, Vector2Scale(dir, r)), vel, (float)GetRandomValue(1, 3));
    }
}

static void SpawnHeart(std::vector<Body>& bodies, Vector2 c) {
    // the classic parametric heart, at rest; gravity does the heartbreak
    const int count = 72;
    const float scale = 12.0f;
    for (int i = 0; i < count; i++) {
        float t = 2.0f * PI * i / count;
        float x = 16.0f * sinf(t) * sinf(t) * sinf(t);
        float y = 13.0f * cosf(t) - 5.0f * cosf(2 * t) - 2.0f * cosf(3 * t) - cosf(4 * t);
        AddBody(bodies, {c.x + x * scale, c.y - y * scale}, {0, 0}, 10.0f);
    }
}

static void SpawnTrojans(std::vector<Body>& bodies, Vector2 c) {
    // a planet shares its orbit with two asteroid swarms 60 degrees ahead and
    // behind, at the L4/L5 Lagrange points; real Trojans, same trick Jupiter
    // uses. Stable as long as the planet stays much lighter than the star.
    float starMass = 9000.0f;
    AddBody(bodies, c, {0, 0}, starMass);
    float r = 380.0f;
    float baseAng = 0.0f;
    Vector2 planetPos = {c.x + cosf(baseAng) * r, c.y + sinf(baseAng) * r};
    AddBody(bodies, planetPos, OrbitVelocity(c, planetPos, starMass), 220.0f);
    for (float side : {1.0f, -1.0f}) {
        float lAng = baseAng + side * 60.0f * DEG2RAD;
        for (int i = 0; i < 60; i++) {
            float jitterR = r + GetRandomValue(-25, 25);
            float jitterAng = lAng + GetRandomValue(-400, 400) / 10000.0f;
            Vector2 pos = {c.x + cosf(jitterAng) * jitterR, c.y + sinf(jitterAng) * jitterR};
            AddBody(bodies, pos, OrbitVelocity(c, pos, starMass), 1.5f);
        }
    }
}

static void SpawnChaos(std::vector<Body>& bodies, Vector2 c) {
    // three bodies at an irregular triangle, near zero net momentum: unlike
    // Figure-8's stable choreography, this is genuinely chaotic — small
    // changes in the mass slider send it down completely different paths
    const float m = 900.0f;
    const float r = 220.0f;
    const float angOffsets[3] = {0.0f, 132.0f, 251.0f};   // deliberately irregular
    Vector2 vel[3];
    Vector2 momentum = {0, 0};
    for (int i = 0; i < 3; i++) {
        float ang = angOffsets[i] * DEG2RAD;
        Vector2 pos = {c.x + cosf(ang) * r, c.y + sinf(ang) * r};
        float speed = sqrtf(G * m / r) * 0.85f;
        Vector2 tangent = {-sinf(ang), cosf(ang)};
        vel[i] = Vector2Scale(tangent, speed);
        momentum = Vector2Add(momentum, vel[i]);
        AddBody(bodies, pos, vel[i], m);
    }
    // cancel net drift so the whole system stays put on screen
    Vector2 correction = Vector2Scale(momentum, -1.0f / 3.0f);
    for (int i = 0; i < 3; i++) bodies[bodies.size() - 3 + i].vel = Vector2Add(vel[i], correction);
}

static void SpawnVortex(std::vector<Body>& bodies, Vector2 c) {
    // a dust disk split into counter-rotating halves around a light core; the
    // shear at the boundary winds up into a churning vortex before it tears
    float coreMass = 1500.0f;
    AddBody(bodies, c, {0, 0}, coreMass);
    const int count = 900;
    for (int i = 0; i < count; i++) {
        float t = (float)GetRandomValue(0, 1000) / 1000.0f;
        float r = 40.0f + sqrtf(t) * 340.0f;
        float ang = GetRandomValue(0, 359) * DEG2RAD;
        Vector2 pos = {c.x + cosf(ang) * r, c.y + sinf(ang) * r};
        float dir = (i % 2 == 0) ? 1.0f : -1.0f;   // half prograde, half retrograde
        Vector2 vel = Vector2Scale(OrbitVelocity(c, pos, coreMass), dir);
        AddBody(bodies, pos, vel, (float)GetRandomValue(1, 2));
    }
}

static void SpawnSmiley(std::vector<Body>& bodies, Vector2 c) {
    // a grinning face at rest; give it a moment before gravity wipes the smile
    AddBody(bodies, {c.x - 55.0f, c.y - 40.0f}, {0, 0}, 90.0f);   // left eye
    AddBody(bodies, {c.x + 55.0f, c.y - 40.0f}, {0, 0}, 90.0f);   // right eye
    const int mouthCount = 26;
    for (int i = 0; i < mouthCount; i++) {
        float t = (float)i / (mouthCount - 1);
        // sweep the lower arc of a circle (200deg -> 340deg, through straight
        // down); corners sit higher than the middle, which is the smile shape
        float ang = (200.0f + 140.0f * t) * DEG2RAD;
        Vector2 pos = {c.x + cosf(ang) * 110.0f, c.y - sinf(ang) * 110.0f + 30.0f};
        AddBody(bodies, pos, {0, 0}, 8.0f);
    }
}

static void SpawnBlackHole(std::vector<Body>& bodies, Vector2 c, float mass) {
    AddBody(bodies, c, {0, 0}, mass);
    Body& bh = bodies.back();
    bh.isBlackHole = true;
    bh.color = {168, 120, 255, 255};   // accretion violet, used by trails
}

static void SpawnPlanet(std::vector<Body>& bodies, Vector2 c, float mass) {
    AddBody(bodies, c, {0, 0}, mass);
}

static void SpawnWhiteHole(std::vector<Body>& bodies, Vector2 c, float mass) {
    AddBody(bodies, c, {0, 0}, mass);
    Body& wh = bodies.back();
    wh.isWhiteHole = true;
    wh.color = WHITEHOLE_COLOR;
}

std::vector<Body> MakePattern(int type, Vector2 c, float mass) {
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
        case PAT_PINWHEEL: SpawnPinwheel(v, c); break;
        case PAT_CLUSTER: SpawnCluster(v, c); break;
        case PAT_SATURN: SpawnSaturn(v, c); break;
        case PAT_ROSETTE: SpawnRosette(v, c); break;
        case PAT_TATOOINE: SpawnTatooine(v, c); break;
        case PAT_SUPERNOVA: SpawnSupernova(v, c); break;
        case PAT_HEART: SpawnHeart(v, c); break;
        case PAT_TROJANS: SpawnTrojans(v, c); break;
        case PAT_CHAOS: SpawnChaos(v, c); break;
        case PAT_VORTEX: SpawnVortex(v, c); break;
        case PAT_SMILEY: SpawnSmiley(v, c); break;
        case PAT_BLACKHOLE: SpawnBlackHole(v, c, mass); break;
        case PAT_PLANET: SpawnPlanet(v, c, mass); break;
        case PAT_WHITEHOLE: SpawnWhiteHole(v, c, mass); break;
    }
    return v;
}
