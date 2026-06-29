#version 410 core

// ─── G-Buffer geometry pass vertex shader ───────────────────────────────────
// Outputs world-space position, normal, and UV for deferred shading

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoord;

out vec3 FragPos;
out vec3 Normal;
out vec2 TexCoord;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

// Previous frame MVP for motion blur velocity buffer
uniform mat4 prevMVP;
out vec4 ClipPosCurr;
out vec4 ClipPosPrev;

void main() {
    vec4 worldPos = model * vec4(aPos, 1.0);
    FragPos       = worldPos.xyz;
    Normal        = normalize(mat3(transpose(inverse(model))) * aNormal);
    TexCoord      = aTexCoord;

    vec4 clipPos  = projection * view * worldPos;
    ClipPosCurr   = clipPos;
    ClipPosPrev   = prevMVP * vec4(aPos, 1.0);

    gl_Position   = clipPos;
}
