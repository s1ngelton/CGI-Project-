#pragma once
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <map>
#include "Shader.h"

// Forward declarations to avoid pulling in Assimp headers here
struct aiNode;
struct aiScene;
struct aiMesh;

struct Vertex {
    glm::vec4 Position;   // xyz = position, w unused
    glm::vec4 Normal;     // xyz = normal, w unused
    glm::vec2 TexCoords;  // uv
    glm::vec2 _pad0;      // padding for alignment
    glm::vec4 Tangent;    // xyz = tangent, w unused
};

struct MeshInfo {
    uint32_t firstIndex;
    uint32_t indexCount;
    uint32_t materialIndex;
    float roughness;
    float metallic;
    float emissiveStrength;
    float _pad0[2];
    glm::vec4 albedoColor;
    glm::vec4 emissiveColor;
    float ior;
    float _pad1[3];
};


struct BVHNode {
    glm::vec3 bboxMin;
    uint32_t leftChild;   // 0xFFFFFFFF means leaf; otherwise index of left child
    glm::vec3 bboxMax;
    uint32_t rightChild;  // if leaf: triangle index; else index of right child
};

struct TriangleRef {
    uint32_t triIdx;      // The first index of this triangle in the global `indices` buffer
    glm::vec3 centroid;   // Center of the triangle
};

struct LightTriangle {
    glm::vec4 v0;           // reads as 12 bytes, padded to 16 in std430
    glm::vec4 v1;
    glm::vec4 v2;
    glm::vec4 emission;
    float area;
    float cumArea;
    float _pad[2];
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
    float IOR = 1.0;

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

    void clearEmissive() {
        for (Mesh& m : m_meshes) m.emissiveStrength = 0.0f;
    }

    const std::vector<Mesh>& GetMeshes() const{
    return m_meshes;
    }

    // Returns {min, max} AABB in model-local space.
    std::pair<glm::vec4, glm::vec4> computeAABB() const;

    // Assign PBR maps explicitly (for models where the MTL doesn't reference them).
    // Pass empty string to skip any individual map.
    void loadPBRMaps(const std::string& normalPath,
                     const std::string& roughPath,
                     const std::string& metalPath,
                     const std::string& aoPath);

private:
    std::vector<Mesh>                    m_meshes;
    std::string                          m_directory;
    std::map<std::string, unsigned int>  m_texCache;

    void processNode(aiNode* node, const aiScene* scene);
    Mesh processMesh(aiMesh* mesh, const aiScene* scene);
    unsigned int loadTexture(const std::string& path);
};
