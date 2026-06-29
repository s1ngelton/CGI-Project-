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
};

class Mesh {
public:
    std::vector<Vertex>       vertices;
    std::vector<unsigned int> indices;
    unsigned int              diffuseTexID     = 0;
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

private:
    std::vector<Mesh>                    m_meshes;
    std::string                          m_directory;
    std::map<std::string, unsigned int>  m_texCache;

    void processNode(aiNode* node, const aiScene* scene);
    Mesh processMesh(aiMesh* mesh, const aiScene* scene);
    unsigned int loadTexture(const std::string& path);
};
