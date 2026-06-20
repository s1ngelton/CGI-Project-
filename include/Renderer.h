#pragma once
#include <glad/glad.h>
#include <glm/glm.hpp>
#include "Shader.h"
#include "Camera.h"

// Forward declarations
class Scene;

struct RenderSettings {
    bool shadows    = true;
    bool softShadow = true;   // PCSS
    bool ao         = true;   // SSAO
    bool dof        = true;   // Depth of field
    bool motionBlur = true;
};

class Renderer {
public:
    RenderSettings settings;

    Renderer(int width, int height);
    ~Renderer();

    void render(Scene& scene, const Camera& camera, float deltaTime);

private:
    int   m_width, m_height;

    // ── Framebuffers ────────────────────────────────────────────────────────
    unsigned int m_gBuffer;           // G-buffer for deferred shading
    unsigned int m_gPosition;         // world-space position
    unsigned int m_gNormal;           // world-space normal
    unsigned int m_gAlbedo;           // albedo + specular
    unsigned int m_gDepth;

    unsigned int m_shadowFBO;         // shadow map pass
    unsigned int m_shadowMap;
    static constexpr int SHADOW_RES = 4096;

    unsigned int m_hdrFBO;            // HDR colour buffer
    unsigned int m_hdrColor;
    unsigned int m_hdrDepth;

    unsigned int m_pingpongFBO[2];    // for multi-pass post-process
    unsigned int m_pingpongColor[2];

    unsigned int m_ssaoFBO;
    unsigned int m_ssaoColor;
    unsigned int m_ssaoBlurFBO;
    unsigned int m_ssaoBlurColor;

    // ── Shaders ─────────────────────────────────────────────────────────────
    Shader m_gBufferShader;
    Shader m_shadowShader;
    Shader m_lightingShader;
    Shader m_ssaoShader;
    Shader m_ssaoBlurShader;
    Shader m_dofShader;
    Shader m_motionBlurShader;
    Shader m_tonemapShader;

    // ── Screen quad ─────────────────────────────────────────────────────────
    unsigned int m_quadVAO = 0;
    unsigned int m_quadVBO = 0;

    // ── Previous frame data (motion blur) ───────────────────────────────────
    glm::mat4 m_prevViewProj = glm::mat4(1.0f);

    void initFramebuffers();
    void initShaders();
    void initSSAOKernel();
    void renderQuad();

    void passShadow  (Scene& scene, const Camera& cam);
    void passGBuffer (Scene& scene, const Camera& cam);
    void passSSAO    (const Camera& cam);
    void passLighting(Scene& scene, const Camera& cam);
    void passDOF     ();
    void passMotionBlur(const Camera& cam);
    void passTonemap ();
};
