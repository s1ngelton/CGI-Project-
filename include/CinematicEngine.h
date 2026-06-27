#pragma once
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include "Camera.h"

// A single keyframe: time + camera pose
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
    float duration = 0.0f;  // auto-computed from last keyframe

    void load(const std::string& jsonPath);

    // Add a keyframe manually (useful for testing without a JSON file)
    void addKeyframe(float t, glm::vec3 pos, glm::vec3 target, float fov = 45.0f);

    // Evaluate the spline at time t and write result into camera
    void update(float t, Camera& camera);

    // Reset playback
    void reset() { m_time = 0.0f; }

private:
    float m_time = 0.0f;

    // Catmull-Rom spline interpolation between four control points
    static glm::vec3 catmullRom(glm::vec3 p0, glm::vec3 p1,
                                 glm::vec3 p2, glm::vec3 p3, float t);

    // Find the segment index and local t for a given global time
    void findSegment(float time, int& i, float& localT) const;
};
