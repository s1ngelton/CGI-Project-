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

    // Returns {min, max} AABB in model-local space.
    std::pair<glm::vec3, glm::vec3> computeAABB() const;

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
