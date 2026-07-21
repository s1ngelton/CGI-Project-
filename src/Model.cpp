#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include "Model.h"
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
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
    // location 3 — tangent
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          (void*)offsetof(Vertex, Tangent));

    glBindVertexArray(0);
}

void Mesh::draw(Shader& shader) const {
    // Base color — unit 0
    if (diffuseTexID != 0) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, diffuseTexID);
        shader.setInt ("texture_diffuse1", 0);
        shader.setBool("hasTexture", true);
    } else {
        shader.setBool("hasTexture", false);
        shader.setVec3("albedoColor", albedoColor);
    }

    // Normal map — unit 1
    shader.setBool("hasNormalTex", normalTexID != 0);
    if (normalTexID) {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, normalTexID);
        shader.setInt("normalMap", 1);
    }

    // Roughness map — unit 2
    shader.setBool("hasRoughnessTex", roughnessTexID != 0);
    if (roughnessTexID) {
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, roughnessTexID);
        shader.setInt("roughnessMap", 2);
    }

    // Metallic map — unit 3
    shader.setBool("hasMetallicTex", metallicTexID != 0);
    if (metallicTexID) {
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, metallicTexID);
        shader.setInt("metallicMap", 3);
    }

    // AO map — unit 4
    shader.setBool("hasAOTex", aoTexID != 0);
    if (aoTexID) {
        glActiveTexture(GL_TEXTURE4);
        glBindTexture(GL_TEXTURE_2D, aoTexID);
        shader.setInt("aoMap", 4);
    }

    // Material fallbacks when maps are absent
    shader.setFloat("matRoughness", roughness);
    shader.setFloat("matMetallic",  metallic);

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
        aiProcess_Triangulate          |
        aiProcess_FlipUVs              |
        aiProcess_GenSmoothNormals     |
        aiProcess_CalcTangentSpace     |   // required for normal mapping
        aiProcess_JoinIdenticalVertices|
        aiProcess_PreTransformVertices);

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

std::pair<glm::vec3, glm::vec3> Model::computeAABB() const {
    glm::vec3 mn( 1e30f), mx(-1e30f);
    for (const Mesh& mesh : m_meshes)
        for (const Vertex& v : mesh.vertices) {
            mn = glm::min(mn, v.Position);
            mx = glm::max(mx, v.Position);
        }
    return {mn, mx};
}

void Model::loadPBRMaps(const std::string& normalPath,
                        const std::string& roughPath,
                        const std::string& metalPath,
                        const std::string& aoPath) {
    unsigned int nid = normalPath.empty() ? 0 : loadTexture(normalPath);
    unsigned int rid = roughPath.empty()  ? 0 : loadTexture(roughPath);
    unsigned int mid = metalPath.empty()  ? 0 : loadTexture(metalPath);
    unsigned int aid = aoPath.empty()     ? 0 : loadTexture(aoPath);
    for (Mesh& m : m_meshes) {
        if (nid) m.normalTexID    = nid;
        if (rid) m.roughnessTexID = rid;
        if (mid) m.metallicTexID  = mid;
        if (aid) m.aoTexID        = aid;
    }
}

void Model::loadDiffuseMap(const std::string& diffusePath) {
    if (diffusePath.empty()) return;
    unsigned int did = loadTexture(diffusePath);
    if (!did) return;
    for (Mesh& m : m_meshes) m.diffuseTexID = did;
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
        v.Tangent   = mesh->mTangents
                      ? glm::vec3(mesh->mTangents[i].x, mesh->mTangents[i].y, mesh->mTangents[i].z)
                      : glm::vec3(1.0f, 0.0f, 0.0f);
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

    Mesh m(std::move(vertices), std::move(indices), diffuseTex, albedo);

    if (mesh->mMaterialIndex < scene->mNumMaterials) {
        aiMaterial* mat = scene->mMaterials[mesh->mMaterialIndex];

        // Base color
        aiString texPath;
        if (mat->GetTexture(aiTextureType_DIFFUSE, 0, &texPath) == AI_SUCCESS) {
            diffuseTex = loadTexture(m_directory + "/" + texPath.C_Str());
            m.diffuseTexID = diffuseTex;
        }
        if (diffuseTex == 0) {
            aiColor3D col(0.8f, 0.8f, 0.8f);
            mat->Get(AI_MATKEY_COLOR_DIFFUSE, col);
            m.albedoColor = { col.r, col.g, col.b };
        }

        // PBR maps — try Assimp's typed queries; fall back to alternate type keys
        auto tryTex = [&](aiTextureType type) -> unsigned int {
            aiString p;
            if (mat->GetTexture(type, 0, &p) == AI_SUCCESS)
                return loadTexture(m_directory + "/" + p.C_Str());
            return 0;
        };

        m.normalTexID    = tryTex(aiTextureType_NORMALS);
        if (!m.normalTexID) m.normalTexID = tryTex(aiTextureType_HEIGHT);

        m.roughnessTexID = tryTex(aiTextureType_DIFFUSE_ROUGHNESS);
        if (!m.roughnessTexID) m.roughnessTexID = tryTex(aiTextureType_SHININESS);

        m.metallicTexID  = tryTex(aiTextureType_METALNESS);

        m.aoTexID        = tryTex(aiTextureType_AMBIENT_OCCLUSION);
        if (!m.aoTexID) m.aoTexID = tryTex(aiTextureType_LIGHTMAP);

        // Emissive
        aiColor3D emit(0.0f, 0.0f, 0.0f);
        mat->Get(AI_MATKEY_COLOR_EMISSIVE, emit);
        if (emit.r > 0.0f || emit.g > 0.0f || emit.b > 0.0f) {
            m.emissiveColor    = glm::vec3(emit.r, emit.g, emit.b);
            m.emissiveStrength = 1.0f;
        }
    }

    return m;
}

void Model::loadCPUAlbedo(const std::string& path) {
    int w, h, ch;
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &ch, 3);
    if (!data) {
        std::cerr << "[RT] CPU albedo load failed: " << path << "\n";
        return;
    }

    auto tex = std::make_shared<CPUTexture>();
    tex->width  = w;
    tex->height = h;
    tex->pixels.resize(w * h);

    // sRGB → linear float (IEC 61966-2-1)
    for (int i = 0; i < w * h; ++i) {
        auto srgb = [](float c) -> float {
            return c <= 0.04045f ? c / 12.92f
                                 : std::pow((c + 0.055f) / 1.055f, 2.4f);
        };
        tex->pixels[i] = {
            srgb(data[i*3+0] / 255.0f),
            srgb(data[i*3+1] / 255.0f),
            srgb(data[i*3+2] / 255.0f)
        };
    }
    stbi_image_free(data);
    std::cout << "[RT] CPU albedo: " << path << " (" << w << "×" << h << ")\n";

    for (Mesh& m : m_meshes)
        m.cpuAlbedo = tex;  // all meshes share one allocation
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
