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
Camera camera(glm::vec3(0.0f, 1.35f, 9.5f));
float  lastX      = SCR_WIDTH  / 2.0f;
float  lastY      = SCR_HEIGHT / 2.0f;
bool   firstMouse = true;
float  deltaTime  = 0.0f;
float  lastFrame  = 0.0f;

// Interactive toggles (ImGui / keyboard)
bool  enableShadows    = true;
bool  enableSoftShadow = true;
bool  enableAO         = true;
bool  enableDOF        = true;
bool  enableMotionBlur = true;
bool  cinematicMode    = false;   // true = play cinematic, false = free camera
bool  uiMode           = false;   // Tab: unlock cursor so ImGui is clickable

// Tonemap / HDR controls
float globalExposure  = 1.0f;
int   globalTonemapOp = 0;        // 0 = ACES, 1 = Reinhard

// Bloom controls
bool  enableBloom      = false;
float bloomThreshold   = 1.0f;
float bloomKnee        = 0.5f;
int   bloomIterations  = 5;
float bloomIntensity   = 0.6f;

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
        case GLFW_KEY_6:      enableBloom      = !enableBloom;        break;
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

// ─── Main ───────────────────────────────────────────────────────────────────
int main() {
    // ── Init GLFW ──────────────────────────────────────────────────────────
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);   // 4.1 = Mac compatible
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

    // "Control" room camera — 35 mm lens, straight-on, centered
    camera.Yaw   = -90.0f;    // look in -Z toward room
    camera.Pitch = -0.30f;    // look-at (0,1.30,0) from (0,1.35,9.5)
    camera.Zoom  = 35.0f;
    camera.ProcessMouseMovement(0.0f, 0.0f);   // commit Yaw/Pitch to Front vector

    glEnable(GL_DEPTH_TEST);

    // ── ImGui ──────────────────────────────────────────────────────────────
#ifdef HAS_IMGUI
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 410");
#endif

    // ── Scene setup ────────────────────────────────────────────────────────
    Renderer        renderer(SCR_WIDTH, SCR_HEIGHT);
    glfwSetWindowUserPointer(window, &renderer);   // lets resize callback reach renderer
    Scene           scene;
    CinematicEngine cinematic;
    AudioManager    audio;

    scene.load("assets/scene.json");       // loads models, lights, etc.
    cinematic.load("assets/cameras.json"); // keyframe data for camera paths
    audio.load("assets/audio.json");       // sound cues

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

        renderer.render(scene, camera, deltaTime);

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
