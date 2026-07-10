#include "RayTracer.h"
#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <random>
#include <string>
#include "Scene.h"

void RayTracer::Initialize(uint32_t width, uint32_t height)
{
    m_width = width;
    m_height = height;

    glGenBuffers(1, &m_vertexSSBO);
    glGenBuffers(1, &m_indexSSBO);
    glGenBuffers(1, &m_meshSSBO);
}




void RayTracer::UploadScene(const Scene& scene)
{
    m_vertexSSBO.clear();
    m_indexSSBO.clear();
    m_meshSSBO.clear();
    for (const SceneObject& object : scene.objects)
    {
        const Model& model = object.model;
        for (const Mesh& mesh : object.model.GetMeshes()){
            uint32_t firstVertex = m_vertexSSBO.size();
            uint32_t firstIndex = m_indexSSBO.size();
            m_vertexSSBO.insert(m_vertexSSBO.end(), mesh.vertices.begin(), mesh.vertices.end());
        };

    };
}
