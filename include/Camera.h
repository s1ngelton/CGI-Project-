#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

enum Camera_Movement { FORWARD, BACKWARD, LEFT, RIGHT };

class Camera {
public:
    glm::vec3 Position;
    glm::vec3 Front;
    glm::vec3 Up;
    glm::vec3 Right;
    glm::vec3 WorldUp;

    float Yaw         = -90.0f;
    float Pitch       =   0.0f;
    float MovSpeed    =   2.5f;
    float MouseSens   =   0.1f;
    float Zoom        =  45.0f;

    explicit Camera(glm::vec3 position = glm::vec3(0.0f))
        : Position(position), WorldUp(glm::vec3(0.0f, 1.0f, 0.0f)) {
        Front = glm::vec3(0.0f, 0.0f, -1.0f);
        updateVectors();
    }

    glm::mat4 GetViewMatrix() const {
        return glm::lookAt(Position, Position + Front, Up);
    }

    void ProcessKeyboard(Camera_Movement dir, float dt) {
        float v = MovSpeed * dt;
        if (dir == FORWARD)  Position += Front * v;
        if (dir == BACKWARD) Position -= Front * v;
        if (dir == LEFT)     Position -= Right * v;
        if (dir == RIGHT)    Position += Right * v;
    }

    void ProcessMouseMovement(float xoffset, float yoffset, bool constrain = true) {
        Yaw   += xoffset * MouseSens;
        Pitch += yoffset * MouseSens;
        if (constrain) Pitch = glm::clamp(Pitch, -89.0f, 89.0f);
        updateVectors();
    }

    void ProcessMouseScroll(float yoffset) {
        Zoom = glm::clamp(Zoom - yoffset, 1.0f, 90.0f);
    }

    // Allow cinematic engine to override position + orientation directly
    void SetPose(glm::vec3 pos, glm::vec3 front) {
        Position = pos;
        glm::vec3 f = glm::normalize(front);
        Pitch = glm::degrees(asin(glm::clamp(f.y, -1.0f, 1.0f)));
        Yaw   = glm::degrees(atan2(f.z, f.x));
        updateVectors();
    }

private:
    void updateVectors() {
        glm::vec3 f;
        f.x   = cos(glm::radians(Yaw)) * cos(glm::radians(Pitch));
        f.y   = sin(glm::radians(Pitch));
        f.z   = sin(glm::radians(Yaw)) * cos(glm::radians(Pitch));
        Front = glm::normalize(f);
        Right = glm::normalize(glm::cross(Front, WorldUp));
        Up    = glm::normalize(glm::cross(Right, Front));
    }
};
