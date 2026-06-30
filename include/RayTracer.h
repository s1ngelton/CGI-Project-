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

private:
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
