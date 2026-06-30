#version 410 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoords;
// location 3 (Tangent) is bound by the VAO but unused here

out vec3 WorldPos;
out vec3 WorldNormal;
out vec2 TexCoord;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main() {
    vec4 worldPos = model * vec4(aPos, 1.0);
    WorldPos    = worldPos.xyz;
    WorldNormal = normalize(mat3(transpose(inverse(model))) * aNormal);
    TexCoord    = aTexCoords;
    gl_Position = projection * view * worldPos;
}
