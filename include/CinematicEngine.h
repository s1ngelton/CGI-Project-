#pragma once
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include "Camera.h"
#include "Scene.h"
#include "AudioManager.h"

struct Keyframe {
    float     time;       // seconds from start
    glm::vec3 position;
    glm::vec3 target;     // look-at point
    float     fov;        // field of view in degrees
};

class CinematicEngine {
public:
    std::vector<Keyframe> keyframes;
    bool  loop     = false;
    float duration = 45.0f;  // 45 seconds for 3 scenes

    void load(const std::string& jsonPath);

    void addKeyframe(float t, glm::vec3 pos, glm::vec3 target, float fov = 45.0f);

    // Update camera pose, animate scene objects, and trigger sound events
    void update(float t, Camera& camera, Scene& scene, AudioManager& audio, float deltaTime);

    void reset() { m_time = 0.0f; }

private:
    float m_time = 0.0f;
    float m_lastTime = 0.0f;
    float m_walkingTime = 0.0f;

    static glm::vec3 catmullRom(glm::vec3 p0, glm::vec3 p1,
                                 glm::vec3 p2, glm::vec3 p3, float t);

    void findSegment(float time, int& i, float& localT) const;
    void triggerEvent(float t, Camera& camera, Scene& scene, AudioManager& audio);
};
