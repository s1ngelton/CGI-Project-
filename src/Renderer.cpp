#include "Renderer.h"
#include "Scene.h"
#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <random>
#include <string>

// Hardcoded light — shared between passShadow and passLighting.
// Replace with Scene light list once multiple lights are needed.
static const glm::vec3 kLightPos(0.0f, 2.8f, 0.0f);

Renderer::Renderer(int width, int height)
    : m_width(width), m_height(height) {
    initFramebuffers();
    initShaders();
    initSSAOKernel();
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

    if (m_quadVAO) {
        glDeleteVertexArrays(1, &m_quadVAO);
        glDeleteBuffers(1, &m_quadVBO);
    }
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
    m_tonemapShader        = Shader("shaders/lighting.vert", "shaders/tonemap.frag");
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

// ── Render passes ─────────────────────────────────────────────────────────────

void Renderer::passShadow(Scene& scene, const Camera& cam) {
    // Orthographic projection: treats the point light as a directional
    // source for the purposes of a single shadow map.
    // Up = (1,0,0) because kLightPos points straight down at origin.
    glm::mat4 lightProj = glm::ortho(-5.0f, 5.0f, -5.0f, 5.0f, 0.1f, 20.0f);
    glm::mat4 lightView = glm::lookAt(kLightPos,
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
    m_reflectionShader.setVec3 ("lightPos",     kLightPos);
    m_reflectionShader.setVec3 ("lightColor",   glm::vec3(1.0f, 0.95f, 0.85f));
    m_reflectionShader.setFloat("lightIntensity", 10.0f);

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

    // Single point light — warm white, just under ceiling panel
    m_lightingShader.setVec3 ("lightPos",       kLightPos);
    m_lightingShader.setVec3 ("lightColor",     glm::vec3(1.0f, 0.95f, 0.85f));
    m_lightingShader.setFloat("lightIntensity", 10.0f);
    m_lightingShader.setVec3 ("viewPos",        cam.Position);

    // Feature toggles
    m_lightingShader.setBool("useShadows",     settings.shadows);
    m_lightingShader.setBool("useSoftShadows", settings.softShadow);
    m_lightingShader.setBool("useAO",          settings.ao);

    renderQuad();

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::passBrightPass() {
    // Extract HDR pixels above threshold into ping-pong[0] at half resolution.
    // passTonemap() restores the viewport to full-res afterwards.
    glBindFramebuffer(GL_FRAMEBUFFER, m_pingpongFBO[0]);
    glViewport(0, 0, m_width / 2, m_height / 2);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);

    m_brightPassShader.use();

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_hdrColor);
    m_brightPassShader.setInt  ("hdrBuffer", 0);
    m_brightPassShader.setFloat("threshold", settings.bloomThreshold);
    m_brightPassShader.setFloat("knee",      settings.bloomKnee);

    renderQuad();

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glEnable(GL_DEPTH_TEST);
    // Viewport intentionally left at half-res; passBloomBlur runs at the same res.
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

void Renderer::passBloomComposite() {
    // Additively blend blurred bloom (m_pingpongColor[0]) back into m_hdrFBO.
    // GL_ONE + GL_ONE means: hdr_dst = hdr_dst + bloom_src.  No read-write
    // hazard because we write to m_hdrFBO while sampling m_pingpongColor[0].
    glBindFramebuffer(GL_FRAMEBUFFER, m_hdrFBO);
    glViewport(0, 0, m_width, m_height);
    glDisable(GL_DEPTH_TEST);

    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);

    m_bloomCompositeShader.use();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_pingpongColor[0]);
    m_bloomCompositeShader.setInt  ("bloomBlur",      0);
    m_bloomCompositeShader.setFloat("bloomIntensity", settings.bloomIntensity);
    renderQuad();

    glDisable(GL_BLEND);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glEnable(GL_DEPTH_TEST);
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

void Renderer::render(Scene& scene, const Camera& camera, float deltaTime) {
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
        passBrightPass();
        passBloomBlur();
        passBloomComposite();
    }

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
