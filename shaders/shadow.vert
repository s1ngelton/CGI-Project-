#version 410 core

// ─── Shadow map pass ─────────────────────────────────────────────────────────
// Renders the scene from the light's point of view to produce a depth map.
// This depth map is then sampled in the lighting pass to determine shadows.

layout (location = 0) in vec3 aPos;

uniform mat4 lightSpaceMatrix;  // light projection * light view
uniform mat4 model;

void main() {
    gl_Position = lightSpaceMatrix * model * vec4(aPos, 1.0);
}
