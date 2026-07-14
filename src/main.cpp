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
#include "RayTracer.h"
#include "GPURayTracer.h"
#include <thread>
#include <string>
#include <chrono>
#include <filesystem>
#include <cstring>
#include <stb_image_write.h>
#include <stb_image.h>

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

// Color grade + vignette
bool  gradeEnable      = false;
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

// Cubemap reflection probe
bool  enableProbe    = true;
float probeStrength  = 1.0f;

// CPU ray tracer (offline)
RayTracer   rayTracer;
RenderTask  g_rtTask;
std::thread g_rtThread;

// RT overlay: shows the last completed RT image in the GL window
GLuint g_rtTex  = 0;
GLuint g_rtProg = 0;
GLuint g_rtVAO  = 0;
bool   g_showRTNormals = false;

// Chair pose — live-adjust because free models have unpredictable scale/origin
float chairPosX  =  0.3f;
float chairPosY  =  0.0f;
float chairPosZ  =  0.2f;
float chairScale =  1.0f;
float chairYaw   =  180.0f;  // default faces camera; dial with slider

// ─── CP4 step-1/2 unit test scaffolding ─────────────────────────────────────
// Minimal box builder for isolated glass unit tests (--gpu-cp4-step1/-step2).
// Vertex normals are set explicitly per face, so winding order doesn't matter
// (the ray tracer interpolates stored normals, not geometric ones).
static Mesh makeTestBox(glm::vec3 lo, glm::vec3 hi, glm::vec3 albedo,
                        float roughness = 0.5f, float metallic = 0.0f) {
    std::vector<Vertex> verts;
    std::vector<unsigned int> idx;
    auto addFace = [&](glm::vec3 n, glm::vec3 v0, glm::vec3 v1, glm::vec3 v2, glm::vec3 v3) {
        unsigned int base = (unsigned int)verts.size();
        Vertex a; a.Position = v0; a.Normal = n; a.TexCoords = {0,0};
        Vertex b; b.Position = v1; b.Normal = n; b.TexCoords = {1,0};
        Vertex c; c.Position = v2; c.Normal = n; c.TexCoords = {1,1};
        Vertex d; d.Position = v3; d.Normal = n; d.TexCoords = {0,1};
        verts.push_back(a); verts.push_back(b); verts.push_back(c); verts.push_back(d);
        idx.push_back(base+0); idx.push_back(base+1); idx.push_back(base+2);
        idx.push_back(base+0); idx.push_back(base+2); idx.push_back(base+3);
    };
    glm::vec3 p000{lo.x,lo.y,lo.z}, p100{hi.x,lo.y,lo.z}, p110{hi.x,hi.y,lo.z}, p010{lo.x,hi.y,lo.z};
    glm::vec3 p001{lo.x,lo.y,hi.z}, p101{hi.x,lo.y,hi.z}, p111{hi.x,hi.y,hi.z}, p011{lo.x,hi.y,hi.z};
    addFace({0,0,1},  p001,p101,p111,p011);  // +Z
    addFace({0,0,-1}, p100,p000,p010,p110);  // -Z
    addFace({1,0,0},  p101,p100,p110,p111);  // +X
    addFace({-1,0,0}, p000,p001,p011,p010);  // -X
    addFace({0,1,0},  p011,p111,p110,p010);  // +Y
    addFace({0,-1,0}, p000,p100,p101,p001);  // -Y

    Mesh m(verts, idx, 0, albedo);
    m.roughness = roughness;
    m.metallic  = metallic;
    return m;
}

// ─── Callbacks ──────────────────────────────────────────────────────────────
// ── Compile a minimal GLSL program from inline source strings ─────────────────
// Used for the RT normals display quad — avoids touching Renderer internals.
static GLuint makeRTQuadProgram() {
    const char* vSrc = R"glsl(
#version 410 core
out vec2 TexCoord;
void main() {
    int vid = gl_VertexID;
    float x = float((vid & 1) << 2) - 1.0;
    float y = float((vid & 2) << 1) - 1.0;
    // Y-flip: our CPU pixels are top-to-bottom; GL expects bottom-to-top
    TexCoord = vec2(x * 0.5 + 0.5, 1.0 - (y * 0.5 + 0.5));
    gl_Position = vec4(x, y, 0.0, 1.0);
}
)glsl";
    const char* fSrc = R"glsl(
#version 410 core
in  vec2 TexCoord;
out vec4 FragColor;
uniform sampler2D uTex;
void main() { FragColor = vec4(texture(uTex, TexCoord).rgb, 1.0); }
)glsl";
    auto compile = [](GLenum type, const char* src) {
        GLuint s = glCreateShader(type);
        glShaderSource(s, 1, &src, nullptr);
        glCompileShader(s);
        GLint ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char buf[512]; glGetShaderInfoLog(s, 512, nullptr, buf);
            std::cerr << "[RT display] " << buf << "\n";
        }
        return s;
    };
    GLuint vs = compile(GL_VERTEX_SHADER,   vSrc);
    GLuint fs = compile(GL_FRAGMENT_SHADER, fSrc);
    GLuint p  = glCreateProgram();
    glAttachShader(p, vs); glAttachShader(p, fs);
    glLinkProgram(p);
    glDeleteShader(vs); glDeleteShader(fs);
    return p;
}

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
        case GLFW_KEY_8:      enableProbe       = !enableProbe;        break;
        case GLFW_KEY_B: {
            auto* r = static_cast<Renderer*>(glfwGetWindowUserPointer(window));
            if (r) r->requestBake();
            break;
        }
        case GLFW_KEY_R: {
            if (g_rtTask.active.load()) {
                // Cancel the running render; keep overlay to show partial result
                g_rtTask.cancel = true;
                if (g_rtThread.joinable()) g_rtThread.join();
                std::cout << "[RT] Cancelled after "
                          << g_rtTask.samplesDone.load() << " pass(es).\n";
            } else if (!g_showRTNormals) {
                // Start a new progressive render in a background thread
                if (!rayTracer.hasScene()) {
                    std::cerr << "[RT] Scene not built yet.\n";
                    break;
                }
                g_rtTask.cancel      = false;
                g_rtTask.active      = true;
                g_rtTask.samplesDone = 0;
                g_rtTask.rowsDone    = 0;
                g_rtTask.bufDirty    = false;
                rayTracer.setBloomSettings({enableBloom, bloomThreshold, bloomKnee,
                                            bloomIterations, bloomIntensity});
                Camera capCam      = camera;
                float  capExposure = globalExposure;
                g_rtThread = std::thread([capCam, capExposure]() {
                    rayTracer.renderBeautyProgressive(
                        capCam, SCR_WIDTH, SCR_HEIGHT,
                        capExposure, 8,          // 8 AA passes; cancel any time with R
                        g_rtTask,
                        "raytrace_beauty.png");
                });
                g_showRTNormals = true;
                std::cout << "[RT] Progressive render started (8 passes). R = cancel.\n";
            } else {
                // Overlay visible, render idle — hide it
                g_showRTNormals = false;
                glfwSetWindowTitle(window, TITLE);
            }
            break;
        }
        // ── RT checkpoint renders (one pass each for quick validation) ────────
        // 9 = checkpoint (b): BRDF+shadows, glass transparent → rt_chkpt_b.png
        // 0 = checkpoint (c): full Fresnel glass reflections   → rt_chkpt_c.png
        case GLFW_KEY_9:
        case GLFW_KEY_0: {
            bool passThrough = (key == GLFW_KEY_9);
            if (g_rtTask.active.load()) {
                g_rtTask.cancel = true;
                if (g_rtThread.joinable()) g_rtThread.join();
            }
            if (!rayTracer.hasScene()) { std::cerr << "[RT] Scene not built.\n"; break; }
            g_rtTask.cancel      = false;
            g_rtTask.active      = true;
            g_rtTask.samplesDone = 0;
            g_rtTask.rowsDone    = 0;
            g_rtTask.bufDirty    = false;
            rayTracer.setBloomSettings({});  // no bloom on checkpoint validation renders
            Camera capCam      = camera;
            float  capExposure = globalExposure;
            std::string outPath = passThrough ? "rt_chkpt_b.png" : "rt_chkpt_c.png";
            g_rtThread = std::thread([capCam, capExposure, passThrough, outPath]() {
                rayTracer.renderBeautyProgressive(
                    capCam, SCR_WIDTH, SCR_HEIGHT,
                    capExposure, 1,        // single pass — fast, enough for validation
                    g_rtTask, outPath,
                    passThrough);
            });
            g_showRTNormals = true;
            std::cout << "[RT] Checkpoint " << (passThrough ? "(b)" : "(c)")
                      << " started → " << outPath << "\n"
                      << "     RT output: flat albedo colors (NO textures), "
                      << (passThrough ? "glass transparent" : "glass reflections") << "\n";
            break;
        }

        // ── F: offline animation render (push-in shot) ───────────────────────
        // Renders AnimConfig::numFrames to frames/frame_XXXX.png, each at full SPP.
        // Edit AnimConfig defaults in RayTracer.h to change the shot.
        // R cancels mid-sequence (already-saved frames are kept).
        case GLFW_KEY_F: {
            if (g_rtTask.active.load()) {
                g_rtTask.cancel = true;
                if (g_rtThread.joinable()) g_rtThread.join();
            }
            if (!rayTracer.hasScene()) { std::cerr << "[Anim] Scene not built.\n"; break; }
            g_rtTask.cancel = false;
            g_rtTask.active = true;
            g_rtTask.samplesDone = 0;
            g_rtTask.rowsDone    = 0;
            g_rtTask.bufDirty    = false;
            g_showRTNormals = false;  // hide RT overlay — animation writes to disk only
            rayTracer.setBloomSettings({enableBloom, bloomThreshold, bloomKnee,
                                        bloomIterations, bloomIntensity});
            float capExposure = globalExposure;
            // AnimConfig is baked into RayTracer.h — user edits and recompiles to change shot.
            // To do a fast preview: set spp=1 and numFrames=5 in AnimConfig, recompile, press F.
            g_rtThread = std::thread([capExposure]() {
                rayTracer.renderAnimation(SCR_WIDTH, SCR_HEIGHT, capExposure, g_rtTask.cancel);
                g_rtTask.active = false;
            });
            glfwSetWindowTitle(window, (std::string(TITLE) + " [Anim: rendering — R to cancel]").c_str());
            std::cout << "[Anim] Started. Progress in console. R to cancel.\n";
            break;
        }

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
int main(int argc, char** argv) {
    bool compareMode   = false;
    bool temporalMode  = false;
    bool recordMode    = false;
    bool rtRecordMode  = false;
    bool gpuCP1Mode    = false;
    bool gpuCP2Mode    = false;
    bool gpuCP4Mode    = false;
    bool gpuCP5Mode    = false;
    bool gpuAnimMode   = false;
    bool gpuCP4Step1   = false;
    bool gpuCP4Step2   = false;
    bool gpuCP4Step25  = false;
    bool gpuCP4Step35  = false;
    bool gpuCP4PixelDiag = false;
    bool gpuCP4StackTrace = false;
    int  temporalSPP   = 4;    // default; override with --temporal 8
    for (int i = 1; i < argc; ++i) {
        std::string a(argv[i]);
        if (a == "--compare")  compareMode  = true;
        if (a == "--record")   recordMode   = true;
        if (a == "--rtrecord") rtRecordMode = true;
        if (a == "--gpu-cp1")  gpuCP1Mode   = true;
        if (a == "--gpu-cp2")  gpuCP2Mode   = true;
        if (a == "--gpu-cp4")  gpuCP4Mode   = true;
        if (a == "--gpu-cp5")  gpuCP5Mode   = true;
        if (a == "--gpu-anim") gpuAnimMode  = true;
        if (a == "--gpu-cp4-step1")   gpuCP4Step1  = true;
        if (a == "--gpu-cp4-step2")   gpuCP4Step2  = true;
        if (a == "--gpu-cp4-step2-5") gpuCP4Step25 = true;
        if (a == "--gpu-cp4-step3-5") gpuCP4Step35 = true;
        if (a == "--gpu-cp4-pixel-diag") gpuCP4PixelDiag = true;
        if (a == "--gpu-cp4-stack-trace") gpuCP4StackTrace = true;
        if (a == "--temporal") {
            temporalMode = true;
            if (i + 1 < argc) {
                int v = std::atoi(argv[i + 1]);
                if (v > 0) { temporalSPP = v; ++i; }
            }
        }
    }

    // ── Init GLFW ──────────────────────────────────────────────────────────
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
#ifdef __APPLE__
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);   // 4.1 = Mac ceiling, no compute shaders
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#else
    // 4.3 = minimum for compute shaders (GPU ray tracer path). Confirmed the NVIDIA
    // Linux driver honors this exactly rather than silently upgrading a 4.1 request.
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
#endif
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    if (compareMode || recordMode || rtRecordMode || gpuCP1Mode || gpuCP2Mode || gpuCP4Mode
        || gpuCP5Mode || gpuAnimMode || gpuCP4Step1 || gpuCP4Step2 || gpuCP4Step25 || gpuCP4Step35
        || gpuCP4PixelDiag || gpuCP4StackTrace)
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);   // headless: context only, no window

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

    // ── RT normals display setup ───────────────────────────────────────────
    g_rtProg = makeRTQuadProgram();
    glGenVertexArrays(1, &g_rtVAO);   // empty VAO; vertex positions come from gl_VertexID
    glGenTextures(1, &g_rtTex);
    glBindTexture(GL_TEXTURE_2D, g_rtTex);
    // Explicitly zero-fill: macOS Metal-backed GL leaves nullptr-init as VRAM
    // garbage (often the previous framebuffer) so the "empty" RT overlay looks
    // like the raster scene. Black is unambiguous.
    {
        std::vector<uint8_t> black(SCR_WIDTH * SCR_HEIGHT * 3, 0);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, SCR_WIDTH, SCR_HEIGHT,
                     0, GL_RGB, GL_UNSIGNED_BYTE, black.data());
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);

    // "Control" room camera — 35 mm lens, straight-on, centered
    camera.Yaw   = -90.0f;    // look in -Z toward room
    camera.Pitch = -0.30f;    // look-at (0,1.30,0) from (0,1.35,9.5)
    camera.Zoom  = 35.0f;
    camera.ProcessMouseMovement(0.0f, 0.0f);   // commit Yaw/Pitch to Front vector

    glEnable(GL_DEPTH_TEST);

    // ── CP4 STEP 1: isolated single-glass-pane unit test ────────────────────
    // Minimal scene, deliberately NOT the real room: one glass pane (a real
    // thin box, front+back faces, same as the real scene's glassPanel()), one
    // opaque backdrop behind it, one light placed on-axis so every dot product
    // in the BRDF is exactly 1 or a clean number — hand-computable. See the
    // conversation for the full derivation (front/back-face ping-pong inside
    // the slab, tint compounding per transmission, depth exhaustion at the
    // 4th glass hit). CPU is the oracle: first check CPU against the hand
    // numbers (sanity on my derivation), then GPU against CPU (the real test).
    if (gpuCP4Step1) {
        std::cout << "\n=== CP4 STEP 1: single glass pane, hand-verified pixels ===\n";

        Scene testScene;
        SceneObject backdrop;
        backdrop.model.addMesh(makeTestBox({-1.0f,-2.0f,-0.05f}, {1.0f,2.0f,0.0f},
                                           {0.8f,0.2f,0.2f}, 0.5f, 0.0f));
        testScene.objects.push_back(std::move(backdrop));

        SceneObject glassPane;
        glassPane.model.addMesh(makeTestBox({-2.0f,-2.0f,0.9f}, {2.0f,2.0f,1.0f},
                                            {0.0f,0.0f,0.0f}));
        testScene.glassObjects.push_back(std::move(glassPane));

        RayTracer testRT;
        testRT.buildScene(testScene);
        std::vector<RTLight> testLights = { { {0.0f,0.0f,5.0f}, {1.0f,1.0f,1.0f}, 5.0f } };
        const float testLightRadius = 10.0f;
        testRT.setLights(testLights, testLightRadius);

        const int TW = 101, TH = 101;  // odd -> exact center pixel at (50,50), ndcX=ndcY=0
        const int cx = TW / 2, cy = TH / 2;

        Camera cam1({0.0f, 0.0f, 2.0f});  // center ray: hits backdrop through glass
        Camera cam2({1.5f, 0.0f, 2.0f});  // offset: glass hit, backdrop missed -> background

        auto sample = [&](const std::vector<uint8_t>& pix, int x, int y) {
            int i = (y * TW + x) * 3;
            return glm::ivec3(pix[i], pix[i+1], pix[i+2]);
        };

        std::vector<uint8_t> cpuPix1, cpuPix2;
        testRT.renderBeauty(cam1, TW, TH, "unit_cpu_pixel1.png", &cpuPix1, 1.0f, false);
        testRT.renderBeauty(cam2, TW, TH, "unit_cpu_pixel2.png", &cpuPix2, 1.0f, false);
        glm::ivec3 cpu1 = sample(cpuPix1, cx, cy);
        glm::ivec3 cpu2 = sample(cpuPix2, cx, cy);

        // Hand-derived (see conversation): pixel1 hits backdrop via the
        // straight-through leaf (weight=(1-F)^2, tint=T^2) plus 4 background
        // leaves from the front/back-face ping-pong; pixel2 is the same tree
        // with the backdrop leaf replaced by background too.
        const glm::ivec3 hand1(206, 156, 173);
        const glm::ivec3 hand2(24, 31, 52);

        std::cout << "Pixel1 (center, backdrop through glass):\n"
                  << "  CPU=(" << cpu1.x << "," << cpu1.y << "," << cpu1.z << ")"
                  << "  hand=(" << hand1.x << "," << hand1.y << "," << hand1.z << ")"
                  << "  |CPU-hand|=(" << std::abs(cpu1.x-hand1.x) << "," << std::abs(cpu1.y-hand1.y)
                  << "," << std::abs(cpu1.z-hand1.z) << ")\n";
        std::cout << "Pixel2 (offset, background through glass):\n"
                  << "  CPU=(" << cpu2.x << "," << cpu2.y << "," << cpu2.z << ")"
                  << "  hand=(" << hand2.x << "," << hand2.y << "," << hand2.z << ")"
                  << "  |CPU-hand|=(" << std::abs(cpu2.x-hand2.x) << "," << std::abs(cpu2.y-hand2.y)
                  << "," << std::abs(cpu2.z-hand2.z) << ")\n";

        GPURayTracer gpuRT;
        if (!gpuRT.available()) {
            std::cout << "GPU path unavailable on this context.\n";
            return 0;
        }
        auto exportData = testRT.exportForGPU();
        gpuRT.upload(exportData);

        std::vector<uint8_t> gpuPix1, gpuPix2;
        gpuRT.renderGlassGPU(cam1, TW, TH, 1.0f, testLights, testLightRadius, gpuPix1, nullptr);
        gpuRT.renderGlassGPU(cam2, TW, TH, 1.0f, testLights, testLightRadius, gpuPix2, nullptr);
        glm::ivec3 gpu1 = sample(gpuPix1, cx, cy);
        glm::ivec3 gpu2 = sample(gpuPix2, cx, cy);

        std::cout << "\nPixel1: GPU=(" << gpu1.x << "," << gpu1.y << "," << gpu1.z << ")"
                  << "  |CPU-GPU|=(" << std::abs(cpu1.x-gpu1.x) << "," << std::abs(cpu1.y-gpu1.y)
                  << "," << std::abs(cpu1.z-gpu1.z) << ")\n";
        std::cout << "Pixel2: GPU=(" << gpu2.x << "," << gpu2.y << "," << gpu2.z << ")"
                  << "  |CPU-GPU|=(" << std::abs(cpu2.x-gpu2.x) << "," << std::abs(cpu2.y-gpu2.y)
                  << "," << std::abs(cpu2.z-gpu2.z) << ")\n";

        std::cout << "\n=== CP4 STEP 1 done. ===\n";
        return 0;
    }

    // ── CP4 STEP 2: two glass panes in sequence, hand-verified ─────────────
    // paneA (near, Z=[1.4,1.5]) -> 0.4-unit open gap -> paneB (far, Z=[0.9,1.0])
    // -> backdrop (Z=[-0.05,0]). Depth budget is 4; each physical pane costs 2
    // hits (front+back face), so a straight-through ray spends its ENTIRE
    // budget crossing both panes and can never reach the backdrop — every leaf
    // resolves to background. This is the exact tight-budget mechanism
    // CP4_GLASS_NOTES flagged. Full hand-derived tree: 8 leaves (vs step 1's
    // 5), tint compounding T^0..T^4 across paths — see conversation.
    if (gpuCP4Step2) {
        std::cout << "\n=== CP4 STEP 2: two glass panes, hand-verified pixels ===\n";

        Scene testScene;
        SceneObject backdrop;
        backdrop.model.addMesh(makeTestBox({-1.0f,-2.0f,-0.05f}, {1.0f,2.0f,0.0f},
                                           {0.8f,0.2f,0.2f}, 0.5f, 0.0f));
        testScene.objects.push_back(std::move(backdrop));

        SceneObject paneA;  // near
        paneA.model.addMesh(makeTestBox({-2.0f,-2.0f,1.4f}, {2.0f,2.0f,1.5f}, {0,0,0}));
        testScene.glassObjects.push_back(std::move(paneA));

        SceneObject paneB;  // far
        paneB.model.addMesh(makeTestBox({-2.0f,-2.0f,0.9f}, {2.0f,2.0f,1.0f}, {0,0,0}));
        testScene.glassObjects.push_back(std::move(paneB));

        RayTracer testRT;
        testRT.buildScene(testScene);
        std::vector<RTLight> testLights = { { {0.0f,0.0f,5.0f}, {1.0f,1.0f,1.0f}, 5.0f } };
        const float testLightRadius = 10.0f;
        testRT.setLights(testLights, testLightRadius);

        const int TW = 101, TH = 101;
        const int cx = TW / 2, cy = TH / 2;

        Camera cam1({0.0f, 0.0f, 2.0f});  // center: crosses both panes, backdrop unreachable
        Camera cam2({5.0f, 0.0f, 2.0f});  // misses both panes entirely: pure background control

        auto sample = [&](const std::vector<uint8_t>& pix, int x, int y) {
            int i = (y * TW + x) * 3;
            return glm::ivec3(pix[i], pix[i+1], pix[i+2]);
        };

        std::vector<uint8_t> cpuPix1, cpuPix2;
        testRT.renderBeauty(cam1, TW, TH, "unit2_cpu_pixel1.png", &cpuPix1, 1.0f, false);
        testRT.renderBeauty(cam2, TW, TH, "unit2_cpu_pixel2.png", &cpuPix2, 1.0f, false);
        glm::ivec3 cpu1 = sample(cpuPix1, cx, cy);
        glm::ivec3 cpu2 = sample(cpuPix2, cx, cy);

        const glm::ivec3 hand1(19, 27, 52);  // both panes crossed, backdrop unreached -> background, 8-leaf tint mix
        const glm::ivec3 hand2(32, 37, 52);  // pure background control, no glass hit at all

        std::cout << "Pixel1 (crosses both panes, backdrop unreachable within depth budget):\n"
                  << "  CPU=(" << cpu1.x << "," << cpu1.y << "," << cpu1.z << ")"
                  << "  hand=(" << hand1.x << "," << hand1.y << "," << hand1.z << ")"
                  << "  |CPU-hand|=(" << std::abs(cpu1.x-hand1.x) << "," << std::abs(cpu1.y-hand1.y)
                  << "," << std::abs(cpu1.z-hand1.z) << ")\n";
        std::cout << "Pixel2 (misses both panes, pure background control):\n"
                  << "  CPU=(" << cpu2.x << "," << cpu2.y << "," << cpu2.z << ")"
                  << "  hand=(" << hand2.x << "," << hand2.y << "," << hand2.z << ")"
                  << "  |CPU-hand|=(" << std::abs(cpu2.x-hand2.x) << "," << std::abs(cpu2.y-hand2.y)
                  << "," << std::abs(cpu2.z-hand2.z) << ")\n";

        GPURayTracer gpuRT;
        if (!gpuRT.available()) {
            std::cout << "GPU path unavailable on this context.\n";
            return 0;
        }
        auto exportData = testRT.exportForGPU();
        gpuRT.upload(exportData);

        std::vector<uint8_t> gpuPix1, gpuPix2;
        gpuRT.renderGlassGPU(cam1, TW, TH, 1.0f, testLights, testLightRadius, gpuPix1, nullptr);
        gpuRT.renderGlassGPU(cam2, TW, TH, 1.0f, testLights, testLightRadius, gpuPix2, nullptr);
        glm::ivec3 gpu1 = sample(gpuPix1, cx, cy);
        glm::ivec3 gpu2 = sample(gpuPix2, cx, cy);

        std::cout << "\nPixel1: GPU=(" << gpu1.x << "," << gpu1.y << "," << gpu1.z << ")"
                  << "  |CPU-GPU|=(" << std::abs(cpu1.x-gpu1.x) << "," << std::abs(cpu1.y-gpu1.y)
                  << "," << std::abs(cpu1.z-gpu1.z) << ")\n";
        std::cout << "Pixel2: GPU=(" << gpu2.x << "," << gpu2.y << "," << gpu2.z << ")"
                  << "  |CPU-GPU|=(" << std::abs(cpu2.x-gpu2.x) << "," << std::abs(cpu2.y-gpu2.y)
                  << "," << std::abs(cpu2.z-gpu2.z) << ")\n";

        std::cout << "\n=== CP4 STEP 2 done. ===\n";
        return 0;
    }

    // ── CP4 STEP 2.5: competing paths of different lengths under truncation ─
    // Same paneA/paneB ping-pong as step 2, but a second opaque surface
    // ("backdrop2") sits in the path of paneA's FIRST reflection, so that leaf
    // resolves to real shaded geometry after just 1 glass hit — while the
    // other 7 leaves (identical to step 2's tree, just re-verified here) still
    // require up to 4 hits and get truncated to background. This is the
    // asymmetric-competing-paths case: a short-lived branch reaching real
    // content coexists with several deep branches hitting the depth wall.
    // Tests whether GPU's explicit stack keeps/truncates the same set of
    // leaves as CPU's recursion, independent of processing order.
    if (gpuCP4Step25) {
        std::cout << "\n=== CP4 STEP 2.5: competing paths under depth truncation ===\n";

        Scene testScene;
        SceneObject backdrop;
        backdrop.model.addMesh(makeTestBox({-1.0f,-2.0f,-0.05f}, {1.0f,2.0f,0.0f},
                                           {0.8f,0.2f,0.2f}, 0.5f, 0.0f));
        testScene.objects.push_back(std::move(backdrop));

        // Catches paneA's first-bounce reflection. NdotL<=0 for the on-axis
        // light from this face's orientation, so Lo = emissive only — clean,
        // unambiguous, distinguishable-from-background leaf value.
        SceneObject backdrop2;
        {
            Mesh m2 = makeTestBox({-2.0f,-2.0f,3.9f}, {2.0f,2.0f,4.0f}, {0.3f,0.3f,0.3f}, 0.5f, 0.0f);
            m2.emissiveColor    = glm::vec3(0.5f, 0.3f, 0.1f);
            m2.emissiveStrength = 1.0f;
            backdrop2.model.addMesh(m2);
        }
        testScene.objects.push_back(std::move(backdrop2));

        SceneObject paneA;
        paneA.model.addMesh(makeTestBox({-2.0f,-2.0f,1.9f}, {2.0f,2.0f,2.0f}, {0,0,0}));
        testScene.glassObjects.push_back(std::move(paneA));

        SceneObject paneB;
        paneB.model.addMesh(makeTestBox({-2.0f,-2.0f,1.4f}, {2.0f,2.0f,1.5f}, {0,0,0}));
        testScene.glassObjects.push_back(std::move(paneB));

        RayTracer testRT;
        testRT.buildScene(testScene);
        std::vector<RTLight> testLights = { { {0.0f,0.0f,5.0f}, {1.0f,1.0f,1.0f}, 5.0f } };
        const float testLightRadius = 10.0f;
        testRT.setLights(testLights, testLightRadius);

        const int TW = 101, TH = 101;
        const int cx = TW / 2, cy = TH / 2;

        Camera cam1({0.0f, 0.0f, 2.5f});  // between paneA and backdrop2: crosses both panes,
                                          // AND catches paneA's reflection on backdrop2
        Camera cam2({5.0f, 0.0f, 2.5f});  // misses everything: pure background control

        auto sample = [&](const std::vector<uint8_t>& pix, int x, int y) {
            int i = (y * TW + x) * 3;
            return glm::ivec3(pix[i], pix[i+1], pix[i+2]);
        };

        std::vector<uint8_t> cpuPix1, cpuPix2;
        testRT.renderBeauty(cam1, TW, TH, "unit25_cpu_pixel1.png", &cpuPix1, 1.0f, false);
        testRT.renderBeauty(cam2, TW, TH, "unit25_cpu_pixel2.png", &cpuPix2, 1.0f, false);
        glm::ivec3 cpu1 = sample(cpuPix1, cx, cy);
        glm::ivec3 cpu2 = sample(cpuPix2, cx, cy);

        // Hand-derived: identical to step 2's tree except the F-weighted,
        // untinted leaf (was background) is now backdrop2's emissive color.
        const glm::ivec3 hand1(41, 39, 54);
        const glm::ivec3 hand2(32, 37, 52);  // same background control as step 2

        std::cout << "Pixel1 (paneA reflection hits backdrop2, transmit path truncated as in step 2):\n"
                  << "  CPU=(" << cpu1.x << "," << cpu1.y << "," << cpu1.z << ")"
                  << "  hand=(" << hand1.x << "," << hand1.y << "," << hand1.z << ")"
                  << "  |CPU-hand|=(" << std::abs(cpu1.x-hand1.x) << "," << std::abs(cpu1.y-hand1.y)
                  << "," << std::abs(cpu1.z-hand1.z) << ")\n";
        std::cout << "Pixel2 (misses everything, pure background control):\n"
                  << "  CPU=(" << cpu2.x << "," << cpu2.y << "," << cpu2.z << ")"
                  << "  hand=(" << hand2.x << "," << hand2.y << "," << hand2.z << ")"
                  << "  |CPU-hand|=(" << std::abs(cpu2.x-hand2.x) << "," << std::abs(cpu2.y-hand2.y)
                  << "," << std::abs(cpu2.z-hand2.z) << ")\n";

        GPURayTracer gpuRT;
        if (!gpuRT.available()) {
            std::cout << "GPU path unavailable on this context.\n";
            return 0;
        }
        auto exportData = testRT.exportForGPU();
        gpuRT.upload(exportData);

        std::vector<uint8_t> gpuPix1, gpuPix2;
        gpuRT.renderGlassGPU(cam1, TW, TH, 1.0f, testLights, testLightRadius, gpuPix1, nullptr);
        gpuRT.renderGlassGPU(cam2, TW, TH, 1.0f, testLights, testLightRadius, gpuPix2, nullptr);
        glm::ivec3 gpu1 = sample(gpuPix1, cx, cy);
        glm::ivec3 gpu2 = sample(gpuPix2, cx, cy);

        std::cout << "\nPixel1: GPU=(" << gpu1.x << "," << gpu1.y << "," << gpu1.z << ")"
                  << "  |CPU-GPU|=(" << std::abs(cpu1.x-gpu1.x) << "," << std::abs(cpu1.y-gpu1.y)
                  << "," << std::abs(cpu1.z-gpu1.z) << ")\n";
        std::cout << "Pixel2: GPU=(" << gpu2.x << "," << gpu2.y << "," << gpu2.z << ")"
                  << "  |CPU-GPU|=(" << std::abs(cpu2.x-gpu2.x) << "," << std::abs(cpu2.y-gpu2.y)
                  << "," << std::abs(cpu2.z-gpu2.z) << ")\n";

        // Isolation check: look at backdrop2's near face (Z=3.9) directly,
        // no glass in the way, to verify the hand assumption about its shaded
        // color without any glass-recursion confound.
        {
            Camera cam3({0.0f, 0.0f, 3.5f});
            cam3.Yaw = 90.0f;               // Front becomes (0,0,1): faces +Z, toward backdrop2
            cam3.ProcessMouseMovement(0.0f, 0.0f);
            std::vector<uint8_t> cpuPix3;
            testRT.renderBeauty(cam3, TW, TH, "unit25_cpu_isolation.png", &cpuPix3, 1.0f, false);
            glm::ivec3 cpu3 = sample(cpuPix3, cx, cy);
            std::cout << "\n[Isolation] backdrop2 near-face direct (no glass): CPU=("
                      << cpu3.x << "," << cpu3.y << "," << cpu3.z << ")"
                      << "  hand-if-Lo-is-emissive-only(0.5,0.3,0.1)=(204,175,99)\n";
        }

        std::cout << "\n=== CP4 STEP 2.5 done. ===\n";
        return 0;
    }

    // ── CP4 STEP 3.5: LIT opaque surface reached via glass REFLECTION ──────
    // Steps 1-2.5 never tested this: step 1's opaque hit was reached via
    // TRANSMISSION (direction preserved) with NdotL>0 (works). Step 2.5's
    // reflection-reached opaque hit was deliberately NdotL<=0 (emissive-only)
    // so it couldn't distinguish "correctly negative" from "incorrectly
    // negative." This test isolates exactly that gap: a LIT opaque surface
    // (genuine NdotL>0 expected) reached via the REFLECTED ray specifically,
    // where V must be the bounced ray direction, not the primary camera ray.
    if (gpuCP4Step35) {
        std::cout << "\n=== CP4 STEP 3.5: lit opaque surface via glass reflection ===\n";

        Scene testScene;
        SceneObject mirrorTarget;
        mirrorTarget.model.addMesh(makeTestBox({-2.0f,-2.0f,3.9f}, {2.0f,2.0f,4.0f},
                                               {0.3f,0.6f,0.9f}, 0.5f, 0.0f));
        testScene.objects.push_back(std::move(mirrorTarget));

        SceneObject glassPane;
        glassPane.model.addMesh(makeTestBox({-2.0f,-2.0f,0.9f}, {2.0f,2.0f,1.0f}, {0,0,0}));
        testScene.glassObjects.push_back(std::move(glassPane));

        RayTracer testRT;
        testRT.buildScene(testScene);
        // Light positioned BELOW mirror_target's Z (between glass and target) so its
        // near face (outward normal -Z) gets genuine NdotL>0 — not the emissive-only
        // trick from step 2.5.
        std::vector<RTLight> testLights = { { {0.0f,0.0f,3.5f}, {1.0f,1.0f,1.0f}, 5.0f } };
        const float testLightRadius = 10.0f;
        testRT.setLights(testLights, testLightRadius);

        const int TW = 101, TH = 101;
        const int cx = TW / 2, cy = TH / 2;

        auto sample = [&](const std::vector<uint8_t>& pix, int x, int y) {
            int i = (y * TW + x) * 3;
            return glm::ivec3(pix[i], pix[i+1], pix[i+2]);
        };

        // Isolation check: mirror_target's own shading, no glass, direct view.
        {
            Camera camIso({0.0f, 0.0f, 3.5f});
            camIso.Yaw = 90.0f;  // Front -> (0,0,1), faces +Z toward mirror_target
            camIso.ProcessMouseMovement(0.0f, 0.0f);
            std::vector<uint8_t> cpuPixIso;
            testRT.renderBeauty(camIso, TW, TH, "unit35_cpu_isolation.png", &cpuPixIso, 1.0f, false);
            glm::ivec3 cpuIso = sample(cpuPixIso, cx, cy);
            std::cout << "[Isolation] mirror_target direct (no glass): CPU=("
                      << cpuIso.x << "," << cpuIso.y << "," << cpuIso.z
                      << ")  hand-predicted-Lo-only(no-tonemap-composition)=n/a, see pixel1 below for full chain\n";
        }

        Camera cam1({0.0f, 0.0f, 2.0f});  // through glass: reflect->mirror_target, transmit->ping-pong->bg

        std::vector<uint8_t> cpuPix1;
        testRT.renderBeauty(cam1, TW, TH, "unit35_cpu_pixel1.png", &cpuPix1, 1.0f, false);
        glm::ivec3 cpu1 = sample(cpuPix1, cx, cy);

        const glm::ivec3 hand1(53, 74, 101);

        std::cout << "Pixel1 (glass reflection -> lit mirror_target, glass transmission -> background):\n"
                  << "  CPU=(" << cpu1.x << "," << cpu1.y << "," << cpu1.z << ")"
                  << "  hand=(" << hand1.x << "," << hand1.y << "," << hand1.z << ")"
                  << "  |CPU-hand|=(" << std::abs(cpu1.x-hand1.x) << "," << std::abs(cpu1.y-hand1.y)
                  << "," << std::abs(cpu1.z-hand1.z) << ")\n";

        GPURayTracer gpuRT;
        if (!gpuRT.available()) {
            std::cout << "GPU path unavailable on this context.\n";
            return 0;
        }
        auto exportData = testRT.exportForGPU();
        gpuRT.upload(exportData);

        std::vector<uint8_t> gpuPix1;
        gpuRT.renderGlassGPU(cam1, TW, TH, 1.0f, testLights, testLightRadius, gpuPix1, nullptr);
        glm::ivec3 gpu1 = sample(gpuPix1, cx, cy);

        std::cout << "\nPixel1: GPU=(" << gpu1.x << "," << gpu1.y << "," << gpu1.z << ")"
                  << "  |CPU-GPU|=(" << std::abs(cpu1.x-gpu1.x) << "," << std::abs(cpu1.y-gpu1.y)
                  << "," << std::abs(cpu1.z-gpu1.z) << ")\n";

        std::cout << "\n=== CP4 STEP 3.5 done. ===\n";
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
    Renderer        renderer(SCR_WIDTH, SCR_HEIGHT);
    glfwSetWindowUserPointer(window, &renderer);   // lets resize callback reach renderer
    Scene           scene;
    CinematicEngine cinematic;
    AudioManager    audio;

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
    // CPU texture for ray tracer — bilinear-sampled at hit UV, sRGB→linear.
    // The scalar teal is kept as a fallback if the file is missing.
    chairObj.model.setAlbedoColor(glm::vec3(0.05f, 0.20f, 0.25f));
    chairObj.model.loadCPUAlbedo("assets/models/Textures/Sofa_baseColor.png");

    {
        auto [mn, mx] = chairObj.model.computeAABB();
        glm::vec3 sz  = mx - mn;
        std::cout << "[Chair AABB]  min(" << mn.x << ", " << mn.y << ", " << mn.z << ")"
                  << "  max(" << mx.x << ", " << mx.y << ", " << mx.z << ")\n"
                  << "[Chair AABB]  " << sz.x << "w × " << sz.z << "d × " << sz.y
                  << "h  |  model floor at Y=" << mn.y << "\n";
    }

    scene.objects.push_back(std::move(chairObj));
    const int kChairIdx = (int)scene.objects.size() - 1;

    // Build BVH over the complete scene (opaque + glass objects)
    rayTracer.buildScene(scene);

    // Wire up ceiling lights — identical constants to kCeilLights in Renderer.cpp
    const std::vector<RTLight> kCeilLightsRT = {
        { glm::vec3(-1.5f, 2.9f,  1.0f), glm::vec3(1.0f, 0.95f, 0.85f), 5.0f },
        { glm::vec3( 1.5f, 2.9f,  1.0f), glm::vec3(1.0f, 0.95f, 0.85f), 5.0f },
        { glm::vec3(-1.5f, 2.9f, -1.0f), glm::vec3(1.0f, 0.95f, 0.85f), 5.0f },
        { glm::vec3( 1.5f, 2.9f, -1.0f), glm::vec3(1.0f, 0.95f, 0.85f), 5.0f },
    };
    const float kCeilLightRadius = 8.0f;
    rayTracer.setLights(kCeilLightsRT, kCeilLightRadius);

    // ── GPU CP1: BVH traversal validation (normals-only, no shading) ───────
    // Compares GPURayTracer's compute-shader BVH walk against the CPU reference
    // renderNormals() at full 1920x1080 (same res as the known "2hr" CPU baseline)
    // so the timing ratio is directly comparable. Two cameras: the wide
    // establishing shot (lots of glass wall / frame geometry) and the close-up
    // chair shot (compareMode's camera) to stress different parts of the BVH.
    if (gpuCP1Mode) {
        struct TestCam { const char* name; glm::vec3 pos; glm::vec3 lookAt; };
        const TestCam testCams[] = {
            { "wide",  {0.0f, 1.55f, 6.5f}, {0.3f, 0.65f, 0.2f} },  // AnimConfig startPos
            { "close", {0.3f, 1.15f, 2.8f}, {0.3f, 0.65f, 0.2f} },  // AnimConfig endPos / --compare cam
        };

        GPURayTracer gpuRT;
        std::cout << "\n=== CP1: GPU BVH traversal vs CPU reference ===\n";
        if (!gpuRT.available()) {
            std::cout << "GPU path unavailable on this context (need GL 4.3+ compute shaders). "
                       << "CPU path is unaffected.\n";
            return 0;
        }

        auto exportData = rayTracer.exportForGPU();
        gpuRT.upload(exportData);
        std::cout << "Uploaded " << exportData.nodeCount << " BVH nodes, "
                   << exportData.triCount << " triangles to GPU.\n\n";

        for (const auto& tc : testCams) {
            Camera cam(tc.pos);
            cam.SetPose(tc.pos, tc.lookAt - tc.pos);
            cam.Zoom = 35.0f;

            std::vector<uint8_t> cpuPixels;
            auto ct0 = std::chrono::steady_clock::now();
            rayTracer.renderNormals(cam, SCR_WIDTH, SCR_HEIGHT,
                                    std::string("cp1_cpu_") + tc.name + ".png", &cpuPixels);
            double cpuSec = std::chrono::duration<double>(
                                std::chrono::steady_clock::now() - ct0).count();

            std::vector<uint8_t> gpuPixels;
            double gpuSec = 0.0;
            gpuRT.renderNormalsGPU(cam, SCR_WIDTH, SCR_HEIGHT, gpuPixels, &gpuSec);
            stbi_write_png((std::string("cp1_gpu_") + tc.name + ".png").c_str(),
                          SCR_WIDTH, SCR_HEIGHT, 3, gpuPixels.data(), SCR_WIDTH * 3);

            // Diff stats: per-channel abs difference, tolerance-based match rate.
            size_t nBytes = cpuPixels.size();
            uint64_t sumAbsDiff = 0;
            int      maxAbsDiff = 0;
            size_t   within2    = 0;  // pixels where every channel differs by <=2/255
            for (size_t i = 0; i < nBytes; i += 3) {
                int matched = 1;
                for (int c = 0; c < 3; ++c) {
                    int d = std::abs((int)cpuPixels[i+c] - (int)gpuPixels[i+c]);
                    sumAbsDiff += (uint64_t)d;
                    if (d > maxAbsDiff) maxAbsDiff = d;
                    if (d > 2) matched = 0;
                }
                within2 += (size_t)matched;
            }
            size_t nPixels = nBytes / 3;
            double meanAbsDiff = (double)sumAbsDiff / (double)nBytes;
            double matchPct    = 100.0 * (double)within2 / (double)nPixels;

            // Side-by-side composite (CPU | thin divider | GPU).
            {
                int gap = 4;
                int cw  = SCR_WIDTH * 2 + gap;
                std::vector<uint8_t> combo((size_t)cw * SCR_HEIGHT * 3, 40);
                for (int y = 0; y < SCR_HEIGHT; ++y) {
                    std::memcpy(&combo[(size_t)y*cw*3], &cpuPixels[(size_t)y*SCR_WIDTH*3], SCR_WIDTH*3);
                    std::memcpy(&combo[(size_t)y*cw*3 + (SCR_WIDTH+gap)*3],
                               &gpuPixels[(size_t)y*SCR_WIDTH*3], SCR_WIDTH*3);
                }
                stbi_write_png((std::string("cp1_sidebyside_") + tc.name + ".png").c_str(),
                              cw, SCR_HEIGHT, 3, combo.data(), cw * 3);
            }

            std::cout << "[" << tc.name << "]  CPU: " << cpuSec << " s   GPU: " << gpuSec << " s"
                       << "   speedup: " << (cpuSec / std::max(gpuSec, 1e-6)) << "x\n"
                      << "         match(<=2/255 all channels): " << matchPct << "%"
                      << "   mean|diff|: " << meanAbsDiff << "/255   max|diff|: " << maxAbsDiff << "/255\n"
                      << "         wrote cp1_cpu_" << tc.name << ".png, cp1_gpu_" << tc.name
                      << ".png, cp1_sidebyside_" << tc.name << ".png\n\n";
        }

        std::cout << "=== CP1 done. Open the cp1_sidebyside_*.png files. ===\n";
        return 0;
    }

    // ── GPU CP2: BRDF + shadow rays vs CPU checkpoint (b) ───────────────────
    // CPU reference is RayTracer::renderBeauty(glassPassThrough=true) — this is
    // the ALREADY 24-thread-parallel path (unlike CP1's renderNormals, which is
    // single-threaded), so this speedup number is a true apples-to-apples GPU
    // vs. threaded-CPU comparison, no /24 correction needed.
    if (gpuCP2Mode) {
        struct TestCam { const char* name; glm::vec3 pos; glm::vec3 lookAt; };
        const TestCam testCams[] = {
            { "wide",  {0.0f, 1.55f, 6.5f}, {0.3f, 0.65f, 0.2f} },
            { "close", {0.3f, 1.15f, 2.8f}, {0.3f, 0.65f, 0.2f} },
        };

        GPURayTracer gpuRT;
        std::cout << "\n=== CP2: GPU shaded (BRDF+shadows, no reflections/glass) vs CPU checkpoint (b) ===\n";
        if (!gpuRT.available()) {
            std::cout << "GPU path unavailable on this context. CPU path is unaffected.\n";
            return 0;
        }

        auto exportData = rayTracer.exportForGPU();
        gpuRT.upload(exportData);
        std::cout << "Uploaded " << exportData.materialCount << " materials, "
                   << (exportData.albedoPixelsRGBA ? "1" : "0") << " albedo texture ("
                   << exportData.albedoW << "x" << exportData.albedoH << ").\n\n";

        for (const auto& tc : testCams) {
            Camera cam(tc.pos);
            cam.SetPose(tc.pos, tc.lookAt - tc.pos);
            cam.Zoom = 35.0f;

            std::vector<uint8_t> cpuPixels;
            auto ct0 = std::chrono::steady_clock::now();
            rayTracer.renderBeauty(cam, SCR_WIDTH, SCR_HEIGHT,
                                   std::string("cp2_cpu_") + tc.name + ".png",
                                   &cpuPixels, globalExposure, /*glassPassThrough=*/true);
            double cpuSec = std::chrono::duration<double>(
                                std::chrono::steady_clock::now() - ct0).count();

            std::vector<uint8_t> gpuPixels;
            double gpuSec = 0.0;
            gpuRT.renderShadedGPU(cam, SCR_WIDTH, SCR_HEIGHT, globalExposure,
                                  kCeilLightsRT, kCeilLightRadius, gpuPixels, &gpuSec);
            stbi_write_png((std::string("cp2_gpu_") + tc.name + ".png").c_str(),
                          SCR_WIDTH, SCR_HEIGHT, 3, gpuPixels.data(), SCR_WIDTH * 3);

            // Diff stats at several tolerances — shading involves float
            // order-of-operations differences (GPU vs CPU FMA/transcendental
            // paths) so this won't be bit-exact like CP1; "within a few LSBs"
            // is the expected/acceptable outcome, not 0/255.
            size_t nBytes = cpuPixels.size();
            uint64_t sumAbsDiff = 0;
            int      maxAbsDiff = 0;
            size_t   within2 = 0, within8 = 0, within16 = 0;
            for (size_t i = 0; i < nBytes; i += 3) {
                int m2 = 1, m8 = 1, m16 = 1;
                for (int c = 0; c < 3; ++c) {
                    int d = std::abs((int)cpuPixels[i+c] - (int)gpuPixels[i+c]);
                    sumAbsDiff += (uint64_t)d;
                    if (d > maxAbsDiff) maxAbsDiff = d;
                    if (d > 2)  m2  = 0;
                    if (d > 8)  m8  = 0;
                    if (d > 16) m16 = 0;
                }
                within2  += (size_t)m2;
                within8  += (size_t)m8;
                within16 += (size_t)m16;
            }
            size_t nPixels = nBytes / 3;
            double meanAbsDiff = (double)sumAbsDiff / (double)nBytes;

            {
                int gap = 4;
                int cw  = SCR_WIDTH * 2 + gap;
                std::vector<uint8_t> combo((size_t)cw * SCR_HEIGHT * 3, 40);
                for (int y = 0; y < SCR_HEIGHT; ++y) {
                    std::memcpy(&combo[(size_t)y*cw*3], &cpuPixels[(size_t)y*SCR_WIDTH*3], SCR_WIDTH*3);
                    std::memcpy(&combo[(size_t)y*cw*3 + (SCR_WIDTH+gap)*3],
                               &gpuPixels[(size_t)y*SCR_WIDTH*3], SCR_WIDTH*3);
                }
                stbi_write_png((std::string("cp2_sidebyside_") + tc.name + ".png").c_str(),
                              cw, SCR_HEIGHT, 3, combo.data(), cw * 3);
            }

            std::cout << "[" << tc.name << "]  CPU(24-thread): " << cpuSec << " s   GPU: " << gpuSec << " s"
                       << "   speedup: " << (cpuSec / std::max(gpuSec, 1e-6)) << "x\n"
                      << "         match <=2/255: " << (100.0*within2/nPixels) << "%"
                      << "   <=8/255: " << (100.0*within8/nPixels) << "%"
                      << "   <=16/255: " << (100.0*within16/nPixels) << "%\n"
                      << "         mean|diff|: " << meanAbsDiff << "/255   max|diff|: " << maxAbsDiff << "/255\n"
                      << "         wrote cp2_cpu_" << tc.name << ".png, cp2_gpu_" << tc.name
                      << ".png, cp2_sidebyside_" << tc.name << ".png\n\n";
        }

        std::cout << "=== CP2 done. Open the cp2_sidebyside_*.png files. ===\n";
        return 0;
    }

    // ── GPU CP4: full beauty (glass Fresnel) vs CPU checkpoint (c) ──────────
    // CPU reference is renderBeauty(glassPassThrough=false) — the actual
    // recursive Fresnel reflect/transmit path. GPU uses the iterative
    // throughput-stack reformulation (see rt_cp4_glass.comp).
    if (gpuCP4Mode) {
        struct TestCam { const char* name; glm::vec3 pos; glm::vec3 lookAt; };
        const TestCam testCams[] = {
            { "wide",  {0.0f, 1.55f, 6.5f}, {0.3f, 0.65f, 0.2f} },
            { "close", {0.3f, 1.15f, 2.8f}, {0.3f, 0.65f, 0.2f} },
        };

        GPURayTracer gpuRT;
        std::cout << "\n=== CP4: GPU glass Fresnel vs CPU checkpoint (c) ===\n";
        if (!gpuRT.available()) {
            std::cout << "GPU path unavailable on this context. CPU path is unaffected.\n";
            return 0;
        }

        auto exportData = rayTracer.exportForGPU();
        gpuRT.upload(exportData);

        for (const auto& tc : testCams) {
            Camera cam(tc.pos);
            cam.SetPose(tc.pos, tc.lookAt - tc.pos);
            cam.Zoom = 35.0f;

            std::vector<uint8_t> cpuPixels;
            auto ct0 = std::chrono::steady_clock::now();
            rayTracer.renderBeauty(cam, SCR_WIDTH, SCR_HEIGHT,
                                   std::string("cp4_cpu_") + tc.name + ".png",
                                   &cpuPixels, globalExposure, /*glassPassThrough=*/false);
            double cpuSec = std::chrono::duration<double>(
                                std::chrono::steady_clock::now() - ct0).count();

            std::vector<uint8_t> gpuPixels;
            double gpuSec = 0.0;
            gpuRT.renderGlassGPU(cam, SCR_WIDTH, SCR_HEIGHT, globalExposure,
                                 kCeilLightsRT, kCeilLightRadius, gpuPixels, &gpuSec);
            stbi_write_png((std::string("cp4_gpu_") + tc.name + ".png").c_str(),
                          SCR_WIDTH, SCR_HEIGHT, 3, gpuPixels.data(), SCR_WIDTH * 3);

            size_t nBytes = cpuPixels.size();
            uint64_t sumAbsDiff = 0;
            int      maxAbsDiff = 0;
            size_t   within2 = 0, within8 = 0, within16 = 0;
            for (size_t i = 0; i < nBytes; i += 3) {
                int m2 = 1, m8 = 1, m16 = 1;
                for (int c = 0; c < 3; ++c) {
                    int d = std::abs((int)cpuPixels[i+c] - (int)gpuPixels[i+c]);
                    sumAbsDiff += (uint64_t)d;
                    if (d > maxAbsDiff) maxAbsDiff = d;
                    if (d > 2)  m2  = 0;
                    if (d > 8)  m8  = 0;
                    if (d > 16) m16 = 0;
                }
                within2  += (size_t)m2;
                within8  += (size_t)m8;
                within16 += (size_t)m16;
            }
            size_t nPixels = nBytes / 3;
            double meanAbsDiff = (double)sumAbsDiff / (double)nBytes;

            {
                int gap = 4;
                int cw  = SCR_WIDTH * 2 + gap;
                std::vector<uint8_t> combo((size_t)cw * SCR_HEIGHT * 3, 40);
                for (int y = 0; y < SCR_HEIGHT; ++y) {
                    std::memcpy(&combo[(size_t)y*cw*3], &cpuPixels[(size_t)y*SCR_WIDTH*3], SCR_WIDTH*3);
                    std::memcpy(&combo[(size_t)y*cw*3 + (SCR_WIDTH+gap)*3],
                               &gpuPixels[(size_t)y*SCR_WIDTH*3], SCR_WIDTH*3);
                }
                stbi_write_png((std::string("cp4_sidebyside_") + tc.name + ".png").c_str(),
                              cw, SCR_HEIGHT, 3, combo.data(), cw * 3);
            }

            std::cout << "[" << tc.name << "]  CPU(24-thread): " << cpuSec << " s   GPU: " << gpuSec << " s"
                       << "   speedup: " << (cpuSec / std::max(gpuSec, 1e-6)) << "x\n"
                      << "         match <=2/255: " << (100.0*within2/nPixels) << "%"
                      << "   <=8/255: " << (100.0*within8/nPixels) << "%"
                      << "   <=16/255: " << (100.0*within16/nPixels) << "%\n"
                      << "         mean|diff|: " << meanAbsDiff << "/255   max|diff|: " << maxAbsDiff << "/255\n"
                      << "         wrote cp4_cpu_" << tc.name << ".png, cp4_gpu_" << tc.name
                      << ".png, cp4_sidebyside_" << tc.name << ".png\n\n";
        }

        std::cout << "=== CP4 done. Open the cp4_sidebyside_*.png files. ===\n";
        return 0;
    }

    // ── GPU CP4 pixel diagnostic — full scene, real failing pixels ─────────
    // Four hand-verified synthetic tests (steps 1, 2, 2.5, 3.5) proved the
    // glass Fresnel/throughput logic correct given identical inputs. So the
    // full-scene divergence must come from DIFFERENT INPUTS at the failing
    // pixels, not different logic. This walks the primary ray for specific
    // failing pixels (glass TRANSMISSION only, matching checkpoint b) on CPU
    // and GPU independently and compares hit identity, normal, view vector,
    // and NdotL in order — reporting the first point of divergence.
    if (gpuCP4PixelDiag) {
        std::cout << "\n=== CP4 pixel diagnostic: full scene, failing pixels ===\n";

        GPURayTracer gpuRT;
        if (!gpuRT.available()) {
            std::cout << "GPU path unavailable on this context.\n";
            return 0;
        }
        auto exportData = rayTracer.exportForGPU();
        gpuRT.upload(exportData);

        glm::vec3 closePos = {0.3f, 1.15f, 2.8f}, closeLookAt = {0.3f, 0.65f, 0.2f};
        Camera cam(closePos);
        cam.SetPose(closePos, closeLookAt - closePos);
        cam.Zoom = 35.0f;

        struct TargetPixel { const char* name; int px, py; };
        const TargetPixel targets[] = {
            { "ceiling",    960, 60  },
            { "chair_body", 960, 900 },
            { "frame_post", 960, 500 },
            { "dark_wall (matches in aggregate test)", 1500, 400 },
            { "chair_edge1",  900, 850 },
            { "chair_edge2", 1020, 950 },
            { "chair_top",    960, 700 },
            { "floor_near_chair", 700, 1000 },
            { "corner_frame", 100, 900 },
            { "wide_wall2",  1800, 200 },
        };

        for (const auto& tp : targets) {
            auto cpuDiag = rayTracer.debugTraceOpaqueViaGlassPassThrough(cam, SCR_WIDTH, SCR_HEIGHT, tp.px, tp.py);
            int maxSp = -1, traverseCalls = -1;
            auto gpuDiag = gpuRT.debugCaptureGPU(cam, SCR_WIDTH, SCR_HEIGHT, tp.px, tp.py, kCeilLightsRT, &maxSp, &traverseCalls);

            std::cout << "\n--- " << tp.name << " (" << tp.px << "," << tp.py << ") ---\n";
            std::cout << "  BVH traversal occupancy: maxSp=" << maxSp << " / MAX_STACK=160"
                      << "   traverseBVH() calls for this pixel=" << traverseCalls << "\n";

            if (cpuDiag.isBackground != gpuDiag.isBackground || cpuDiag.hit != gpuDiag.hit) {
                std::cout << "  DIVERGE at hit/background: CPU hit=" << cpuDiag.hit
                          << " bg=" << cpuDiag.isBackground << "   GPU hit=" << gpuDiag.hit
                          << " bg=" << gpuDiag.isBackground << "\n";
                continue;
            }
            if (!cpuDiag.hit) {
                std::cout << "  Both resolved to background (no opaque hit found via pass-through).\n";
                continue;
            }

            std::cout << "  prim:   CPU=" << cpuDiag.prim << "   GPU=" << gpuDiag.prim
                      << (cpuDiag.prim == gpuDiag.prim ? "  [MATCH]" : "  [DIVERGE]") << "\n";
            std::cout << "  hitPos: CPU=(" << cpuDiag.hitPos.x << "," << cpuDiag.hitPos.y << "," << cpuDiag.hitPos.z << ")"
                      << "   GPU=(" << gpuDiag.hitPos.x << "," << gpuDiag.hitPos.y << "," << gpuDiag.hitPos.z << ")\n";
            std::cout << "  baryUV: CPU=(" << cpuDiag.baryU << "," << cpuDiag.baryV << ")"
                      << "   GPU=(" << gpuDiag.baryU << "," << gpuDiag.baryV << ")\n";

            if (cpuDiag.prim != gpuDiag.prim) {
                std::cout << "  ^^ DIVERGE at hit identity (different triangle) — stopping here.\n";
                continue;
            }

            std::cout << "  N:      CPU=(" << cpuDiag.N.x << "," << cpuDiag.N.y << "," << cpuDiag.N.z << ")"
                      << "   GPU=(" << gpuDiag.N.x << "," << gpuDiag.N.y << "," << gpuDiag.N.z << ")\n";
            float nDiff = std::abs(cpuDiag.N.x-gpuDiag.N.x)+std::abs(cpuDiag.N.y-gpuDiag.N.y)+std::abs(cpuDiag.N.z-gpuDiag.N.z);
            std::cout << "  |N diff| = " << nDiff << (nDiff > 0.01f ? "  [DIVERGE]" : "  [match]") << "\n";

            std::cout << "  V:      CPU=(" << cpuDiag.V.x << "," << cpuDiag.V.y << "," << cpuDiag.V.z << ")"
                      << "   GPU=(" << gpuDiag.V.x << "," << gpuDiag.V.y << "," << gpuDiag.V.z << ")\n";
            float vDiff = std::abs(cpuDiag.V.x-gpuDiag.V.x)+std::abs(cpuDiag.V.y-gpuDiag.V.y)+std::abs(cpuDiag.V.z-gpuDiag.V.z);
            std::cout << "  |V diff| = " << vDiff << (vDiff > 0.01f ? "  [DIVERGE]" : "  [match]") << "\n";

            std::cout << "  NdotL:  CPU=(";
            for (int i = 0; i < cpuDiag.numLights; ++i) std::cout << cpuDiag.NdotL[i] << (i+1<cpuDiag.numLights?",":"");
            std::cout << ")   GPU=(";
            for (int i = 0; i < gpuDiag.numLights; ++i) std::cout << gpuDiag.NdotL[i] << (i+1<gpuDiag.numLights?",":"");
            std::cout << ")\n";
        }

        std::cout << "\n=== pixel diagnostic done. ===\n";
        return 0;
    }

    // ── GPU CP4 full push/pop/throughput trace ──────────────────────────────
    // Instruments the REAL rt_cp4_glass.comp (not a reimplementation) for one
    // target pixel: every push/pop/leaf event with its weight/depth/tint,
    // purely additive (never touches result/control-flow). Verified against a
    // known-good pixel first (per the broken-early-return lesson), then run on
    // the actual failing chair_body pixel and compared against CPU's true
    // recursion for the same ray (same event log format, so directly comparable
    // even though CPU recursion order and GPU LIFO-stack order can legitimately
    // differ without being a bug — see step 2.5 discussion).
    if (gpuCP4StackTrace) {
        std::cout << "\n=== CP4 stack trace: push/pop/throughput ===\n";

        GPURayTracer gpuRT;
        if (!gpuRT.available()) {
            std::cout << "GPU path unavailable on this context.\n";
            return 0;
        }
        auto exportData = rayTracer.exportForGPU();
        gpuRT.upload(exportData);

        glm::vec3 closePos = {0.3f, 1.15f, 2.8f}, closeLookAt = {0.3f, 0.65f, 0.2f};
        Camera cam(closePos);
        cam.SetPose(closePos, closeLookAt - closePos);
        cam.Zoom = 35.0f;

        auto eventName = [](int e) -> const char* {
            switch (e) {
                case 0: return "push-reflect";
                case 1: return "push-transmit";
                case 2: return "leaf-depth-exhausted-bg";
                case 3: return "leaf-miss-bg";
                case 4: return "leaf-opaque";
                case 5: return "DROP-OVERFLOW";
                default: return "?";
            }
        };
        auto printTrace = [&](const char* label, const std::vector<float>& trace) {
            int count = trace.empty() ? 0 : (int)(trace[0] + 0.5f);
            std::cout << label << ": " << count << " entries\n";
            float weightSum = 0.0f;
            for (int i = 0; i < count; ++i) {
                int b = 1 + i * 6;
                int evt = (int)(trace[b+0] + 0.5f);
                float w = trace[b+1], d = trace[b+2], sp = trace[b+3], tintR = trace[b+4], extra = trace[b+5];
                std::cout << "  [" << i << "] " << eventName(evt)
                          << "  weight=" << w << "  depth=" << d << "  sp=" << sp
                          << "  tint.r=" << tintR << "  extra=" << extra << "\n";
                if (evt == 2 || evt == 3 || evt == 4) weightSum += w;  // leaves only
            }
            std::cout << "  weightSum(leaves) = " << weightSum << (std::abs(weightSum - 1.0f) > 0.01f ? "  [SHOULD BE ~1.0 -- MISMATCH]" : "  [ok]") << "\n";
        };

        struct TargetPixel { const char* name; int px, py; };
        const TargetPixel targets[] = {
            { "dark_wall (known-good control)", 1500, 400 },
            { "chair_body (FAILING)",            960,  900 },
        };

        for (const auto& tp : targets) {
            std::cout << "\n--- " << tp.name << " (" << tp.px << "," << tp.py << ") ---\n";
            auto cpuTrace = rayTracer.debugTraceGlassFull(cam, SCR_WIDTH, SCR_HEIGHT, tp.px, tp.py);
            auto gpuTrace = gpuRT.traceGlassGPU(cam, SCR_WIDTH, SCR_HEIGHT, tp.px, tp.py, kCeilLightsRT, kCeilLightRadius);
            printTrace("CPU (true recursion)", cpuTrace);
            printTrace("GPU (LIFO stack)", gpuTrace);
        }

        std::cout << "\n=== stack trace done. ===\n";
        return 0;
    }

    // ── GPU CP5: single-frame preview pipeline sanity check ────────────────
    // Not diffed against CPU (glass is an intentional approximation on the GPU
    // preview path — see rt_cp5_gpu_beauty.comp). Confirms the HDR->OIDN->bloom->
    // tonemap pipeline works end to end and reports real-world speedup at
    // typical final-quality settings (CPU: spp=4 + OIDN + bloom).
    if (gpuCP5Mode) {
        struct TestCam { const char* name; glm::vec3 pos; glm::vec3 lookAt; };
        const TestCam testCams[] = {
            { "wide",  {0.0f, 1.55f, 6.5f}, {0.3f, 0.65f, 0.2f} },
            { "close", {0.3f, 1.15f, 2.8f}, {0.3f, 0.65f, 0.2f} },
        };

        GPURayTracer gpuRT;
        std::cout << "\n=== CP5: GPU preview pipeline (HDR + OIDN + bloom + tonemap) ===\n";
        if (!gpuRT.available()) {
            std::cout << "GPU path unavailable on this context. CPU path is unaffected.\n";
            return 0;
        }

        auto exportData = rayTracer.exportForGPU();
        gpuRT.upload(exportData);

        rayTracer.setOIDNSettings({true});
        rayTracer.setBloomSettings({true, bloomThreshold, bloomKnee, bloomIterations, bloomIntensity});

        for (const auto& tc : testCams) {
            Camera cam(tc.pos);
            cam.SetPose(tc.pos, tc.lookAt - tc.pos);
            cam.Zoom = 35.0f;

            auto ct0 = std::chrono::steady_clock::now();
            rayTracer.renderFrame(cam, SCR_WIDTH, SCR_HEIGHT, globalExposure, 4,
                                  std::string("cp5_cpu_") + tc.name + ".png");
            double cpuSec = std::chrono::duration<double>(
                                std::chrono::steady_clock::now() - ct0).count();

            std::vector<glm::vec3> hdr, albedo, normal;
            double gpuRenderSec = 0.0;
            auto gt0 = std::chrono::steady_clock::now();
            gpuRT.renderPreviewGPU(cam, SCR_WIDTH, SCR_HEIGHT, kCeilLightsRT, kCeilLightRadius,
                                   hdr, albedo, normal, &gpuRenderSec);
            rayTracer.postProcessAndSave(hdr, albedo, normal, SCR_WIDTH, SCR_HEIGHT, globalExposure,
                                         std::string("cp5_gpu_") + tc.name + ".png");
            double gpuTotalSec = std::chrono::duration<double>(
                                     std::chrono::steady_clock::now() - gt0).count();

            std::cout << "[" << tc.name << "]  CPU(spp4+OIDN+bloom): " << cpuSec << " s"
                       << "   GPU render: " << gpuRenderSec << " s"
                       << "   GPU total(+OIDN+bloom): " << gpuTotalSec << " s"
                      << "   speedup: " << (cpuSec / std::max(gpuTotalSec, 1e-6)) << "x\n";

            // Side-by-side for visual sanity check (not a pixel diff — glass differs by design).
            {
                int cw_, ch_, cn_, gw_, gh_, gn_;
                unsigned char* cpuRaw = stbi_load((std::string("cp5_cpu_") + tc.name + ".png").c_str(),
                                                  &cw_, &ch_, &cn_, 3);
                unsigned char* gpuRaw = stbi_load((std::string("cp5_gpu_") + tc.name + ".png").c_str(),
                                                  &gw_, &gh_, &gn_, 3);
                if (cpuRaw && gpuRaw && cw_ == gw_ && ch_ == gh_) {
                    int gap = 4;
                    int cw  = cw_ * 2 + gap;
                    std::vector<uint8_t> combo((size_t)cw * ch_ * 3, 40);
                    for (int y = 0; y < ch_; ++y) {
                        std::memcpy(&combo[(size_t)y*cw*3], cpuRaw + (size_t)y*cw_*3, (size_t)cw_*3);
                        std::memcpy(&combo[(size_t)y*cw*3 + (cw_+gap)*3],
                                   gpuRaw + (size_t)y*cw_*3, (size_t)cw_*3);
                    }
                    stbi_write_png((std::string("cp5_sidebyside_") + tc.name + ".png").c_str(),
                                  cw, ch_, 3, combo.data(), cw * 3);
                }
                if (cpuRaw) stbi_image_free(cpuRaw);
                if (gpuRaw) stbi_image_free(gpuRaw);
            }
        }

        std::cout << "=== CP5 done. Open cp5_cpu_*.png / cp5_gpu_*.png to compare visually. ===\n";
        return 0;
    }

    // ── GPU animation preview — fast iteration render ───────────────────────
    // Same push-in shot as RayTracer::renderAnimation (AnimConfig defaults),
    // but renders every frame on GPU (rt_cp5_gpu_beauty.comp: opaque BRDF+shadows
    // matching CPU, glass approximated as a non-recursive Fresnel sheen) through
    // the same OIDN->bloom->tonemap pipeline as the CPU path. This is the
    // iteration tool; the final Abgabe render still goes through
    // RayTracer::renderAnimation (CPU, exact recursive glass), untouched.
    if (gpuAnimMode) {
        GPURayTracer gpuRT;
        std::cout << "\n=== GPU animation preview ===\n";
        if (!gpuRT.available()) {
            std::cout << "GPU path unavailable on this context. CPU path is unaffected.\n";
            return 0;
        }

        auto exportData = rayTracer.exportForGPU();
        gpuRT.upload(exportData);
        rayTracer.setOIDNSettings({true});
        rayTracer.setBloomSettings({enableBloom, bloomThreshold, bloomKnee,
                                    bloomIterations, bloomIntensity});

        // Mirrors AnimConfig defaults from RayTracer.h.
        const glm::vec3 kStart  = {0.0f, 1.55f, 6.5f};
        const glm::vec3 kEnd    = {0.3f, 1.15f, 2.8f};
        const glm::vec3 kLookAt = {0.3f, 0.65f, 0.2f};
        const float     kFov    = 35.0f;
        const int       kFrames = 60;
        const std::string outDir = "gpu_preview_frames";

        std::filesystem::create_directories(outDir);
        std::cout << kFrames << " frames -> " << outDir << "/frame_XXXX.png\n" << std::flush;

        auto animT0 = std::chrono::steady_clock::now();
        int saved = 0;
        for (int f = 0; f < kFrames; ++f) {
            float t_raw   = (kFrames > 1) ? (float)f / (float)(kFrames - 1) : 0.0f;
            float t_eased = t_raw * t_raw * (3.0f - 2.0f * t_raw);

            glm::vec3 pos   = glm::mix(kStart, kEnd, t_eased);
            glm::vec3 front = glm::normalize(kLookAt - pos);
            Camera cam(pos);
            cam.SetPose(pos, front);
            cam.Zoom = kFov;

            std::vector<glm::vec3> hdr, albedo, normal;
            double gpuSec = 0.0;
            gpuRT.renderPreviewGPU(cam, SCR_WIDTH, SCR_HEIGHT, kCeilLightsRT, kCeilLightRadius,
                                   hdr, albedo, normal, &gpuSec);

            char path[512];
            std::snprintf(path, sizeof(path), "%s/frame_%04d.png", outDir.c_str(), f);
            bool ok = rayTracer.postProcessAndSave(hdr, albedo, normal, SCR_WIDTH, SCR_HEIGHT,
                                                   globalExposure, path);
            if (ok) ++saved;

            std::cout << "\rframe " << (f + 1) << "/" << kFrames
                      << "  (" << gpuSec << " s render)" << std::flush;
        }
        double totalSec = std::chrono::duration<double>(
                              std::chrono::steady_clock::now() - animT0).count();

        std::cout << "\nDone — " << saved << "/" << kFrames << " frames saved to " << outDir
                  << "/ in " << totalSec << " s (" << (totalSec / kFrames) << " s/frame avg)\n"
                  << "\n  Stitch to mp4 (30 fps, H.264):\n"
                  << "  ffmpeg -framerate 30 -i " << outDir << "/frame_%04d.png \\\n"
                  << "         -c:v libx264 -pix_fmt yuv420p -crf 18 gpu_preview.mp4\n"
                  << std::flush;

        return 0;
    }

    // ── Compare mode: 3 timed renders, then exit ───────────────────────────
    if (compareMode) {
        // Animation end-frame camera: closest approach to the chair — worst case for detail.
        glm::vec3 cPos    = glm::vec3(0.3f, 1.15f, 2.8f);
        glm::vec3 cLookAt = glm::vec3(0.3f, 0.65f, 0.2f);
        Camera testCam(cPos);
        testCam.SetPose(cPos, cLookAt - cPos);
        testCam.Zoom = 35.0f;

        struct Run { int spp; bool oidn; const char* path; };
        const Run runs[] = {
            {  4, false, "compare_04spp_nooidn.png" },
            {  4, true,  "compare_04spp_oidn.png"   },
            { 64, false, "compare_64spp_nooidn.png"  },
        };

        std::cout << "\n=== OIDN comparison render ===\n"
                  << "Resolution: " << SCR_WIDTH << "x" << SCR_HEIGHT << "\n\n";

        for (auto& r : runs) {
            rayTracer.setOIDNSettings({r.oidn});
            rayTracer.setBloomSettings({enableBloom, bloomThreshold, bloomKnee,
                                        bloomIterations, bloomIntensity});
            std::cout << "--- " << r.path
                      << "  (" << r.spp << " spp, OIDN=" << (r.oidn ? "ON" : "off") << ") ---\n";
            auto t0 = std::chrono::steady_clock::now();
            bool ok = rayTracer.renderFrame(testCam, SCR_WIDTH, SCR_HEIGHT,
                                            globalExposure, r.spp, r.path);
            auto t1 = std::chrono::steady_clock::now();
            double sec = std::chrono::duration<double>(t1 - t0).count();
            std::cout << (ok ? "  saved" : "  FAILED") << "  —  " << sec << " s\n\n";
        }

        std::cout << "=== Done. Open the three PNGs side by side to compare. ===\n";
        glfwTerminate();
        return 0;
    }

    // ── Temporal flicker preview: 5 frames near animation end ──────────────
    // Renders frames 55-59 of the 60-frame push-in at the chosen spp+OIDN.
    // Watch the resulting sequence in motion to confirm no inter-frame shimmer.
    if (temporalMode) {
        // Mirror AnimConfig defaults from RayTracer.h
        const glm::vec3 kStart  = glm::vec3( 0.0f,  1.55f,  6.5f);
        const glm::vec3 kEnd    = glm::vec3( 0.3f,  1.15f,  2.8f);
        const glm::vec3 kLookAt = glm::vec3( 0.3f,  0.65f,  0.2f);
        const float     kFov    = 35.0f;
        const int       kN      = 60;

        rayTracer.setOIDNSettings({true});
        rayTracer.setBloomSettings({enableBloom, bloomThreshold, bloomKnee,
                                    bloomIterations, bloomIntensity});

        std::filesystem::create_directories("temporal");

        std::cout << "\n=== Temporal preview: frames 55-59 / " << kN
                  << "  (" << temporalSPP << " spp + OIDN) ===\n\n";

        for (int f = 55; f <= 59; ++f) {
            float t_raw   = (float)f / (float)(kN - 1);
            float t_ease  = t_raw * t_raw * (3.0f - 2.0f * t_raw);
            glm::vec3 pos = glm::mix(kStart, kEnd, t_ease);
            glm::vec3 fwd = glm::normalize(kLookAt - pos);
            Camera cam(pos);
            cam.SetPose(pos, fwd);
            cam.Zoom = kFov;

            char path[64];
            std::snprintf(path, sizeof(path), "temporal/frame_%04d.png", f);
            std::cout << "frame " << f << "  pos=(" << pos.x << ", " << pos.y
                      << ", " << pos.z << ") → " << path << "\n";
            auto t0 = std::chrono::steady_clock::now();
            rayTracer.renderFrame(cam, SCR_WIDTH, SCR_HEIGHT,
                                  globalExposure, temporalSPP, path);
            double sec = std::chrono::duration<double>(
                             std::chrono::steady_clock::now() - t0).count();
            std::cout << "  done in " << sec << " s\n";
        }

        std::cout << "\nView as sequence:  ffmpeg -framerate 5 -i temporal/frame_%04d.png"
                  << " -vf scale=960:540 temporal_preview.mp4\n";
        glfwTerminate();
        return 0;
    }

    // ── Record mode: raster push-in, 30 s × 30 fps → frames/ ─────────────
    if (recordMode) {
        std::filesystem::create_directories("frames");

        const glm::vec3 kRecStart  = {  0.0f, 1.55f, 8.0f };
        const glm::vec3 kRecEnd    = {  0.3f, 0.80f, 2.0f };
        const glm::vec3 kRecLookAt = {  0.3f, 0.65f, 0.2f };
        const float     kRecFov    = 35.0f;
        const int       kRecFrames = 900;   // 30 s × 30 fps

        renderer.settings.shadows    = true;
        renderer.settings.softShadow = true;
        renderer.settings.ao         = true;
        renderer.settings.dof        = false;
        renderer.settings.motionBlur = false;
        renderer.settings.exposure   = globalExposure;

        std::vector<uint8_t> pixels(SCR_WIDTH * SCR_HEIGHT * 3);
        std::vector<uint8_t> flipped(SCR_WIDTH * SCR_HEIGHT * 3);

        for (int f = 0; f < kRecFrames; ++f) {
            float t_raw  = (float)f / (float)(kRecFrames - 1);
            float t_ease = t_raw * t_raw * (3.0f - 2.0f * t_raw);

            glm::vec3 pos = glm::mix(kRecStart, kRecEnd, t_ease);
            camera.SetPose(pos, glm::normalize(kRecLookAt - pos));
            camera.Zoom = kRecFov;

            {
                glm::mat4 T = glm::translate(glm::mat4(1.0f),
                                             glm::vec3(chairPosX, chairPosY, chairPosZ));
                glm::mat4 R = glm::rotate(glm::mat4(1.0f), glm::radians(chairYaw),
                                          glm::vec3(0.0f, 1.0f, 0.0f));
                glm::mat4 S = glm::scale(glm::mat4(1.0f), glm::vec3(chairScale));
                scene.objects[kChairIdx].transform = T * R * S;
            }

            renderer.render(scene, camera, 1.0f / 30.0f);

            glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
            glReadBuffer(GL_BACK);
            glReadPixels(0, 0, SCR_WIDTH, SCR_HEIGHT, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());

            for (int row = 0; row < SCR_HEIGHT; ++row) {
                const uint8_t* src = pixels.data() + (SCR_HEIGHT - 1 - row) * SCR_WIDTH * 3;
                uint8_t*       dst = flipped.data() + row * SCR_WIDTH * 3;
                std::memcpy(dst, src, SCR_WIDTH * 3);
            }

            char path[64];
            std::snprintf(path, sizeof(path), "frames/frame_%04d.png", f);
            stbi_write_png(path, SCR_WIDTH, SCR_HEIGHT, 3, flipped.data(), SCR_WIDTH * 3);

            if (f % 30 == 0)
                std::cout << "[Record] frame " << f << " / " << kRecFrames
                          << "  (" << (f * 100 / kRecFrames) << "%)\n" << std::flush;

            glfwPollEvents();
        }

        std::cout << "[Record] Done. Stitch with:\n"
                  << "  ffmpeg -framerate 30 -i frames/frame_%04d.png"
                  << " -c:v libx264 -pix_fmt yuv420p animation.mp4\n";
        glfwTerminate();
        return 0;
    }

    // ── RT record: CPU ray-traced push-in, 30 s × 30 fps @ 480×270, spp=1 ──
    if (rtRecordMode) {
        AnimConfig cfg;
        cfg.startPos  = { 0.0f, 1.55f, 8.0f };
        cfg.endPos    = { 0.3f, 0.80f, 2.0f };
        cfg.lookAt    = { 0.3f, 0.65f, 0.2f };
        cfg.fovDeg    = 35.0f;
        cfg.numFrames = 900;
        cfg.spp       = 1;
        cfg.outDir    = "rtframes";
        rayTracer.setAnimConfig(cfg);

        std::atomic<bool> cancel{false};
        rayTracer.renderAnimation(480, 270, globalExposure, cancel);

        std::cout << "\nStitching video...\n";
        std::system("ffmpeg -framerate 30 -i rtframes/frame_%04d.png "
                    "-c:v libx264 -pix_fmt yuv420p -crf 18 "
                    "animation_rt.mp4 -y 2>&1");
        std::cout << "Done → animation_rt.mp4\n";
        glfwTerminate();
        return 0;
    }

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

        renderer.settings.probeEnable   = enableProbe;
        renderer.settings.probeStrength = probeStrength;

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

        renderer.render(scene, camera, deltaTime);

        // ── RT progressive: upload new pixels whenever render thread signals ──
        if (g_rtTask.bufDirty.exchange(false)) {
            {
                std::lock_guard<std::mutex> lk(g_rtTask.bufMutex);
                if (!g_rtTask.buf.empty() && g_rtTex) {
                    glBindTexture(GL_TEXTURE_2D, g_rtTex);
                    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                                    SCR_WIDTH, SCR_HEIGHT,
                                    GL_RGB, GL_UNSIGNED_BYTE, g_rtTask.buf.data());
                    glBindTexture(GL_TEXTURE_2D, 0);
                }
            }
            // Title bar: pass-1 shows scan row progress; later passes show spp counter
            int s    = g_rtTask.samplesDone.load();
            int rows = g_rtTask.rowsDone.load();
            std::string suffix;
            if (g_rtTask.active.load()) {
                if (s == 0)
                    suffix = " [RT: scan " + std::to_string(rows) + "/" +
                             std::to_string(SCR_HEIGHT) + " — R to cancel]";
                else
                    suffix = " [RT: " + std::to_string(s) + "/8 spp — R to cancel]";
            } else {
                suffix = " [RT: " + std::to_string(s) + " spp done — R to hide]";
            }
            glfwSetWindowTitle(window, (std::string(TITLE) + suffix).c_str());
        }

        // ── RT overlay: overdraws raster when active ──────────────────────
        // Reset every piece of GL state the renderer's glass/blend passes may
        // leave enabled — if any of these are on, the overlay either disappears
        // (wrong blend) or is clipped (cull face) or blocked (depth test/write).
        if (g_showRTNormals && g_rtTex) {
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glDisable(GL_DEPTH_TEST);
            glDisable(GL_BLEND);
            glDisable(GL_CULL_FACE);
            glDisable(GL_STENCIL_TEST);
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            glDepthMask(GL_FALSE);
            glViewport(0, 0, SCR_WIDTH, SCR_HEIGHT);
            glUseProgram(g_rtProg);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, g_rtTex);
            glUniform1i(glGetUniformLocation(g_rtProg, "uTex"), 0);
            glBindVertexArray(g_rtVAO);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            glBindVertexArray(0);
            glEnable(GL_DEPTH_TEST);
            glDepthMask(GL_TRUE);
        }

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
        ImGui::Checkbox("[8] Cubemap probe", &enableProbe);
        ImGui::SliderFloat("Probe strength", &probeStrength, 0.0f, 2.0f, "%.2f");
        if (ImGui::Button("[B] Rebake probe")) renderer.requestBake();
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
    // Stop render thread before destroying GL resources it might be uploading to
    g_rtTask.cancel = true;
    if (g_rtThread.joinable()) g_rtThread.join();

    glDeleteTextures(1,      &g_rtTex);
    glDeleteProgram(g_rtProg);
    glDeleteVertexArrays(1,  &g_rtVAO);
#ifdef HAS_IMGUI
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
#endif
    glfwTerminate();
    return 0;
}
