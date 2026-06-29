#version 410 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoord;

out vec3 WorldPos;
out vec3 WorldNormal;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main() {
    vec4 worldPos  = model * vec4(aPos, 1.0);
    WorldPos       = worldPos.xyz;
    WorldNormal    = mat3(transpose(inverse(model))) * aNormal;

    // Clip everything at or below Y = 0.006 — excludes floor slab and plinth
    gl_ClipDistance[0] = worldPos.y - 0.006;

    gl_Position = projection * view * worldPos;
}
