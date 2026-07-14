#pragma once
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>
#include "RayTracer.h"

class Camera;

// GPU compute-shader ray tracer — staged/checkpointed port of RayTracer.
// Added alongside the CPU path; never modifies it. Requires GL 4.3+ (compute
// shaders). If the context doesn't support that, available() returns false
// and callers should fall back to the CPU RayTracer.
//
// CP1: BVH traversal + primary rays -> hit normal as color (no shading).
class GPURayTracer {
public:
    GPURayTracer();
    ~GPURayTracer();

    bool available() const { return m_available; }

    // Uploads BVH nodes/indices/vertices/normals from a CPU RayTracer's export.
    // Call again after any buildScene() change on the CPU side.
    void upload(const RayTracer::GPUBVHExport& data);

    // ── CP1 ─────────────────────────────────────────────────────────────────
    // Casts one primary ray per pixel, traverses the uploaded BVH, writes hit
    // normal*0.5+0.5 as RGB8 (background = (5,6,10)) — byte-identical encoding
    // to RayTracer::renderNormals so outputs are directly diffable.
    bool renderNormalsGPU(const Camera& cam, int w, int h,
                          std::vector<uint8_t>& outPixels,
                          double* outSeconds = nullptr);

    // ── CP2 ─────────────────────────────────────────────────────────────────
    // Cook-Torrance GGX + shadow rays, glass fully transparent (pass-through),
    // no reflections. Mirrors RayTracer::renderBeauty(glassPassThrough=true).
    // uNumLights is clamped to 4 (matches RTLight usage elsewhere in the engine).
    bool renderShadedGPU(const Camera& cam, int w, int h, float exposure,
                         const std::vector<RTLight>& lights, float lightRadius,
                         std::vector<uint8_t>& outPixels,
                         double* outSeconds = nullptr);

    // ── CP4 ─────────────────────────────────────────────────────────────────
    // Full beauty: CP2's opaque BRDF+shadows plus glass Fresnel reflect/transmit
    // via an iterative throughput stack. Mirrors RayTracer::renderBeauty with
    // glassPassThrough=false (CPU checkpoint (c)). Same uniform/buffer bindings
    // as renderShadedGPU, different program.
    bool renderGlassGPU(const Camera& cam, int w, int h, float exposure,
                        const std::vector<RTLight>& lights, float lightRadius,
                        std::vector<uint8_t>& outPixels,
                        double* outSeconds = nullptr);

    // ── CP5 ─────────────────────────────────────────────────────────────────
    // GPU iteration/preview: CP2's opaque BRDF+shadows + non-recursive glass
    // sheen approximation (see rt_cp5_gpu_beauty.comp header). Outputs raw HDR
    // color plus albedo/normal AOVs (no exposure/tonemap applied — hand these
    // to RayTracer::postProcessAndSave for OIDN/bloom/tonemap, same pipeline
    // renderFrame uses). Not diffed against CPU — glass is an intentional,
    // documented divergence for this path only.
    bool renderPreviewGPU(const Camera& cam, int w, int h,
                          const std::vector<RTLight>& lights, float lightRadius,
                          std::vector<glm::vec3>& outColor,
                          std::vector<glm::vec3>& outAlbedo,
                          std::vector<glm::vec3>& outNormal,
                          double* outSeconds = nullptr);

    // ── Diagnostic ─────────────────────────────────────────────────────────
    // GPU-side counterpart to RayTracer::debugTraceOpaqueViaGlassPassThrough —
    // same pass-through-only walk, for direct comparison at one target pixel.
    // outMaxSp/outTraverseCalls (optional): worst-case simultaneous BVH
    // traversal stack occupancy and total traverseBVH() call count for this
    // pixel — real occupancy data for the register-pressure question.
    RayTracer::RayDiagnostic debugCaptureGPU(const Camera& cam, int w, int h,
                                             int targetPx, int targetPy,
                                             const std::vector<RTLight>& lights,
                                             int* outMaxSp = nullptr,
                                             int* outTraverseCalls = nullptr);

    // Full push/pop/throughput trace from the REAL rt_cp4_glass.comp (not a
    // reimplementation) for one target pixel. Returns the raw trace buffer:
    // [0]=entryCount, then 6 floats/entry: [eventType,weight,depth,sp,tint.r,extra].
    // eventType: 0=push-reflect 1=push-transmit 2=leaf-depth-exhausted-bg
    //            3=leaf-miss-bg 4=leaf-opaque 5=drop-overflow
    std::vector<float> traceGlassGPU(const Camera& cam, int w, int h,
                                     int targetPx, int targetPy,
                                     const std::vector<RTLight>& lights, float lightRadius);

private:
    bool m_available = false;
    bool m_uploaded   = false;

    GLuint m_progNormals = 0;
    GLuint m_progShaded  = 0;
    GLuint m_progGlass   = 0;
    GLuint m_progPreview = 0;
    GLuint m_progDebugDiag = 0;
    GLuint m_ssboDebugOut  = 0;
    GLuint m_ssboTrace     = 0;

    GLuint m_ssboNodes   = 0;
    GLuint m_ssboIndices = 0;
    GLuint m_ssboVerts   = 0;
    GLuint m_ssboNormals = 0;

    GLuint m_ssboMaterials   = 0;
    GLuint m_ssboTriMat      = 0;
    GLuint m_ssboTriFlags    = 0;
    GLuint m_ssboTriUV       = 0;
    GLuint m_ssboShadowNodes   = 0;
    GLuint m_ssboShadowIndices = 0;
    GLuint m_ssboShadowVerts   = 0;

    GLuint m_albedoTex    = 0;
    bool   m_hasAlbedoTex = false;

    GLuint m_outTex = 0;
    int    m_texW = 0, m_texH = 0;

    // CP5 HDR/AOV outputs (separate from m_outTex, which stays RGBA8 for CP2/CP4).
    GLuint m_hdrColorTex  = 0;
    GLuint m_hdrAlbedoTex = 0;
    GLuint m_hdrNormalTex = 0;
    int    m_hdrW = 0, m_hdrH = 0;

    void ensureOutputTexture(int w, int h);
    void ensureHDROutputTextures(int w, int h);
    static GLuint compileComputeProgram(const std::string& path);

    // Shared dispatch path for renderShadedGPU/renderGlassGPU — see .cpp.
    bool dispatchShading(GLuint prog, const char* callerName,
                         const Camera& cam, int w, int h, float exposure,
                         const std::vector<RTLight>& lights, float lightRadius,
                         std::vector<uint8_t>& outPixels, double* outSeconds);
};
