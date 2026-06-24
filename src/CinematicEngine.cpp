#include "CinematicEngine.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <iostream>
#include <cmath>

void CinematicEngine::load(const std::string& jsonPath) {
    std::cout << "[Cinematic] Setting up 3-Scene Storyboard path (45s total)\n";

    // --- Scene 1 Keyframes (0.0s - 15.0s) ---
    addKeyframe(0.0f,  {2.0f, 1.15f, 7.6f},  {2.0f, 1.0f, 4.0f});     // Already sitting on sofa, watching movie
    addKeyframe(7.0f,  {2.0f, 1.15f, 7.6f},  {2.0f, 1.0f, 4.0f});     // TV switches to static, stares in surprise
    addKeyframe(10.0f, {2.0f, 1.15f, 7.6f},  {2.0f, 1.0f, 4.0f});     // Stares at TV static
    addKeyframe(11.5f, {2.0f, 1.5f, 5.5f},   {2.0f, 1.0f, 3.9f});     // Approaches TV
    addKeyframe(12.5f, {2.0f, 1.5f, 5.5f},   {2.0f, 1.0f, 3.9f});     // Kicks TV
    addKeyframe(15.0f, {2.0f, 1.5f, 7.0f},   {-2.0f, 1.0f, -5.0f});   // Backs up, turns to kitchen

    // --- Scene 2 Keyframes (15.0s - 30.0s) ---
    addKeyframe(17.0f, {1.0f, 1.5f, 7.0f},   {-5.1f, 1.2f, 8.0f});    // Stand up, look at hallway switch
    addKeyframe(20.0f, {-4.5f, 1.5f, 8.0f},  {-5.1f, 1.2f, 8.0f});    // Walk to switch
    addKeyframe(23.0f, {-4.5f, 1.5f, 8.0f},  {-10.1f, 1.5f, 5.0f});   // Lightning flash, look at shadow
    addKeyframe(26.0f, {-8.0f, 1.5f, 5.0f},  {-9.9f, 1.5f, 5.0f});    // Walk to fuse box
    addKeyframe(30.0f, {-8.0f, 1.5f, 5.0f},  {0.0f, 1.5f, 0.0f});     // Switch flipped, lights turn on

    // --- Scene 3 Keyframes (30.0s - 45.0s) ---
    addKeyframe(33.0f, {0.0f, 1.5f, 2.0f},   {-6.0f, 1.5f, -3.0f});   // Return, look at dining room
    addKeyframe(36.0f, {0.0f, 1.5f, -2.0f},  {-2.0f, 1.0f, -8.1f});   // Fridge door opens, look at fridge
    addKeyframe(40.0f, {-2.0f, 1.5f, 2.0f},  {-8.0f, 1.0f, 10.0f});   // Run towards front door
    addKeyframe(43.0f, {-5.0f, 0.2f, 7.0f},  {-5.0f, 0.1f, 10.0f});   // Trip, fall to floor
    addKeyframe(45.0f, {-5.0f, 0.2f, 7.0f},  {-5.0f, 0.1f, 10.0f});   // Black screen
}

void CinematicEngine::addKeyframe(float t, glm::vec3 pos, glm::vec3 tgt, float fov) {
    keyframes.push_back({t, pos, tgt, fov});
    std::sort(keyframes.begin(), keyframes.end(),
              [](const Keyframe& a, const Keyframe& b){ return a.time < b.time; });
}

void CinematicEngine::update(float t, Camera& camera, Scene& scene, AudioManager& audio, float deltaTime) {
    if (keyframes.size() < 2) return;

    m_time = loop ? fmod(t, duration) : std::min(t, duration);

    int   i;
    float localT;
    findSegment(m_time, i, localT);

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

    // Natural breathing / head-sway camera motion
    float swayX = std::sin(m_time * 1.5f) * 0.02f;
    float swayY = std::cos(m_time * 1.0f) * 0.015f;
    pos.x += swayX;
    pos.y += swayY;
    tgt.x += std::sin(m_time * 0.8f) * 0.015f;
    tgt.y += std::cos(m_time * 0.6f) * 0.01f;

    // Check if the camera is moving (excluding the final fall/trip)
    float speed = glm::distance(pos, camera.Position) / (deltaTime + 0.0001f);
    bool isMoving = (speed > 0.1f) && (m_time < 42.0f);
    if (isMoving) {
        m_walkingTime += deltaTime;
    }

    // Apply camera pose
    camera.SetPose(pos, tgt - pos);
    camera.Zoom = fov;

    // Handle Object Animations & Events
    triggerEvent(m_time, camera, scene, audio);
    
    m_lastTime = m_time;
}

void CinematicEngine::triggerEvent(float t, Camera& camera, Scene& scene, AudioManager& audio) {
    // Find TV screen quad, ceiling lamp, Kühlschrank door, leg, shoe, and shadow monster
    int tvScreenIdx = -1;
    int lampIdx = -1;
    int kfrTuerIdx = -1;
    int kfrHandleIdx = -1;
    int legIdx = -1;
    int shoeIdx = -1;
    int shadowMonsterIdx = -1;
    int fuseDoorIdx = -1;
    int walkLegLIdx = -1, walkShoeLIdx = -1;
    int walkLegRIdx = -1, walkShoeRIdx = -1;
    for (size_t i = 0; i < scene.objects.size(); ++i) {
        if (scene.objects[i].name == "TV_Screen") tvScreenIdx = i;
        if (scene.objects[i].name == "Ceiling_Lamp") lampIdx = i;
        if (scene.objects[i].name == "Kuehlschrank_Tuer") kfrTuerIdx = i;
        if (scene.objects[i].name == "Kuehlschrank_Handle") kfrHandleIdx = i;
        if (scene.objects[i].name == "Kicking_Leg") legIdx = i;
        if (scene.objects[i].name == "Kicking_Shoe") shoeIdx = i;
        if (scene.objects[i].name == "Shadow_Monster") shadowMonsterIdx = i;
        if (scene.objects[i].name == "Sicherungskasten_Tuer") fuseDoorIdx = i;
        if (scene.objects[i].name == "Walking_Leg_L") walkLegLIdx = i;
        if (scene.objects[i].name == "Walking_Shoe_L") walkShoeLIdx = i;
        if (scene.objects[i].name == "Walking_Leg_R") walkLegRIdx = i;
        if (scene.objects[i].name == "Walking_Shoe_R") walkShoeRIdx = i;
    }

    // --- Scene 1: TV static & Kick ---
    // TV program playing (0.0s - 7.0s), Static noise (7.0s - 12.0s), Off (12.0s+)
    if (t <= 7.0f) {
        if (tvScreenIdx != -1) {
            float pulse = std::sin(t * 3.0f) * 0.1f + 0.9f;
            scene.objects[tvScreenIdx].color = glm::vec3(0.9f, 0.95f, 1.0f) * pulse * 1.5f; // TV program light
        }
    } else if (t > 7.0f && t < 12.0f) {
        float noise = (float)(rand() % 100) / 100.0f;
        if (tvScreenIdx != -1) {
            scene.objects[tvScreenIdx].color = glm::vec3(0.5f + 0.5f * noise);
        }
        if (m_lastTime <= 7.0f) {
            audio.load("tv_static");
        }
    } else {
        // If it was playing, stop it!
        if (m_lastTime > 7.0f && m_lastTime < 12.0f) {
            audio.stop("tv_static");
        }
        
        // Also support flicker at 14.0s - 15.0s
        if (t >= 14.0f && t < 15.0f) {
            float noise = (float)(rand() % 100) / 100.0f;
            if (tvScreenIdx != -1) {
                scene.objects[tvScreenIdx].color = glm::vec3(0.1f, 0.2f, 0.8f) * noise;
            }
            if (m_lastTime < 14.0f) {
                audio.load("tv_static");
            }
        } else {
            if (m_lastTime >= 14.0f && m_lastTime < 15.0f) {
                audio.stop("tv_static");
            }
            if (tvScreenIdx != -1) {
                scene.objects[tvScreenIdx].color = glm::vec3(0.0f, 0.0f, 0.0f);
            }
        }
    }

    // --- Animate Kicking Leg (11.5s - 12.5s) ---
    if (t >= 11.5f && t < 12.5f) {
        float t_kick = t - 11.5f; // 0 to 1
        float progress = 0.0f;
        if (t_kick < 0.5f) {
            progress = std::sin(t_kick / 0.5f * M_PI / 2.0f); // Snap forward
        } else {
            progress = std::cos((t_kick - 0.5f) / 0.5f * M_PI / 2.0f); // Return
        }

        glm::vec3 camPos = camera.Position;
        glm::vec3 camFront = camera.Front;
        glm::vec3 camRight = camera.Right;
        glm::vec3 camUp = camera.Up;

        // Position relative to camera frame (kicks from distance: max extension 1.55f to hit TV screen at Z=3.89)
        glm::vec3 legPos = camPos + camFront * glm::mix(0.1f, 1.55f, progress) - camUp * glm::mix(0.6f, 0.15f, progress) + camRight * 0.12f;
        glm::vec3 shoePos = legPos + camFront * 0.25f;

        // Rotation matrix to align box with camera look direction
        glm::mat4 rot = glm::mat4(1.0f);
        rot[0] = glm::vec4(camRight, 0.0f);
        rot[1] = glm::vec4(camUp, 0.0f);
        rot[2] = glm::vec4(-camFront, 0.0f);

        if (legIdx != -1) {
            glm::mat4 model = glm::mat4(1.0f);
            model = glm::translate(model, legPos);
            model = model * rot;
            model = glm::scale(model, glm::vec3(0.12f, 0.12f, 0.8f));
            scene.objects[legIdx].transform = model;
        }

        if (shoeIdx != -1) {
            glm::mat4 modelShoe = glm::mat4(1.0f);
            modelShoe = glm::translate(modelShoe, shoePos);
            modelShoe = modelShoe * rot;
            modelShoe = glm::scale(modelShoe, glm::vec3(0.14f, 0.14f, 0.2f));
            scene.objects[shoeIdx].transform = modelShoe;
        }

        // Trigger kick sound effect at peak impact (12.0s)
        if (m_lastTime < 12.0f && t >= 12.0f) {
            audio.load("kick");
        }
    } else {
        // Hide leg and shoe below ground
        if (legIdx != -1) {
            scene.objects[legIdx].transform = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -10.0f, 0.0f));
        }
        if (shoeIdx != -1) {
            scene.objects[shoeIdx].transform = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -10.0f, 0.0f));
        }
    }

    // --- Scene 2: Lightning & Lights out ---
    // Lightning at t = 16.0s and t = 22.0s
    if ((t >= 16.0f && t < 16.5f) || (t >= 22.0f && t < 22.5f) || (t >= 22.6f && t < 23.0f)) {
        if ((m_lastTime < 16.0f && t >= 16.0f) || (m_lastTime < 22.0f && t >= 22.0f)) {
            audio.load("thunder"); // Trigger thunder sound
        }
    }

    // Room Light state
    bool roomLightOn = true;
    if ((t >= 16.5f && t < 29.0f) || t >= 44.0f) {
        roomLightOn = false;
    }

    if (lampIdx != -1) {
        if (roomLightOn) {
            scene.objects[lampIdx].color = glm::vec3(2.0f, 2.0f, 1.8f); // Glowing light
        } else {
            scene.objects[lampIdx].color = glm::vec3(0.15f, 0.15f, 0.15f); // Off
        }
    }

    // Shadow monster visibility
    if (shadowMonsterIdx != -1) {
        if (t >= 22.0f && t < 24.0f) {
            scene.objects[shadowMonsterIdx].transform = glm::translate(glm::mat4(1.0f), glm::vec3(-9.5f, 1.5f, 5.0f));
            scene.objects[shadowMonsterIdx].color = glm::vec3(0.0f, 0.0f, 0.0f);
        } else if (t >= 35.0f && t < 40.0f) {
            float progress = (t - 35.0f) / 5.0f;
            scene.objects[shadowMonsterIdx].transform = glm::translate(glm::mat4(1.0f), glm::vec3(-6.0f + progress * 4.0f, 1.0f, -3.0f));
        } else {
            scene.objects[shadowMonsterIdx].transform = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -10.0f, 0.0f));
        }
    }

    // Fuse Box Door Animation (26.5s - 28.5s)
    if (fuseDoorIdx != -1) {
        if (t >= 26.5f) {
            float doorAngle = std::min(110.0f, (t - 26.5f) * 75.0f); // Open up to 110 degrees
            glm::mat4 model = glm::mat4(1.0f);
            model = glm::translate(model, glm::vec3(-9.9f, 1.5f, 5.15f)); // Pivot at Z edge
            model = glm::rotate(model, glm::radians(doorAngle), glm::vec3(0.0f, 1.0f, 0.0f));
            model = glm::translate(model, glm::vec3(0.05f, 0.0f, -0.15f));
            model = glm::scale(model, glm::vec3(0.02f, 0.4f, 0.3f));
            scene.objects[fuseDoorIdx].transform = model;

            if (m_lastTime < 26.5f) {
                audio.load("fridge_open"); // Play metal door creak
            }
        } else {
            // Closed position
            glm::mat4 model = glm::mat4(1.0f);
            model = glm::translate(model, glm::vec3(-9.84f, 1.5f, 5.0f));
            model = glm::scale(model, glm::vec3(0.02f, 0.4f, 0.3f));
            scene.objects[fuseDoorIdx].transform = model;
        }
    }

    // --- Scene 3: Kühlschrank Opens & Trip ---
    if (t >= 35.0f && kfrTuerIdx != -1) {
        float angle = std::min(90.0f, (t - 35.0f) * 45.0f);
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, glm::vec3(-2.4f, 1.0f, -8.1f)); // Pivot
        model = glm::rotate(model, glm::radians(-angle), glm::vec3(0.0f, 1.0f, 0.0f));
        
        glm::mat4 doorModel = glm::translate(model, glm::vec3(0.4f, 0.0f, 0.0f));
        doorModel = glm::scale(doorModel, glm::vec3(0.8f, 1.0f, 0.1f));
        scene.objects[kfrTuerIdx].transform = doorModel;

        if (kfrHandleIdx != -1) {
            glm::mat4 handleModel = glm::translate(model, glm::vec3(0.78f, 0.2f, 0.08f));
            handleModel = glm::scale(handleModel, glm::vec3(0.02f, 0.04f, 0.2f));
            scene.objects[kfrHandleIdx].transform = handleModel;
        }
        
        if (m_lastTime < 35.0f) {
            audio.load("fridge_open");
        }
    } else {
        if (kfrTuerIdx != -1) {
            glm::mat4 model = glm::mat4(1.0f);
            model = glm::translate(model, glm::vec3(-2.0f, 1.0f, -8.1f));
            model = glm::scale(model, glm::vec3(0.8f, 1.0f, 0.1f));
            scene.objects[kfrTuerIdx].transform = model;
        }
        if (kfrHandleIdx != -1) {
            glm::mat4 model = glm::mat4(1.0f);
            model = glm::translate(model, glm::vec3(-1.62f, 1.2f, -8.02f));
            model = glm::scale(model, glm::vec3(0.02f, 0.04f, 0.2f));
            scene.objects[kfrHandleIdx].transform = model;
        }
    }

    // Trip sound at 43.0s
    if (m_lastTime < 43.0f && t >= 43.0f) {
        audio.load("trip_bang");
    }

    // --- Animate Walking Legs & Shoes ---
    // If the protagonist is moving, m_walkingTime has accumulated
    static float lastWalkingTime = 0.0f;
    bool isWalking = (m_walkingTime > lastWalkingTime);
    lastWalkingTime = m_walkingTime;

    if (isWalking && t < 42.0f) {
        float cycle = m_walkingTime * 8.0f; // walking frequency

        // Trigger footstep audio synchronized with walking steps
        static float lastCycle = 0.0f;
        if (std::floor(cycle / 3.14159265f) != std::floor(lastCycle / 3.14159265f)) {
            audio.load("footstep");
        }
        lastCycle = cycle;
        glm::vec3 camPos = camera.Position;
        glm::vec3 camFront = camera.Front;
        glm::vec3 camRight = camera.Right;
        glm::vec3 camUp = camera.Up;

        // Rotation matrix to align walking feet with camera orientation
        glm::mat4 rot = glm::mat4(1.0f);
        rot[0] = glm::vec4(camRight, 0.0f);
        rot[1] = glm::vec4(camUp, 0.0f);
        rot[2] = glm::vec4(-camFront, 0.0f);

        // Left leg/foot stride (swings forward on sin(cycle) > 0)
        float swingL = std::sin(cycle);
        float progressL = std::max(0.0f, swingL);
        glm::vec3 legPosL = camPos + camFront * glm::mix(0.1f, 0.6f, progressL) - camUp * glm::mix(0.6f, 0.45f, progressL) - camRight * 0.15f;
        glm::vec3 shoePosL = legPosL + camFront * 0.18f;

        if (walkLegLIdx != -1) {
            glm::mat4 model = glm::mat4(1.0f);
            model = glm::translate(model, legPosL);
            model = model * rot;
            model = glm::scale(model, glm::vec3(0.1f, 0.1f, 0.6f));
            scene.objects[walkLegLIdx].transform = model;
        }
        if (walkShoeLIdx != -1) {
            glm::mat4 model = glm::mat4(1.0f);
            model = glm::translate(model, shoePosL);
            model = model * rot;
            model = glm::scale(model, glm::vec3(0.12f, 0.12f, 0.18f));
            scene.objects[walkShoeLIdx].transform = model;
        }

        // Right leg/foot stride (swings forward on sin(cycle) < 0)
        float swingR = -std::sin(cycle);
        float progressR = std::max(0.0f, swingR);
        glm::vec3 legPosR = camPos + camFront * glm::mix(0.1f, 0.6f, progressR) - camUp * glm::mix(0.6f, 0.45f, progressR) + camRight * 0.15f;
        glm::vec3 shoePosR = legPosR + camFront * 0.18f;

        if (walkLegRIdx != -1) {
            glm::mat4 model = glm::mat4(1.0f);
            model = glm::translate(model, legPosR);
            model = model * rot;
            model = glm::scale(model, glm::vec3(0.1f, 0.1f, 0.6f));
            scene.objects[walkLegRIdx].transform = model;
        }
        if (walkShoeRIdx != -1) {
            glm::mat4 model = glm::mat4(1.0f);
            model = glm::translate(model, shoePosR);
            model = model * rot;
            model = glm::scale(model, glm::vec3(0.12f, 0.12f, 0.18f));
            scene.objects[walkShoeRIdx].transform = model;
        }
    } else {
        // Hide legs and shoes below ground when stationary
        if (walkLegLIdx != -1) scene.objects[walkLegLIdx].transform = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -10.0f, 0.0f));
        if (walkShoeLIdx != -1) scene.objects[walkShoeLIdx].transform = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -10.0f, 0.0f));
        if (walkLegRIdx != -1) scene.objects[walkLegRIdx].transform = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -10.0f, 0.0f));
        if (walkShoeRIdx != -1) scene.objects[walkShoeRIdx].transform = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -10.0f, 0.0f));
    }
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
    float t2 = t * t;
    float t3 = t2 * t;
    return 0.5f * (
        (2.0f * p1) +
        (-p0 + p2) * t +
        (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
        (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3
    );
}
