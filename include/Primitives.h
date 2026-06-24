#pragma once
#include <glad/glad.h>
#include <vector>

class Primitives {
public:
    static unsigned int getCubeVAO();
    static unsigned int getQuadVAO();
    static unsigned int getSphereVAO();

private:
    static unsigned int cubeVAO;
    static unsigned int quadVAO;
    static unsigned int sphereVAO;
    static unsigned int sphereIndexCount;
};
