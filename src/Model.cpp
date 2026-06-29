#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include "Model.h"
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <cstddef>
#include <iostream>

// ── Mesh ─────────────────────────────────────────────────────────────────────

Mesh::Mesh(std::vector<Vertex>       verts,
           std::vector<unsigned int> idx,
           unsigned int              diffuseTex,
           glm::vec3                 albedo)
    : vertices(std::move(verts))
    , indices(std::move(idx))
    , diffuseTexID(diffuseTex)
    , albedoColor(albedo)
{
    setupMesh();
}

void Mesh::setupMesh() {
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glGenBuffers(1, &EBO);

    glBindVertexArray(VAO);

    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)(vertices.size() * sizeof(Vertex)),
                 vertices.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 (GLsizeiptr)(indices.size() * sizeof(unsigned int)),
                 indices.data(), GL_STATIC_DRAW);

    // location 0 — position
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          (void*)offsetof(Vertex, Position));
    // location 1 — normal
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          (void*)offsetof(Vertex, Normal));
    // location 2 — texcoords
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          (void*)offsetof(Vertex, TexCoords));

    glBindVertexArray(0);
}

void Mesh::draw(Shader& shader) const {
    if (diffuseTexID != 0) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, diffuseTexID);
        shader.setInt("texture_diffuse1", 0);
        shader.setBool("hasTexture", true);
    } else {
        shader.setBool("hasTexture", false);
        shader.setVec3("albedoColor", albedoColor);
    }

    shader.setVec3 ("emissiveColor",    emissiveColor);
    shader.setFloat("emissiveStrength", emissiveStrength);

    glBindVertexArray(VAO);
    glDrawElements(GL_TRIANGLES, (GLsizei)indices.size(), GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}

// ── Model ─────────────────────────────────────────────────────────────────────

void Model::load(const std::string& path) {
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(path,
        aiProcess_Triangulate |
        aiProcess_FlipUVs     |
        aiProcess_GenNormals);

    if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode) {
        std::cerr << "[Assimp] " << importer.GetErrorString() << "\n";
        return;
    }

    m_directory = path.substr(0, path.find_last_of('/'));
    processNode(scene->mRootNode, scene);
}

void Model::draw(Shader& shader) const {
    for (const Mesh& mesh : m_meshes)
        mesh.draw(shader);
}

void Model::processNode(aiNode* node, const aiScene* scene) {
    for (unsigned int i = 0; i < node->mNumMeshes; ++i) {
        aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
        m_meshes.push_back(processMesh(mesh, scene));
    }
    for (unsigned int i = 0; i < node->mNumChildren; ++i)
        processNode(node->mChildren[i], scene);
}

Mesh Model::processMesh(aiMesh* mesh, const aiScene* scene) {
    std::vector<Vertex>       vertices;
    std::vector<unsigned int> indices;

    vertices.reserve(mesh->mNumVertices);
    for (unsigned int i = 0; i < mesh->mNumVertices; ++i) {
        Vertex v;
        v.Position  = { mesh->mVertices[i].x, mesh->mVertices[i].y, mesh->mVertices[i].z };
        v.Normal    = mesh->HasNormals()
                      ? glm::vec3(mesh->mNormals[i].x, mesh->mNormals[i].y, mesh->mNormals[i].z)
                      : glm::vec3(0.0f, 1.0f, 0.0f);
        v.TexCoords = mesh->mTextureCoords[0]
                      ? glm::vec2(mesh->mTextureCoords[0][i].x, mesh->mTextureCoords[0][i].y)
                      : glm::vec2(0.0f);
        vertices.push_back(v);
    }

    for (unsigned int i = 0; i < mesh->mNumFaces; ++i) {
        const aiFace& face = mesh->mFaces[i];
        for (unsigned int j = 0; j < face.mNumIndices; ++j)
            indices.push_back(face.mIndices[j]);
    }

    // Textures & fallback colour
    unsigned int diffuseTex = 0;
    glm::vec3    albedo(0.8f);

    if (mesh->mMaterialIndex < scene->mNumMaterials) {
        aiMaterial* mat = scene->mMaterials[mesh->mMaterialIndex];

        aiString texPath;
        if (mat->GetTexture(aiTextureType_DIFFUSE, 0, &texPath) == AI_SUCCESS) {
            std::string fullPath = m_directory + "/" + texPath.C_Str();
            diffuseTex = loadTexture(fullPath);
        }

        // No texture — read Kd from material as fallback colour
        if (diffuseTex == 0) {
            aiColor3D col(0.8f, 0.8f, 0.8f);
            mat->Get(AI_MATKEY_COLOR_DIFFUSE, col);
            albedo = { col.r, col.g, col.b };
        }
    }

    Mesh m(std::move(vertices), std::move(indices), diffuseTex, albedo);

    // Read emissive from material if present
    if (mesh->mMaterialIndex < scene->mNumMaterials) {
        aiMaterial* mat  = scene->mMaterials[mesh->mMaterialIndex];
        aiColor3D   emit(0.0f, 0.0f, 0.0f);
        mat->Get(AI_MATKEY_COLOR_EMISSIVE, emit);
        if (emit.r > 0.0f || emit.g > 0.0f || emit.b > 0.0f) {
            m.emissiveColor    = glm::vec3(emit.r, emit.g, emit.b);
            m.emissiveStrength = 1.0f;
        }
    }

    return m;
}

unsigned int Model::loadTexture(const std::string& path) {
    auto it = m_texCache.find(path);
    if (it != m_texCache.end())
        return it->second;

    int w, h, ch;
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &ch, 0);
    if (!data) {
        std::cerr << "[stb_image] Failed to load: " << path << "\n";
        return 0;
    }

    GLenum fmt = (ch == 4) ? GL_RGBA : (ch == 3) ? GL_RGB : GL_RED;

    unsigned int id;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexImage2D(GL_TEXTURE_2D, 0, (GLint)fmt, w, h, 0, fmt, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,     GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,     GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    stbi_image_free(data);
    m_texCache[path] = id;
    return id;
}
