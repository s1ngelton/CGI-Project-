#version 410 core
out vec4 FragColor;

in vec3 WorldPos;
in vec3 WorldNormal;
in vec2 TexCoord;

// ── Material — uniform names match Mesh::draw() exactly ──────────────────────
uniform sampler2D texture_diffuse1;   // unit 0
uniform bool      hasTexture;
uniform vec3      albedoColor;

uniform bool      hasRoughnessTex;
uniform sampler2D roughnessMap;       // unit 2
uniform bool      hasMetallicTex;
uniform sampler2D metallicMap;        // unit 3
uniform bool      hasAOTex;
uniform sampler2D aoMap;              // unit 4

uniform float matRoughness;
uniform float matMetallic;
uniform vec3  emissiveColor;
uniform float emissiveStrength;

// ── Ceiling lights — IDENTICAL values to passLighting / lighting.frag ─────────
// MAINTENANCE: keep this block in sync with lighting.frag when light params change.
const int MAX_LIGHTS = 4;
uniform int   numLights;
uniform vec3  lightPositions[MAX_LIGHTS];
uniform vec3  lightColors[MAX_LIGHTS];
uniform float lightIntensities[MAX_LIGHTS];
uniform float lightRadius;

// ── Camera + shadow ───────────────────────────────────────────────────────────
uniform vec3      viewPos;
uniform sampler2D shadowMap;          // unit 5 — bound by bakeCubemap, not Mesh::draw
uniform mat4      lightSpaceMatrix;
uniform bool      useShadows;

// ── BRDF helpers — IDENTICAL to lighting.frag ─────────────────────────────────
// MAINTENANCE: keep these three functions in sync with lighting.frag.
const float PI = 3.14159265359;

float D_GGX(float NdotH, float roughness) {
    float a  = roughness * roughness;
    float a2 = a * a;
    float d  = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}

float G_SmithGGX(float NdotV, float NdotL, float roughness) {
    float k  = (roughness + 1.0) * (roughness + 1.0) / 8.0;
    float gv = NdotV / (NdotV * (1.0 - k) + k);
    float gl = NdotL / (NdotL * (1.0 - k) + k);
    return gv * gl;
}

vec3 F_Schlick(float HdotV, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - HdotV, 0.0, 1.0), 5.0);
}

// PCF 5×5, 2-texel step — IDENTICAL kernel to lighting.frag::shadowPCF.
// PCSS intentionally omitted: the difference is invisible at 512×512 bake resolution.
float shadowPCF(vec4 fragPosLightSpace, vec3 normal, vec3 lightDir) {
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords      = projCoords * 0.5 + 0.5;
    if (projCoords.z > 1.0) return 0.0;

    float bias      = max(0.005 * (1.0 - dot(normal, lightDir)), 0.001);
    float shadow    = 0.0;
    vec2  texelSize = 1.0 / textureSize(shadowMap, 0);

    for (int x = -2; x <= 2; ++x)
    for (int y = -2; y <= 2; ++y) {
        float d = texture(shadowMap, projCoords.xy + vec2(x, y) * texelSize * 2.0).r;
        shadow += (projCoords.z - bias > d) ? 1.0 : 0.0;
    }
    return shadow / 25.0;
}

void main() {
    vec3 N = normalize(WorldNormal);
    vec3 V = normalize(viewPos - WorldPos);

    // Albedo: sRGB → linear when textured (mirrors gbuffer.frag)
    vec3 albedo = hasTexture
        ? pow(texture(texture_diffuse1, TexCoord).rgb, vec3(2.2))
        : albedoColor;

    float roughness = hasRoughnessTex ? texture(roughnessMap, TexCoord).r : matRoughness;
    float metallic  = hasMetallicTex  ? texture(metallicMap,  TexCoord).r : matMetallic;
    float ao        = hasAOTex        ? texture(aoMap,         TexCoord).r : 1.0;

    float NdotV = max(dot(N, V), 0.0);

    // Shadow from light[0] only — identical gate to lighting.frag
    float shadow0 = 0.0;
    if (useShadows) {
        vec3 shadowL           = normalize(lightPositions[0] - WorldPos);
        vec4 fragPosLightSpace = lightSpaceMatrix * vec4(WorldPos, 1.0);
        shadow0                = shadowPCF(fragPosLightSpace, N, shadowL);
    }

    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    vec3 Lo = vec3(0.0);

    // ── Light loop — IDENTICAL structure, attenuation, and BRDF to lighting.frag ──
    for (int i = 0; i < numLights; ++i) {
        vec3  Li     = normalize(lightPositions[i] - WorldPos);
        vec3  Hi     = normalize(V + Li);
        float NdotLi = max(dot(N, Li), 0.0);
        float NdotHi = max(dot(N, Hi), 0.0);
        float HdotVi = max(dot(Hi, V), 0.0);

        float dist_i = length(lightPositions[i] - WorldPos);
        float att2_i = dist_i * dist_i / (lightRadius * lightRadius);
        float att_i  = max(0.0, 1.0 - att2_i);
        att_i        = att_i * att_i;

        vec3  Fi      = F_Schlick(HdotVi, F0);
        float Di      = D_GGX(NdotHi, roughness);
        float Gi      = G_SmithGGX(NdotV, NdotLi, roughness);

        vec3 specBRDF = Di * Gi * Fi / max(4.0 * NdotV * NdotLi, 0.001);
        vec3 kDi      = (vec3(1.0) - Fi) * (1.0 - metallic);
        vec3 diffBRDF = kDi * albedo / PI;

        vec3 radiance = lightColors[i] * att_i * lightIntensities[i];

        float s = (i == 0) ? shadow0 : 0.0;
        Lo += (diffBRDF + specBRDF) * radiance * NdotLi * (1.0 - s);
    }

    Lo *= ao;

    // Emissive — additive, bypasses BRDF (identical intent to lighting.frag)
    // RGB16F cubemap stores values > 1.0 so the bright ceiling carries full brightness.
    vec3 emissive = emissiveColor * emissiveStrength;

    FragColor = vec4(Lo + emissive, 1.0);
}
