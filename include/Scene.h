#pragma once
#include <vector>
#include <string>
#include <glm/glm.hpp>
#include "Model.h"
#include "Shader.h"

struct SceneObject {
    Model     model;
    glm::mat4 transform      = glm::mat4(1.0f);
    bool      skipReflection = false;
    glm::vec3 sortCenter     = glm::vec3(0.0f);  // world-space center for back-to-front sorting
};

class Scene {
public:
    std::vector<SceneObject> objects;       // opaque — drawn in G-buffer pass
    std::vector<SceneObject> glassObjects;  // transparent — drawn in forward glass pass

    void load(const std::string& path);
    void draw(Shader& shader) const;
    void drawForReflection(Shader& shader) const;

    // Sort glassObjects back-to-front and draw — call inside the forward glass pass.
    void drawGlassSorted(Shader& shader, const glm::vec3& camPos) const;
};
