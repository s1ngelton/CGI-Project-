#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

// nanort — header-only SAH BVH, confined to this TU
#include "nanort.h"
#include <random>
#include <thread>

#include "RayTracer.h"
#include "Scene.h"
#include "Camera.h"
#include "Model.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <filesystem>
#include <iostream>
#include <cmath>

#ifdef HAS_OIDN
#include <OpenImageDenoise/oidn.hpp>
static void applyOIDN(std::vector<glm::vec3>& color,
                      std::vector<glm::vec3>& albedo,
                      std::vector<glm::vec3>& normal,
                      int w, int h) {
    oidn::DeviceRef dev = oidn::newDevice(oidn::DeviceType::CPU);
    dev.commit();
    oidn::FilterRef flt = dev.newFilter("RT");
    flt.setImage("color",  (void*)color.data(),  oidn::Format::Float3, w, h);
    flt.setImage("albedo", (void*)albedo.data(), oidn::Format::Float3, w, h);
    flt.setImage("normal", (void*)normal.data(), oidn::Format::Float3, w, h);
    flt.setImage("output", (void*)color.data(),  oidn::Format::Float3, w, h);
    flt.set("hdr", true);
    flt.commit();
    flt.execute();
    const char* err;
    if (dev.getError(err) != oidn::Error::None)
        std::cerr << "[OIDN] " << err << "\n";
}
#endif

// ── pimpl: nanort BVH types stay out of the header ───────────────────────────
struct RayTracer::BVH {
    nanort::BVHAccel<float> accel;
};

RayTracer::RayTracer()  = default;
RayTracer::~RayTracer() = default;

// ── Scene extraction ──────────────────────────────────────────────────────────
void RayTracer::buildScene(const Scene& scene) {
    m_tris.clear();
    m_vertices.clear();
    m_faces.clear();
    m_bvh.reset();
    m_shadowVerts.clear();
    m_shadowFaces.clear();
    m_shadowBvh.reset();

    // Appends all triangles from `objs` into the primary BVH arrays.
    // `glass` = true marks triangles as Fresnel-reflective glass (checkpoint c).
    auto addObjects = [&](const std::vector<SceneObject>& objs, bool glass) {
        for (const SceneObject& obj : objs) {
            const glm::mat4& M  = obj.transform;
            glm::mat3 normalM = glm::mat3(glm::transpose(glm::inverse(M)));

            for (const Mesh& mesh : obj.model.getMeshes()) {
                const auto& verts = mesh.vertices;
                const auto& idx   = mesh.indices;

                for (size_t i = 0; i + 2 < idx.size(); i += 3) {
                    glm::vec3 w0 = glm::vec3(M * glm::vec4(verts[idx[i+0]].Position, 1.0f));
                    glm::vec3 w1 = glm::vec3(M * glm::vec4(verts[idx[i+1]].Position, 1.0f));
                    glm::vec3 w2 = glm::vec3(M * glm::vec4(verts[idx[i+2]].Position, 1.0f));

                    glm::vec3 n0 = glm::normalize(normalM * verts[idx[i+0]].Normal);
                    glm::vec3 n1 = glm::normalize(normalM * verts[idx[i+1]].Normal);
                    glm::vec3 n2 = glm::normalize(normalM * verts[idx[i+2]].Normal);

                    RTTriangle tri;
                    tri.v0 = w0; tri.v1 = w1; tri.v2 = w2;
                    tri.n0 = n0; tri.n1 = n1; tri.n2 = n2;
                    tri.uv0 = verts[idx[i+0]].TexCoords;
                    tri.uv1 = verts[idx[i+1]].TexCoords;
                    tri.uv2 = verts[idx[i+2]].TexCoords;
                    tri.mesh    = &mesh;
                    tri.isGlass = glass;
                    m_tris.push_back(tri);

                    unsigned int base = (unsigned int)(m_vertices.size() / 3);
                    m_vertices.push_back(w0.x); m_vertices.push_back(w0.y); m_vertices.push_back(w0.z);
                    m_vertices.push_back(w1.x); m_vertices.push_back(w1.y); m_vertices.push_back(w1.z);
                    m_vertices.push_back(w2.x); m_vertices.push_back(w2.y); m_vertices.push_back(w2.z);
                    m_faces.push_back(base + 0);
                    m_faces.push_back(base + 1);
                    m_faces.push_back(base + 2);
                }
            }
        }
    };

    addObjects(scene.objects,      false);
    // Glass panels are now included so sTraceRay can apply Fresnel reflection/transmission.
    // Their albedo=(0,0,0) is irrelevant — the glass branch in sTraceRay never reads albedo.
    addObjects(scene.glassObjects, true);

    if (m_tris.empty()) {
        std::cerr << "[RayTracer] No triangles extracted — scene may be empty.\n";
        return;
    }

    unsigned int numTris = (unsigned int)m_tris.size();
    {
        nanort::TriangleMesh<float>    nanortMesh(m_vertices.data(), m_faces.data(), sizeof(float) * 3);
        nanort::TriangleSAHPred<float> pred      (m_vertices.data(), m_faces.data(), sizeof(float) * 3);
        nanort::BVHBuildOptions<float> opts;
        m_bvh = std::make_unique<BVH>();
        bool ok = m_bvh->accel.Build(numTris, nanortMesh, pred, opts);
        if (!ok) {
            std::cerr << "[RayTracer] BVH build failed.\n";
            m_bvh.reset();
            return;
        }
        auto stats = m_bvh->accel.GetStatistics();
        std::cout << "[RayTracer] Primary BVH: " << numTris << " tris, "
                  << "depth " << stats.max_tree_depth << "\n";
    }

    // ── Shadow BVH: opaque non-emissive geometry only ─────────────────────────
    // Glass excluded (transparent to shadows). Emissive ceiling excluded
    // (sits at Y≈2.9, same height as the 4 point lights — blocks all upward shadow rays).
    {
        for (const SceneObject& obj : scene.objects) {
            const glm::mat4& M = obj.transform;
            for (const Mesh& mesh : obj.model.getMeshes()) {
                if (mesh.emissiveStrength > 0.0f) continue;

                const auto& verts = mesh.vertices;
                const auto& idx   = mesh.indices;
                for (size_t i = 0; i + 2 < idx.size(); i += 3) {
                    glm::vec3 w0 = glm::vec3(M * glm::vec4(verts[idx[i+0]].Position, 1.0f));
                    glm::vec3 w1 = glm::vec3(M * glm::vec4(verts[idx[i+1]].Position, 1.0f));
                    glm::vec3 w2 = glm::vec3(M * glm::vec4(verts[idx[i+2]].Position, 1.0f));

                    unsigned int base = (unsigned int)(m_shadowVerts.size() / 3);
                    m_shadowVerts.push_back(w0.x); m_shadowVerts.push_back(w0.y); m_shadowVerts.push_back(w0.z);
                    m_shadowVerts.push_back(w1.x); m_shadowVerts.push_back(w1.y); m_shadowVerts.push_back(w1.z);
                    m_shadowVerts.push_back(w2.x); m_shadowVerts.push_back(w2.y); m_shadowVerts.push_back(w2.z);
                    m_shadowFaces.push_back(base + 0);
                    m_shadowFaces.push_back(base + 1);
                    m_shadowFaces.push_back(base + 2);
                }
            }
        }

        unsigned int numShadow = (unsigned int)(m_shadowFaces.size() / 3);
        if (numShadow > 0) {
            nanort::TriangleMesh<float>    sM   (m_shadowVerts.data(), m_shadowFaces.data(), sizeof(float)*3);
            nanort::TriangleSAHPred<float> sPred(m_shadowVerts.data(), m_shadowFaces.data(), sizeof(float)*3);
            nanort::BVHBuildOptions<float> sOpts;
            m_shadowBvh = std::make_unique<BVH>();
            bool ok = m_shadowBvh->accel.Build(numShadow, sM, sPred, sOpts);
            if (!ok) { std::cerr << "[RayTracer] Shadow BVH build failed.\n"; m_shadowBvh.reset(); }
            else {
                auto s = m_shadowBvh->accel.GetStatistics();
                std::cout << "[RayTracer] Shadow BVH: " << numShadow << " tris, "
                          << "depth " << s.max_tree_depth << "\n";
            }
        }
    }
}

// ── Checkpoint (a): normal visualisation ─────────────────────────────────────
bool RayTracer::renderNormals(const Camera& cam, int w, int h, const std::string& outPath,
                              std::vector<uint8_t>* outPixels) {
    if (!m_bvh || m_tris.empty()) {
        std::cerr << "[RayTracer] renderNormals: call buildScene() first.\n";
        return false;
    }

    std::vector<uint8_t> pixels(w * h * 3, 0);

    float aspect     = (float)w / (float)h;
    float tanHalfFov = std::tan(cam.Zoom * 0.5f * (float)M_PI / 180.0f);

    nanort::TriangleMesh<float>        nanortMesh(m_vertices.data(), m_faces.data(), sizeof(float) * 3);
    nanort::TriangleIntersector<float> intersector(nanortMesh);
    nanort::BVHTraceOptions            traceOpts;

    for (int py = 0; py < h; ++py) {
        for (int px = 0; px < w; ++px) {
            float ndcX = (2.0f * (px + 0.5f) / w  - 1.0f) * aspect * tanHalfFov;
            float ndcY = (1.0f - 2.0f * (py + 0.5f) / h)  *          tanHalfFov;

            glm::vec3 rd = glm::normalize(cam.Front + ndcX * cam.Right + ndcY * cam.Up);

            nanort::Ray<float> ray;
            ray.org[0] = cam.Position.x; ray.org[1] = cam.Position.y; ray.org[2] = cam.Position.z;
            ray.dir[0] = rd.x;           ray.dir[1] = rd.y;           ray.dir[2] = rd.z;
            ray.min_t  = 0.001f;
            ray.max_t  = 1.0e30f;

            nanort::TriangleIntersection<float> isect;
            bool hit = m_bvh->accel.Traverse(ray, intersector, &isect, traceOpts);

            uint8_t r, g, b;
            if (hit) {
                const RTTriangle& tri = m_tris[isect.prim_id];
                float u = isect.u, v = isect.v, w0 = 1.0f - u - v;
                glm::vec3 N = glm::normalize(w0 * tri.n0 + u * tri.n1 + v * tri.n2);
                r = (uint8_t)((N.x * 0.5f + 0.5f) * 255.0f);
                g = (uint8_t)((N.y * 0.5f + 0.5f) * 255.0f);
                b = (uint8_t)((N.z * 0.5f + 0.5f) * 255.0f);
            } else {
                r = 5; g = 6; b = 10;
            }

            int i = (py * w + px) * 3;
            pixels[i+0] = r; pixels[i+1] = g; pixels[i+2] = b;
        }
    }

    if (outPixels) *outPixels = pixels;

    int ok = stbi_write_png(outPath.c_str(), w, h, 3, pixels.data(), w * 3);
    if (ok) std::cout << "[RayTracer] Normals saved: " << outPath << "\n";
    else    std::cerr << "[RayTracer] Failed to write PNG: " << outPath << "\n";
    return ok != 0;
}

// ── BRDF helpers — verbatim from lighting.frag ────────────────────────────────
static constexpr float kPI = 3.14159265359f;

static float D_GGX(float NdotH, float roughness) {
    float a  = roughness * roughness;
    float a2 = a * a;
    float d  = NdotH * NdotH * (a2 - 1.0f) + 1.0f;
    return a2 / (kPI * d * d);
}

static float G_SmithGGX(float NdotV, float NdotL, float roughness) {
    float k  = (roughness + 1.0f) * (roughness + 1.0f) / 8.0f;
    float gv = NdotV / (NdotV * (1.0f - k) + k);
    float gl  = NdotL / (NdotL * (1.0f - k) + k);
    return gv * gl;
}

static glm::vec3 F_Schlick(float HdotV, glm::vec3 F0) {
    float t  = std::max(1.0f - HdotV, 0.0f);
    float t5 = t * t * t * t * t;
    return F0 + (glm::vec3(1.0f) - F0) * t5;
}

// Narkowicz 2015 ACES approximation — identical to tonemap.frag
static glm::vec3 tonemapACES(glm::vec3 x) {
    const float a=2.51f, b=0.03f, c=2.43f, d=0.59f, e=0.14f;
    return glm::clamp((x*(a*x+b))/(x*(c*x+d)+e), 0.0f, 1.0f);
}

// ── Checkpoint (c): recursive ray trace context ───────────────────────────────
// Bundles all BVH/mesh/intersector pointers so sTraceRay can be a static free function.
struct TraceCtx {
    const nanort::BVHAccel<float>*            primaryBVH;
    const nanort::TriangleMesh<float>*         primaryMesh;
    const nanort::TriangleIntersector<float>*  primaryInter;
    const nanort::BVHAccel<float>*             shadowBVH;    // null if not built
    const nanort::TriangleMesh<float>*         shadowMesh;
    const nanort::TriangleIntersector<float>*  shadowInter;
    const std::vector<RTTriangle>*             tris;
    const std::vector<RTLight>*                lights;
    float                                      lightRadius;
    nanort::BVHTraceOptions                    opts;
    glm::vec3                                  background;
    // Checkpoint selector: true = (b) pass-through glass, false = (c) Fresnel reflections
    bool                                       glassPassThrough = false;
};

// Forward declaration (function is recursive)
static glm::vec3 sTraceRay(const nanort::Ray<float>& ray, int depth, const TraceCtx& ctx);

static glm::vec3 sTraceRay(const nanort::Ray<float>& ray, int depth, const TraceCtx& ctx) {
    if (depth <= 0) return ctx.background;

    nanort::TriangleIntersection<float> isect;
    if (!ctx.primaryBVH->Traverse(ray, *ctx.primaryInter, &isect, ctx.opts))
        return ctx.background;

    const RTTriangle& tri = (*ctx.tris)[isect.prim_id];
    float u = isect.u, v = isect.v, w0 = 1.0f - u - v;

    glm::vec3 N = glm::normalize(w0*tri.n0 + u*tri.n1 + v*tri.n2);
    glm::vec3 org(ray.org[0], ray.org[1], ray.org[2]);
    glm::vec3 rd (ray.dir[0], ray.dir[1], ray.dir[2]);
    glm::vec3 hitPos = org + rd * isect.t;
    glm::vec3 V      = -rd;   // direction from hitPos toward ray origin
    if (glm::dot(N, V) < 0.0f) N = -N;   // two-sided surface
    float NdotV = std::max(glm::dot(N, V), 0.0f);
    float eps   = std::max(0.005f, isect.t * 1e-4f);

    // ── Glass: Fresnel-blended reflection + straight-through transmission ─────
    if (tri.isGlass) {
        // Checkpoint (b) mode: glass is fully transparent — just continue the ray.
        // This matches how the G-buffer pass treats glass: skip it and see the room.
        if (ctx.glassPassThrough) {
            glm::vec3 transOrg = hitPos + rd * eps;
            nanort::Ray<float> tr;
            tr.org[0]=transOrg.x; tr.org[1]=transOrg.y; tr.org[2]=transOrg.z;
            tr.dir[0]=rd.x;       tr.dir[1]=rd.y;       tr.dir[2]=rd.z;
            tr.min_t=0.0f; tr.max_t=1.0e30f;
            return sTraceRay(tr, depth, ctx);  // don't spend a depth count on glass pass-through
        }

        float cosTheta = NdotV;
        float t1       = 1.0f - cosTheta;
        float fresnel  = 0.04f + 0.96f * (t1*t1*t1*t1*t1);

        // Mirror reflection off the glass surface
        glm::vec3 R       = glm::reflect(rd, N);   // rd = incident direction
        glm::vec3 reflOrg = hitPos + N * eps;
        nanort::Ray<float> reflRay;
        reflRay.org[0]=reflOrg.x; reflRay.org[1]=reflOrg.y; reflRay.org[2]=reflOrg.z;
        reflRay.dir[0]=R.x;       reflRay.dir[1]=R.y;       reflRay.dir[2]=R.z;
        reflRay.min_t=0.0f; reflRay.max_t=1.0e30f;
        glm::vec3 reflColor = sTraceRay(reflRay, depth-1, ctx);

        // Transmitted ray continues in the same direction past the panel.
        // Offset in the ray direction to push the origin through the 2 mm glass.
        glm::vec3 transOrg = hitPos + rd * eps;
        nanort::Ray<float> transRay;
        transRay.org[0]=transOrg.x; transRay.org[1]=transOrg.y; transRay.org[2]=transOrg.z;
        transRay.dir[0]=rd.x;       transRay.dir[1]=rd.y;       transRay.dir[2]=rd.z;
        transRay.min_t=0.0f; transRay.max_t=1.0e30f;

        // Subtle blue tint for glass transmission — matches glass.frag's dark-glass look
        static const glm::vec3 kGlassTint(0.82f, 0.88f, 1.0f);
        glm::vec3 transColor = sTraceRay(transRay, depth-1, ctx) * kGlassTint;

        return glm::mix(transColor, reflColor, fresnel);
    }

    // ── Opaque: Cook-Torrance GGX BRDF + any-hit shadow rays ─────────────────
    const Mesh* mesh  = tri.mesh;

    // Albedo: sample CPU texture if loaded, else fall back to scalar albedoColor.
    // aiProcess_FlipUVs + stbi_load-without-flip → same convention as GL raster.
    glm::vec3 albedo;
    if (mesh->cpuAlbedo && mesh->cpuAlbedo->valid()) {
        glm::vec2 texUV = w0*tri.uv0 + u*tri.uv1 + v*tri.uv2;
        albedo = mesh->cpuAlbedo->sample(texUV.x, texUV.y);
    } else {
        albedo = mesh->albedoColor;
    }

    float roughness   = std::max(mesh->roughness, 0.04f);
    float metallic    = mesh->metallic;
    glm::vec3 F0      = glm::mix(glm::vec3(0.04f), albedo, metallic);
    glm::vec3 shadowOrg = hitPos + N * eps;

    glm::vec3 Lo(0.0f);
    for (const RTLight& light : *ctx.lights) {
        glm::vec3 toLight     = light.position - hitPos;
        float     distToLight = glm::length(toLight);
        if (distToLight < 1e-6f) continue;
        glm::vec3 Li = toLight / distToLight;

        float NdotLi = std::max(glm::dot(N, Li), 0.0f);
        if (NdotLi <= 0.0f) continue;

        // Any-hit shadow ray (shadow BVH: opaque non-emissive only)
        nanort::Ray<float> shadowRay;
        shadowRay.org[0]=shadowOrg.x; shadowRay.org[1]=shadowOrg.y; shadowRay.org[2]=shadowOrg.z;
        shadowRay.dir[0]=Li.x;        shadowRay.dir[1]=Li.y;        shadowRay.dir[2]=Li.z;
        shadowRay.min_t=0.0f;
        shadowRay.max_t=distToLight - 0.02f;

        nanort::TriangleIntersection<float> shadowIsect;
        if (ctx.shadowBVH &&
            ctx.shadowBVH->Traverse(shadowRay, *ctx.shadowInter, &shadowIsect, ctx.opts))
            continue;

        // BRDF (verbatim from lighting.frag)
        glm::vec3 Hi  = glm::normalize(V + Li);
        float NdotHi  = std::max(glm::dot(N, Hi), 0.0f);
        float HdotVi  = std::max(glm::dot(Hi, V), 0.0f);
        glm::vec3 Fi  = F_Schlick(HdotVi, F0);
        float     Di  = D_GGX(NdotHi, roughness);
        float     Gi  = G_SmithGGX(NdotV, NdotLi, roughness);

        glm::vec3 specBRDF = Di*Gi*Fi / std::max(4.0f*NdotV*NdotLi, 0.001f);
        glm::vec3 kDi      = (glm::vec3(1.0f)-Fi)*(1.0f-metallic);
        glm::vec3 diffBRDF = kDi*albedo/kPI;

        // Windowed quadratic attenuation (verbatim from lighting.frag)
        float dist2 = distToLight * distToLight;
        float att2  = dist2 / (ctx.lightRadius * ctx.lightRadius);
        float att   = std::max(0.0f, 1.0f - att2);
        att        *= att;

        glm::vec3 radiance = light.color * att * light.intensity;
        Lo += (diffBRDF + specBRDF) * radiance * NdotLi;
    }

    // Emissive — additive, bypasses shadow/BRDF
    return Lo + mesh->emissiveColor * mesh->emissiveStrength;
}

// ── Checkpoint (b/c): beauty render ──────────────────────────────────────────
bool RayTracer::renderBeauty(const Camera& cam, int w, int h,
                              const std::string& outPath,
                              std::vector<uint8_t>* outPixels,
                              float exposure,
                              bool glassPassThrough) {
    if (!m_bvh || m_tris.empty()) {
        std::cerr << "[RayTracer] renderBeauty: call buildScene() first.\n";
        return false;
    }
    if (m_lights.empty()) {
        std::cerr << "[RayTracer] renderBeauty: call setLights() first.\n";
        return false;
    }

    float aspect     = (float)w / (float)h;
    float tanHalfFov = std::tan(cam.Zoom * 0.5f * (float)M_PI / 180.0f);

    nanort::TriangleMesh<float>        nanortMesh(m_vertices.data(), m_faces.data(), sizeof(float)*3);
    nanort::TriangleIntersector<float> intersector(nanortMesh);

    nanort::TriangleMesh<float>        shadowMesh (m_shadowVerts.data(), m_shadowFaces.data(), sizeof(float)*3);
    nanort::TriangleIntersector<float> shadowInter(shadowMesh);

    static const glm::vec3 kBackground(0.02f, 0.025f, 0.04f);

    TraceCtx ctx;
    ctx.primaryBVH       = &m_bvh->accel;
    ctx.primaryMesh      = &nanortMesh;
    ctx.primaryInter     = &intersector;
    ctx.shadowBVH        = m_shadowBvh ? &m_shadowBvh->accel : nullptr;
    ctx.shadowMesh       = &shadowMesh;
    ctx.shadowInter      = &shadowInter;
    ctx.tris             = &m_tris;
    ctx.lights           = &m_lights;
    ctx.lightRadius      = m_lightRadius;
    ctx.background       = kBackground;
    ctx.glassPassThrough = glassPassThrough;

    std::vector<glm::vec3> hdr(w * h, kBackground);

    unsigned nThreads = std::max(1u, std::thread::hardware_concurrency());
    {
        std::vector<std::thread> threads;
        threads.reserve(nThreads);
        for (unsigned t = 0; t < nThreads; ++t) {
            threads.emplace_back([&, t]() {
                // TriangleIntersector has mutable ray-state fields — must be per-thread.
                nanort::TriangleMesh<float>        lMesh (m_vertices.data(),    m_faces.data(),       sizeof(float)*3);
                nanort::TriangleIntersector<float> lInter(lMesh);
                nanort::TriangleMesh<float>        lSMesh(m_shadowVerts.data(), m_shadowFaces.data(), sizeof(float)*3);
                nanort::TriangleIntersector<float> lSInter(lSMesh);
                TraceCtx lCtx = ctx;
                lCtx.primaryMesh  = &lMesh;
                lCtx.primaryInter = &lInter;
                lCtx.shadowMesh   = &lSMesh;
                lCtx.shadowInter  = &lSInter;

                for (int py = (int)t; py < h; py += (int)nThreads) {
                    if (t == 0 && py % std::max(1, h/10) == 0)
                        std::cout << "[RayTracer] Beauty: " << (py * 100 / h) << "%\n" << std::flush;
                    for (int px = 0; px < w; ++px) {
                        float ndcX = (2.0f * (px + 0.5f) / w  - 1.0f) * aspect * tanHalfFov;
                        float ndcY = (1.0f - 2.0f * (py + 0.5f) / h)  *          tanHalfFov;
                        glm::vec3 rd = glm::normalize(cam.Front + ndcX * cam.Right + ndcY * cam.Up);
                        nanort::Ray<float> ray;
                        ray.org[0]=cam.Position.x; ray.org[1]=cam.Position.y; ray.org[2]=cam.Position.z;
                        ray.dir[0]=rd.x;           ray.dir[1]=rd.y;           ray.dir[2]=rd.z;
                        ray.min_t=0.001f; ray.max_t=1.0e30f;
                        hdr[py*w+px] = sTraceRay(ray, 4, lCtx);
                    }
                }
            });
        }
        for (auto& th : threads) th.join();
    }
    std::cout << "[RayTracer] Beauty: 100% — tonemapping...\n";

    // Tonemap + sRGB gamma (mirrors tonemap.frag: ACES, exposure, pow(1/2.2))
    std::vector<uint8_t> pixels(w * h * 3);
    for (int i = 0; i < w * h; ++i) {
        glm::vec3 c = tonemapACES(hdr[i] * exposure);
        c.r = std::pow(c.r, 1.0f / 2.2f);
        c.g = std::pow(c.g, 1.0f / 2.2f);
        c.b = std::pow(c.b, 1.0f / 2.2f);
        pixels[i*3+0] = (uint8_t)(glm::clamp(c.r, 0.0f, 1.0f) * 255.0f);
        pixels[i*3+1] = (uint8_t)(glm::clamp(c.g, 0.0f, 1.0f) * 255.0f);
        pixels[i*3+2] = (uint8_t)(glm::clamp(c.b, 0.0f, 1.0f) * 255.0f);
    }

    if (outPixels) *outPixels = pixels;

    int ok = stbi_write_png(outPath.c_str(), w, h, 3, pixels.data(), w * 3);
    if (ok) std::cout << "[RayTracer] Beauty saved: " << outPath << "\n";
    else    std::cerr << "[RayTracer] Failed to write PNG: " << outPath << "\n";
    return ok != 0;
}

// ── CPU bloom helpers ─────────────────────────────────────────────────────────
// Separable box blur using a sliding window — O(w*h) per pass regardless of radius.
// Three passes of box blur approximate a Gaussian well.

static void boxBlurH(const std::vector<glm::vec3>& src, std::vector<glm::vec3>& dst,
                     int w, int h, int r) {
    float inv = 1.0f / (float)(2 * r + 1);
    for (int y = 0; y < h; ++y) {
        const glm::vec3* row = src.data() + y * w;
        glm::vec3*       out = dst.data() + y * w;
        glm::vec3 sum(0.0f);
        for (int k = -r; k <= r; ++k)
            sum += row[std::max(0, std::min(w - 1, k))];
        for (int x = 0; x < w; ++x) {
            out[x] = sum * inv;
            sum -= row[std::max(0, x - r)];
            sum += row[std::min(w - 1, x + r + 1)];
        }
    }
}

static void boxBlurV(const std::vector<glm::vec3>& src, std::vector<glm::vec3>& dst,
                     int w, int h, int r) {
    float inv = 1.0f / (float)(2 * r + 1);
    for (int x = 0; x < w; ++x) {
        glm::vec3 sum(0.0f);
        for (int k = -r; k <= r; ++k)
            sum += src[std::max(0, std::min(h - 1, k)) * w + x];
        for (int y = 0; y < h; ++y) {
            dst[y * w + x] = sum * inv;
            sum -= src[std::max(0, y - r) * w + x];
            sum += src[std::min(h - 1, y + r + 1) * w + x];
        }
    }
}

// Bright-pass → iterative box blur (radius doubles each iteration) → additive composite.
// Radius sequence 4, 8, 16, 32, 64… mirrors the GPU downsample pyramid at each iteration.
static void applyBloomCPU(std::vector<glm::vec3>& hdr, int w, int h,
                           float threshold, float /*knee*/,
                           int iterations, float intensity) {
    std::vector<glm::vec3> bright(w * h);
    for (int i = 0; i < w * h; ++i) {
        float br = std::max({hdr[i].r, hdr[i].g, hdr[i].b});
        float wt = (br > 1e-6f) ? std::max(0.0f, br - threshold) / br : 0.0f;
        bright[i] = hdr[i] * wt;
    }

    std::vector<glm::vec3> tmp(w * h);
    for (int iter = 0; iter < iterations; ++iter) {
        int r = 4 << iter;  // 4, 8, 16, 32, 64 …
        boxBlurH(bright, tmp, w, h, r);
        boxBlurV(tmp, bright, w, h, r);
    }

    for (int i = 0; i < w * h; ++i)
        hdr[i] += bright[i] * intensity;
}

// ── Checkpoint (d): progressive multi-sample render ───────────────────────────
void RayTracer::renderBeautyProgressive(const Camera& cam, int w, int h,
                                         float exposure, int maxSamples,
                                         RenderTask& task,
                                         const std::string& outPath,
                                         bool glassPassThrough) {
    if (!m_bvh || m_tris.empty() || m_lights.empty()) {
        std::cerr << "[RayTracer] renderBeautyProgressive: scene/lights not ready.\n";
        task.active = false;
        return;
    }

    float aspect     = (float)w / (float)h;
    float tanHalfFov = std::tan(cam.Zoom * 0.5f * (float)M_PI / 180.0f);

    nanort::TriangleMesh<float>        nanortMesh(m_vertices.data(), m_faces.data(), sizeof(float)*3);
    nanort::TriangleIntersector<float> intersector(nanortMesh);
    nanort::TriangleMesh<float>        shadowMesh (m_shadowVerts.data(), m_shadowFaces.data(), sizeof(float)*3);
    nanort::TriangleIntersector<float> shadowInter(shadowMesh);

    static const glm::vec3 kBackground(0.02f, 0.025f, 0.04f);
    TraceCtx ctx;
    ctx.primaryBVH       = &m_bvh->accel;
    ctx.primaryMesh      = &nanortMesh;
    ctx.primaryInter     = &intersector;
    ctx.shadowBVH        = m_shadowBvh ? &m_shadowBvh->accel : nullptr;
    ctx.shadowMesh       = &shadowMesh;
    ctx.shadowInter      = &shadowInter;
    ctx.tris             = &m_tris;
    ctx.lights           = &m_lights;
    ctx.lightRadius      = m_lightRadius;
    ctx.background       = kBackground;
    ctx.glassPassThrough = glassPassThrough;

    task.rowsDone = 0;

    // Pre-fill pixel buffer with dark background so partial results look correct
    {
        std::lock_guard<std::mutex> lk(task.bufMutex);
        task.buf.assign(w * h * 3, 5);
    }

    std::vector<glm::vec3> accumHDR(w * h, glm::vec3(0.0f));

    // Tonemap the full accumHDR buffer (averaged by `sample`) into task.buf.
    auto tonemap = [&](int sample) {
        float inv = 1.0f / (float)sample;
        std::lock_guard<std::mutex> lk(task.bufMutex);
        for (int i = 0; i < w*h; ++i) {
            glm::vec3 c = tonemapACES(accumHDR[i] * inv * exposure);
            c.r = std::pow(c.r, 1.0f/2.2f);
            c.g = std::pow(c.g, 1.0f/2.2f);
            c.b = std::pow(c.b, 1.0f/2.2f);
            task.buf[i*3+0] = (uint8_t)(glm::clamp(c.r,0.0f,1.0f)*255.0f);
            task.buf[i*3+1] = (uint8_t)(glm::clamp(c.g,0.0f,1.0f)*255.0f);
            task.buf[i*3+2] = (uint8_t)(glm::clamp(c.b,0.0f,1.0f)*255.0f);
        }
    };

    // ── Pass 1: no jitter, threads fill rows in parallel ─────────────────────
    unsigned nThreads = std::max(1u, std::thread::hardware_concurrency());
    {
        std::vector<std::thread> threads;
        threads.reserve(nThreads);
        for (unsigned t = 0; t < nThreads; ++t) {
            threads.emplace_back([&, t]() {
                nanort::TriangleMesh<float>        lMesh (m_vertices.data(),    m_faces.data(),       sizeof(float)*3);
                nanort::TriangleIntersector<float> lInter(lMesh);
                nanort::TriangleMesh<float>        lSMesh(m_shadowVerts.data(), m_shadowFaces.data(), sizeof(float)*3);
                nanort::TriangleIntersector<float> lSInter(lSMesh);
                TraceCtx lCtx = ctx;
                lCtx.primaryMesh  = &lMesh;
                lCtx.primaryInter = &lInter;
                lCtx.shadowMesh   = &lSMesh;
                lCtx.shadowInter  = &lSInter;

                for (int py = (int)t; py < h && !task.cancel; py += (int)nThreads) {
                    for (int px = 0; px < w; ++px) {
                        float ndcX = (2.0f*(px+0.5f)/w - 1.0f) * aspect * tanHalfFov;
                        float ndcY = (1.0f - 2.0f*(py+0.5f)/h) *          tanHalfFov;
                        glm::vec3 rd = glm::normalize(cam.Front + ndcX*cam.Right + ndcY*cam.Up);
                        nanort::Ray<float> ray;
                        ray.org[0]=cam.Position.x; ray.org[1]=cam.Position.y; ray.org[2]=cam.Position.z;
                        ray.dir[0]=rd.x; ray.dir[1]=rd.y; ray.dir[2]=rd.z;
                        ray.min_t=0.001f; ray.max_t=1.0e30f;
                        accumHDR[py*w+px] = sTraceRay(ray, 4, lCtx);
                    }
                    {
                        std::lock_guard<std::mutex> lk(task.bufMutex);
                        for (int px = 0; px < w; ++px) {
                            int i = py*w+px;
                            glm::vec3 c = tonemapACES(accumHDR[i] * exposure);
                            c.r = std::pow(c.r, 1.0f/2.2f);
                            c.g = std::pow(c.g, 1.0f/2.2f);
                            c.b = std::pow(c.b, 1.0f/2.2f);
                            task.buf[i*3+0] = (uint8_t)(glm::clamp(c.r,0.0f,1.0f)*255.0f);
                            task.buf[i*3+1] = (uint8_t)(glm::clamp(c.g,0.0f,1.0f)*255.0f);
                            task.buf[i*3+2] = (uint8_t)(glm::clamp(c.b,0.0f,1.0f)*255.0f);
                        }
                    }
                    ++task.rowsDone;
                    task.bufDirty = true;
                }
            });
        }
        for (auto& th : threads) th.join();
    }
    if (!task.cancel) {
        task.samplesDone = 1;
        std::cout << "[RayTracer] Pass 1/" << maxSamples << "\n" << std::flush;
    }

    // ── Passes 2+: jittered AA, update after each complete pass ──────────────
    for (int s = 2; s <= maxSamples && !task.cancel; ++s) {
        {
            std::vector<std::thread> threads;
            threads.reserve(nThreads);
            for (unsigned t = 0; t < nThreads; ++t) {
                threads.emplace_back([&, t, s]() {
                    nanort::TriangleMesh<float>        lMesh (m_vertices.data(),    m_faces.data(),       sizeof(float)*3);
                    nanort::TriangleIntersector<float> lInter(lMesh);
                    nanort::TriangleMesh<float>        lSMesh(m_shadowVerts.data(), m_shadowFaces.data(), sizeof(float)*3);
                    nanort::TriangleIntersector<float> lSInter(lSMesh);
                    TraceCtx lCtx = ctx;
                    lCtx.primaryMesh  = &lMesh;
                    lCtx.primaryInter = &lInter;
                    lCtx.shadowMesh   = &lSMesh;
                    lCtx.shadowInter  = &lSInter;

                    std::mt19937 rng((unsigned)(s * 1000003u) ^ (t * 2654435761u));
                    std::uniform_real_distribution<float> jit(-0.5f, 0.5f);

                    for (int py = (int)t; py < h && !task.cancel; py += (int)nThreads) {
                        for (int px = 0; px < w; ++px) {
                            float jx = jit(rng), jy = jit(rng);
                            float ndcX = (2.0f*(px+0.5f+jx)/w - 1.0f) * aspect * tanHalfFov;
                            float ndcY = (1.0f - 2.0f*(py+0.5f+jy)/h) *          tanHalfFov;
                            glm::vec3 rd = glm::normalize(cam.Front + ndcX*cam.Right + ndcY*cam.Up);
                            nanort::Ray<float> ray;
                            ray.org[0]=cam.Position.x; ray.org[1]=cam.Position.y; ray.org[2]=cam.Position.z;
                            ray.dir[0]=rd.x; ray.dir[1]=rd.y; ray.dir[2]=rd.z;
                            ray.min_t=0.001f; ray.max_t=1.0e30f;
                            accumHDR[py*w+px] += sTraceRay(ray, 4, lCtx);
                        }
                    }
                });
            }
            for (auto& th : threads) th.join();
        }

        if (!task.cancel) {
            tonemap(s);
            task.samplesDone = s;
            task.bufDirty    = true;
            std::cout << "[RayTracer] Pass " << s << "/" << maxSamples << "\n" << std::flush;
        }
    }

    // Save PNG on clean completion — applies bloom before tonemap (same order as raster pipeline).
    // The progressive display (task.buf) is ACES-only for speed; bloom runs once here at the end.
    if (!task.cancel && !outPath.empty()) {
        int totalSamples = task.samplesDone.load();
        float inv = exposure / std::max(1, totalSamples);

        std::vector<glm::vec3> finalHDR(w * h);
        for (int i = 0; i < w * h; ++i)
            finalHDR[i] = accumHDR[i] * inv;

        if (m_bloom.enabled) {
            std::cout << "[RayTracer] Applying bloom (threshold=" << m_bloom.threshold
                      << ", iters=" << m_bloom.iterations << ", intensity=" << m_bloom.intensity << ")...\n";
            applyBloomCPU(finalHDR, w, h, m_bloom.threshold, m_bloom.knee,
                          m_bloom.iterations, m_bloom.intensity);
        }

        std::vector<uint8_t> pngBuf(w * h * 3);
        for (int i = 0; i < w * h; ++i) {
            glm::vec3 c = tonemapACES(finalHDR[i]);
            c.r = std::pow(c.r, 1.0f / 2.2f);
            c.g = std::pow(c.g, 1.0f / 2.2f);
            c.b = std::pow(c.b, 1.0f / 2.2f);
            pngBuf[i*3+0] = (uint8_t)(glm::clamp(c.r, 0.0f, 1.0f) * 255.0f);
            pngBuf[i*3+1] = (uint8_t)(glm::clamp(c.g, 0.0f, 1.0f) * 255.0f);
            pngBuf[i*3+2] = (uint8_t)(glm::clamp(c.b, 0.0f, 1.0f) * 255.0f);
        }
        if (stbi_write_png(outPath.c_str(), w, h, 3, pngBuf.data(), w * 3))
            std::cout << "[RayTracer] Saved: " << outPath
                      << (m_bloom.enabled ? " (bloom+tonemap)" : " (tonemap only)") << "\n";
        else
            std::cerr << "[RayTracer] PNG write failed: " << outPath << "\n";
    }

    task.active = false;
}

// ── Animation: single frame to full convergence ───────────────────────────────
// Accumulates `samples` passes (pass 1 = no jitter, passes 2+ = ±0.5 px jitter),
// applies bloom+tonemap, writes outPath. Prints per-sample progress with \r.
bool RayTracer::renderFrame(const Camera& cam, int w, int h, float exposure,
                             int samples, const std::string& outPath) {
    float aspect     = (float)w / (float)h;
    float tanHalfFov = std::tan(cam.Zoom * 0.5f * (float)M_PI / 180.0f);

    nanort::TriangleMesh<float>        nanortMesh(m_vertices.data(), m_faces.data(), sizeof(float)*3);
    nanort::TriangleIntersector<float> intersector(nanortMesh);
    nanort::TriangleMesh<float>        shadowMesh (m_shadowVerts.data(), m_shadowFaces.data(), sizeof(float)*3);
    nanort::TriangleIntersector<float> shadowInter(shadowMesh);

    static const glm::vec3 kBg(0.02f, 0.025f, 0.04f);
    TraceCtx ctx;
    ctx.primaryBVH       = &m_bvh->accel;
    ctx.primaryMesh      = &nanortMesh;
    ctx.primaryInter     = &intersector;
    ctx.shadowBVH        = m_shadowBvh ? &m_shadowBvh->accel : nullptr;
    ctx.shadowMesh       = &shadowMesh;
    ctx.shadowInter      = &shadowInter;
    ctx.tris             = &m_tris;
    ctx.lights           = &m_lights;
    ctx.lightRadius      = m_lightRadius;
    ctx.background       = kBg;
    ctx.glassPassThrough = false;  // always full Fresnel for final renders

    std::vector<glm::vec3> accumHDR(w * h, glm::vec3(0.0f));
    // Aux buffers for OIDN: filled once during pass 1 (no-jitter primary hit), then frozen.
    // Never accumulated — averaged values at material edges would blur the denoiser's guides.
    std::vector<glm::vec3> auxAlbedo(w * h, kBg);
    std::vector<glm::vec3> auxNormal(w * h, glm::vec3(0.f, 0.f, 1.f));

    unsigned nThreads = std::max(1u, std::thread::hardware_concurrency());

    // Pass 1: no jitter (seed the accumulation buffer)
    std::cout << "  sample  1/" << samples << std::flush;
    {
        std::vector<std::thread> threads;
        threads.reserve(nThreads);
        for (unsigned t = 0; t < nThreads; ++t) {
            threads.emplace_back([&, t]() {
                nanort::TriangleMesh<float>        lMesh (m_vertices.data(),    m_faces.data(),       sizeof(float)*3);
                nanort::TriangleIntersector<float> lInter(lMesh);
                nanort::TriangleMesh<float>        lSMesh(m_shadowVerts.data(), m_shadowFaces.data(), sizeof(float)*3);
                nanort::TriangleIntersector<float> lSInter(lSMesh);
                TraceCtx lCtx = ctx;
                lCtx.primaryMesh  = &lMesh;
                lCtx.primaryInter = &lInter;
                lCtx.shadowMesh   = &lSMesh;
                lCtx.shadowInter  = &lSInter;

                for (int py = (int)t; py < h; py += (int)nThreads) {
                    for (int px = 0; px < w; ++px) {
                        float ndcX = (2.0f*(px+0.5f)/w - 1.0f) * aspect * tanHalfFov;
                        float ndcY = (1.0f - 2.0f*(py+0.5f)/h) * tanHalfFov;
                        glm::vec3 rd = glm::normalize(cam.Front + ndcX*cam.Right + ndcY*cam.Up);
                        nanort::Ray<float> ray;
                        ray.org[0]=cam.Position.x; ray.org[1]=cam.Position.y; ray.org[2]=cam.Position.z;
                        ray.dir[0]=rd.x; ray.dir[1]=rd.y; ray.dir[2]=rd.z;
                        ray.min_t=0.001f; ray.max_t=1.0e30f;

                        // Capture primary-hit albedo + normal for OIDN.
                        // One extra BVH traverse per pixel (no shading); lInter is safe to
                        // reuse — Traverse is stateless across calls (see renderNormals pattern).
                        nanort::TriangleIntersection<float> auxIsect;
                        if (lCtx.primaryBVH->Traverse(ray, lInter, &auxIsect, lCtx.opts)) {
                            const RTTriangle& at = (*lCtx.tris)[auxIsect.prim_id];
                            float au = auxIsect.u, av = auxIsect.v, aw = 1.f - au - av;
                            auxNormal[py*w+px] = glm::normalize(aw*at.n0 + au*at.n1 + av*at.n2);
                            if (!at.isGlass && at.mesh->cpuAlbedo && at.mesh->cpuAlbedo->valid()) {
                                glm::vec2 uv = aw*at.uv0 + au*at.uv1 + av*at.uv2;
                                auxAlbedo[py*w+px] = at.mesh->cpuAlbedo->sample(uv.x, uv.y);
                            } else if (!at.isGlass) {
                                auxAlbedo[py*w+px] = at.mesh->albedoColor;
                            }
                            // glass hit: leave auxAlbedo at kBg default
                        }

                        accumHDR[py*w+px] = sTraceRay(ray, 4, lCtx);
                    }
                }
            });
        }
        for (auto& th : threads) th.join();
    }

    // Passes 2..samples: jittered AA
    for (int s = 2; s <= samples; ++s) {
        std::cout << "\r  sample " << s << "/" << samples << std::flush;
        std::vector<std::thread> threads;
        threads.reserve(nThreads);
        for (unsigned t = 0; t < nThreads; ++t) {
            threads.emplace_back([&, t, s]() {
                nanort::TriangleMesh<float>        lMesh (m_vertices.data(),    m_faces.data(),       sizeof(float)*3);
                nanort::TriangleIntersector<float> lInter(lMesh);
                nanort::TriangleMesh<float>        lSMesh(m_shadowVerts.data(), m_shadowFaces.data(), sizeof(float)*3);
                nanort::TriangleIntersector<float> lSInter(lSMesh);
                TraceCtx lCtx = ctx;
                lCtx.primaryMesh  = &lMesh;
                lCtx.primaryInter = &lInter;
                lCtx.shadowMesh   = &lSMesh;
                lCtx.shadowInter  = &lSInter;

                std::mt19937 rng((unsigned)(s * 1000003u) ^ (t * 2654435761u));
                std::uniform_real_distribution<float> jit(-0.5f, 0.5f);
                for (int py = (int)t; py < h; py += (int)nThreads) {
                    for (int px = 0; px < w; ++px) {
                        float jx = jit(rng), jy = jit(rng);
                        float ndcX = (2.0f*(px+0.5f+jx)/w - 1.0f) * aspect * tanHalfFov;
                        float ndcY = (1.0f - 2.0f*(py+0.5f+jy)/h) * tanHalfFov;
                        glm::vec3 rd = glm::normalize(cam.Front + ndcX*cam.Right + ndcY*cam.Up);
                        nanort::Ray<float> ray;
                        ray.org[0]=cam.Position.x; ray.org[1]=cam.Position.y; ray.org[2]=cam.Position.z;
                        ray.dir[0]=rd.x; ray.dir[1]=rd.y; ray.dir[2]=rd.z;
                        ray.min_t=0.001f; ray.max_t=1.0e30f;
                        accumHDR[py*w+px] += sTraceRay(ray, 4, lCtx);
                    }
                }
            });
        }
        for (auto& th : threads) th.join();
    }
    std::cout << " — done\n" << std::flush;

    // Build averaged HDR, then: denoise → bloom → tonemap
    float inv = exposure / (float)std::max(1, samples);
    std::vector<glm::vec3> finalHDR(w * h);
    for (int i = 0; i < w * h; ++i)
        finalHDR[i] = accumHDR[i] * inv;

#ifdef HAS_OIDN
    if (m_oidn.enabled) {
        std::cout << "  [OIDN] denoising...\n" << std::flush;
        applyOIDN(finalHDR, auxAlbedo, auxNormal, w, h);
    }
#endif

    if (m_bloom.enabled)
        applyBloomCPU(finalHDR, w, h, m_bloom.threshold, m_bloom.knee,
                      m_bloom.iterations, m_bloom.intensity);

    std::vector<uint8_t> pngBuf(w * h * 3);
    for (int i = 0; i < w * h; ++i) {
        glm::vec3 c = tonemapACES(finalHDR[i]);
        c.r = std::pow(c.r, 1.0f/2.2f);
        c.g = std::pow(c.g, 1.0f/2.2f);
        c.b = std::pow(c.b, 1.0f/2.2f);
        pngBuf[i*3+0] = (uint8_t)(glm::clamp(c.r, 0.0f, 1.0f) * 255.0f);
        pngBuf[i*3+1] = (uint8_t)(glm::clamp(c.g, 0.0f, 1.0f) * 255.0f);
        pngBuf[i*3+2] = (uint8_t)(glm::clamp(c.b, 0.0f, 1.0f) * 255.0f);
    }
    if (!stbi_write_png(outPath.c_str(), w, h, 3, pngBuf.data(), w * 3)) {
        std::cerr << "[RayTracer] Frame write failed: " << outPath << "\n";
        return false;
    }
    return true;
}

// ── Animation render loop ─────────────────────────────────────────────────────
void RayTracer::renderAnimation(int w, int h, float exposure, std::atomic<bool>& cancel) {
    if (!m_bvh || m_tris.empty() || m_lights.empty()) {
        std::cerr << "[Anim] Scene not ready — call buildScene() and setLights() first.\n";
        return;
    }

    const AnimConfig& cfg = m_animCfg;
    int N = cfg.numFrames;

    std::error_code ec;
    std::filesystem::create_directories(cfg.outDir, ec);
    if (ec) {
        std::cerr << "[Anim] Cannot create output dir '" << cfg.outDir << "': " << ec.message() << "\n";
        return;
    }

    std::cout << "[Anim] " << N << " frames × " << cfg.spp << " spp → "
              << cfg.outDir << "/frame_XXXX.png\n"
              << "[Anim] R to cancel (keeps already-saved frames)\n" << std::flush;

    int saved = 0;
    for (int f = 0; f < N; ++f) {
        if (cancel) break;

        float t_raw   = (N > 1) ? (float)f / (float)(N - 1) : 0.0f;
        float t_eased = t_raw * t_raw * (3.0f - 2.0f * t_raw);  // smoothstep S-curve

        glm::vec3 pos   = glm::mix(cfg.startPos, cfg.endPos, t_eased);
        glm::vec3 front = glm::normalize(cfg.lookAt - pos);

        Camera cam;
        cam.SetPose(pos, front);
        cam.Zoom = cfg.fovDeg;

        char path[512];
        std::snprintf(path, sizeof(path), "%s/frame_%04d.png", cfg.outDir.c_str(), f);

        std::cout << "[Anim] frame " << (f + 1) << "/" << N
                  << "  t=" << t_eased
                  << "  pos=(" << pos.x << ", " << pos.y << ", " << pos.z << ")\n"
                  << std::flush;

        if (renderFrame(cam, w, h, exposure, cfg.spp, path))
            ++saved;
        else
            std::cerr << "[Anim] Write failed for frame " << f << "\n";
    }

    if (cancel) {
        std::cout << "[Anim] Cancelled — " << saved << "/" << N
                  << " frames saved to " << cfg.outDir << "/\n" << std::flush;
    } else {
        std::cout << "[Anim] Complete — " << saved << " frames saved to " << cfg.outDir << "/\n"
                  << "\n"
                  << "  Stitch to mp4 (30 fps, H.264):\n"
                  << "  ffmpeg -framerate 30 -i " << cfg.outDir << "/frame_%04d.png \\\n"
                  << "         -c:v libx264 -pix_fmt yuv420p -crf 18 animation.mp4\n"
                  << "\n"
                  << "  Quick preview (faster encode, slightly lower quality):\n"
                  << "  ffmpeg -framerate 30 -i " << cfg.outDir << "/frame_%04d.png \\\n"
                  << "         -c:v libx264 -pix_fmt yuv420p -crf 23 -preset fast animation.mp4\n"
                  << std::flush;
    }
}
