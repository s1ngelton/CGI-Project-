#version 410 core

layout (location = 0) out vec4 gPosition;   // xyz=world pos, w=baked AO
layout (location = 1) out vec4 gNormal;     // xyz=world normal (mapped), w=metallic
layout (location = 2) out vec4 gAlbedoSpec; // rgb=albedo, a=roughness
layout (location = 3) out vec2 gVelocity;   // NDC velocity for motion blur
layout (location = 4) out vec3 gEmissive;

in vec3 FragPos;
in vec3 Normal;
in vec3 WorldTangent;
in vec2 TexCoord;
in vec4 ClipPosCurr;
in vec4 ClipPosPrev;

// Base color
uniform sampler2D texture_diffuse1;
uniform bool      hasTexture;
uniform vec3      albedoColor;

// PBR maps (bound by Mesh::draw when present)
uniform sampler2D normalMap;
uniform sampler2D roughnessMap;
uniform sampler2D metallicMap;
uniform sampler2D aoMap;
uniform bool      hasNormalTex;
uniform bool      hasRoughnessTex;
uniform bool      hasMetallicTex;
uniform bool      hasAOTex;

// Material fallbacks when maps are absent
uniform float matRoughness;
uniform float matMetallic;

// Emissive
uniform vec3  emissiveColor;
uniform float emissiveStrength;

void main() {
    // ── Albedo ────────────────────────────────────────────────────────────────
    vec3 albedo = hasTexture
        ? pow(texture(texture_diffuse1, TexCoord).rgb, vec3(2.2))  // sRGB→linear
        : albedoColor;

    // ── Normal mapping ────────────────────────────────────────────────────────
    vec3 N = normalize(Normal);
    vec3 finalNormal = N;

    if (hasNormalTex) {
        vec3 T = normalize(WorldTangent);
        T = normalize(T - dot(T, N) * N);   // Gram-Schmidt re-orthogonalize
        vec3 B = cross(N, T);
        mat3 TBN = mat3(T, B, N);
        vec3 nSample = texture(normalMap, TexCoord).rgb * 2.0 - 1.0;
        finalNormal = normalize(TBN * nSample);
    }

    // ── PBR scalars ───────────────────────────────────────────────────────────
    float roughness = hasRoughnessTex ? texture(roughnessMap, TexCoord).r : matRoughness;
    float metallic  = hasMetallicTex  ? texture(metallicMap,  TexCoord).r : matMetallic;
    float ao        = hasAOTex        ? texture(aoMap,        TexCoord).r : 1.0;

    // ── G-buffer writes ───────────────────────────────────────────────────────
    gPosition   = vec4(FragPos,     ao);        // .w = baked AO
    gNormal     = vec4(finalNormal, metallic);  // .w = metallic
    gAlbedoSpec = vec4(albedo,      roughness); // .a = roughness
    gEmissive   = emissiveColor * emissiveStrength;

    // ── Velocity (NDC delta for motion blur) ──────────────────────────────────
    vec2 currNDC = (ClipPosCurr.xy / ClipPosCurr.w) * 0.5 + 0.5;
    vec2 prevNDC = (ClipPosPrev.xy / ClipPosPrev.w) * 0.5 + 0.5;
    gVelocity    = currNDC - prevNDC;
}
