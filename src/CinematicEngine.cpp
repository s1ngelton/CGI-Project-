#include "CinematicEngine.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <iostream>

void CinematicEngine::load(const std::string& jsonPath) {
    // TODO: parse JSON file (use nlohmann/json or a simple hand-rolled parser)
    // For now, populate with a default horror-scene camera path:
    std::cout << "[Cinematic] JSON loading not yet implemented — using default path\n";

    addKeyframe(0.0f,  {0.0f, 1.5f,  4.0f}, {0.0f, 1.0f, 0.0f});   // wide shot
    addKeyframe(3.0f,  {2.0f, 1.5f,  2.0f}, {0.0f, 1.0f, 0.0f});   // pan right
    addKeyframe(6.0f,  {0.5f, 0.5f,  0.5f}, {0.0f, 1.0f, 0.0f});   // creep in low
    addKeyframe(9.0f,  {0.0f, 2.5f, -1.0f}, {0.0f, 0.5f, 0.0f});   // overhead
    addKeyframe(12.0f, {0.0f, 1.5f,  4.0f}, {0.0f, 1.0f, 0.0f});   // back to start
}

void CinematicEngine::addKeyframe(float t, glm::vec3 pos, glm::vec3 tgt, float fov) {
    keyframes.push_back({t, pos, tgt, fov});
    // Keep sorted by time
    std::sort(keyframes.begin(), keyframes.end(),
              [](const Keyframe& a, const Keyframe& b){ return a.time < b.time; });
    duration = keyframes.empty() ? 0.0f : keyframes.back().time;
}

void CinematicEngine::update(float t, Camera& camera) {
    if (keyframes.size() < 2) return;

    float time = loop ? fmod(t, duration) : std::min(t, duration);

    int   i;
    float localT;
    findSegment(time, i, localT);

    // Clamp indices for Catmull-Rom (need p0..p3)
    int i0 = std::max(0, i - 1);
    int i1 = i;
    int i2 = std::min((int)keyframes.size() - 1, i + 1);
    int i3 = std::min((int)keyframes.size() - 1, i + 2);

    glm::vec3 pos = catmullRom(keyframes[i0].position, keyframes[i1].position,
                                keyframes[i2].position, keyframes[i3].position,
                                localT);

    glm::vec3 tgt = catmullRom(keyframes[i0].target, keyframes[i1].target,
                                keyframes[i2].target, keyframes[i3].target,
                                localT);

    float fov = glm::mix(keyframes[i1].fov, keyframes[i2].fov, localT);

    camera.SetPose(pos, tgt - pos);
    camera.Zoom = fov;
}

void CinematicEngine::findSegment(float time, int& i, float& localT) const {
    for (i = 0; i < (int)keyframes.size() - 1; ++i) {
        if (time <= keyframes[i + 1].time) {
            float segLen = keyframes[i + 1].time - keyframes[i].time;
            localT = (segLen > 0.0f) ? (time - keyframes[i].time) / segLen : 0.0f;
            return;
        }
    }
    i      = (int)keyframes.size() - 2;
    localT = 1.0f;
}

glm::vec3 CinematicEngine::catmullRom(glm::vec3 p0, glm::vec3 p1,
                                       glm::vec3 p2, glm::vec3 p3, float t) {
    // Standard Catmull-Rom formula
    float t2 = t * t;
    float t3 = t2 * t;
    return 0.5f * (
        (2.0f * p1) +
        (-p0 + p2) * t +
        (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
        (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3
    );
}
