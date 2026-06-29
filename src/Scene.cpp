#include "Scene.h"
#include <glm/gtc/matrix_transform.hpp>

// ─── Procedural box builder ───────────────────────────────────────────────────
// Generates a closed AABB mesh with per-face normals.  No UVs needed —
// we use albedoColor / emissiveColor from the Mesh material fields instead.
static Mesh makeBox(glm::vec3 lo, glm::vec3 hi,
                    glm::vec3 albedo,
                    glm::vec3 emissive       = glm::vec3(0.0f),
                    float     emissStrength  = 0.0f)
{
    // Each face: 4 verts CCW from outside, indices 0,1,2 + 0,2,3
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
        auto base = (unsigned int)verts.size();
        for (int i = 0; i < 4; ++i) {
            Vertex vt;
            vt.Position  = f.v[i];
            vt.Normal    = f.n;
            vt.TexCoords = glm::vec2(0.0f);
            verts.push_back(vt);
        }
        idx.insert(idx.end(), {base,base+1,base+2, base,base+2,base+3});
    }

    Mesh m(std::move(verts), std::move(idx), 0, albedo);
    m.emissiveColor    = emissive;
    m.emissiveStrength = emissStrength;
    return m;
}

// Wrap a single procedural box in a SceneObject (identity transform).
static SceneObject boxObj(glm::vec3 lo, glm::vec3 hi,
                          glm::vec3 albedo,
                          glm::vec3 emissive      = glm::vec3(0.0f),
                          float     emissStrength = 0.0f)
{
    SceneObject o;
    o.model.addMesh(makeBox(lo, hi, albedo, emissive, emissStrength));
    return o;
}

// ─── Scene::load ─────────────────────────────────────────────────────────────
// "Control" glowing glass-cube room.
// Right-handed, Y-up.  Room interior: X[-3,+3]  Z[-2,+2]  Y[0,3].
// All frame bars: 0.08 × 0.08 m square, centred on room edges (half = 0.04 m).
void Scene::load(const std::string& /*path*/) {
    const float H = 0.04f;   // half bar width

    const glm::vec3 kFrameAlb  {0.70f, 0.70f, 0.70f};
    const glm::vec3 kFrameEmit {0.85f, 0.92f, 1.00f};   // cool white
    const float     kFrameStr  = 8.0f;

    const glm::vec3 kBotAlb    {0.20f, 0.20f, 0.20f};

    const glm::vec3 kCeilEmit  {1.00f, 0.97f, 0.90f};   // warm white

    // ── 1. Plinth ─────────────────────────────────────────────────────────────
    objects.push_back(boxObj(
        {-3.4f, -0.7f, -2.4f}, {3.4f, 0.0f, 2.4f},
        {0.06f, 0.06f, 0.07f}));

    // ── 2. Room floor (thin slab, optional emissive warm pool) ────────────────
    objects.push_back(boxObj(
        {-3.0f, 0.0f, -2.0f}, {3.0f, 0.005f, 2.0f},
        {0.15f, 0.10f, 0.08f},
        {1.0f, 0.35f, 0.15f}, 0.4f));
    objects.back().skipReflection = true;  // floor must not render into its own reflection

    // ── 3. Vertical corner posts ──────────────────────────────────────────────
    const glm::vec2 corners[4] = {{-3,-2},{3,-2},{3,2},{-3,2}};
    for (auto c : corners)
        objects.push_back(boxObj(
            {c.x-H, 0.f, c.y-H}, {c.x+H, 3.f, c.y+H},
            kFrameAlb, kFrameEmit, kFrameStr));

    // ── 4. Front mullions (X = -1.5, 0, +1.5  at Z = +2) ────────────────────
    for (float mx : {-1.5f, 0.0f, 1.5f})
        objects.push_back(boxObj(
            {mx-H, 0.f, 2.f-H}, {mx+H, 3.f, 2.f+H},
            kFrameAlb, kFrameEmit, kFrameStr));

    // ── 5. Top frame  Y = 3: 4 perimeter bars + 2 cross-bars ─────────────────
    // Front & back
    objects.push_back(boxObj({-3.f,3.f-H, 2.f-H},{3.f,3.f+H, 2.f+H}, kFrameAlb,kFrameEmit,kFrameStr));
    objects.push_back(boxObj({-3.f,3.f-H,-2.f-H},{3.f,3.f+H,-2.f+H}, kFrameAlb,kFrameEmit,kFrameStr));
    // Left & right
    objects.push_back(boxObj({-3.f-H,3.f-H,-2.f},{-3.f+H,3.f+H,2.f}, kFrameAlb,kFrameEmit,kFrameStr));
    objects.push_back(boxObj({ 3.f-H,3.f-H,-2.f},{ 3.f+H,3.f+H,2.f}, kFrameAlb,kFrameEmit,kFrameStr));
    // Cross-bar at Z=0 (spans X)
    objects.push_back(boxObj({-3.f,3.f-H,-H},{3.f,3.f+H,H}, kFrameAlb,kFrameEmit,kFrameStr));
    // Cross-bar at X=0 (spans Z)
    objects.push_back(boxObj({-H,3.f-H,-2.f},{H,3.f+H,2.f}, kFrameAlb,kFrameEmit,kFrameStr));

    // ── 6. Bottom frame  Y = 0: 4 perimeter bars, dark, no emissive ──────────
    objects.push_back(boxObj({-3.f,-H, 2.f-H},{3.f,H, 2.f+H}, kBotAlb));
    objects.push_back(boxObj({-3.f,-H,-2.f-H},{3.f,H,-2.f+H}, kBotAlb));
    objects.push_back(boxObj({-3.f-H,-H,-2.f},{-3.f+H,H,2.f}, kBotAlb));
    objects.push_back(boxObj({ 3.f-H,-H,-2.f},{ 3.f+H,H,2.f}, kBotAlb));

    // ── 7. Ceiling light panel ────────────────────────────────────────────────
    objects.push_back(boxObj(
        {-2.7f, 2.93f, -1.7f}, {2.7f, 2.95f, 1.7f},
        {1.0f, 1.0f, 1.0f},
        kCeilEmit, 10.0f));  // stronger than frame bars (8) → brightest thing in frame
}

// ─── Scene::draw ─────────────────────────────────────────────────────────────
void Scene::draw(Shader& shader) const {
    for (const SceneObject& obj : objects) {
        shader.setMat4("model", obj.transform);
        obj.model.draw(shader);
    }
}

// ─── Scene::drawForReflection ─────────────────────────────────────────────────
// Same as draw() but skips objects flagged with skipReflection (i.e. the floor).
void Scene::drawForReflection(Shader& shader) const {
    for (const SceneObject& obj : objects) {
        if (obj.skipReflection) continue;
        shader.setMat4("model", obj.transform);
        obj.model.draw(shader);
    }
}
