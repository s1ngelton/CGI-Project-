#include "Renderer.h"
#include "Scene.h"
#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>

Renderer::Renderer(int width, int height)
    : m_width(width), m_height(height) {
    initFramebuffers();
    initShaders();
}

Renderer::~Renderer() {
    glDeleteTextures(1, &m_gPosition);
    glDeleteTextures(1, &m_gNormal);
    glDeleteTextures(1, &m_gAlbedo);
    glDeleteRenderbuffers(1, &m_gDepth);
    glDeleteFramebuffers(1, &m_gBuffer);

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

    // The gbuffer frag also writes gVelocity at location 3; no attachment bound
    // there yet — output is discarded until motion blur adds GL_COLOR_ATTACHMENT3.
    GLenum drawBuffers[3] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2 };
    glDrawBuffers(3, drawBuffers);

    // Depth renderbuffer
    glGenRenderbuffers(1, &m_gDepth);
    glBindRenderbuffer(GL_RENDERBUFFER, m_gDepth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, m_width, m_height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_gDepth);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::cerr << "GBuffer framebuffer incomplete!\n";

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// ── Shader init ──────────────────────────────────────────────────────────────

void Renderer::initShaders() {
    m_gBufferShader = Shader("shaders/gbuffer.vert", "shaders/gbuffer.frag");
}

void Renderer::initSSAOKernel() {
    // TODO: generate random hemisphere samples + noise texture
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
    // TODO: depth pass from light POV into m_shadowFBO
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

    // Debug blit: albedo attachment directly to screen to verify geometry
    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_gBuffer);
    glReadBuffer(GL_COLOR_ATTACHMENT2);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glBlitFramebuffer(0, 0, m_width, m_height,
                      0, 0, m_width, m_height,
                      GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::passSSAO(const Camera& cam) {
    // TODO: read gPosition + gNormal, generate AO into m_ssaoFBO
}

void Renderer::passLighting(Scene& scene, const Camera& cam) {
    // TODO: deferred Phong from GBuffer + shadow lookup
}

void Renderer::passDOF() {
    // TODO: post-process on HDR buffer
}

void Renderer::passMotionBlur(const Camera& cam) {
    // TODO: velocity-based blur using m_prevViewProj
}

void Renderer::passTonemap() {
    // TODO: HDR -> LDR final output
}

// ── Top-level render ──────────────────────────────────────────────────────────

void Renderer::render(Scene& scene, const Camera& camera, float deltaTime) {
    passGBuffer(scene, camera);

    // m_prevViewProj must be updated at end of frame, after motion blur pass
    glm::mat4 view = camera.GetViewMatrix();
    glm::mat4 proj = glm::perspective(glm::radians(camera.Zoom),
                                      (float)m_width / (float)m_height,
                                      0.1f, 100.0f);
    m_prevViewProj = proj * view;
}
