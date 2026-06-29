#pragma once
#include <vector>
#include <string>
#include <glm/glm.hpp>
#include "Model.h"
#include "Shader.h"

struct SceneObject {
    Model     model;
    glm::mat4 transform     = glm::mat4(1.0f);
    bool      skipReflection = false;  // set true on the floor — it shouldn't appear in its own mirror
};

class Scene {
public:
    std::vector<SceneObject> objects;

    void load(const std::string& path);
    void draw(Shader& shader) const;
    void drawForReflection(Shader& shader) const;
};
