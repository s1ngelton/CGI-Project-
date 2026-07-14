#include "GPURayTracer.h"
#include "Camera.h"

#include <fstream>
#include <sstream>
#include <iostream>
#include <cmath>
#include <chrono>
#include <string>
#include <algorithm>

GPURayTracer::GPURayTracer() {
    m_available = (GLAD_GL_VERSION_4_3 != 0);
    if (!m_available) {
        std::cerr << "[GPURayTracer] GL context < 4.3 — compute shaders unavailable, "
                     "GPU path disabled (CPU RayTracer remains the working path).\n";
        return;
    }
    m_progNormals = compileComputeProgram("shaders/rt_cp1_normals.comp");
    if (m_progNormals == 0) {
        std::cerr << "[GPURayTracer] CP1 compute shader failed to compile — GPU path disabled.\n";
        m_available = false;
        return;
    }
    m_progShaded = compileComputeProgram("shaders/rt_cp2_shaded.comp");
    if (m_progShaded == 0) {
        std::cerr << "[GPURayTracer] CP2 compute shader failed to compile — GPU path disabled.\n";
        m_available = false;
        return;
    }
    m_progGlass = compileComputeProgram("shaders/rt_cp4_glass.comp");
    if (m_progGlass == 0) {
        std::cerr << "[GPURayTracer] CP4 compute shader failed to compile — GPU path disabled.\n";
        m_available = false;
        return;
    }
    m_progPreview = compileComputeProgram("shaders/rt_cp5_gpu_beauty.comp");
    if (m_progPreview == 0) {
        std::cerr << "[GPURayTracer] CP5 compute shader failed to compile — GPU path disabled.\n";
        m_available = false;
        return;
    }
    m_progDebugDiag = compileComputeProgram("shaders/rt_debug_diag.comp");
    if (m_progDebugDiag == 0) {
        std::cerr << "[GPURayTracer] debug-diag compute shader failed to compile — GPU path disabled.\n";
        m_available = false;
        return;
    }
    glGenBuffers(1, &m_ssboDebugOut);
    glGenBuffers(1, &m_ssboTrace);
    {
        std::vector<float> zeros(1 + 42 * 6, 0.0f);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_ssboTrace);
        glBufferData(GL_SHADER_STORAGE_BUFFER, (GLsizeiptr)(zeros.size() * sizeof(float)),
                    zeros.data(), GL_DYNAMIC_COPY);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    }

    glGenBuffers(1, &m_ssboNodes);
    glGenBuffers(1, &m_ssboIndices);
    glGenBuffers(1, &m_ssboVerts);
    glGenBuffers(1, &m_ssboNormals);

    glGenBuffers(1, &m_ssboMaterials);
    glGenBuffers(1, &m_ssboTriMat);
    glGenBuffers(1, &m_ssboTriFlags);
    glGenBuffers(1, &m_ssboTriUV);
    glGenBuffers(1, &m_ssboShadowNodes);
    glGenBuffers(1, &m_ssboShadowIndices);
    glGenBuffers(1, &m_ssboShadowVerts);
}

GPURayTracer::~GPURayTracer() {
    if (m_progNormals) glDeleteProgram(m_progNormals);
    if (m_progShaded)  glDeleteProgram(m_progShaded);
    if (m_progGlass)   glDeleteProgram(m_progGlass);
    if (m_progPreview) glDeleteProgram(m_progPreview);
    if (m_progDebugDiag) glDeleteProgram(m_progDebugDiag);
    if (m_ssboDebugOut)  glDeleteBuffers(1, &m_ssboDebugOut);
    if (m_ssboTrace)     glDeleteBuffers(1, &m_ssboTrace);
    if (m_ssboNodes)   glDeleteBuffers(1, &m_ssboNodes);
    if (m_ssboIndices) glDeleteBuffers(1, &m_ssboIndices);
    if (m_ssboVerts)   glDeleteBuffers(1, &m_ssboVerts);
    if (m_ssboNormals) glDeleteBuffers(1, &m_ssboNormals);
    if (m_ssboMaterials)     glDeleteBuffers(1, &m_ssboMaterials);
    if (m_ssboTriMat)        glDeleteBuffers(1, &m_ssboTriMat);
    if (m_ssboTriFlags)      glDeleteBuffers(1, &m_ssboTriFlags);
    if (m_ssboTriUV)         glDeleteBuffers(1, &m_ssboTriUV);
    if (m_ssboShadowNodes)   glDeleteBuffers(1, &m_ssboShadowNodes);
    if (m_ssboShadowIndices) glDeleteBuffers(1, &m_ssboShadowIndices);
    if (m_ssboShadowVerts)   glDeleteBuffers(1, &m_ssboShadowVerts);
    if (m_albedoTex)   glDeleteTextures(1, &m_albedoTex);
    if (m_outTex)      glDeleteTextures(1, &m_outTex);
    if (m_hdrColorTex)  glDeleteTextures(1, &m_hdrColorTex);
    if (m_hdrAlbedoTex) glDeleteTextures(1, &m_hdrAlbedoTex);
    if (m_hdrNormalTex) glDeleteTextures(1, &m_hdrNormalTex);
}

GLuint GPURayTracer::compileComputeProgram(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        std::cerr << "[GPURayTracer] Shader file not found: " << path << "\n";
        return 0;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    std::string src = ss.str();
    const char* csrc = src.c_str();

    GLuint sh = glCreateShader(GL_COMPUTE_SHADER);
    glShaderSource(sh, 1, &csrc, nullptr);
    glCompileShader(sh);
    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetShaderInfoLog(sh, sizeof(log), nullptr, log);
        std::cerr << "[GPURayTracer] Compute shader compile error (" << path << "):\n" << log << "\n";
        glDeleteShader(sh);
        return 0;
    }

    GLuint prog = glCreateProgram();
    glAttachShader(prog, sh);
    glLinkProgram(prog);
    glDeleteShader(sh);

    GLint linked = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &linked);
    if (!linked) {
        char log[4096];
        glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
        std::cerr << "[GPURayTracer] Compute program link error (" << path << "):\n" << log << "\n";
        glDeleteProgram(prog);
        return 0;
    }
    return prog;
}

void GPURayTracer::upload(const RayTracer::GPUBVHExport& data) {
    if (!m_available) return;

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_ssboNodes);
    glBufferData(GL_SHADER_STORAGE_BUFFER,
                (GLsizeiptr)(data.nodeCount * data.nodeStrideBytes),
                data.nodeBytes, GL_STATIC_DRAW);

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_ssboIndices);
    glBufferData(GL_SHADER_STORAGE_BUFFER,
                (GLsizeiptr)(data.indexCount * sizeof(unsigned int)),
                data.indices, GL_STATIC_DRAW);

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_ssboVerts);
    glBufferData(GL_SHADER_STORAGE_BUFFER,
                (GLsizeiptr)(data.vertexFloatCount * sizeof(float)),
                data.vertices, GL_STATIC_DRAW);

    // Build a tightly-packed 9-floats/tri normals buffer from RTTriangle —
    // not zero-copy (RTTriangle interleaves mesh*/uv/isGlass too), but a cheap
    // one-time conversion done once per buildScene().
    std::vector<float> norms;
    norms.reserve(data.triCount * 9);
    for (size_t i = 0; i < data.triCount; ++i) {
        const RTTriangle& t = data.tris[i];
        norms.push_back(t.n0.x); norms.push_back(t.n0.y); norms.push_back(t.n0.z);
        norms.push_back(t.n1.x); norms.push_back(t.n1.y); norms.push_back(t.n1.z);
        norms.push_back(t.n2.x); norms.push_back(t.n2.y); norms.push_back(t.n2.z);
    }
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_ssboNormals);
    glBufferData(GL_SHADER_STORAGE_BUFFER,
                (GLsizeiptr)(norms.size() * sizeof(float)),
                norms.data(), GL_STATIC_DRAW);

    // ── CP2: materials, per-tri lookups, shadow BVH, albedo texture ──────────
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_ssboMaterials);
    glBufferData(GL_SHADER_STORAGE_BUFFER,
                (GLsizeiptr)(data.materialCount * sizeof(RayTracer::GPUMaterial)),
                data.materials, GL_STATIC_DRAW);

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_ssboTriMat);
    glBufferData(GL_SHADER_STORAGE_BUFFER,
                (GLsizeiptr)(data.triCount * sizeof(unsigned int)),
                data.triMaterialID, GL_STATIC_DRAW);

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_ssboTriFlags);
    glBufferData(GL_SHADER_STORAGE_BUFFER,
                (GLsizeiptr)(data.triCount * sizeof(unsigned int)),
                data.triFlags, GL_STATIC_DRAW);

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_ssboTriUV);
    glBufferData(GL_SHADER_STORAGE_BUFFER,
                (GLsizeiptr)(data.triCount * 6 * sizeof(float)),
                data.triUVs, GL_STATIC_DRAW);

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_ssboShadowNodes);
    glBufferData(GL_SHADER_STORAGE_BUFFER,
                (GLsizeiptr)(data.shadowNodeCount * data.shadowNodeStrideBytes),
                data.shadowNodeBytes, GL_STATIC_DRAW);

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_ssboShadowIndices);
    glBufferData(GL_SHADER_STORAGE_BUFFER,
                (GLsizeiptr)(data.shadowIndexCount * sizeof(unsigned int)),
                data.shadowIndices, GL_STATIC_DRAW);

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_ssboShadowVerts);
    glBufferData(GL_SHADER_STORAGE_BUFFER,
                (GLsizeiptr)(data.shadowVertexFloatCount * sizeof(float)),
                data.shadowVertices, GL_STATIC_DRAW);

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    m_hasAlbedoTex = (data.albedoPixelsRGBA != nullptr && data.albedoW > 0 && data.albedoH > 0);
    if (m_hasAlbedoTex) {
        if (m_albedoTex) glDeleteTextures(1, &m_albedoTex);
        glGenTextures(1, &m_albedoTex);
        glBindTexture(GL_TEXTURE_2D, m_albedoTex);
        glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA32F, data.albedoW, data.albedoH);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, data.albedoW, data.albedoH,
                        GL_RGBA, GL_FLOAT, data.albedoPixelsRGBA);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    m_uploaded = true;
}

void GPURayTracer::ensureOutputTexture(int w, int h) {
    if (m_outTex != 0 && m_texW == w && m_texH == h) return;
    if (m_outTex) glDeleteTextures(1, &m_outTex);
    glGenTextures(1, &m_outTex);
    glBindTexture(GL_TEXTURE_2D, m_outTex);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, w, h);
    glBindTexture(GL_TEXTURE_2D, 0);
    m_texW = w; m_texH = h;
}

void GPURayTracer::ensureHDROutputTextures(int w, int h) {
    if (m_hdrColorTex != 0 && m_hdrW == w && m_hdrH == h) return;
    if (m_hdrColorTex)  glDeleteTextures(1, &m_hdrColorTex);
    if (m_hdrAlbedoTex) glDeleteTextures(1, &m_hdrAlbedoTex);
    if (m_hdrNormalTex) glDeleteTextures(1, &m_hdrNormalTex);

    auto makeF32 = [&](GLuint& tex) {
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA32F, w, h);
        glBindTexture(GL_TEXTURE_2D, 0);
    };
    makeF32(m_hdrColorTex);
    makeF32(m_hdrAlbedoTex);
    makeF32(m_hdrNormalTex);
    m_hdrW = w; m_hdrH = h;
}

bool GPURayTracer::renderNormalsGPU(const Camera& cam, int w, int h,
                                    std::vector<uint8_t>& outPixels,
                                    double* outSeconds) {
    if (!m_available || !m_uploaded) {
        std::cerr << "[GPURayTracer] renderNormalsGPU: not available/uploaded.\n";
        return false;
    }

    ensureOutputTexture(w, h);

    auto t0 = std::chrono::steady_clock::now();

    glUseProgram(m_progNormals);

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_ssboNodes);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, m_ssboIndices);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, m_ssboVerts);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, m_ssboNormals);
    glBindImageTexture(4, m_outTex, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);

    float aspect     = (float)w / (float)h;
    float tanHalfFov = std::tan(cam.Zoom * 0.5f * (float)M_PI / 180.0f);

    glUniform3fv(glGetUniformLocation(m_progNormals, "uCamPos"),   1, &cam.Position.x);
    glUniform3fv(glGetUniformLocation(m_progNormals, "uCamFront"), 1, &cam.Front.x);
    glUniform3fv(glGetUniformLocation(m_progNormals, "uCamRight"), 1, &cam.Right.x);
    glUniform3fv(glGetUniformLocation(m_progNormals, "uCamUp"),    1, &cam.Up.x);
    glUniform1f(glGetUniformLocation(m_progNormals, "uAspect"),     aspect);
    glUniform1f(glGetUniformLocation(m_progNormals, "uTanHalfFov"), tanHalfFov);
    glUniform1i(glGetUniformLocation(m_progNormals, "uWidth"),      w);
    glUniform1i(glGetUniformLocation(m_progNormals, "uHeight"),     h);

    GLuint groupsX = (GLuint)((w + 7) / 8);
    GLuint groupsY = (GLuint)((h + 7) / 8);
    glDispatchCompute(groupsX, groupsY, 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
    glFinish(); // include full GPU completion in the timing, matching CPU's synchronous renderNormals

    auto t1 = std::chrono::steady_clock::now();
    if (outSeconds) *outSeconds = std::chrono::duration<double>(t1 - t0).count();

    outPixels.resize((size_t)w * h * 3);
    std::vector<uint8_t> rgba((size_t)w * h * 4);
    glBindTexture(GL_TEXTURE_2D, m_outTex);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    glBindTexture(GL_TEXTURE_2D, 0);

    for (size_t i = 0; i < (size_t)w * h; ++i) {
        outPixels[i*3+0] = rgba[i*4+0];
        outPixels[i*3+1] = rgba[i*4+1];
        outPixels[i*3+2] = rgba[i*4+2];
    }

    return true;
}

// Shared by renderShadedGPU (CP2) and renderGlassGPU (CP4) — both programs use
// identical buffer bindings and uniform names, differing only in shader logic.
bool GPURayTracer::dispatchShading(GLuint prog, const char* callerName,
                                   const Camera& cam, int w, int h, float exposure,
                                   const std::vector<RTLight>& lights, float lightRadius,
                                   std::vector<uint8_t>& outPixels,
                                   double* outSeconds) {
    if (!m_available || !m_uploaded) {
        std::cerr << "[GPURayTracer] " << callerName << ": not available/uploaded.\n";
        return false;
    }

    ensureOutputTexture(w, h);

    auto t0 = std::chrono::steady_clock::now();

    glUseProgram(prog);

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_ssboNodes);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, m_ssboIndices);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, m_ssboVerts);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, m_ssboNormals);
    glBindImageTexture(4, m_outTex, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, m_ssboMaterials);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 6, m_ssboTriMat);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 7, m_ssboTriFlags);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 8, m_ssboTriUV);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 9,  m_ssboShadowNodes);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 10, m_ssboShadowIndices);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 11, m_ssboShadowVerts);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 12, m_ssboTrace);
    // -1,-1 never matches a real pixel, so tracing is off unless traceGlassGPU
    // explicitly overrides these after calling dispatchShading's setup.
    glUniform1i(glGetUniformLocation(prog, "uTracePx"), -1);
    glUniform1i(glGetUniformLocation(prog, "uTracePy"), -1);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_hasAlbedoTex ? m_albedoTex : 0);
    glUniform1i(glGetUniformLocation(prog, "uAlbedoTex"), 0);

    float aspect     = (float)w / (float)h;
    float tanHalfFov = std::tan(cam.Zoom * 0.5f * (float)M_PI / 180.0f);

    glUniform3fv(glGetUniformLocation(prog, "uCamPos"),   1, &cam.Position.x);
    glUniform3fv(glGetUniformLocation(prog, "uCamFront"), 1, &cam.Front.x);
    glUniform3fv(glGetUniformLocation(prog, "uCamRight"), 1, &cam.Right.x);
    glUniform3fv(glGetUniformLocation(prog, "uCamUp"),    1, &cam.Up.x);
    glUniform1f(glGetUniformLocation(prog, "uAspect"),     aspect);
    glUniform1f(glGetUniformLocation(prog, "uTanHalfFov"), tanHalfFov);
    glUniform1i(glGetUniformLocation(prog, "uWidth"),      w);
    glUniform1i(glGetUniformLocation(prog, "uHeight"),     h);
    glUniform1f(glGetUniformLocation(prog, "uExposure"),   exposure);

    int numLights = std::min((int)lights.size(), 4);
    glUniform1i(glGetUniformLocation(prog, "uNumLights"), numLights);
    glUniform1f(glGetUniformLocation(prog, "uLightRadius"), lightRadius);
    for (int i = 0; i < numLights; ++i) {
        std::string base = "uLightPos[" + std::to_string(i) + "]";
        glUniform3fv(glGetUniformLocation(prog, base.c_str()), 1, &lights[i].position.x);
        base = "uLightColor[" + std::to_string(i) + "]";
        glUniform3fv(glGetUniformLocation(prog, base.c_str()), 1, &lights[i].color.x);
        base = "uLightIntensity[" + std::to_string(i) + "]";
        glUniform1f(glGetUniformLocation(prog, base.c_str()), lights[i].intensity);
    }

    GLuint groupsX = (GLuint)((w + 7) / 8);
    GLuint groupsY = (GLuint)((h + 7) / 8);
    glDispatchCompute(groupsX, groupsY, 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
    glFinish();

    auto t1 = std::chrono::steady_clock::now();
    if (outSeconds) *outSeconds = std::chrono::duration<double>(t1 - t0).count();

    outPixels.resize((size_t)w * h * 3);
    std::vector<uint8_t> rgba((size_t)w * h * 4);
    glBindTexture(GL_TEXTURE_2D, m_outTex);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    glBindTexture(GL_TEXTURE_2D, 0);

    for (size_t i = 0; i < (size_t)w * h; ++i) {
        outPixels[i*3+0] = rgba[i*4+0];
        outPixels[i*3+1] = rgba[i*4+1];
        outPixels[i*3+2] = rgba[i*4+2];
    }

    return true;
}

bool GPURayTracer::renderShadedGPU(const Camera& cam, int w, int h, float exposure,
                                   const std::vector<RTLight>& lights, float lightRadius,
                                   std::vector<uint8_t>& outPixels,
                                   double* outSeconds) {
    return dispatchShading(m_progShaded, "renderShadedGPU", cam, w, h, exposure,
                           lights, lightRadius, outPixels, outSeconds);
}

bool GPURayTracer::renderGlassGPU(const Camera& cam, int w, int h, float exposure,
                                  const std::vector<RTLight>& lights, float lightRadius,
                                  std::vector<uint8_t>& outPixels,
                                  double* outSeconds) {
    return dispatchShading(m_progGlass, "renderGlassGPU", cam, w, h, exposure,
                           lights, lightRadius, outPixels, outSeconds);
}

bool GPURayTracer::renderPreviewGPU(const Camera& cam, int w, int h,
                                    const std::vector<RTLight>& lights, float lightRadius,
                                    std::vector<glm::vec3>& outColor,
                                    std::vector<glm::vec3>& outAlbedo,
                                    std::vector<glm::vec3>& outNormal,
                                    double* outSeconds) {
    if (!m_available || !m_uploaded) {
        std::cerr << "[GPURayTracer] renderPreviewGPU: not available/uploaded.\n";
        return false;
    }

    ensureHDROutputTextures(w, h);

    auto t0 = std::chrono::steady_clock::now();

    glUseProgram(m_progPreview);

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_ssboNodes);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, m_ssboIndices);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, m_ssboVerts);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, m_ssboNormals);
    glBindImageTexture(4, m_hdrColorTex,  0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
    glBindImageTexture(5, m_hdrAlbedoTex, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
    glBindImageTexture(6, m_hdrNormalTex, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, m_ssboMaterials);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 6, m_ssboTriMat);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 7, m_ssboTriFlags);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 8, m_ssboTriUV);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 9,  m_ssboShadowNodes);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 10, m_ssboShadowIndices);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 11, m_ssboShadowVerts);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_hasAlbedoTex ? m_albedoTex : 0);
    glUniform1i(glGetUniformLocation(m_progPreview, "uAlbedoTex"), 0);

    float aspect     = (float)w / (float)h;
    float tanHalfFov = std::tan(cam.Zoom * 0.5f * (float)M_PI / 180.0f);

    glUniform3fv(glGetUniformLocation(m_progPreview, "uCamPos"),   1, &cam.Position.x);
    glUniform3fv(glGetUniformLocation(m_progPreview, "uCamFront"), 1, &cam.Front.x);
    glUniform3fv(glGetUniformLocation(m_progPreview, "uCamRight"), 1, &cam.Right.x);
    glUniform3fv(glGetUniformLocation(m_progPreview, "uCamUp"),    1, &cam.Up.x);
    glUniform1f(glGetUniformLocation(m_progPreview, "uAspect"),     aspect);
    glUniform1f(glGetUniformLocation(m_progPreview, "uTanHalfFov"), tanHalfFov);
    glUniform1i(glGetUniformLocation(m_progPreview, "uWidth"),      w);
    glUniform1i(glGetUniformLocation(m_progPreview, "uHeight"),     h);

    int numLights = std::min((int)lights.size(), 4);
    glUniform1i(glGetUniformLocation(m_progPreview, "uNumLights"), numLights);
    glUniform1f(glGetUniformLocation(m_progPreview, "uLightRadius"), lightRadius);
    for (int i = 0; i < numLights; ++i) {
        std::string base = "uLightPos[" + std::to_string(i) + "]";
        glUniform3fv(glGetUniformLocation(m_progPreview, base.c_str()), 1, &lights[i].position.x);
        base = "uLightColor[" + std::to_string(i) + "]";
        glUniform3fv(glGetUniformLocation(m_progPreview, base.c_str()), 1, &lights[i].color.x);
        base = "uLightIntensity[" + std::to_string(i) + "]";
        glUniform1f(glGetUniformLocation(m_progPreview, base.c_str()), lights[i].intensity);
    }

    GLuint groupsX = (GLuint)((w + 7) / 8);
    GLuint groupsY = (GLuint)((h + 7) / 8);
    glDispatchCompute(groupsX, groupsY, 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
    glFinish();

    auto t1 = std::chrono::steady_clock::now();
    if (outSeconds) *outSeconds = std::chrono::duration<double>(t1 - t0).count();

    auto readF32 = [&](GLuint tex, std::vector<glm::vec3>& out) {
        std::vector<float> rgba((size_t)w * h * 4);
        glBindTexture(GL_TEXTURE_2D, tex);
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, rgba.data());
        glBindTexture(GL_TEXTURE_2D, 0);
        out.resize((size_t)w * h);
        for (size_t i = 0; i < (size_t)w * h; ++i)
            out[i] = glm::vec3(rgba[i*4+0], rgba[i*4+1], rgba[i*4+2]);
    };
    readF32(m_hdrColorTex,  outColor);
    readF32(m_hdrAlbedoTex, outAlbedo);
    readF32(m_hdrNormalTex, outNormal);

    return true;
}

RayTracer::RayDiagnostic GPURayTracer::debugCaptureGPU(const Camera& cam, int w, int h,
                                                        int targetPx, int targetPy,
                                                        const std::vector<RTLight>& lights,
                                                        int* outMaxSp, int* outTraverseCalls) {
    RayTracer::RayDiagnostic diag;
    if (!m_available || !m_uploaded) return diag;

    glUseProgram(m_progDebugDiag);

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_ssboNodes);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, m_ssboIndices);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, m_ssboVerts);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, m_ssboNormals);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 7, m_ssboTriFlags);

    std::vector<float> zeros(21, -999.0f);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_ssboDebugOut);
    glBufferData(GL_SHADER_STORAGE_BUFFER, (GLsizeiptr)(zeros.size() * sizeof(float)),
                zeros.data(), GL_DYNAMIC_COPY);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 12, m_ssboDebugOut);

    float aspect     = (float)w / (float)h;
    float tanHalfFov = std::tan(cam.Zoom * 0.5f * (float)M_PI / 180.0f);

    glUniform3fv(glGetUniformLocation(m_progDebugDiag, "uCamPos"),   1, &cam.Position.x);
    glUniform3fv(glGetUniformLocation(m_progDebugDiag, "uCamFront"), 1, &cam.Front.x);
    glUniform3fv(glGetUniformLocation(m_progDebugDiag, "uCamRight"), 1, &cam.Right.x);
    glUniform3fv(glGetUniformLocation(m_progDebugDiag, "uCamUp"),    1, &cam.Up.x);
    glUniform1f(glGetUniformLocation(m_progDebugDiag, "uAspect"),     aspect);
    glUniform1f(glGetUniformLocation(m_progDebugDiag, "uTanHalfFov"), tanHalfFov);
    glUniform1i(glGetUniformLocation(m_progDebugDiag, "uWidth"),      w);
    glUniform1i(glGetUniformLocation(m_progDebugDiag, "uHeight"),     h);
    glUniform1i(glGetUniformLocation(m_progDebugDiag, "uTargetPx"),   targetPx);
    glUniform1i(glGetUniformLocation(m_progDebugDiag, "uTargetPy"),   targetPy);

    int numLights = std::min((int)lights.size(), 4);
    glUniform1i(glGetUniformLocation(m_progDebugDiag, "uNumLights"), numLights);
    for (int i = 0; i < numLights; ++i) {
        std::string base = "uLightPos[" + std::to_string(i) + "]";
        glUniform3fv(glGetUniformLocation(m_progDebugDiag, base.c_str()), 1, &lights[i].position.x);
    }

    GLuint groupsX = (GLuint)((w + 7) / 8);
    GLuint groupsY = (GLuint)((h + 7) / 8);
    glDispatchCompute(groupsX, groupsY, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    glFinish();

    std::vector<float> result(21);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_ssboDebugOut);
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, (GLsizeiptr)(21 * sizeof(float)), result.data());
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    diag.hit          = result[0] > 0.5f;
    diag.isBackground = result[1] > 0.5f;
    diag.prim         = (unsigned int)(result[2] + 0.5f);
    diag.hitPos        = glm::vec3(result[3], result[4], result[5]);
    diag.baryU = result[6]; diag.baryV = result[7];
    diag.N = glm::vec3(result[8], result[9], result[10]);
    diag.V = glm::vec3(result[11], result[12], result[13]);
    diag.numLights = (int)(result[14] + 0.5f);
    for (int i = 0; i < 4; ++i) diag.NdotL[i] = result[15 + i];
    if (outMaxSp)         *outMaxSp         = (int)(result[19] + 0.5f);
    if (outTraverseCalls) *outTraverseCalls = (int)(result[20] + 0.5f);

    return diag;
}

// NON-FUNCTIONAL — failed its own verification, left in place with this note
// rather than silently deleted. rt_cp4_glass.comp was reverted to its clean
// (working, non-instrumented) state, so uTracePx/uTracePy/TraceBuf no longer
// exist there and this call will just read back zeros. Investigation record:
// instrumenting rt_cp4_glass.comp directly (adding uTracePx/uTracePy uniforms
// + a TraceBuf SSBO + push/pop logging) produced trace[0]==0 for every target
// pixel, always "written" by invocation (0,0) instead of the requested pixel —
// verified via glGetUniformiv that the HOST-side uniform value WAS correctly
// set (e.g. 1500/400), yet the shader behaved as if it read 0. Ruled out:
// stale cached bool (recomputed the comparison fresh via macro, no change),
// GL errors (none reported), buffer binding number (tried 12 and 13, no
// change), unsized-vs-fixed array declaration (no change). A dramatically
// simplified version of the same shader (early-return before the main loop)
// DID correctly see the uniform. This points at some resource/register-
// pressure-related compiler behavior specific to this large, branch-heavy
// shader (12 SSBOs, nested MAX_STACK=160 arrays, GLASS_STACK=16 struct array)
// rather than a logic bug in the instrumentation code — but that's inference,
// not confirmed. Left for a future attempt; see conversation for full trail.
std::vector<float> GPURayTracer::traceGlassGPU(const Camera& cam, int w, int h,
                                               int targetPx, int targetPy,
                                               const std::vector<RTLight>& lights, float lightRadius) {
    if (!m_available || !m_uploaded) return {};

    ensureOutputTexture(w, h);

    // Clear the trace buffer's entry count before this dispatch.
    {
        float zero = 0.0f;
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_ssboTrace);
        glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(float), &zero);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    }

    glUseProgram(m_progGlass);

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_ssboNodes);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, m_ssboIndices);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, m_ssboVerts);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, m_ssboNormals);
    glBindImageTexture(4, m_outTex, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, m_ssboMaterials);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 6, m_ssboTriMat);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 7, m_ssboTriFlags);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 8, m_ssboTriUV);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 9,  m_ssboShadowNodes);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 10, m_ssboShadowIndices);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 11, m_ssboShadowVerts);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 12, m_ssboTrace);
    GLint locPx = glGetUniformLocation(m_progGlass, "uTracePx");
    GLint locPy = glGetUniformLocation(m_progGlass, "uTracePy");
    glUniform1i(locPx, targetPx);
    glUniform1i(locPy, targetPy);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_hasAlbedoTex ? m_albedoTex : 0);
    glUniform1i(glGetUniformLocation(m_progGlass, "uAlbedoTex"), 0);

    float aspect     = (float)w / (float)h;
    float tanHalfFov = std::tan(cam.Zoom * 0.5f * (float)M_PI / 180.0f);
    glUniform3fv(glGetUniformLocation(m_progGlass, "uCamPos"),   1, &cam.Position.x);
    glUniform3fv(glGetUniformLocation(m_progGlass, "uCamFront"), 1, &cam.Front.x);
    glUniform3fv(glGetUniformLocation(m_progGlass, "uCamRight"), 1, &cam.Right.x);
    glUniform3fv(glGetUniformLocation(m_progGlass, "uCamUp"),    1, &cam.Up.x);
    glUniform1f(glGetUniformLocation(m_progGlass, "uAspect"),     aspect);
    glUniform1f(glGetUniformLocation(m_progGlass, "uTanHalfFov"), tanHalfFov);
    glUniform1i(glGetUniformLocation(m_progGlass, "uWidth"),      w);
    glUniform1i(glGetUniformLocation(m_progGlass, "uHeight"),     h);
    glUniform1f(glGetUniformLocation(m_progGlass, "uExposure"),   1.0f);

    int numLights = std::min((int)lights.size(), 4);
    glUniform1i(glGetUniformLocation(m_progGlass, "uNumLights"), numLights);
    glUniform1f(glGetUniformLocation(m_progGlass, "uLightRadius"), lightRadius);
    for (int i = 0; i < numLights; ++i) {
        std::string base = "uLightPos[" + std::to_string(i) + "]";
        glUniform3fv(glGetUniformLocation(m_progGlass, base.c_str()), 1, &lights[i].position.x);
        base = "uLightColor[" + std::to_string(i) + "]";
        glUniform3fv(glGetUniformLocation(m_progGlass, base.c_str()), 1, &lights[i].color.x);
        base = "uLightIntensity[" + std::to_string(i) + "]";
        glUniform1f(glGetUniformLocation(m_progGlass, base.c_str()), lights[i].intensity);
    }

    GLuint groupsX = (GLuint)((w + 7) / 8);
    GLuint groupsY = (GLuint)((h + 7) / 8);
    glDispatchCompute(groupsX, groupsY, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
    glFinish();

    std::vector<float> full(1 + 42 * 6);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_ssboTrace);
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, (GLsizeiptr)(full.size() * sizeof(float)), full.data());
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    int count = (int)(full[0] + 0.5f);
    count = std::max(0, std::min(count, 42));
    std::vector<float> out(1 + count * 6);
    out[0] = (float)count;
    for (int i = 0; i < count * 6; ++i) out[1 + i] = full[1 + i];
    return out;
}
