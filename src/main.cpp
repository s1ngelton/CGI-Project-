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
#include <thread>
#include <string>
#include <chrono>
#include <filesystem>

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
    int  temporalSPP   = 4;    // default; override with --temporal 8
    for (int i = 1; i < argc; ++i) {
        std::string a(argv[i]);
        if (a == "--compare")  compareMode  = true;
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
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);   // 4.1 = Mac compatible
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
    if (compareMode)
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
    rayTracer.setLights({
        { glm::vec3(-1.5f, 2.9f,  1.0f), glm::vec3(1.0f, 0.95f, 0.85f), 5.0f },
        { glm::vec3( 1.5f, 2.9f,  1.0f), glm::vec3(1.0f, 0.95f, 0.85f), 5.0f },
        { glm::vec3(-1.5f, 2.9f, -1.0f), glm::vec3(1.0f, 0.95f, 0.85f), 5.0f },
        { glm::vec3( 1.5f, 2.9f, -1.0f), glm::vec3(1.0f, 0.95f, 0.85f), 5.0f },
    }, 8.0f);

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
