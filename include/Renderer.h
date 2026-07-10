#pragma once
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <vector>
#include "Shader.h"
#include "ComputeShader.h"
#include "Camera.h"
#include "Model.h"


// Forward declarations
class Scene;

struct RenderSettings {
    bool  shadows    = true;
    bool  softShadow = true;   // PCSS
    bool  ao         = true;   // SSAO
    bool  dof        = true;   // Depth of field
    bool  motionBlur = true;

    float exposure      = 1.0f;
    int   tonemapOp     = 0;    // 0 = ACES filmic, 1 = Reinhard

    bool  bloom           = false;
    float bloomThreshold  = 1.0f;
    float bloomKnee       = 0.1f;
    int   bloomIterations = 5;    // H+V cycles; more = wider glow
    float bloomIntensity  = 0.04f; // used in composite (CP4)

    // ── Color grade + vignette ──────────────────────────────────────────────
    bool      gradeEnable      = false;
    float     temperature      = 0.0f;
    glm::vec3 gradeTint        = glm::vec3(1.0f);
    float     saturation       = 1.0f;
    glm::vec3 shadowLift       = glm::vec3(0.0f);
    float     vignetteStrength = 0.0f;
    float     vignetteSoftness = 0.45f;

    // ── Planar floor reflection ──────────────────────────────────────────────
    bool  reflection   = false;
    float reflectivity = 0.25f;  // Fresnel scale
    float glossyBlur   = 0.0f;   // blur radius in texels (0 = sharp)
    
    //Raytracing settings
    int numSamples = 32;
    int lightSamples;

    //Denoising settings
    float sigmaColor = 0.2;
    float sigmaDepth = 5.0;
    float sigmaNormal = 0.25;
};

class Renderer {
public:
    RenderSettings settings;

    Renderer(int width, int height);
    ~Renderer();
    void LoadScene(Scene& scene);

    void saveRayTracingImage(const std::string& filename);
    void saveFinalImage(const std::string& filename);

    void renderRaytracing (Scene& scene, const Camera& camera, float deltaTime, float time);
    void renderRasterizer (Scene& scene, const Camera& camera, float deltaTime, float time);
    void resize (int w, int h);

private:
    int   m_width, m_height;
    float m_time;

    // ── Framebuffers ────────────────────────────────────────────────────────
    unsigned int m_gBuffer;           // G-buffer for deferred shading
    unsigned int m_gPosition;         // world-space position
    unsigned int m_gNormal;           // world-space normal
    unsigned int m_gAlbedo;           // albedo + specular
    unsigned int m_gEmissive  = 0;    // emissive RGB16F (attachment 4)
    unsigned int m_gDepth;

    unsigned int m_shadowFBO;         // shadow map pass
    unsigned int m_shadowMap;
    static constexpr int SHADOW_RES = 4096;

    unsigned int m_hdrFBO;            // HDR colour buffer
    unsigned int m_hdrColor;
    unsigned int m_hdrDepth;

    unsigned int m_bloomTex = 0;
    unsigned int m_bloomFBO = 0;

    unsigned int m_pingpongFBO[2]   = {0, 0};  // bloom blur ping-pong (initialised in bloom checkpoint)
    unsigned int m_pingpongColor[2] = {0, 0};

    unsigned int m_reflFBO   = 0;   // planar floor reflection (half-res, RGBA16F + depth)
    unsigned int m_reflColor = 0;
    unsigned int m_reflDepth = 0;   // depth renderbuffer

    unsigned int m_ssaoFBO        = 0;
    unsigned int m_ssaoColor      = 0;
    unsigned int m_ssaoBlurFBO    = 0;
    unsigned int m_ssaoBlurColor  = 0;
    unsigned int m_ssaoNoise      = 0;
    std::vector<glm::vec3> m_ssaoKernel;

    unsigned int m_denoisedFBO = 0;

    unsigned int m_screenTex;
    unsigned int m_reflPos;
    unsigned int m_reflNorm;
    unsigned int m_denoisedTex = 0;
    
    unsigned int m_textureTex;

    unsigned int m_finalTex = 0;

    unsigned int m_vertexSSBO;
    unsigned int m_indexSSBO;
    unsigned int m_meshSSBO;
    unsigned int m_lightTriangleSSBO;

    unsigned int m_bvhNodeSSBO = 0;
    unsigned int m_bvhLeafCountSSBO = 0;
    unsigned int m_bvhTrisSSBO = 0;
    unsigned int m_triMeshSSBO = 0;
    std::vector<uint32_t> m_triMesh;

    float m_totalEmissiveArea = 0.0;

    std::vector<Vertex> m_vertices;
    std::vector<uint32_t> m_indices;
    std::vector<MeshInfo> m_meshes;
    std::vector<LightTriangle> m_lightTriangles;

    // ── Shaders ─────────────────────────────────────────────────────────────
    Shader m_gBufferShader;
    Shader m_shadowShader;
    Shader m_lightingShader;
    Shader m_ssaoShader;
    Shader m_ssaoBlurShader;
    Shader m_brightPassShader;
    Shader m_bloomBlurShader;
    Shader m_bloomCompositeShader;
    Shader m_reflectionShader;
    Shader m_glassShader;
    Shader m_dofShader;
    Shader m_motionBlurShader;
    Shader m_tonemapShader;
    Shader m_ScreenSampler2D;
    Shader m_denoiserShader;

    ComputeShader m_rayTracerShader;

    // ── Screen quad ─────────────────────────────────────────────────────────
    unsigned int m_quadVAO = 0;
    unsigned int m_quadVBO = 0;

    // ── Previous frame data (motion blur) ───────────────────────────────────
    glm::mat4 m_prevViewProj     = glm::mat4(1.0f);

    // ── Shadow pass shared state ─────────────────────────────────────────────
    glm::mat4 m_lightSpaceMatrix = glm::mat4(1.0f);

    // ── Reflection pass shared state ─────────────────────────────────────────
    glm::mat4 m_reflectionProjView = glm::mat4(1.0f);

    void initFramebuffers();
    void initRayTracingSSBOs(Scene& scene);
    void initShaders();
    void initSSAOKernel();
    void renderQuad();

    void passShadow     (Scene& scene, const Camera& cam);
    void passReflection (Scene& scene, const Camera& cam);
    void passGBuffer (Scene& scene, const Camera& cam);
    void passSSAO      (const Camera& cam);
    void passSSAOBlur  ();
    void passLighting  (Scene& scene, const Camera& cam);
    void passBrightPass(GLuint inputTex);
    void passBloomComposite(GLuint inputTex, GLuint outputFBO);
    void passBloomBlur();
    void applyBloom(GLuint inputTex, GLuint outputFBO);
    void passGlass     (Scene& scene, const Camera& cam);
    void passDOF       ();
    void passMotionBlur(const Camera& cam);
    void passTonemap();
    void passDenoiser();
    void applyDenoiserIterations(int iterations);

    void UploadScene(const Scene& scene);
    void buildBVH(std::vector<TriangleRef>& tris, uint32_t start, uint32_t end, std::vector<BVHNode>& nodes,
    std::vector<uint32_t>& leafCounts, std::vector<uint32_t>& bvhTris, const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices, uint32_t& nextNodeId);
    void buildAndUploadBVH();
    void UploadSceneToGPU();
    void passRayTracing (const Camera& cam);
};
