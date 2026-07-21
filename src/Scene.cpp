#include "Scene.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>

// ─── Procedural box builder ───────────────────────────────────────────────────
// uvScale = world metres per texture repeat. Planar-mapped per face from the
// face's dominant axis, with a matching per-face tangent — only matters for
// meshes that actually get a diffuse/normal texture assigned afterwards
// (e.g. via Model::loadDiffuseMap/loadPBRMaps); untextured callers are
// unaffected since hasTexture/hasNormalTex stay false without a bound map.
static Mesh makeBox(glm::vec3 lo, glm::vec3 hi,
                    glm::vec3 albedo,
                    glm::vec3 emissive      = glm::vec3(0.0f),
                    float     emissStrength = 0.0f,
                    float     roughness     = 0.5f,
                    float     metallic      = 0.0f,
                    float     uvScale       = 1.0f)
{
    struct Face { glm::vec3 n; glm::vec3 v[4]; };

    float lx = lo.x, hx = hi.x;
    float ly = lo.y, hy = hi.y;
    float lz = lo.z, hz = hi.z;

    Face faces[6] = {
        { { 1, 0, 0}, { {hx,ly,hz},{hx,ly,lz},{hx,hy,lz},{hx,hy,hz} } },
        { {-1, 0, 0}, { {lx,ly,lz},{lx,ly,hz},{lx,hy,hz},{lx,hy,lz} } },
        { { 0, 1, 0}, { {lx,hy,hz},{hx,hy,hz},{hx,hy,lz},{lx,hy,lz} } },
        { { 0,-1, 0}, { {lx,ly,lz},{hx,ly,lz},{hx,ly,hz},{lx,ly,hz} } },
        { { 0, 0, 1}, { {lx,ly,hz},{hx,ly,hz},{hx,hy,hz},{lx,hy,hz} } },
        { { 0, 0,-1}, { {hx,ly,lz},{lx,ly,lz},{lx,hy,lz},{hx,hy,lz} } },
    };

    std::vector<Vertex>       verts;
    std::vector<unsigned int> idx;
    verts.reserve(24);
    idx.reserve(36);

    for (auto& f : faces) {
        // Planar-project onto the two world axes orthogonal to this face's normal.
        glm::vec3 absN = glm::abs(f.n);
        glm::vec3 uAxis = (absN.x >= absN.y && absN.x >= absN.z) ? glm::vec3(0,0,1)
                         : (absN.y >= absN.x && absN.y >= absN.z) ? glm::vec3(1,0,0)
                         : glm::vec3(1,0,0);
        glm::vec3 vAxis = (absN.x >= absN.y && absN.x >= absN.z) ? glm::vec3(0,1,0)
                         : (absN.y >= absN.x && absN.y >= absN.z) ? glm::vec3(0,0,1)
                         : glm::vec3(0,1,0);

        auto base = (unsigned int)verts.size();
        for (int i = 0; i < 4; ++i) {
            Vertex vt;
            vt.Position  = f.v[i];
            vt.Normal    = f.n;
            vt.TexCoords = glm::vec2(glm::dot(f.v[i], uAxis), glm::dot(f.v[i], vAxis)) / uvScale;
            vt.Tangent   = uAxis;
            verts.push_back(vt);
        }
        idx.insert(idx.end(), {base,base+1,base+2, base,base+2,base+3});
    }

    Mesh m(std::move(verts), std::move(idx), 0, albedo);
    m.emissiveColor    = emissive;
    m.emissiveStrength = emissStrength;
    m.roughness        = roughness;
    m.metallic         = metallic;
    return m;
}

static SceneObject boxObj(glm::vec3 lo, glm::vec3 hi,
                          glm::vec3 albedo,
                          glm::vec3 emissive      = glm::vec3(0.0f),
                          float     emissStrength = 0.0f,
                          float     roughness     = 0.5f,
                          float     metallic      = 0.0f,
                          float     uvScale       = 1.0f)
{
    SceneObject o;
    o.model.addMesh(makeBox(lo, hi, albedo, emissive, emissStrength, roughness, metallic, uvScale));
    return o;
}

// Build a glass panel SceneObject with its world-space sortCenter set.
static SceneObject glassPanel(glm::vec3 lo, glm::vec3 hi) {
    // Glass meshes have no diffuse/PBR maps — the glass shader handles shading itself.
    SceneObject o;
    o.model.addMesh(makeBox(lo, hi, glm::vec3(0.0f)));  // albedo unused by glass shader
    o.sortCenter = (lo + hi) * 0.5f;
    return o;
}

// ─── Scene::load ─────────────────────────────────────────────────────────────
// "Control" glass-cube room with dark brushed-metal frame and glowing ceiling.
// Right-handed, Y-up.  Room interior: X[-3,+3]  Z[-2,+2]  Y[0,3].
// All frame bars: 0.08 × 0.08 m square, centred on room edges (half = 0.04 m).
void Scene::load(const std::string& /*path*/) {
    const float H = 0.04f;   // half bar width

    // Brushed dark metal — used for all structural frame elements
    const glm::vec3 kFrameAlb  {0.06f, 0.06f, 0.07f};
    const float     kFrameRough = 0.30f;
    const float     kFrameMetal = 1.00f;
    // No emissive — frame is dark, illuminated only by ceiling lights

    const glm::vec3 kCeilEmit  {1.00f, 0.97f, 0.90f};   // warm white ceiling glow

    // ── 1. Plinth ─────────────────────────────────────────────────────────────
    objects.push_back(boxObj(
        {-3.4f, -0.7f, -2.4f}, {3.4f, 0.0f, 2.4f},
        {0.06f, 0.06f, 0.07f},
        glm::vec3(0.0f), 0.0f, 0.7f, 0.0f));  // rough, non-metallic concrete

    // ── 2. Room floor (lit from above — no self-emission) ─────────────────────
    objects.push_back(boxObj(
        {-3.0f, 0.0f, -2.0f}, {3.0f, 0.005f, 2.0f},
        {0.15f, 0.10f, 0.08f},              // fallback albedo if the texture fails to load
        glm::vec3(0.0f), 0.0f, 0.6f, 0.0f,
        1.5f));                             // uvScale: 1.5 m per plank-texture tile
    objects.back().skipReflection = true;
    objects.back().model.loadDiffuseMap("assets/models/Textures/textures/raw_plank_wall_diff_4k.jpg");
    objects.back().model.loadPBRMaps(
        "assets/models/Textures/textures/raw_plank_wall_nor_gl_4k.png",
        "assets/models/Textures/textures/raw_plank_wall_rough_4k.png",
        "", "");   // no metallic/AO maps in this set — wood floor is non-metallic
    // GPU texture handles above are unreadable from the CPU ray tracer — it
    // needs its own copy (same pattern as the chair's Sofa_baseColor.png).
    objects.back().model.loadCPUAlbedo("assets/models/Textures/textures/raw_plank_wall_diff_4k.jpg");

    // ── 3. Vertical corner posts ──────────────────────────────────────────────
    const glm::vec2 corners[4] = {{-3,-2},{3,-2},{3,2},{-3,2}};
    for (auto c : corners)
        objects.push_back(boxObj(
            {c.x-H, 0.f, c.y-H}, {c.x+H, 3.f, c.y+H},
            kFrameAlb, glm::vec3(0.0f), 0.0f, kFrameRough, kFrameMetal));

    // ── 4. Front mullions (X = -1.5, 0, +1.5  at Z = +2) ────────────────────
    for (float mx : {-1.5f, 0.0f, 1.5f})
        objects.push_back(boxObj(
            {mx-H, 0.f, 2.f-H}, {mx+H, 3.f, 2.f+H},
            kFrameAlb, glm::vec3(0.0f), 0.0f, kFrameRough, kFrameMetal));

    // ── 5. Top frame  Y = 3: 4 perimeter bars + 2 cross-bars ─────────────────
    objects.push_back(boxObj({-3.f,3.f-H, 2.f-H},{3.f,3.f+H, 2.f+H}, kFrameAlb,glm::vec3(0.0f),0.0f,kFrameRough,kFrameMetal));
    objects.push_back(boxObj({-3.f,3.f-H,-2.f-H},{3.f,3.f+H,-2.f+H}, kFrameAlb,glm::vec3(0.0f),0.0f,kFrameRough,kFrameMetal));
    objects.push_back(boxObj({-3.f-H,3.f-H,-2.f},{-3.f+H,3.f+H,2.f}, kFrameAlb,glm::vec3(0.0f),0.0f,kFrameRough,kFrameMetal));
    objects.push_back(boxObj({ 3.f-H,3.f-H,-2.f},{ 3.f+H,3.f+H,2.f}, kFrameAlb,glm::vec3(0.0f),0.0f,kFrameRough,kFrameMetal));
    objects.push_back(boxObj({-3.f,3.f-H,-H},{3.f,3.f+H,H},           kFrameAlb,glm::vec3(0.0f),0.0f,kFrameRough,kFrameMetal));
    objects.push_back(boxObj({-H,3.f-H,-2.f},{H,3.f+H,2.f},            kFrameAlb,glm::vec3(0.0f),0.0f,kFrameRough,kFrameMetal));

    // ── 6. Bottom frame  Y = 0: 4 perimeter bars ─────────────────────────────
    objects.push_back(boxObj({-3.f,-H, 2.f-H},{3.f,H, 2.f+H}, kFrameAlb,glm::vec3(0.0f),0.0f,kFrameRough,kFrameMetal));
    objects.push_back(boxObj({-3.f,-H,-2.f-H},{3.f,H,-2.f+H}, kFrameAlb,glm::vec3(0.0f),0.0f,kFrameRough,kFrameMetal));
    objects.push_back(boxObj({-3.f-H,-H,-2.f},{-3.f+H,H,2.f}, kFrameAlb,glm::vec3(0.0f),0.0f,kFrameRough,kFrameMetal));
    objects.push_back(boxObj({ 3.f-H,-H,-2.f},{ 3.f+H,H,2.f}, kFrameAlb,glm::vec3(0.0f),0.0f,kFrameRough,kFrameMetal));

    // ── 7. Ceiling light panel — 3×3 grid of lit squares, divided by the same
    //      brushed-metal bar style as the wall mullions/frame ────────────────
    // emissiveStrength 5.0 → well above bloom threshold, strong warm-white glow
    {
        const float lx0 = -2.7f, lx1 = 2.7f;   // same overall footprint as before
        const float lz0 = -1.7f, lz1 = 1.7f;
        const float barW  = 2.0f * H;
        const int   kGrid = 3;
        const float cellW = (lx1 - lx0 - (kGrid - 1) * barW) / kGrid;
        const float cellD = (lz1 - lz0 - (kGrid - 1) * barW) / kGrid;

        // colX[i]/rowZ[i] = start edge of cell i (cell spans [start, start+cellW/D])
        float colX[kGrid], rowZ[kGrid];
        for (int i = 0; i < kGrid; ++i) {
            colX[i] = lx0 + i * (cellW + barW);
            rowZ[i] = lz0 + i * (cellD + barW);
        }

        for (int cx = 0; cx < kGrid; ++cx)
            for (int cz = 0; cz < kGrid; ++cz)
                objects.push_back(boxObj(
                    {colX[cx], 2.93f, rowZ[cz]}, {colX[cx] + cellW, 2.95f, rowZ[cz] + cellD},
                    {1.0f, 1.0f, 1.0f}, kCeilEmit, 5.0f));

        // Dividing bars, each spanning the full light footprint so the grid
        // reads as one frame — same material as the wall frame/mullions.
        for (int i = 1; i < kGrid; ++i)
            objects.push_back(boxObj(
                {colX[i] - barW, 2.93f, lz0}, {colX[i], 2.95f, lz1},
                kFrameAlb, glm::vec3(0.0f), 0.0f, kFrameRough, kFrameMetal));
        for (int i = 1; i < kGrid; ++i)
            objects.push_back(boxObj(
                {lx0, 2.93f, rowZ[i] - barW}, {lx1, 2.95f, rowZ[i]},
                kFrameAlb, glm::vec3(0.0f), 0.0f, kFrameRough, kFrameMetal));
    }

    // ── 8. Glass panels (forward transparent pass, not in G-buffer) ───────────
    // Panes are 2 mm thick, centred in the plane of each wall.
    // Front wall (Z = +2): 4 bays between corner posts and mullions
    glassObjects.push_back(glassPanel({-2.96f, H, 1.999f}, {-1.54f, 3.f-H, 2.001f}));
    glassObjects.push_back(glassPanel({-1.46f, H, 1.999f}, {-0.04f, 3.f-H, 2.001f}));
    glassObjects.push_back(glassPanel({ 0.04f, H, 1.999f}, { 1.46f, 3.f-H, 2.001f}));
    glassObjects.push_back(glassPanel({ 1.54f, H, 1.999f}, { 2.96f, 3.f-H, 2.001f}));

    // Back wall (Z = -2): one full-width pane
    glassObjects.push_back(glassPanel({-2.96f, H, -2.001f}, {2.96f, 3.f-H, -1.999f}));

    // Left wall (X = -3): one full-depth pane
    glassObjects.push_back(glassPanel({-3.001f, H, -1.96f}, {-2.999f, 3.f-H, 1.96f}));

    // Right wall (X = +3): one full-depth pane
    glassObjects.push_back(glassPanel({ 2.999f, H, -1.96f}, { 3.001f, 3.f-H, 1.96f}));
}

// ─── Scene::draw ─────────────────────────────────────────────────────────────
void Scene::draw(Shader& shader) const {
    for (const SceneObject& obj : objects) {
        shader.setMat4("model", obj.transform);
        obj.model.draw(shader);
    }
}

// ─── Scene::drawForReflection ─────────────────────────────────────────────────
void Scene::drawForReflection(Shader& shader) const {
    for (const SceneObject& obj : objects) {
        if (obj.skipReflection) continue;
        shader.setMat4("model", obj.transform);
        obj.model.draw(shader);
    }
}

// ─── Scene::drawGlassSorted ───────────────────────────────────────────────────
void Scene::drawGlassSorted(Shader& shader, const glm::vec3& camPos) const {
    std::vector<const SceneObject*> sorted;
    sorted.reserve(glassObjects.size());
    for (const auto& obj : glassObjects)
        sorted.push_back(&obj);

    std::sort(sorted.begin(), sorted.end(),
              [&](const SceneObject* a, const SceneObject* b) {
                  float da = glm::length(a->sortCenter - camPos);
                  float db = glm::length(b->sortCenter - camPos);
                  return da > db;
              });

    for (const SceneObject* obj : sorted) {
        shader.setMat4("model", obj->transform);
        obj->model.draw(shader);
    }
}
