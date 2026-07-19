#include "curvature.h"

#include "raymath.h"
#include <algorithm>
#include <cmath>
#include <cstring>

// Only the heaviest bodies matter for curvature; dust wells are invisible.
static const int CURV_MAX_BODIES = 64;

// The shader body is identical for both targets; only the version header and
// output conventions differ. Each fragment maps back to world space, gets
// pulled toward every well, and the *warped* position samples the grid lines,
// so lines appear to sag into gravity wells.
static const char* CURV_FS_BODY =
    "uniform vec2 uRes;\n"        // logical screen size
    "uniform float uFbScale;\n"   // framebuffer pixels per logical pixel
    "uniform vec2 uTarget;\n"     // camera
    "uniform vec2 uOffset;\n"
    "uniform float uZoom;\n"
    "uniform float uSpacing;\n"   // grid cell size, world units
    "uniform int uCount;\n"
    "uniform vec2 uPos[64];\n"    // world positions, heaviest first
    "uniform float uMass[64];\n"
    "\n"
    "void main()\n"
    "{\n"
    "    vec2 screenPx = vec2(gl_FragCoord.x, uRes.y*uFbScale - gl_FragCoord.y)/uFbScale;\n"
    "    vec2 world = (screenPx - uOffset)/uZoom + uTarget;\n"
    "\n"
    "    vec2 disp = vec2(0.0);\n"
    "    float depth = 0.0;\n"
    "    for (int i = 0; i < 64; i++)\n"
    "    {\n"
    "        if (i >= uCount) break;\n"
    "        vec2 d = uPos[i] - world;\n"
    "        float dist = length(d) + 1.0;\n"
    "        float pull = uMass[i]/(0.02*dist*dist + 6.0*dist + 300.0);\n"
    "        float x = pull*14.0/dist;\n"
    "        float mag = dist*x/(1.0 + x);\n"   // smooth saturation: approaches but never\n"
    "        disp += (d/dist)*mag;\n"           // reaches the well center, so no flat plateau\n"
    "        depth += pull;\n"
    "    }\n"
    "\n"
    "    vec2 warped = world - disp;\n"   // lines bend toward the well, not away
    "    vec2 cell = abs(fract(warped/uSpacing - 0.5) - 0.5)*uSpacing;\n"
    "    float distLine = min(cell.x, cell.y);\n"
    "    float px = 1.0/(uZoom*uFbScale);\n"             // world units per framebuffer pixel
    "    float line = 1.0 - smoothstep(0.0, px*2.0, distLine);\n"
    "\n"
    "    float glow = clamp(depth*0.35, 0.0, 0.85);\n"
    "    vec3 col = mix(vec3(0.85), vec3(0.45, 0.75, 1.0), clamp(glow*1.6, 0.0, 1.0));\n"
    "    FRAG_OUT = vec4(col, line*(0.10 + glow));\n"
    "}\n";

static Shader g_shader = {};
static bool g_ready = false;
static int locRes, locFbScale, locTarget, locOffset, locZoom, locSpacing, locCount, locPos, locMass;

void CurvatureLoad() {
#ifdef __EMSCRIPTEN__
    const char* header =
        "#version 100\n"
        "precision highp float;\n"
        "#define FRAG_OUT gl_FragColor\n";
#else
    const char* header =
        "#version 330\n"
        "out vec4 fragOut;\n"
        "#define FRAG_OUT fragOut\n";
#endif
    char* source = (char*)MemAlloc((unsigned int)(strlen(header) + strlen(CURV_FS_BODY) + 1));
    strcpy(source, header);
    strcat(source, CURV_FS_BODY);
    g_shader = LoadShaderFromMemory(NULL, source);
    MemFree(source);

    locCount = GetShaderLocation(g_shader, "uCount");
    g_ready = (g_shader.id > 0) && (locCount != -1);   // -1: compile failed, default shader
    if (!g_ready) return;
    locRes = GetShaderLocation(g_shader, "uRes");
    locFbScale = GetShaderLocation(g_shader, "uFbScale");
    locTarget = GetShaderLocation(g_shader, "uTarget");
    locOffset = GetShaderLocation(g_shader, "uOffset");
    locZoom = GetShaderLocation(g_shader, "uZoom");
    locSpacing = GetShaderLocation(g_shader, "uSpacing");
    locPos = GetShaderLocation(g_shader, "uPos");
    locMass = GetShaderLocation(g_shader, "uMass");
}

void CurvatureUnload() {
    if (g_ready) UnloadShader(g_shader);
    g_ready = false;
}

bool CurvatureReady() {
    return g_ready;
}

void CurvatureDraw(const std::vector<Body>& bodies, const Camera2D& camera,
                   int screenWidth, int screenHeight, float fbScale) {
    if (!g_ready) return;

    // same adaptive cell size as the flat grid
    float spacing = 50.0f;
    while (spacing * camera.zoom < 20.0f) spacing *= 4.0f;
    while (spacing * camera.zoom > 80.0f) spacing /= 4.0f;

    // heaviest bodies first; everything else is curvature-invisible anyway
    static std::vector<int> order;
    order.clear();
    for (int i = 0; i < (int)bodies.size(); i++) order.push_back(i);
    int count = std::min((int)order.size(), CURV_MAX_BODIES);
    std::partial_sort(order.begin(), order.begin() + count, order.end(),
                      [&](int a, int b) { return bodies[a].mass > bodies[b].mass; });

    float pos[CURV_MAX_BODIES * 2];
    float mass[CURV_MAX_BODIES];
    for (int k = 0; k < count; k++) {
        pos[k * 2] = bodies[order[k]].pos.x;
        pos[k * 2 + 1] = bodies[order[k]].pos.y;
        mass[k] = bodies[order[k]].mass;
    }

    float res[2] = {(float)screenWidth, (float)screenHeight};
    float target[2] = {camera.target.x, camera.target.y};
    float offset[2] = {camera.offset.x, camera.offset.y};
    SetShaderValue(g_shader, locRes, res, SHADER_UNIFORM_VEC2);
    SetShaderValue(g_shader, locFbScale, &fbScale, SHADER_UNIFORM_FLOAT);
    SetShaderValue(g_shader, locTarget, target, SHADER_UNIFORM_VEC2);
    SetShaderValue(g_shader, locOffset, offset, SHADER_UNIFORM_VEC2);
    SetShaderValue(g_shader, locZoom, &camera.zoom, SHADER_UNIFORM_FLOAT);
    SetShaderValue(g_shader, locSpacing, &spacing, SHADER_UNIFORM_FLOAT);
    SetShaderValue(g_shader, locCount, &count, SHADER_UNIFORM_INT);
    if (count > 0) {
        SetShaderValueV(g_shader, locPos, pos, SHADER_UNIFORM_VEC2, count);
        SetShaderValueV(g_shader, locMass, mass, SHADER_UNIFORM_FLOAT, count);
    }

    BeginShaderMode(g_shader);
    DrawRectangle(0, 0, screenWidth, screenHeight, WHITE);
    EndShaderMode();
}
