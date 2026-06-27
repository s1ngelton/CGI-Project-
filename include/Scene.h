#pragma once
#include <vector>
#include <string>
#include <glm/glm.hpp>
#include "Model.h"
#include "Shader.h"

struct SceneObject {
    Model     model;
    glm::mat4 transform = glm::mat4(1.0f);
};

class Scene {
public:
    std::vector<SceneObject> objects;

    void load(const std::string& path);
    void draw(Shader& shader) const;
};
