#include "Scene.h"
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>

void Scene::load(const std::string& path) {
    // Temporary: load a hardcoded test cube to verify the GBuffer pass.
    // Replace with JSON parsing once geometry is confirmed working.
    SceneObject obj;
    obj.model.load("assets/models/cube.obj");
    obj.transform = glm::mat4(1.0f);
    objects.push_back(std::move(obj));
}

void Scene::draw(Shader& shader) const {
    for (const SceneObject& obj : objects) {
        shader.setMat4("model", obj.transform);
        obj.model.draw(shader);
    }
}
