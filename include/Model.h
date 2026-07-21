#pragma once
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <cmath>
#include "Shader.h"

// CPU-side texture for the ray tracer (GPU textures are unreadable from the CPU).
// Pixels stored as linear-float RGB — sRGB conversion applied on load.
// All meshes in one model share this via shared_ptr (avoid duplicating large images).
struct CPUTexture {
    std::vector<glm::vec3> pixels;
    int width = 0, height = 0;

    bool valid() const { return !pixels.empty(); }

    // Bilinear sample with UV repeat wrapping.
    glm::vec3 sample(float u, float v) const {
        u -= std::floor(u);
        v -= std::floor(v);
        float fx = u * (float)(width  - 1);
        float fy = v * (float)(height - 1);
        int x0 = (int)fx, y0 = (int)fy;
        int x1 = x0 + 1 < width  ? x0 + 1 : x0;
        int y1 = y0 + 1 < height ? y0 + 1 : y0;
        float tx = fx - (float)x0, ty = fy - (float)y0;
        glm::vec3 c00 = pixels[y0 * width + x0];
        glm::vec3 c10 = pixels[y0 * width + x1];
        glm::vec3 c01 = pixels[y1 * width + x0];
        glm::vec3 c11 = pixels[y1 * width + x1];
        return glm::mix(glm::mix(c00, c10, tx), glm::mix(c01, c11, tx), ty);
    }
};

// Forward declarations to avoid pulling in Assimp headers here
struct aiNode;
struct aiScene;
struct aiMesh;

struct Vertex {
    glm::vec3 Position;
    glm::vec3 Normal;
    glm::vec2 TexCoords;
    glm::vec3 Tangent = glm::vec3(0.0f);
};

class Mesh {
public:
    std::vector<Vertex>       vertices;
    std::vector<unsigned int> indices;
    unsigned int              diffuseTexID     = 0;
    unsigned int              normalTexID      = 0;
    unsigned int              roughnessTexID   = 0;
    unsigned int              metallicTexID    = 0;
    unsigned int              aoTexID          = 0;
    float                     roughness        = 0.5f;
    float                     metallic         = 0.0f;
    glm::vec3                 albedoColor      = glm::vec3(0.8f);
    glm::vec3                 emissiveColor    = glm::vec3(0.0f);
    float                     emissiveStrength = 0.0f;
    // CPU-side albedo texture — shared across meshes, null when not loaded.
    // Takes priority over albedoColor in the ray tracer; raster ignores it.
    std::shared_ptr<CPUTexture> cpuAlbedo;

    Mesh(std::vector<Vertex>       verts,
         std::vector<unsigned int> indices,
         unsigned int              diffuseTex,
         glm::vec3                 albedo = glm::vec3(0.8f));

    void draw(Shader& shader) const;

private:
    unsigned int VAO = 0, VBO = 0, EBO = 0;
    void setupMesh();
};

class Model {
public:
    Model() = default;
    explicit Model(const std::string& path) { load(path); }

    void load(const std::string& path);
    void draw(Shader& shader) const;
    void addMesh(Mesh m) { m_meshes.push_back(std::move(m)); }
    const std::vector<Mesh>& getMeshes() const { return m_meshes; }

    void clearEmissive() {
        for (Mesh& m : m_meshes) m.emissiveStrength = 0.0f;
    }

    // Override the CPU-side albedo used by the ray tracer (raster reads the GL
    // diffuse texture and is unaffected).  Use this when the model has a texture
    // the RT can't sample — set to the texture's representative average colour.
    void setAlbedoColor(const glm::vec3& c) {
        for (Mesh& m : m_meshes) m.albedoColor = c;
    }

    // Load a PNG as a CPU-side albedo texture (for the ray tracer).
    // Converts sRGB→linear float. All meshes in this model share one allocation.
    // Falls back to albedoColor if the file is missing.
    void loadCPUAlbedo(const std::string& path);

    // Returns {min, max} AABB in model-local space.
    std::pair<glm::vec3, glm::vec3> computeAABB() const;

    // Assign PBR maps explicitly (for models where the MTL doesn't reference them).
    // Pass empty string to skip any individual map.
    void loadPBRMaps(const std::string& normalPath,
                     const std::string& roughPath,
                     const std::string& metalPath,
                     const std::string& aoPath);

    // Assign a GL diffuse/albedo texture explicitly (for procedural meshes,
    // e.g. the room's boxObj floor, which have no MTL to reference one).
    void loadDiffuseMap(const std::string& diffusePath);

private:
    std::vector<Mesh>                    m_meshes;
    std::string                          m_directory;
    std::map<std::string, unsigned int>  m_texCache;

    void processNode(aiNode* node, const aiScene* scene);
    Mesh processMesh(aiMesh* mesh, const aiScene* scene);
    unsigned int loadTexture(const std::string& path);
};
