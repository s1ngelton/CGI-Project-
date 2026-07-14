#pragma once
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <memory>
#include <atomic>
#include <mutex>

// Forward declarations — nanort types stay confined to RayTracer.cpp (pimpl)
class Scene;
class Camera;
class Mesh;

// Shared state between the render thread and the main (GL upload) thread.
// All fields written by the render thread; read by the main thread.
struct RenderTask {
    std::atomic<bool>    active{false};       // render thread is running
    std::atomic<bool>    cancel{false};       // set to true to request stop
    std::atomic<int>     samplesDone{0};      // AA passes completed so far
    std::atomic<int>     rowsDone{0};         // rows finished during pass-1 scan (0 before pass 1 ends)
    std::atomic<bool>    bufDirty{false};     // buf has new pixels — upload to GL

    std::mutex           bufMutex;
    std::vector<uint8_t> buf;                 // latest tonemapped w*h*3 pixels

    // non-copyable (mutex)
    RenderTask() = default;
    RenderTask(const RenderTask&) = delete;
    RenderTask& operator=(const RenderTask&) = delete;
};

struct OIDNSettings {
    bool enabled = false;
};

// Bloom parameters passed from main.cpp renderer settings → RT PNG export.
// Applied once to the final accumulated HDR before tonemap (same order as raster pipeline).
struct BloomSettings {
    bool  enabled    = false;
    float threshold  = 1.0f;
    float knee       = 0.1f;
    int   iterations = 5;
    float intensity  = 0.04f;
};

// ── Animation shot parameters — edit these to change the push-in ──────────────
// Camera moves from startPos to endPos over numFrames, always looking at lookAt.
// t is smoothstepped (slow in / slow out). At 30 fps: numFrames/30 = seconds.
// spp=1 for a fast preview pass; spp=4..8 for a clean final render.
struct AnimConfig {
    glm::vec3   startPos  = { 0.0f,  1.55f,  6.5f }; // back near room centre
    glm::vec3   endPos    = { 0.3f,  1.15f,  2.8f }; // close to the chair
    glm::vec3   lookAt    = { 0.3f,  0.65f,  0.2f }; // chair centre (seat height ≈ Y 0.65)
    float       fovDeg    = 35.0f;                     // same lens as the interactive scene
    int         numFrames = 60;   // 2 s at 30 fps — bump to 120 for 4 s
    int         spp       = 4;    // samples per frame (set to 1 for a quick preview)
    std::string outDir    = "frames";
};

// Point light as seen by the CPU ray tracer — identical values to kCeilLights in Renderer.cpp
struct RTLight {
    glm::vec3 position;
    glm::vec3 color;
    float     intensity;
};

// Per-triangle data kept in CPU memory alongside the flat nanort arrays.
// m_tris[i] corresponds to face i in the BVH.
struct RTTriangle {
    glm::vec3 v0, v1, v2;   // world-space vertex positions
    glm::vec3 n0, n1, n2;   // world-space vertex normals (for smooth interpolation)
    glm::vec2 uv0, uv1, uv2; // texture coordinates — interpolated for CPU texture sampling
    const Mesh* mesh;        // back-pointer for material lookup
    bool isGlass = false;    // true for glassObjects — handled with Fresnel + transmission
};

class RayTracer {
public:
    RayTracer();
    ~RayTracer();  // defined in .cpp so unique_ptr<BVH> destructor can see the type

    // Extract all triangles from scene into world space and build SAH BVH.
    // Call after scene.load() and after any scene change.
    void buildScene(const Scene& scene);
    bool hasScene() const { return !m_tris.empty(); }

    // ── Checkpoint (a): primary ray → hit normal as RGB ───────────────────────
    // Casts one ray per pixel. Saves a PNG and returns true on success.
    // If outPixels != nullptr, fills it with the raw RGB byte buffer (w*h*3)
    // so the caller can upload it to an OpenGL texture for on-screen display.
    // Background = dark sky tint; hits mapped to normal*0.5+0.5.
    bool renderNormals(const Camera& cam, int w, int h, const std::string& outPath,
                       std::vector<uint8_t>* outPixels = nullptr);

    // ── Checkpoint (b): Cook-Torrance GGX BRDF + any-hit shadow rays ──────────
    // Math is VERBATIM from lighting.frag. Casts 1 primary + up to 4 shadow rays
    // per pixel. Tonemapped to sRGB before PNG write. No reflection bounces yet.
    // Call setLights() before this.
    void setLights(const std::vector<RTLight>& lights, float radius) {
        m_lights      = lights;
        m_lightRadius = radius;
    }
    void setBloomSettings(const BloomSettings& s) { m_bloom  = s; }
    void setOIDNSettings (const OIDNSettings&  s) { m_oidn   = s; }
    void setAnimConfig   (const AnimConfig&    c) { m_animCfg = c; }

    // ── Animation render ──────────────────────────────────────────────────────
    // Renders numFrames to outDir/frame_XXXX.png, each converged to full spp.
    // Checks cancel between frames — safe to abort mid-sequence (already-saved
    // frames are kept). Call from a std::thread; does not touch GL.
    void renderAnimation(int w, int h, float exposure, std::atomic<bool>& cancel);

    // glassPassThrough=true → checkpoint (b): glass transparent, BRDF+shadows only.
    // glassPassThrough=false → checkpoint (c): Fresnel reflection+transmission on glass.
    bool renderBeauty(const Camera& cam, int w, int h, const std::string& outPath,
                      std::vector<uint8_t>* outPixels = nullptr, float exposure = 1.0f,
                      bool glassPassThrough = false);

    // ── Checkpoint (d): progressive multi-sample render ───────────────────────
    // Runs synchronously — call from a std::thread.
    // Pass 1 updates task.buf row-by-row (scanline fill-in effect).
    // Passes 2..maxSamples add jitter (±0.5 px) and update after each full pass.
    // Sets task.bufDirty = true whenever buf is refreshed; main thread uploads.
    // Saves PNG to outPath on clean completion (cancel not requested).
    void renderBeautyProgressive(const Camera& cam, int w, int h,
                                  float exposure, int maxSamples,
                                  RenderTask& task,
                                  const std::string& outPath,
                                  bool glassPassThrough = false);

    // Renders one camera position to full convergence, writes outPath.
    // Prints per-sample progress with \r in-place. Used by renderAnimation() and --compare.
    bool renderFrame(const Camera& cam, int w, int h, float exposure,
                     int samples, const std::string& outPath);

    // ── Diagnostic (read-only, does not touch sTraceRay) ────────────────────────
    // Walks the primary ray for pixel (px,py), following glass TRANSMISSION only
    // (matches checkpoint-b pass-through — ignores the Fresnel reflection branch)
    // until it hits opaque geometry or background. Captures the hit's identity
    // and shading inputs for direct comparison against the GPU's equivalent walk.
    // This mirrors sTraceRay's glass pass-through + normal-interpolation logic by
    // duplication, not by calling it — sTraceRay itself is never modified.
    struct RayDiagnostic {
        bool         hit = false;
        bool         isBackground = false;
        unsigned int prim = 0xFFFFFFFFu;
        glm::vec3  hitPos{0.0f};
        float      baryU = 0.0f, baryV = 0.0f;
        glm::vec3  N{0.0f};
        glm::vec3  V{0.0f};
        int        numLights = 0;
        float      NdotL[4] = {0,0,0,0};
    };
    RayDiagnostic debugTraceOpaqueViaGlassPassThrough(const Camera& cam, int w, int h,
                                                       int px, int py) const;

    // Full recursive glass trace (mirrors sTraceRay's Fresnel branch exactly, by
    // duplication — sTraceRay itself untouched) for one pixel, logged in the same
    // flat format as GPURayTracer::traceGlassGPU for direct comparison:
    // [0]=entryCount, then 6 floats/entry: [eventType,weight,depth,sp(-1 unused),tint.r,extra]
    // eventType: 0=push-reflect 1=push-transmit 2=leaf-depth-exhausted-bg
    //            3=leaf-miss-bg 4=leaf-opaque 5=drop-overflow(unused on CPU)
    std::vector<float> debugTraceGlassFull(const Camera& cam, int w, int h, int px, int py) const;

    // ── GPU export (read-only) ─────────────────────────────────────────────────
    // Zero-copy view into the CPU-built BVH/geometry for uploading to GPU SSBOs.
    // nodeBytes points at nanort::BVHNode<float> — kept as raw bytes here so
    // nanort.h stays out of this header. Layout (verified via offsetof, 40 bytes,
    // no padding): float bmin[3]; float bmax[3]; int flag; int axis; uint data[2];

    // Per-material data for the GPU shader — 48 bytes, flat scalars (not vec3) so
    // the layout matches a GLSL std430 struct byte-for-byte with no manual padding
    // games. Built once per buildScene() by dedup'ing on Mesh pointer.
    struct GPUMaterial {
        float albedoR = 0, albedoG = 0, albedoB = 0, roughness = 0.5f;
        float emissiveR = 0, emissiveG = 0, emissiveB = 0, emissiveStrength = 0;
        float metallic = 0; unsigned int hasAlbedoTex = 0; float _pad0 = 0, _pad1 = 0;
    };

    struct GPUBVHExport {
        const void*          nodeBytes        = nullptr;
        size_t                nodeCount        = 0;
        size_t                nodeStrideBytes  = 0;
        const unsigned int*  indices          = nullptr;  // BVH leaf primitive remap
        size_t                indexCount       = 0;
        const float*         vertices         = nullptr;  // 9 floats/tri: v0,v1,v2
        size_t                vertexFloatCount = 0;
        const RTTriangle*    tris             = nullptr;  // parallel to vertices/prim_id
        size_t                triCount         = 0;

        // ── CP2 additions ──────────────────────────────────────────────────────
        // Shadow BVH — opaque non-emissive geometry only (mirrors m_shadowBvh).
        const void*          shadowNodeBytes        = nullptr;
        size_t                shadowNodeCount        = 0;
        size_t                shadowNodeStrideBytes  = 0;
        const unsigned int*  shadowIndices          = nullptr;
        size_t                shadowIndexCount       = 0;
        const float*         shadowVertices         = nullptr;
        size_t                shadowVertexFloatCount = 0;

        // Materials, deduped by Mesh pointer, plus per-triangle lookups (parallel
        // to `tris`/vertices/prim_id order — NOT BVH leaf order).
        const GPUMaterial*    materials      = nullptr;
        size_t                materialCount  = 0;
        const unsigned int*  triMaterialID  = nullptr;  // index into materials[]
        const unsigned int*  triFlags       = nullptr;  // bit0 = isGlass
        const float*         triUVs         = nullptr;  // 6 floats/tri: uv0,uv1,uv2

        // Single shared CPU albedo texture (the current CPU RT only ever samples
        // one — see buildScene() comment). Linear float RGBA (alpha=1), sRGB
        // already decoded on load, same source CPUTexture the CPU path samples.
        const float*         albedoPixelsRGBA = nullptr; // albedoW*albedoH*4 floats
        int                    albedoW = 0, albedoH = 0;
    };
    GPUBVHExport exportForGPU() const;

    // ── GPU-preview post-process (CP5) ──────────────────────────────────────────
    // Shared tail of renderFrame's pipeline (exposure -> OIDN -> bloom -> tonemap ->
    // write), exposed so the GPU preview path can reuse the exact same CPU post-
    // process code/settings (setBloomSettings/setOIDNSettings) instead of
    // duplicating it. Does not touch renderFrame or any other existing method.
    bool postProcessAndSave(std::vector<glm::vec3>& hdr,
                            std::vector<glm::vec3>& albedo,
                            std::vector<glm::vec3>& normal,
                            int w, int h, float exposure,
                            const std::string& outPath);

private:
    // Built once at the end of buildScene(); see exportForGPU().
    void buildGPUMaterialData();
    std::vector<GPUMaterial>   m_gpuMaterials;
    std::vector<unsigned int>  m_gpuTriMaterialID;
    std::vector<unsigned int>  m_gpuTriFlags;
    std::vector<float>         m_gpuTriUVs;
    std::vector<float>         m_gpuAlbedoRGBA;
    int                          m_gpuAlbedoW = 0, m_gpuAlbedoH = 0;

    // m_tris[i] parallel to BVH face i — used for normal/material lookup after hit
    std::vector<RTTriangle>   m_tris;
    // Flat position arrays fed to nanort (triangle soup: no shared vertices)
    std::vector<float>        m_vertices;   // N*9 floats: v0 v1 v2 per triangle
    std::vector<unsigned int> m_faces;      // N*3 uints:  0,1,2  3,4,5 … per triangle

    // nanort BVH hidden behind pimpl so nanort.h stays out of every translation unit
    struct BVH;
    std::unique_ptr<BVH> m_bvh;

    // Shadow BVH — opaque non-emissive geometry only.
    // Excludes glassObjects (transparent in raster shadow map) and meshes with
    // emissiveStrength > 0 (ceiling fixtures sit at light height and block all
    // upward shadow rays). Mirrors passShadow() which only draws opaque objects.
    std::vector<float>         m_shadowVerts;
    std::vector<unsigned int>  m_shadowFaces;
    std::unique_ptr<BVH>       m_shadowBvh;

    // Lighting (set via setLights — mirrors kCeilLights / kLightRadius in Renderer.cpp)
    std::vector<RTLight> m_lights;
    float                m_lightRadius = 8.0f;

    BloomSettings m_bloom;
    OIDNSettings  m_oidn;
    AnimConfig    m_animCfg;
};
