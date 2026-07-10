#include "Renderer.h"
#include "Scene.h"
#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <random>
#include <string>

// Ceiling area light approximation: 4 point lights in a 2×2 grid at Y=2.9.
// Shadow map is rendered from the central position; all 4 lights share it.
static const glm::vec3 kLightColor(1.0f, 0.95f, 0.85f);   // warm white
static constexpr float kLightIntensity = 5.0f;
static constexpr float kLightRadius    = 8.0f;
static const glm::vec3 kCeilLights[4] = {
    { -1.5f, 2.9f,  1.0f },
    {  1.5f, 2.9f,  1.0f },
    { -1.5f, 2.9f, -1.0f },
    {  1.5f, 2.9f, -1.0f },
};
static const glm::vec3 kShadowLightPos(0.0f, 2.9f, 0.0f);


Renderer::Renderer(int width, int height)
    : m_width(width), m_height(height) {
    initFramebuffers();
    initShaders();
    initSSAOKernel();

    
}

void Renderer::LoadScene(Scene& scene){
    UploadScene(scene);
    UploadSceneToGPU();
    buildAndUploadBVH();

}

Renderer::~Renderer() {
    glDeleteTextures(1, &m_gPosition);
    glDeleteTextures(1, &m_gNormal);
    glDeleteTextures(1, &m_gAlbedo);
    glDeleteTextures(1, &m_gEmissive);
    glDeleteRenderbuffers(1, &m_gDepth);
    glDeleteFramebuffers(1, &m_gBuffer);

    glDeleteTextures(2, m_pingpongColor);
    glDeleteFramebuffers(2, m_pingpongFBO);

    glDeleteTextures(1, &m_hdrColor);
    glDeleteRenderbuffers(1, &m_hdrDepth);
    glDeleteFramebuffers(1, &m_hdrFBO);

    glDeleteTextures(1, &m_shadowMap);
    glDeleteFramebuffers(1, &m_shadowFBO);

    glDeleteTextures(1, &m_ssaoColor);
    glDeleteFramebuffers(1, &m_ssaoFBO);
    glDeleteTextures(1, &m_ssaoBlurColor);
    glDeleteFramebuffers(1, &m_ssaoBlurFBO);
    glDeleteTextures(1, &m_ssaoNoise);

    glDeleteTextures(1, &m_reflColor);
    glDeleteRenderbuffers(1, &m_reflDepth);
    glDeleteFramebuffers(1, &m_reflFBO);

    glDeleteTextures(1, &m_denoisedTex);
    glDeleteFramebuffers(1, &m_denoisedFBO);

    glDeleteTextures(1, &m_bloomTex);
    glDeleteFramebuffers(1, &m_bloomFBO);

    if (m_quadVAO) {
        glDeleteVertexArrays(1, &m_quadVAO);
        glDeleteBuffers(1, &m_quadVBO);
    }

    if (m_screenTex) glDeleteTextures(1, &m_screenTex);
    if (m_reflPos) glDeleteTextures(1, &m_reflPos);
    if (m_reflNorm) glDeleteTextures(1, &m_reflNorm);
    if (m_textureTex) glDeleteTextures(1, &m_textureTex);
    /*
    if (m_normalTex) glDeleteTextures(1, &m_normalTex);
    if (m_metallicTex) glDeleteTextures(1, &m_metallicTex);
    if (m_roughnessTex) glDeleteTextures(1, &m_roughnessTex);
*/
    if (m_vertexSSBO) glDeleteBuffers(1, &m_vertexSSBO);
    if (m_indexSSBO)  glDeleteBuffers(1, &m_indexSSBO);
    if (m_meshSSBO)   glDeleteBuffers(1, &m_meshSSBO);
    if (m_lightTriangleSSBO)   glDeleteBuffers(1, &m_lightTriangleSSBO);

    glDeleteBuffers(1, &m_bvhNodeSSBO);
    glDeleteBuffers(1, &m_bvhLeafCountSSBO);
    glDeleteBuffers(1, &m_bvhTrisSSBO);
    glDeleteBuffers(1, &m_triMeshSSBO);
}

// ── Framebuffer init ─────────────────────────────────────────────────────────

void Renderer::initFramebuffers() {
    glGenFramebuffers(1, &m_gBuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, m_gBuffer);

    // Position — world-space XYZ (RGBA16F; alpha unused)
    glGenTextures(1, &m_gPosition);
    glBindTexture(GL_TEXTURE_2D, m_gPosition);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, m_width, m_height, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_gPosition, 0);

    // Normal — world-space XYZ (RGBA16F; alpha unused)
    glGenTextures(1, &m_gNormal);
    glBindTexture(GL_TEXTURE_2D, m_gNormal);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, m_width, m_height, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, m_gNormal, 0);

    // Albedo RGB + specular A (RGBA8)
    glGenTextures(1, &m_gAlbedo);
    glBindTexture(GL_TEXTURE_2D, m_gAlbedo);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_width, m_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D, m_gAlbedo, 0);

    // Emissive — RGB16F so values > 1.0 survive into bloom
    glGenTextures(1, &m_gEmissive);
    glBindTexture(GL_TEXTURE_2D, m_gEmissive);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, m_width, m_height, 0, GL_RGB, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT4, GL_TEXTURE_2D, m_gEmissive, 0);

    // Slot 3 kept as GL_NONE: gbuffer frag writes gVelocity there but the
    // attachment stays unbound until the motion blur pass is implemented.
    GLenum drawBuffers[5] = {
        GL_COLOR_ATTACHMENT0,  // gPosition
        GL_COLOR_ATTACHMENT1,  // gNormal
        GL_COLOR_ATTACHMENT2,  // gAlbedoSpec
        GL_NONE,               // gVelocity (reserved for motion blur)
        GL_COLOR_ATTACHMENT4,  // gEmissive
    };
    glDrawBuffers(5, drawBuffers);

    // Depth renderbuffer
    glGenRenderbuffers(1, &m_gDepth);
    glBindRenderbuffer(GL_RENDERBUFFER, m_gDepth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, m_width, m_height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_gDepth);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::cerr << "GBuffer framebuffer incomplete!\n";

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // ── HDR color buffer (lighting pass writes here) ──────────────────────────
    glGenFramebuffers(1, &m_hdrFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, m_hdrFBO);

    glGenTextures(1, &m_hdrColor);
    glBindTexture(GL_TEXTURE_2D, m_hdrColor);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, m_width, m_height, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_hdrColor, 0);

    // Depth renderbuffer — needed so the forward glass pass can depth-test against opaque geo
    glGenRenderbuffers(1, &m_hdrDepth);
    glBindRenderbuffer(GL_RENDERBUFFER, m_hdrDepth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, m_width, m_height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_hdrDepth);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::cerr << "HDR framebuffer incomplete!\n";

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // ── Shadow map FBO ────────────────────────────────────────────────────────
    glGenFramebuffers(1, &m_shadowFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, m_shadowFBO);

    glGenTextures(1, &m_shadowMap);
    glBindTexture(GL_TEXTURE_2D, m_shadowMap);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24,
                 SHADOW_RES, SHADOW_RES, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    float shadowBorder[] = {1.0f, 1.0f, 1.0f, 1.0f};
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, shadowBorder);

    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_shadowMap, 0);
    // No colour attachment — tell the driver explicitly
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::cerr << "Shadow framebuffer incomplete!\n";

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // ── SSAO FBO ──────────────────────────────────────────────────────────────
    glGenFramebuffers(1, &m_ssaoFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, m_ssaoFBO);

    glGenTextures(1, &m_ssaoColor);
    glBindTexture(GL_TEXTURE_2D, m_ssaoColor);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R16F, m_width, m_height, 0, GL_RED, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_ssaoColor, 0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::cerr << "SSAO framebuffer incomplete!\n";

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // ── SSAO blur FBO ─────────────────────────────────────────────────────────
    glGenFramebuffers(1, &m_ssaoBlurFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, m_ssaoBlurFBO);

    glGenTextures(1, &m_ssaoBlurColor);
    glBindTexture(GL_TEXTURE_2D, m_ssaoBlurColor);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R16F, m_width, m_height, 0, GL_RED, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_ssaoBlurColor, 0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::cerr << "SSAO blur framebuffer incomplete!\n";

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // ── Reflection FBO (half resolution, RGBA16F + depth) ────────────────────
    {
        int rw = m_width  / 2;
        int rh = m_height / 2;

        glGenFramebuffers(1, &m_reflFBO);
        glBindFramebuffer(GL_FRAMEBUFFER, m_reflFBO);

        glGenTextures(1, &m_reflColor);
        glBindTexture(GL_TEXTURE_2D, m_reflColor);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, rw, rh, 0, GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_reflColor, 0);

        glGenRenderbuffers(1, &m_reflDepth);
        glBindRenderbuffer(GL_RENDERBUFFER, m_reflDepth);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, rw, rh);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_reflDepth);

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            std::cerr << "Reflection framebuffer incomplete!\n";

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    //Bloom blur FBO
    // Bloom composite result (full resolution, for raytracing)
    glGenTextures(1, &m_bloomTex);
    glBindTexture(GL_TEXTURE_2D, m_bloomTex);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA16F, m_width, m_height);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenFramebuffers(1, &m_bloomFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, m_bloomFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_bloomTex, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // ── Bloom ping-pong FBOs (half resolution) ────────────────────────────────
    // [0] = bright-pass output / blur source
    // [1] = blur destination (they swap each iteration in CP3)
    int halfW = m_width  / 2;
    int halfH = m_height / 2;
    for (int i = 0; i < 2; ++i) {
        glGenFramebuffers(1, &m_pingpongFBO[i]);
        glBindFramebuffer(GL_FRAMEBUFFER, m_pingpongFBO[i]);

        glGenTextures(1, &m_pingpongColor[i]);
        glBindTexture(GL_TEXTURE_2D, m_pingpongColor[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, halfW, halfH, 0, GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, m_pingpongColor[i], 0);

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            std::cerr << "Bloom ping-pong FBO " << i << " incomplete!\n";

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    //Raytracer denoiser FBO

    // Denoised output texture
    glGenTextures(1, &m_denoisedTex);
    glBindTexture(GL_TEXTURE_2D, m_denoisedTex);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA32F, m_width, m_height);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // FBO for denoised output
    glGenFramebuffers(1, &m_denoisedFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, m_denoisedFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_denoisedTex, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::cerr << "Denoised FBO incomplete!\n";
    glBindFramebuffer(GL_FRAMEBUFFER, 0);


    //RayTracing Output Texture
    glGenTextures(1, &m_screenTex);
    glBindTexture(GL_TEXTURE_2D, m_screenTex);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA32F, m_width, m_height); // <-- 1, not 0
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenTextures(1, &m_reflPos);
    glBindTexture(GL_TEXTURE_2D, m_reflPos);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA32F, m_width, m_height);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glGenTextures(1, &m_reflNorm);
    glBindTexture(GL_TEXTURE_2D, m_reflNorm);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA32F, m_width, m_height);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);


    glGenTextures(1, &m_textureTex);
    glBindTexture(GL_TEXTURE_2D_ARRAY, m_textureTex);

    glTexStorage3D(GL_TEXTURE_2D_ARRAY, 1, GL_RGBA8, 1028, 1028, 64); // Change to dynamic size
/*
    glGenTextures(1, &m_normalTex);
    glBindTexture(GL_TEXTURE_2D_ARRAY, m_normalTex);

    glTexStorage3D(GL_TEXTURE_2D_ARRAY, 1, GL_RGBA8, 1028, 1028, 64); // Change to dynamic size

    glGenTextures(1, &m_metallicTex);
    glBindTexture(GL_TEXTURE_2D_ARRAY, m_metallicTex);

    glTexStorage3D(GL_TEXTURE_2D_ARRAY, 1, GL_RGBA8, 1028, 1028, 64); // Change to dynamic size

    glGenTextures(1, &m_roughnessTex);
    glBindTexture(GL_TEXTURE_2D_ARRAY, m_roughnessTex);

    glTexStorage3D(GL_TEXTURE_2D_ARRAY, 1, GL_RGBA8, 1028, 1028, 64); // Change to dynamic size
*/
    //RayTracing SSBOs
    glGenBuffers(1, &m_vertexSSBO);
    glGenBuffers(1, &m_indexSSBO);
    glGenBuffers(1, &m_meshSSBO);
    glGenBuffers(1, &m_lightTriangleSSBO);
    

}

// ── Shader init ──────────────────────────────────────────────────────────────

void Renderer::initShaders() {
    m_gBufferShader   = Shader("shaders/gbuffer.vert",  "shaders/gbuffer.frag");
    m_lightingShader  = Shader("shaders/lighting.vert", "shaders/lighting.frag");
    m_shadowShader    = Shader("shaders/shadow.vert",   "shaders/shadow.frag");
    m_ssaoShader       = Shader("shaders/lighting.vert", "shaders/ssao.frag");
    m_ssaoBlurShader   = Shader("shaders/lighting.vert", "shaders/ssao_blur.frag");
    m_brightPassShader     = Shader("shaders/lighting.vert", "shaders/bloom_bright.frag");
    m_bloomBlurShader      = Shader("shaders/lighting.vert", "shaders/bloom_blur.frag");
    m_bloomCompositeShader = Shader("shaders/lighting.vert", "shaders/bloom_composite.frag");
    m_reflectionShader     = Shader("shaders/reflection.vert", "shaders/reflection.frag");
    m_glassShader          = Shader("shaders/glass.vert",       "shaders/glass.frag");
    m_tonemapShader        = Shader("shaders/lighting.vert", "shaders/tonemap.frag");
    m_ScreenSampler2D       = Shader("shaders/sampler2D.vert", "shaders/sampler2D.frag");
    m_denoiserShader        = Shader("shaders/sampler2D.vert", "shaders/denoiser.frag");

    m_rayTracerShader = ComputeShader("shaders/raytrace.comp");
}

void Renderer::initSSAOKernel() {
    std::default_random_engine rng(42);
    std::uniform_real_distribution<float> dist01(0.0f, 1.0f);
    std::uniform_real_distribution<float> distS(-1.0f, 1.0f);

    m_ssaoKernel.reserve(64);
    for (int i = 0; i < 64; ++i) {
        glm::vec3 sample(distS(rng), distS(rng), dist01(rng));
        sample = glm::normalize(sample) * dist01(rng);

        float scale = float(i) / 64.0f;
        scale = glm::mix(0.1f, 1.0f, scale * scale);
        m_ssaoKernel.push_back(sample * scale);
    }

    // 4×4 noise texture: random rotation vectors around z (no z component)
    std::vector<glm::vec3> noise;
    noise.reserve(16);
    for (int i = 0; i < 16; ++i)
        noise.push_back(glm::vec3(distS(rng), distS(rng), 0.0f));

    glGenTextures(1, &m_ssaoNoise);
    glBindTexture(GL_TEXTURE_2D, m_ssaoNoise);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, 4, 4, 0, GL_RGB, GL_FLOAT, noise.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glBindTexture(GL_TEXTURE_2D, 0);
}

// RayTracer init
void Renderer::buildBVH(
    std::vector<TriangleRef>& tris,
    uint32_t start,
    uint32_t end,
    std::vector<BVHNode>& nodes,
    std::vector<uint32_t>& leafCounts,
    std::vector<uint32_t>& bvhTris,
    const std::vector<Vertex>& vertices,
    const std::vector<uint32_t>& indices,
    uint32_t& nextNodeId
) {
    // 1. Compute bounding box for this range
    glm::vec3 bboxMin = glm::vec3(FLT_MAX);
    glm::vec3 bboxMax = glm::vec3(-FLT_MAX);
    for (uint32_t i = start; i < end; ++i) {
        uint32_t triIdx = tris[i].triIdx;
        glm::vec3 v0 = glm::vec3(vertices[indices[triIdx]].Position);
        glm::vec3 v1 = glm::vec3(vertices[indices[triIdx + 1]].Position);
        glm::vec3 v2 = glm::vec3(vertices[indices[triIdx + 2]].Position);
        bboxMin = glm::min(bboxMin, glm::min(glm::min(v0, v1), v2));
        bboxMax = glm::max(bboxMax, glm::max(glm::max(v0, v1), v2));
    }

    // 2. Create the node (placeholder)
    BVHNode node;
    node.bboxMin = bboxMin;
    node.bboxMax = bboxMax;
    node.leftChild = 0xFFFFFFFF; // default leaf
    node.rightChild = 0;

    uint32_t nodeIdx = nextNodeId++;
    nodes.push_back(node);
    leafCounts.push_back(0); // placeholder

    // 3. If small enough, create a leaf
    const uint32_t LEAF_SIZE = 4;
    if (end - start <= LEAF_SIZE) {
        // Store the start index in bvhTris
        nodes[nodeIdx].rightChild = (uint32_t)bvhTris.size();
        leafCounts[nodeIdx] = end - start;
        for (uint32_t i = start; i < end; ++i) {
            bvhTris.push_back(tris[i].triIdx);
        }
        return;
    }

    // 4. Internal node: choose split axis (longest extent)
    glm::vec3 extent = bboxMax - bboxMin;
    int axis = 0;
    if (extent.y > extent.x) axis = 1;
    if (extent.z > extent[axis]) axis = 2;

    // 5. Sort triangles by centroid along this axis
    std::sort(tris.begin() + start, tris.begin() + end,
        [axis](const TriangleRef& a, const TriangleRef& b) {
            return a.centroid[axis] < b.centroid[axis];
        });

    // 6. Split at the median
    uint32_t mid = (start + end) / 2;
    // Avoid empty children
    if (mid == start) mid = start + 1;
    if (mid == end) mid = end - 1;

    // 7. Recurse
    uint32_t leftChildIdx = nextNodeId;
    buildBVH(tris, start, mid, nodes, leafCounts, bvhTris, vertices, indices, nextNodeId);
    uint32_t rightChildIdx = nextNodeId;
    buildBVH(tris, mid, end, nodes, leafCounts, bvhTris, vertices, indices, nextNodeId);

    // 8. Update the internal node
    nodes[nodeIdx].leftChild = leftChildIdx;
    nodes[nodeIdx].rightChild = rightChildIdx;
}

void Renderer::UploadScene(const Scene& scene)
{
    m_vertices.clear();
    m_indices.clear();
    m_meshes.clear();
    m_lightTriangles.clear();

    for (const SceneObject& object : scene.objects)
    {
        const Model& model = object.model;

        for (const Mesh& mesh : model.GetMeshes())
        {

            uint32_t vertexOffset = (uint32_t)m_vertices.size();
            uint32_t indexOffset  = (uint32_t)m_indices.size();

            MeshInfo info;
            
            info.firstIndex = indexOffset;
            info.indexCount = (uint32_t)mesh.indices.size();
            info.materialIndex = mesh.diffuseTexID;
            
         
            info.albedoColor = glm::vec4(mesh.albedoColor, 1.0);
            info.emissiveColor = glm::vec4(mesh.emissiveColor, 1.0);
            info.emissiveStrength = mesh.emissiveStrength;
            info.metallic = mesh.metallic;
            info.roughness = mesh.roughness;
            info.ior = mesh.IOR;

            m_meshes.push_back(info);

            m_vertices.insert(m_vertices.end(), mesh.vertices.begin(), mesh.vertices.end());

            for (uint32_t i : mesh.indices){
                m_indices.push_back(i+vertexOffset);
            }
            

            if (info.emissiveStrength > .0) {
                for (uint32_t i = 0; i < mesh.indices.size(); i+=3){
                    LightTriangle lightTri;
                
                    lightTri.emission = glm::vec4(mesh.emissiveColor * mesh.emissiveStrength, 1.0);

                    glm::vec4 p0 = mesh.vertices[mesh.indices[i]].Position;
                    glm::vec4 p1 = mesh.vertices[mesh.indices[i+1]].Position;
                    glm::vec4 p2 = mesh.vertices[mesh.indices[i+2]].Position;
                    lightTri.v0 = p0;
                    lightTri.v1 = p1;
                    lightTri.v2 = p2;

                    lightTri.area = 0.5f * glm::length(glm::cross(glm::vec3(p1 - p0), glm::vec3(p2 - p0)));
                    m_totalEmissiveArea += lightTri.area;
                    lightTri.cumArea = m_totalEmissiveArea;

                    m_lightTriangles.push_back(lightTri);
                }
                    
            }

            

        }
    }

    for (const SceneObject& object : scene.glassObjects)
    {
        const Model& model = object.model;

        for (const Mesh& mesh : model.GetMeshes())
        {

            uint32_t vertexOffset = (uint32_t)m_vertices.size();
            uint32_t indexOffset  = (uint32_t)m_indices.size();

            MeshInfo info;
            
            info.firstIndex = indexOffset;
            info.indexCount = (uint32_t)mesh.indices.size();
            info.materialIndex = mesh.diffuseTexID;
            
         
            info.albedoColor = glm::vec4(mesh.albedoColor, 1.0);
            info.emissiveColor = glm::vec4(mesh.emissiveColor, 1.0);
            info.emissiveStrength = mesh.emissiveStrength;
            info.metallic = mesh.metallic;
            info.roughness = mesh.roughness;
            info.ior = mesh.IOR;

            m_meshes.push_back(info);

            m_vertices.insert(m_vertices.end(), mesh.vertices.begin(), mesh.vertices.end());

            for (uint32_t i : mesh.indices){
                m_indices.push_back(i+vertexOffset);
            }
            

            

            if (info.emissiveStrength > .0) {
                for (uint32_t i = 0; i < mesh.indices.size(); i+=3){
                    LightTriangle lightTri;
                
                    lightTri.emission = glm::vec4(mesh.emissiveColor * mesh.emissiveStrength, 1.0);

                    glm::vec4 p0 = mesh.vertices[mesh.indices[i]].Position;
                    glm::vec4 p1 = mesh.vertices[mesh.indices[i+1]].Position;
                    glm::vec4 p2 = mesh.vertices[mesh.indices[i+2]].Position;
                    lightTri.v0 = p0;
                    lightTri.v1 = p1;
                    lightTri.v2 = p2;

                    lightTri.area = 0.5f * glm::length(glm::cross(glm::vec3(p1 - p0), glm::vec3(p2 - p0)));
                    m_totalEmissiveArea += lightTri.area;
                    lightTri.cumArea = m_totalEmissiveArea;

                    m_lightTriangles.push_back(lightTri);
                }
                    
            }

            

        }
    }
}

void Renderer::buildAndUploadBVH()
{
    // --- 1. Build triMesh (maps triangle index -> mesh ID) ---
    m_triMesh.resize(m_indices.size() / 3, 0);
    for (uint32_t m = 0; m < m_meshes.size(); ++m) {
        const MeshInfo& mesh = m_meshes[m];
        for (uint32_t i = 0; i < mesh.indexCount; i += 3) {
            uint32_t triIdx = mesh.firstIndex + i; // global first index of triangle (in m_indices)
            m_triMesh[triIdx / 3] = m;
        }
    }

    // --- 2. Build TriangleRef list (centroids) ---
    std::vector<TriangleRef> tris;
    tris.reserve(m_indices.size() / 3);
    for (uint32_t i = 0; i < m_indices.size(); i += 3) {
        TriangleRef ref;
        ref.triIdx = i;
        glm::vec3 v0 = glm::vec3(m_vertices[m_indices[i]].Position);
        glm::vec3 v1 = glm::vec3(m_vertices[m_indices[i + 1]].Position);
        glm::vec3 v2 = glm::vec3(m_vertices[m_indices[i + 2]].Position);
        ref.centroid = (v0 + v1 + v2) / 3.0f;
        tris.push_back(ref);
    }

    // --- 3. Build BVH recursively ---
    std::vector<BVHNode> nodes;
    std::vector<uint32_t> leafCounts;
    std::vector<uint32_t> bvhTris;
    nodes.reserve(tris.size() * 2);
    leafCounts.reserve(tris.size() * 2);
    bvhTris.reserve(tris.size());

    uint32_t nextNodeId = 0;
    buildBVH(tris, 0, (uint32_t)tris.size(), nodes, leafCounts, bvhTris, m_vertices, m_indices, nextNodeId);

    // --- 4. Upload to SSBOs (bindings 5-8) ---

    // BVH nodes (binding 5)
    glGenBuffers(1, &m_bvhNodeSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_bvhNodeSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER, nodes.size() * sizeof(BVHNode), nodes.data(), GL_STATIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, m_bvhNodeSSBO);

    // Leaf counts (binding 6)
    glGenBuffers(1, &m_bvhLeafCountSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_bvhLeafCountSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER, leafCounts.size() * sizeof(uint32_t), leafCounts.data(), GL_STATIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 6, m_bvhLeafCountSSBO);

    // BVH triangle indices (binding 7)
    glGenBuffers(1, &m_bvhTrisSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_bvhTrisSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER, bvhTris.size() * sizeof(uint32_t), bvhTris.data(), GL_STATIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 7, m_bvhTrisSSBO);

    // triMesh mapping (binding 8)
    glGenBuffers(1, &m_triMeshSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_triMeshSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER, m_triMesh.size() * sizeof(uint32_t), m_triMesh.data(), GL_STATIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 8, m_triMeshSSBO);

    // Optional: print stats
    std::cout << "BVH built: " << nodes.size() << " nodes, " << bvhTris.size() << " triangles in leaves\n";
}

void Renderer::UploadSceneToGPU()
{
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_vertexSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER,
        m_vertices.size() * sizeof(Vertex),
        m_vertices.data(),
        GL_DYNAMIC_DRAW);

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, m_vertexSSBO);

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_indexSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER,
        m_indices.size() * sizeof(uint32_t),
        m_indices.data(),
        GL_DYNAMIC_DRAW);

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, m_indexSSBO);

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_meshSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER,
        m_meshes.size() * sizeof(MeshInfo),
        m_meshes.data(),
        GL_DYNAMIC_DRAW);

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, m_meshSSBO);

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_lightTriangleSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER,
        m_lightTriangles.size() * sizeof(LightTriangle),
        m_lightTriangles.data(),
        GL_DYNAMIC_DRAW);

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, m_lightTriangleSSBO);
}

// ── Screen quad (used by post-process passes) ─────────────────────────────────

void Renderer::renderQuad() {
    if (m_quadVAO == 0) {
        float verts[] = {
            -1.0f,  1.0f, 0.0f, 1.0f,
            -1.0f, -1.0f, 0.0f, 0.0f,
             1.0f,  1.0f, 1.0f, 1.0f,
             1.0f, -1.0f, 1.0f, 0.0f,
        };
        glGenVertexArrays(1, &m_quadVAO);
        glGenBuffers(1, &m_quadVBO);
        glBindVertexArray(m_quadVAO);
        glBindBuffer(GL_ARRAY_BUFFER, m_quadVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
        glBindVertexArray(0);
    }
    glBindVertexArray(m_quadVAO);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
}

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

void Renderer::saveRayTracingImage(const std::string& filename) {
    glFinish();

    // 1. Read RGBA float pixels
    std::vector<float> pixels(m_width * m_height * 4);
    glBindTexture(GL_TEXTURE_2D, m_screenTex);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, pixels.data());

    // 2. Convert to 8‑bit sRGB (gamma correct) and store in a linear array
    //    This is a flat RGB array, row-major, **top‑to‑bottom**.
    std::vector<unsigned char> image8(m_width * m_height * 3);
    for (int y = 0; y < m_height; ++y) {
        // OpenGL texture is bottom‑up, so we flip vertically here:
        int srcRow = (m_height - 1 - y); // source row (bottom-up)
        for (int x = 0; x < m_width; ++x) {
            int srcIdx = (srcRow * m_width + x) * 4;
            int dstIdx = (y * m_width + x) * 3;

            float r = pixels[srcIdx + 0];
            float g = pixels[srcIdx + 1];
            float b = pixels[srcIdx + 2];

            // Clamp and gamma correct (linear → sRGB)
            r = std::pow(std::min(r, 1.0f), 1.0f / 2.2f);
            g = std::pow(std::min(g, 1.0f), 1.0f / 2.2f);
            b = std::pow(std::min(b, 1.0f), 1.0f / 2.2f);

            image8[dstIdx + 0] = static_cast<unsigned char>(r * 255.0f);
            image8[dstIdx + 1] = static_cast<unsigned char>(g * 255.0f);
            image8[dstIdx + 2] = static_cast<unsigned char>(b * 255.0f);
        }
    }

    // 3. Write PNG with positive stride (no flip needed anymore)
    int stride = m_width * 3; // positive stride, because we already flipped
    if (!stbi_write_png(filename.c_str(), m_width, m_height, 3,
                        image8.data(), stride)) {
        std::cerr << "Failed to write PNG: " << filename << std::endl;
    } else {
        std::cout << "PNG saved: " << filename << std::endl;
    }
    
}
void Renderer::saveFinalImage(const std::string& filename) {
    // Make sure we use the texture that was displayed
    if (m_finalTex == 0) {
        std::cerr << "No final texture to save!" << std::endl;
        return;
    }

    glFinish();

    // Read pixels from the texture directly (no need to render to screen)
    std::vector<float> pixels(m_width * m_height * 4);
    glBindTexture(GL_TEXTURE_2D, m_finalTex);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, pixels.data());

    // Convert to 8-bit sRGB and flip
    std::vector<unsigned char> image8(m_width * m_height * 3);
    for (int y = 0; y < m_height; ++y) {
        int srcRow = (m_height - 1 - y);
        for (int x = 0; x < m_width; ++x) {
            int srcIdx = (srcRow * m_width + x) * 4;
            int dstIdx = (y * m_width + x) * 3;
            float r = std::pow(std::min(pixels[srcIdx + 0], 1.0f), 1.0f / 2.2f);
            float g = std::pow(std::min(pixels[srcIdx + 1], 1.0f), 1.0f / 2.2f);
            float b = std::pow(std::min(pixels[srcIdx + 2], 1.0f), 1.0f / 2.2f);
            image8[dstIdx + 0] = static_cast<unsigned char>(r * 255.0f);
            image8[dstIdx + 1] = static_cast<unsigned char>(g * 255.0f);
            image8[dstIdx + 2] = static_cast<unsigned char>(b * 255.0f);
        }
    }

    int stride = m_width * 3;
    if (!stbi_write_png(filename.c_str(), m_width, m_height, 3, image8.data(), stride)) {
        std::cerr << "Failed to write PNG: " << filename << std::endl;
    } else {
        std::cout << "Saved final image: " << filename << std::endl;
    }
}


// ── Render passes ─────────────────────────────────────────────────────────────

void Renderer::passShadow(Scene& scene, const Camera& cam) {
    // Orthographic projection: treats the point light as a directional
    // source for the purposes of a single shadow map.
    // Orthographic shadow from the central ceiling position, looking straight down.
    // Up = (1,0,0) because the view direction is -Y; any horizontal up vector works.
    glm::mat4 lightProj = glm::ortho(-5.0f, 5.0f, -5.0f, 5.0f, 0.1f, 20.0f);
    glm::mat4 lightView = glm::lookAt(kShadowLightPos,
                                      glm::vec3(0.0f),
                                      glm::vec3(1.0f, 0.0f, 0.0f));
    m_lightSpaceMatrix  = lightProj * lightView;

    glViewport(0, 0, SHADOW_RES, SHADOW_RES);
    glBindFramebuffer(GL_FRAMEBUFFER, m_shadowFBO);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);

    m_shadowShader.use();
    m_shadowShader.setMat4("lightSpaceMatrix", m_lightSpaceMatrix);

    scene.draw(m_shadowShader);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, m_width, m_height);
}

void Renderer::passGBuffer(Scene& scene, const Camera& cam) {
    glBindFramebuffer(GL_FRAMEBUFFER, m_gBuffer);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);

    m_gBufferShader.use();

    glm::mat4 view = cam.GetViewMatrix();
    glm::mat4 proj = glm::perspective(glm::radians(cam.Zoom),
                                      (float)m_width / (float)m_height,
                                      0.1f, 100.0f);
    m_gBufferShader.setMat4("view",       view);
    m_gBufferShader.setMat4("projection", proj);
    m_gBufferShader.setMat4("prevMVP",    m_prevViewProj);

    scene.draw(m_gBufferShader);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::passSSAO(const Camera& cam) {
    glBindFramebuffer(GL_FRAMEBUFFER, m_ssaoFBO);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);

    m_ssaoShader.use();

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_gPosition);
    m_ssaoShader.setInt("gPosition", 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_gNormal);
    m_ssaoShader.setInt("gNormal", 1);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, m_ssaoNoise);
    m_ssaoShader.setInt("texNoise", 2);

    for (int i = 0; i < 64; ++i)
        m_ssaoShader.setVec3("samples[" + std::to_string(i) + "]", m_ssaoKernel[i]);

    glm::mat4 view = cam.GetViewMatrix();
    glm::mat4 proj = glm::perspective(glm::radians(cam.Zoom),
                                      (float)m_width / (float)m_height,
                                      0.1f, 100.0f);
    m_ssaoShader.setMat4("view",       view);
    m_ssaoShader.setMat4("projection", proj);
    m_ssaoShader.setVec2("noiseScale", glm::vec2((float)m_width / 4.0f,
                                                  (float)m_height / 4.0f));

    renderQuad();

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::passSSAOBlur() {
    glBindFramebuffer(GL_FRAMEBUFFER, m_ssaoBlurFBO);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);

    m_ssaoBlurShader.use();

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_ssaoColor);
    m_ssaoBlurShader.setInt("ssaoInput", 0);

    renderQuad();

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::passReflection(Scene& scene, const Camera& cam) {
    int rw = m_width  / 2;
    int rh = m_height / 2;

    glBindFramebuffer(GL_FRAMEBUFFER, m_reflFBO);
    glViewport(0, 0, rw, rh);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);

    // Build reflected view: mirror camera position and front vector across Y=0
    glm::vec3 reflPos   = cam.Position * glm::vec3(1.0f, -1.0f, 1.0f);
    glm::vec3 reflFront = cam.Front    * glm::vec3(1.0f, -1.0f, 1.0f);
    glm::mat4 reflView  = glm::lookAt(reflPos, reflPos + reflFront,
                                      glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 reflProj  = glm::perspective(glm::radians(cam.Zoom),
                                           (float)m_width / (float)m_height,
                                           0.1f, 100.0f);
    m_reflectionProjView = reflProj * reflView;

    // Y-reflection inverts winding — flip to CW so backface culling stays correct
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CW);
    glEnable(GL_CLIP_DISTANCE0);

    m_reflectionShader.use();
    m_reflectionShader.setMat4 ("view",         reflView);
    m_reflectionShader.setMat4 ("projection",   reflProj);
    m_reflectionShader.setVec3 ("lightPos",       kShadowLightPos);
    m_reflectionShader.setVec3 ("lightColor",     kLightColor);
    m_reflectionShader.setFloat("lightIntensity", kLightIntensity);

    scene.drawForReflection(m_reflectionShader);

    glDisable(GL_CLIP_DISTANCE0);
    glFrontFace(GL_CCW);
    glDisable(GL_CULL_FACE);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, m_width, m_height);
}

void Renderer::passLighting(Scene& scene, const Camera& cam) {
    glBindFramebuffer(GL_FRAMEBUFFER, m_hdrFBO);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);

    m_lightingShader.use();

    // Bind GBuffer textures to the units the shader expects
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_gPosition);
    m_lightingShader.setInt("gPosition",   0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_gNormal);
    m_lightingShader.setInt("gNormal",     1);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, m_gAlbedo);
    m_lightingShader.setInt("gAlbedoSpec", 2);

    // Shadow map
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, m_shadowMap);
    m_lightingShader.setInt ("shadowMap",        3);
    m_lightingShader.setMat4("lightSpaceMatrix", m_lightSpaceMatrix);

    // SSAO (blurred)
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, m_ssaoBlurColor);
    m_lightingShader.setInt("ssaoTexture", 4);

    // Emissive G-buffer attachment
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D, m_gEmissive);
    m_lightingShader.setInt("gEmissive", 5);

    // Planar floor reflection
    glActiveTexture(GL_TEXTURE6);
    glBindTexture(GL_TEXTURE_2D, m_reflColor);
    m_lightingShader.setInt  ("reflectionTex",    6);
    m_lightingShader.setBool ("reflectionEnable", settings.reflection);
    m_lightingShader.setMat4 ("reflProjView",     m_reflectionProjView);
    m_lightingShader.setFloat("reflectivity",     settings.reflectivity);
    m_lightingShader.setFloat("glossyBlur",       settings.glossyBlur);

    // 4-light ceiling array
    for (int i = 0; i < 4; ++i) {
        m_lightingShader.setVec3 ("lightPositions["  + std::to_string(i) + "]", kCeilLights[i]);
        m_lightingShader.setVec3 ("lightColors["     + std::to_string(i) + "]", kLightColor);
        m_lightingShader.setFloat("lightIntensities[" + std::to_string(i) + "]", kLightIntensity);
    }
    m_lightingShader.setInt  ("numLights",   4);
    m_lightingShader.setFloat("lightRadius", kLightRadius);
    m_lightingShader.setVec3 ("viewPos",     cam.Position);

    // Feature toggles
    m_lightingShader.setBool("useShadows",     settings.shadows);
    m_lightingShader.setBool("useSoftShadows", settings.softShadow);
    m_lightingShader.setBool("useAO",          settings.ao);

    renderQuad();

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::passRayTracing(const Camera& cam)
{

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, m_width, m_height);
    glDisable(GL_DEPTH_TEST);


    m_rayTracerShader.use();

    m_rayTracerShader.setFloat("time", m_time);

    m_rayTracerShader.setUInt("numSamples", settings.numSamples);
    m_rayTracerShader.setUInt("diffuseSamples", settings.lightSamples);
    m_rayTracerShader.setUInt("emissiveCount", m_lightTriangles.size());
    m_rayTracerShader.setFloat("totalEmissiveArea", m_totalEmissiveArea);

    m_rayTracerShader.setVec3("camPos", cam.Position);
    m_rayTracerShader.setVec2("resolution", glm::vec2(m_width, m_height));
    m_rayTracerShader.setFloat("time", 1.0);
    glm::mat4 view = cam.GetViewMatrix();
    glm::mat4 camWorld = inverse(view);
    m_rayTracerShader.setMat4("camWorld", camWorld);
    

    glBindImageTexture(0, m_screenTex, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
    glBindImageTexture(1, m_reflPos, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
    glBindImageTexture(2, m_reflNorm, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);

    GLuint gx = (m_width  + 15) / 16;
    GLuint gy = (m_height + 15) / 16;
    
    glDispatchCompute(gx, gy, 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT |GL_TEXTURE_FETCH_BARRIER_BIT);

    m_ScreenSampler2D.use();

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_screenTex);

    glUniform1i(glGetUniformLocation(m_ScreenSampler2D.ID, "renderedImage"), 0);

    renderQuad();
}

void Renderer::applyDenoiserIterations(int iterations) {
    for (int i = 0; i < iterations; ++i) {
        // Each pass reads m_screenTex, writes to m_denoisedTex
        passDenoiser();
        // Swap so the next iteration reads the denoised result
        std::swap(m_screenTex, m_denoisedTex);
    }
    std::swap(m_screenTex, m_denoisedTex);
}

void Renderer::passDenoiser() {
    glDisable(GL_DEPTH_TEST);
    glBindFramebuffer(GL_FRAMEBUFFER, m_denoisedFBO);
    glViewport(0, 0, m_width, m_height);

    m_denoiserShader.use();

    m_denoiserShader.setFloat("sigma_depth", settings.sigmaDepth);    // Allow blur across the whole room
    m_denoiserShader.setFloat("sigma_normal", settings.sigmaNormal);   // Allow slight normal variations
    m_denoiserShader.setFloat("sigma_color", settings.sigmaColor);    // Linear color weight (works for 0-2 values)
    
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_screenTex);
    m_denoiserShader.setInt("noisyImage", 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_reflPos);
    m_denoiserShader.setInt("guidePos", 1);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, m_reflNorm);
    m_denoiserShader.setInt("guideNorm", 2);

    renderQuad();

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glEnable(GL_DEPTH_TEST);
}


void Renderer::passBrightPass(GLuint inputTex) {
    glBindFramebuffer(GL_FRAMEBUFFER, m_pingpongFBO[0]);
    glViewport(0, 0, m_width / 2, m_height / 2);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);

    m_brightPassShader.use();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, inputTex);
    m_brightPassShader.setInt("hdrBuffer", 0);
    m_brightPassShader.setFloat("threshold", settings.bloomThreshold);
    m_brightPassShader.setFloat("knee", settings.bloomKnee);

    renderQuad();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glEnable(GL_DEPTH_TEST);
}
void Renderer::passBloomBlur() {
    // Ping-pong separable Gaussian blur at half resolution.
    // Each iteration: H-pass [0]→[1], V-pass [1]→[0].
    // After N iterations the blurred result lives in m_pingpongColor[0].
    // passTonemap() restores the full-res viewport; passBloomComposite (CP4)
    // must set it to full-res too before writing back to m_hdrFBO.

    glDisable(GL_DEPTH_TEST);
    glViewport(0, 0, m_width / 2, m_height / 2);

    m_bloomBlurShader.use();
    m_bloomBlurShader.setInt("image", 0);
    glActiveTexture(GL_TEXTURE0);

    for (int i = 0; i < settings.bloomIterations; ++i) {
        // Horizontal: read from [0], write to [1]
        glBindFramebuffer(GL_FRAMEBUFFER, m_pingpongFBO[1]);
        glClear(GL_COLOR_BUFFER_BIT);
        m_bloomBlurShader.setBool("horizontal", true);
        glBindTexture(GL_TEXTURE_2D, m_pingpongColor[0]);
        renderQuad();

        // Vertical: read from [1], write to [0]
        glBindFramebuffer(GL_FRAMEBUFFER, m_pingpongFBO[0]);
        glClear(GL_COLOR_BUFFER_BIT);
        m_bloomBlurShader.setBool("horizontal", false);
        glBindTexture(GL_TEXTURE_2D, m_pingpongColor[1]);
        renderQuad();
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glEnable(GL_DEPTH_TEST);
}
void Renderer::passBloomComposite(GLuint inputTex, GLuint outputFBO) {
    glBindFramebuffer(GL_FRAMEBUFFER, outputFBO);
    glViewport(0, 0, m_width, m_height);
    glDisable(GL_DEPTH_TEST);

    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);

    m_bloomCompositeShader.use();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, inputTex);
    m_bloomCompositeShader.setInt("hdrBuffer", 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_pingpongColor[0]); // blurred bloom
    m_bloomCompositeShader.setInt("bloomBlur", 1);
    m_bloomCompositeShader.setFloat("bloomIntensity", settings.bloomIntensity);

    renderQuad();

    glDisable(GL_BLEND);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::applyBloom(GLuint inputTex, GLuint outputFBO) {
    // 1. Bright‑pass
    passBrightPass(inputTex);
    // 2. Blur (uses ping‑pong, always works)
    passBloomBlur();
    // 3. Composite
    passBloomComposite(inputTex, outputFBO);
}


void Renderer::passGlass(Scene& scene, const Camera& cam) {
    if (scene.glassObjects.empty()) return;

    // Copy opaque depth from the G-buffer into the HDR FBO's depth buffer so
    // glass fragments depth-test against the fully-rendered opaque scene.
    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_gBuffer);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_hdrFBO);
    glBlitFramebuffer(0, 0, m_width, m_height, 0, 0, m_width, m_height,
                      GL_DEPTH_BUFFER_BIT, GL_NEAREST);

    // Forward transparency into the HDR color buffer
    glBindFramebuffer(GL_FRAMEBUFFER, m_hdrFBO);
    glViewport(0, 0, m_width, m_height);

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);   // glass doesn't write depth
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    m_glassShader.use();

    glm::mat4 view = cam.GetViewMatrix();
    glm::mat4 proj = glm::perspective(glm::radians(cam.Zoom),
                                      (float)m_width / (float)m_height,
                                      0.1f, 100.0f);
    m_glassShader.setMat4 ("view",        view);
    m_glassShader.setMat4 ("projection",  proj);
    m_glassShader.setVec3 ("viewPos",     cam.Position);
    m_glassShader.setFloat("glassOpacity", 0.12f);
    m_glassShader.setVec3 ("glassColor",   glm::vec3(0.01f, 0.02f, 0.03f));
    m_glassShader.setVec3 ("fresnelColor", glm::vec3(1.0f, 0.97f, 0.90f));

    scene.drawGlassSorted(m_glassShader, cam.Position);

    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glDisable(GL_DEPTH_TEST);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::passDOF() {
    // TODO: post-process on HDR buffer
}

void Renderer::passMotionBlur(const Camera& cam) {
    // TODO: velocity-based blur using m_prevViewProj
}

void Renderer::passTonemap() {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, m_width, m_height);   // bloom passes may leave a half-res viewport
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);

    m_tonemapShader.use();

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_hdrColor);
    m_tonemapShader.setInt  ("hdrBuffer", 0);
    m_tonemapShader.setFloat("exposure",  settings.exposure);
    m_tonemapShader.setInt  ("tonemapOp", settings.tonemapOp);

    // Color grade + vignette
    m_tonemapShader.setBool ("gradeEnable",      settings.gradeEnable);
    m_tonemapShader.setFloat("temperature",      settings.temperature);
    m_tonemapShader.setVec3 ("gradeTint",        settings.gradeTint);
    m_tonemapShader.setFloat("saturation",       settings.saturation);
    m_tonemapShader.setVec3 ("shadowLift",       settings.shadowLift);
    m_tonemapShader.setFloat("vignetteStrength", settings.vignetteStrength);
    m_tonemapShader.setFloat("vignetteSoftness", settings.vignetteSoftness);

    renderQuad();

    glEnable(GL_DEPTH_TEST);
}

// ── Top-level render ──────────────────────────────────────────────────────────

void Renderer::renderRaytracing(Scene& scene, const Camera& camera, float deltaTime, float time) {
    m_time = time;

    // 1. Raytrace -> m_screenTex
    passRayTracing(camera);

    // 2. Denoise -> m_denoisedTex
    applyDenoiserIterations(1); // reads m_screenTex, writes to m_denoisedTex

    // 3. Determine final texture
    GLuint finalTex = m_denoisedTex;

    // 4. Apply bloom (if enabled)
    if (settings.bloom) {
        // Bloom reads m_denoisedTex, writes to m_bloomTex
        applyBloom(m_denoisedTex, m_bloomFBO);
        finalTex = m_bloomTex; // final is now in bloomTex
    }

    // 5. Display the final image on screen
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, m_width, m_height);
    glDisable(GL_DEPTH_TEST);

    m_ScreenSampler2D.use();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, finalTex);
    glUniform1i(glGetUniformLocation(m_ScreenSampler2D.ID, "renderedImage"), 0);
    renderQuad();

    glEnable(GL_DEPTH_TEST);

    // Store the final texture for saving later
    m_finalTex = finalTex;
}

void Renderer::renderRasterizer(Scene& scene, const Camera& camera, float deltaTime, float time) {
    m_time = time;
    passShadow  (scene, camera);
    if (settings.reflection)
        passReflection(scene, camera);
    passGBuffer (scene, camera);

    if (settings.ao) {
        passSSAO    (camera);
        passSSAOBlur();
    }

    passLighting(scene, camera);

    // ── Bloom chain ───────────────────────────────────────────────────────────
    if (settings.bloom) {
        applyBloom(m_hdrColor, m_hdrFBO); // input = hdrColor, output = hdrFBO
    }

    // ── Forward transparency: glass panels ────────────────────────────────────
    passGlass(scene, camera);

    // ── DOF / Motion blur go here once implemented ────────────────────────────
    // passDOF()
    // passMotionBlur(camera)

    passTonemap();

    // m_prevViewProj updated at end of frame — must stay after motion blur pass
    glm::mat4 view = camera.GetViewMatrix();
    glm::mat4 proj = glm::perspective(glm::radians(camera.Zoom),
                                      (float)m_width / (float)m_height,
                                      0.1f, 100.0f);
    m_prevViewProj = proj * view;
}

// ── Resize ────────────────────────────────────────────────────────────────────
// Call whenever the window framebuffer dimensions change (GLFW callback).
// Re-uploads storage for every screen-sized texture/renderbuffer in-place;
// FBO attachments don't need to be re-bound because they reference the object.
void Renderer::resize(int w, int h) {
    if (w == 0 || h == 0) return;   // minimized — skip to avoid 0×0 FBOs
    m_width  = w;
    m_height = h;

    // G-Buffer
    glBindTexture(GL_TEXTURE_2D, m_gPosition);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
    glBindTexture(GL_TEXTURE_2D, m_gNormal);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
    glBindTexture(GL_TEXTURE_2D, m_gAlbedo);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glBindTexture(GL_TEXTURE_2D, m_gEmissive);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, w, h, 0, GL_RGB, GL_FLOAT, nullptr);
    glBindRenderbuffer(GL_RENDERBUFFER, m_gDepth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);

    // HDR scene buffer
    glBindTexture(GL_TEXTURE_2D, m_hdrColor);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
    glBindRenderbuffer(GL_RENDERBUFFER, m_hdrDepth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);

    // SSAO buffers
    glBindTexture(GL_TEXTURE_2D, m_ssaoColor);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R16F, w, h, 0, GL_RED, GL_FLOAT, nullptr);
    glBindTexture(GL_TEXTURE_2D, m_ssaoBlurColor);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R16F, w, h, 0, GL_RED, GL_FLOAT, nullptr);

    // Bloom ping-pong buffers (half resolution)
    int halfW = w / 2;
    int halfH = h / 2;
    for (int i = 0; i < 2; ++i) {
        glBindTexture(GL_TEXTURE_2D, m_pingpongColor[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, halfW, halfH, 0, GL_RGBA, GL_FLOAT, nullptr);
    }

    // Reflection buffer (half resolution)
    glBindTexture(GL_TEXTURE_2D, m_reflColor);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, halfW, halfH, 0, GL_RGBA, GL_FLOAT, nullptr);
    glBindRenderbuffer(GL_RENDERBUFFER, m_reflDepth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, halfW, halfH);

    glBindTexture(GL_TEXTURE_2D, 0);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
}
