#pragma once
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

struct SceneObject {
    std::string name;
    unsigned int vao;
    int vertexCount;
    glm::mat4 transform;
    glm::vec3 color;
    
    // Default PBR Material
    float roughness = 0.8f;
    float metallic = 0.0f;
    bool drawAsQuad = false;
};

class Scene {
public:
    std::vector<SceneObject> objects;

    void load(const std::string& path);
    void buildFloorplan();
    void addCube(const std::string& name, glm::vec3 pos, glm::vec3 scale, glm::vec3 color, float roughness = 0.8f, float metallic = 0.0f);
};