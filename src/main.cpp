#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <iostream>

#ifdef HAS_IMGUI
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#endif

#include "Camera.h"
#include "Shader.h"
#include "ComputeShader.h"
#include "Model.h"
#include "Renderer.h"
#include "Scene.h"
#include "CinematicEngine.h"
#include "AudioManager.h"
#include "Scene.h"
#include "AudioManager.h"

// ─── Window settings ────────────────────────────────────────────────────────
constexpr int SCR_WIDTH  = 1920;
constexpr int SCR_HEIGHT = 1080;
const char*   TITLE      = "Horror Engine";

// ─── Global state ───────────────────────────────────────────────────────────
Camera camera(glm::vec3(-2.5f, 1.5f, -1.5f));
float  lastX      = SCR_WIDTH  / 2.0f;
float  lastY      = SCR_HEIGHT / 2.0f;
bool   firstMouse = true;
float  deltaTime  = 0.0f;
float  lastFrame  = 0.0f;
float  currentFrame = 0.0f;

// Interactive toggles (ImGui / keyboard)
bool  enableShadows    = true;
bool  enableSoftShadow = true;
bool  enableAO         = true;
bool  enableDOF        = true;
bool  enableMotionBlur = true;
bool  cinematicMode    = false;   // true = play cinematic, false = free camera
bool  uiMode           = false;   // Tab: unlock cursor so ImGui is clickable
bool  saveImage        = true;

// Tonemap / HDR controls
float globalExposure  = 1.0f;
int   globalTonemapOp = 0;        // 0 = ACES, 1 = Reinhard

// Bloom controls
bool  enableBloom      = true;
float bloomThreshold   = 1.0f;
float bloomKnee        = 0.75f;
int   bloomIterations  = 1;
float bloomIntensity   = 0.1f;

// Color grade + vignette
bool  gradeEnable      = true;
float temperature      = 0.0f;
float gradeTint[3]     = {1.0f, 1.0f, 1.0f};
float saturation       = 1.0f;
float shadowLift[3]    = {0.0f, 0.0f, 0.0f};
float vignetteStrength = 0.0f;
float vignetteSoftness = 0.45f;

// Planar floor reflection
bool  enableReflection  = false;
float reflectivity      = 0.25f;
float glossyBlur        = 0.0f;

//Raytracing Config
int numSamples = 128;
int lightSamples = 1; //Keep at 1, raising numSamples is better

//Denoising Config
float sigmaColor = 0.5f;
float sigmaNormal = 0.1f;
float sigmaDepth = 5.0f;

// Chair pose — live-adjust because free models have unpredictable scale/origin
float chairPosX  =  0.3f;
float chairPosY  =  0.0f;
float chairPosZ  =  0.2f;
float chairScale =  1.0f;
float chairYaw   =  180.0f;  // default faces camera; dial with slider

// ─── Callbacks ──────────────────────────────────────────────────────────────
void framebuffer_size_callback(GLFWwindow* w, int width, int height) {
    if (width == 0 || height == 0) return;   // minimized
    glViewport(0, 0, width, height);
    auto* r = static_cast<Renderer*>(glfwGetWindowUserPointer(w));
    if (r) r->resize(width, height);
}

void mouse_callback(GLFWwindow* window, double xposIn, double yposIn) {
    if (cinematicMode || uiMode) return;
    float xpos = static_cast<float>(xposIn);
    float ypos = static_cast<float>(yposIn);
    if (firstMouse) { lastX = xpos; lastY = ypos; firstMouse = false; }
    camera.ProcessMouseMovement(xpos - lastX, lastY - ypos);
    lastX = xpos; lastY = ypos;
}

void scroll_callback(GLFWwindow*, double, double yoffset) {
    if (uiMode) return;
    camera.ProcessMouseScroll(static_cast<float>(yoffset));
}

void key_callback(GLFWwindow* window, int key, int, int action, int) {
    if (action != GLFW_PRESS) return;
    switch (key) {
        case GLFW_KEY_ESCAPE: glfwSetWindowShouldClose(window, true); break;
        case GLFW_KEY_1:      enableShadows    = !enableShadows;    break;
        case GLFW_KEY_2:      enableSoftShadow = !enableSoftShadow; break;
        case GLFW_KEY_3:      enableAO         = !enableAO;         break;
        case GLFW_KEY_4:      enableDOF        = !enableDOF;        break;
        case GLFW_KEY_5:      enableMotionBlur = !enableMotionBlur; break;
        case GLFW_KEY_SPACE:  cinematicMode    = !cinematicMode;    break;
        case GLFW_KEY_T:      globalTonemapOp  = 1 - globalTonemapOp; break;
        case GLFW_KEY_6:      enableBloom       = !enableBloom;        break;
        case GLFW_KEY_7:      enableReflection  = !enableReflection;  break;
        case GLFW_KEY_TAB:
            uiMode = !uiMode;
            glfwSetInputMode(window, GLFW_CURSOR,
                             uiMode ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
            if (!uiMode) firstMouse = true;  // prevent camera jump on re-entry
            break;
    }
}

void processMovement(GLFWwindow* window) {
    if (cinematicMode || uiMode) return;
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) camera.ProcessKeyboard(FORWARD,  deltaTime);
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) camera.ProcessKeyboard(BACKWARD, deltaTime);
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) camera.ProcessKeyboard(LEFT,     deltaTime);
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) camera.ProcessKeyboard(RIGHT,    deltaTime);

    // [ / ] — hold to ramp exposure down / up (2 stops per second)
    constexpr float kExpSpeed = 2.0f;
    if (glfwGetKey(window, GLFW_KEY_LEFT_BRACKET)  == GLFW_PRESS)
        globalExposure = glm::max(0.05f, globalExposure - kExpSpeed * deltaTime);
    if (glfwGetKey(window, GLFW_KEY_RIGHT_BRACKET) == GLFW_PRESS)
        globalExposure = glm::min(20.0f, globalExposure + kExpSpeed * deltaTime);
}

void SetupScene(Scene& scene, CinematicEngine& cinematic, AudioManager& audio, Renderer& renderer, Camera& camera){

    // "Control" room camera — 35 mm lens, straight-on, centered
    camera.Yaw   = 45.0f;    // look in -Z toward room
    camera.Pitch = -10.0f;    // look-at (0,1.30,0) from (0,1.35,9.5)
    camera.Zoom  = 35.0f;
    camera.ProcessMouseMovement(0.0f, 0.0f);   // commit Yaw/Pitch to Front vector

    scene.load("assets/scene.json");       // loads models, lights, etc.
    cinematic.load("assets/cameras.json"); // keyframe data for camera paths
    audio.load("assets/audio.json");       // sound cues

    

    // ── Chair prop ─────────────────────────────────────────────────────────
    // Drop your .obj (+ .mtl + textures) into assets/models/.
    // The AABB printed below tells you the real size and where Y=0 sits
    // relative to the model origin, so you can dial chairScale/chairYaw/chairPos.
    SceneObject chairObj;
    // Sofa.mtl is missing from the folder — Assimp loads geometry fine, falls
    // back to albedo (0.8,0.8,0.8). Drop Sofa.mtl + textures here when available.
    chairObj.model.load("assets/models/Sofa.obj");
    chairObj.model.clearEmissive();
    chairObj.model.loadPBRMaps(
        "assets/models/Textures/Sofa_Normal.png",
        "assets/models/Textures/Sofa_Roughness.png",
        "assets/models/Textures/Sofa_Metallic.png",
        "assets/models/Textures/Sofa_AO.png"
    );

    {
        auto [mn, mx] = chairObj.model.computeAABB();
        glm::vec3 sz  = mx - mn;
        std::cout << "[Chair AABB]  min(" << mn.x << ", " << mn.y << ", " << mn.z << ")"
                  << "  max(" << mx.x << ", " << mx.y << ", " << mx.z << ")\n"
                  << "[Chair AABB]  " << sz.x << "w × " << sz.z << "d × " << sz.y
                  << "h  |  model floor at Y=" << mn.y << "\n";
    }

    scene.objects.push_back(std::move(chairObj));

    renderer.LoadScene(scene);
}

void RenderConfiguration(Renderer& renderer) {
            // Renderer settings driven by interactive toggles
        renderer.settings.shadows    = enableShadows;
        renderer.settings.softShadow = enableSoftShadow;
        renderer.settings.ao         = enableAO;
        renderer.settings.dof        = enableDOF;
        renderer.settings.motionBlur = enableMotionBlur;
        renderer.settings.exposure       = globalExposure;
        renderer.settings.tonemapOp     = globalTonemapOp;
        renderer.settings.bloom           = enableBloom;
        renderer.settings.bloomThreshold  = bloomThreshold;
        renderer.settings.bloomKnee       = bloomKnee;
        renderer.settings.bloomIterations = bloomIterations;
        renderer.settings.bloomIntensity  = bloomIntensity;

        renderer.settings.gradeEnable      = gradeEnable;
        renderer.settings.temperature      = temperature;
        renderer.settings.gradeTint        = glm::vec3(gradeTint[0],    gradeTint[1],    gradeTint[2]);
        renderer.settings.saturation       = saturation;
        renderer.settings.shadowLift       = glm::vec3(shadowLift[0],   shadowLift[1],   shadowLift[2]);
        renderer.settings.vignetteStrength = vignetteStrength;
        renderer.settings.vignetteSoftness = vignetteSoftness;

        renderer.settings.reflection   = enableReflection;
        renderer.settings.reflectivity = reflectivity;
        renderer.settings.glossyBlur   = glossyBlur;

        renderer.settings.numSamples = numSamples;
        renderer.settings.lightSamples = lightSamples;

        renderer.settings.sigmaColor = sigmaColor;
        renderer.settings.sigmaNormal = sigmaNormal;
        renderer.settings.sigmaDepth = sigmaDepth;

}

// ─── Main ───────────────────────────────────────────────────────────────────
int main() {
   
    // ── Init GLFW ──────────────────────────────────────────────────────────
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);   // 4.1 = Mac compatible
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, TITLE, nullptr, nullptr);
    if (!window) {
        std::cerr << "Failed to create GLFW window\n";
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetCursorPosCallback(window,       mouse_callback);
    glfwSetScrollCallback(window,          scroll_callback);
    glfwSetKeyCallback(window,             key_callback);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    // ── Load OpenGL via GLAD ───────────────────────────────────────────────
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "Failed to initialize GLAD\n";
        return -1;
    }
    std::cout << "OpenGL " << glGetString(GL_VERSION)
              << " | " << glGetString(GL_RENDERER) << "\n";


    glEnable(GL_DEPTH_TEST);

    if (saveImage) {

        Scene scene;
        CinematicEngine cinematic;
        AudioManager audio;
        Renderer renderer(SCR_WIDTH, SCR_HEIGHT);

        SetupScene(scene, cinematic, audio, renderer, camera);

        const int kChairIdx = (int)scene.objects.size() - 1;
        RenderConfiguration(renderer);

        // rebuild chair transform...
        renderer.renderRaytracing(scene, camera, deltaTime, currentFrame);

        glFinish();

        renderer.saveRayTracingImage("NoisyImage.png");
        renderer.saveFinalImage("ProcessedImage.png");
        std::cout << "saveImage completed" << std::endl;
        return 0;
}

    // ── ImGui ──────────────────────────────────────────────────────────────
#ifdef HAS_IMGUI
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 410");
#endif

    // ── Scene setup ────────────────────────────────────────────────────────
    
    Scene           scene;
    CinematicEngine cinematic;
    AudioManager    audio;
    Renderer        renderer(SCR_WIDTH, SCR_HEIGHT);
    glfwSetWindowUserPointer(window, &renderer);   // lets resize callback reach renderer

    SetupScene(scene, cinematic, audio, renderer, camera);

    const int kChairIdx = (int)scene.objects.size() - 1;

    // ── Render loop ────────────────────────────────────────────────────────
    while (!glfwWindowShouldClose(window)) {
        float currentFrame = static_cast<float>(glfwGetTime());
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        processMovement(window);

        // Cinematic camera overrides free camera when playing
        if (cinematicMode) {
            cinematic.update(currentFrame, camera);
        }
        RenderConfiguration(renderer);


        // Rebuild chair transform from sliders each frame
        {
            glm::mat4 T = glm::translate(glm::mat4(1.0f),
                                         glm::vec3(chairPosX, chairPosY, chairPosZ));
            glm::mat4 R = glm::rotate(glm::mat4(1.0f),
                                      glm::radians(chairYaw),
                                      glm::vec3(0.0f, 1.0f, 0.0f));
            glm::mat4 S = glm::scale(glm::mat4(1.0f), glm::vec3(chairScale));
            scene.objects[kChairIdx].transform = T * R * S;
        }

        renderer.renderRasterizer(scene, camera, deltaTime, currentFrame);

        // ── ImGui overlay ─────────────────────────────────────────────────
#ifdef HAS_IMGUI
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowSizeConstraints(
            ImVec2(0, 0),
            ImVec2(FLT_MAX, ImGui::GetIO().DisplaySize.y - 20.0f));
        ImGui::Begin("Horror Engine — Controls");
        ImGui::Text("TAB = toggle UI/camera | SPACE = cinematic | WASD = move");
        if (uiMode) ImGui::TextColored(ImVec4(0.4f,1.0f,0.4f,1.0f), "UI MODE — click freely");
        else        ImGui::TextColored(ImVec4(1.0f,0.6f,0.2f,1.0f), "CAMERA MODE — TAB for UI");
        ImGui::Separator();
        ImGui::Checkbox("[1] Shadow mapping",  &enableShadows);
        ImGui::Checkbox("[2] Soft shadows",    &enableSoftShadow);
        ImGui::Checkbox("[3] Ambient occ.",    &enableAO);
        ImGui::Checkbox("[4] Depth of field",  &enableDOF);
        ImGui::Checkbox("[5] Motion blur",     &enableMotionBlur);
        ImGui::Separator();
        ImGui::SliderFloat("[/]] Exposure",     &globalExposure,  0.05f, 20.0f,  "%.2f");
        const char* tonemapNames[] = {"ACES filmic", "Reinhard"};
        ImGui::Combo("[T] Tonemap",             &globalTonemapOp, tonemapNames, 2);
        ImGui::Separator();
        ImGui::Checkbox("[6] Bloom",            &enableBloom);
        ImGui::SliderFloat("Bloom threshold",   &bloomThreshold,  0.5f, 4.0f, "%.2f");
        ImGui::SliderFloat("Bloom knee",        &bloomKnee,       0.0f, 0.5f, "%.2f");
        ImGui::SliderInt  ("Bloom iterations",  &bloomIterations, 1,    10);
        ImGui::SliderFloat("Bloom intensity",   &bloomIntensity,  0.0f, 1.0f, "%.2f");
        ImGui::Separator();
        ImGui::Checkbox("Color grade",          &gradeEnable);
        ImGui::SliderFloat("Temperature",       &temperature,      -1.0f, 1.0f, "%.2f");
        ImGui::ColorEdit3 ("Tint",               gradeTint);
        ImGui::SliderFloat("Saturation",        &saturation,        0.0f, 2.0f, "%.2f");
        ImGui::ColorEdit3 ("Shadow lift",        shadowLift);
        ImGui::SliderFloat("Vignette str",      &vignetteStrength,  0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Vignette soft",     &vignetteSoftness,  0.0f, 1.0f, "%.2f");
        ImGui::Separator();
        ImGui::Checkbox("[7] Floor reflection", &enableReflection);
        ImGui::SliderFloat("Reflectivity",  &reflectivity,  0.0f, 1.0f,  "%.2f");
        ImGui::SliderFloat("Glossy blur",   &glossyBlur,    0.0f, 5.0f,  "%.1f");
        ImGui::Separator();
        if (ImGui::Button("Control preset")) {
            gradeEnable      = true;
            temperature      = -0.1f;
            gradeTint[0]     = 0.93f; gradeTint[1]  = 0.95f; gradeTint[2]  = 1.06f;
            saturation       = 0.9f;
            shadowLift[0]    = 0.02f; shadowLift[1] = 0.01f; shadowLift[2] = 0.04f;
            vignetteStrength = 0.4f;
            vignetteSoftness = 0.45f;
        }
        ImGui::Separator();
        ImGui::Separator();
        ImGui::Text("Chair pose  (AABB printed to console on load)");
        ImGui::SliderFloat("Chair X",     &chairPosX,   -3.0f,  3.0f,   "%.2f");
        ImGui::SliderFloat("Chair Y",     &chairPosY,   -0.5f,  0.5f,   "%.2f");
        ImGui::SliderFloat("Chair Z",     &chairPosZ,   -2.0f,  2.0f,   "%.2f");
        ImGui::SliderFloat("Chair scale", &chairScale,   0.01f,  5.0f,  "%.3f");
        ImGui::SliderFloat("Chair yaw",   &chairYaw,  -180.0f, 180.0f,  "%.1f");
        ImGui::Separator();
        ImGui::Text("Cinematic: %s", cinematicMode ? "PLAYING" : "paused");
        ImGui::Text("%.1f FPS (%.2f ms)", 1.0f/deltaTime, deltaTime*1000.0f);
        ImGui::End();

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
#endif

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // ── Cleanup ────────────────────────────────────────────────────────────
#ifdef HAS_IMGUI
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
#endif
    glfwTerminate();
    return 0;
}
