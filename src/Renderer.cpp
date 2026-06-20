#include "Renderer.h"
#include "Scene.h"
#include <glad/glad.h>

Renderer::Renderer(int width, int height)
    : m_width(width), m_height(height) {
}

Renderer::~Renderer() {
}

void Renderer::render(Scene& scene, const Camera& camera, float deltaTime) {
    glClearColor(0.01f, 0.01f, 0.01f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    // rendering passes will go here
}