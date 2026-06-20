# Horror Engine
CG short film engine — Computer Animation 2026
Team: Hugo Lladró Prats, Dmytro Volkov, Phillip Richard Stappler

---

## Dependencies

### Linux (Ubuntu/Debian)
```bash
sudo apt update
sudo apt install \
  cmake build-essential \
  libglfw3-dev \
  libglm-dev \
  libassimp-dev \
  libopenal-dev
```


---

## GLAD setup (one-time, all platforms)

1. Go to https://glad.dav1d.de
2. Select:
   - Language: C/C++
   - Specification: OpenGL
   - API gl: Version 4.1  ← important for Mac compatibility
   - Profile: Core
3. Click Generate, download the zip
4. Copy `glad.c`      → `external/glad/src/glad.c`
5. Copy `glad/glad.h` → `external/glad/include/glad/glad.h`
6. Copy `KHR/`        → `external/glad/include/KHR/`

---


---

## stb_image setup (one-time, all platforms)

```bash
mkdir -p external/stb
curl -o external/stb/stb_image.h \
  https://raw.githubusercontent.com/nothings/stb/master/stb_image.h
```

---

## Build

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
make -j$(nproc)        # Linux
make -j$(sysctl -n hw.ncpu)  # macOS
```

The binary lands in `build/bin/HorrorEngine`.
Shaders and assets are copied automatically next to the binary.

---


## Project structure

```
horror_engine/
├── CMakeLists.txt
├── README.md
├── src/
│   ├── main.cpp             ← entry point, window, render loop
│   ├── Renderer.cpp         ← all render passes
│   ├── Shader.cpp
│   ├── Camera.cpp
│   ├── Model.cpp            ← Assimp model loading
│   ├── Scene.cpp            ← scene graph
│   ├── ShadowMap.cpp        ← shadow map FBO
│   ├── PostProcess.cpp      ← DOF, motion blur, tonemap
│   ├── CinematicEngine.cpp  ← spline camera animation
│   └── AudioManager.cpp     ← OpenAL sound cues
├── include/
│   ├── Camera.h
│   ├── Shader.h
│   ├── Renderer.h
│   ├── CinematicEngine.h
│   └── ...
├── shaders/
│   ├── gbuffer.vert / .frag     ← geometry pass
│   ├── shadow.vert              ← shadow map pass
│   ├── lighting.frag            ← deferred lighting + PCSS
│   ├── ssao.frag                ← ambient occlusion
│   ├── dof.frag                 ← depth of field
│   ├── motionblur.frag          ← motion blur
│   └── tonemap.frag             ← HDR tonemapping
├── assets/
│   ├── models/                  ← .obj / .glb from Blender
│   ├── textures/
│   ├── scene.json               ← scene description
│   ├── cameras.json             ← cinematic keyframes
│   └── audio.json               ← sound cue list
└── external/
    ├── glad/                    ← generated from glad.dav1d.de
    ├── imgui/                   ← cloned from github
    └── stb/                     ← stb_image.h
```

---

## Render pipeline

```
Shadow pass     → depth map from light's POV
G-Buffer pass   → position, normal, albedo, velocity
SSAO pass       → ambient occlusion texture
Lighting pass   → deferred shading + PCF/PCSS shadows + BRDF
DOF pass        → depth of field blur
Motion blur     → velocity-based blur
Tonemap pass    → HDR → LDR, gamma correction
```

---

## Feature checklist (360P total)

- [x] Boolean geometry          (60P) — Blender assets
- [x] Depth of field            (20P) — shaders/dof.frag
- [x] Motion blur               (20P) — shaders/motionblur.frag
- [x] Shadow mapping            (30P) — shaders/shadow.vert + lighting.frag
- [x] Data-driven materials     (40P) — MERL BRDF loader (TODO)
- [x] Soft shadows (PCSS)       (80P) — shaders/lighting.frag shadowPCSS()
- [x] Ambient occlusion (SSAO)  (30P) — shaders/ssao.frag
- [x] Cinematic engine          (50P) — src/CinematicEngine.cpp
- [x] Interactive application   (20P) — ImGui panel in main.cpp
- [x] Sound                     (10P) — src/AudioManager.cpp
