#version 410 core
out vec4 FragColor;

in vec2 TexCoord;

// ─── G-Buffer inputs ─────────────────────────────────────────────────────────
uniform sampler2D gPosition;    // xyz=world pos, w=baked AO
uniform sampler2D gNormal;      // xyz=normal (mapped), w=metallic
uniform sampler2D gAlbedoSpec;  // rgb=albedo, a=roughness
uniform sampler2D gEmissive;
uniform sampler2D ssaoTexture;
uniform sampler2D shadowMap;
uniform sampler2D reflectionTex;

// ─── Ceiling light array ──────────────────────────────────────────────────────
const int MAX_LIGHTS = 4;
uniform int   numLights;
uniform vec3  lightPositions[MAX_LIGHTS];
uniform vec3  lightColors[MAX_LIGHTS];
uniform float lightIntensities[MAX_LIGHTS];
uniform float lightRadius;          // shared attenuation radius for all lights

// ─── Camera & shadow ─────────────────────────────────────────────────────────
uniform vec3  viewPos;
uniform mat4  lightSpaceMatrix;

// ─── Reflection ───────────────────────────────────────────────────────────────
uniform bool  reflectionEnable;
uniform mat4  reflProjView;
uniform float reflectivity;
uniform float glossyBlur;

// ─── Feature toggles ─────────────────────────────────────────────────────────
uniform bool  useShadows;
uniform bool  useSoftShadows;
uniform bool  useAO;

// ─── Shadow: wide PCF — 5×5 kernel, 2-texel step ────────────────────────────
float shadowPCF(vec4 fragPosLightSpace, vec3 normal, vec3 lightDir) {
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords      = projCoords * 0.5 + 0.5;
    if (projCoords.z > 1.0) return 0.0;

    float bias      = max(0.005 * (1.0 - dot(normal, lightDir)), 0.001);
    float shadow    = 0.0;
    vec2  texelSize = 1.0 / textureSize(shadowMap, 0);

    // 5×5, step = 2 texels → covers ±4-texel diameter (soft contact shadow)
    for (int x = -2; x <= 2; ++x)
    for (int y = -2; y <= 2; ++y) {
        float d = texture(shadowMap, projCoords.xy + vec2(x,y) * texelSize * 2.0).r;
        shadow += (projCoords.z - bias > d) ? 1.0 : 0.0;
    }
    return shadow / 25.0;
}

// ─── Shadow: PCSS (soft) ─────────────────────────────────────────────────────
float findBlockerDistance(vec2 uv, float receiverDepth, float searchWidth) {
    float blockerSum  = 0.0;
    int   numBlockers = 0;
    float stepSize    = searchWidth / 4.0;

    for (float x = -searchWidth; x <= searchWidth; x += stepSize)
    for (float y = -searchWidth; y <= searchWidth; y += stepSize) {
        float shadowDepth = texture(shadowMap, uv + vec2(x, y)).r;
        if (shadowDepth < receiverDepth) {
            blockerSum += shadowDepth;
            numBlockers++;
        }
    }
    return (numBlockers > 0) ? blockerSum / float(numBlockers) : -1.0;
}

float shadowPCSS(vec4 fragPosLightSpace, vec3 normal, vec3 lightDir) {
    vec3 projCoords    = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords         = projCoords * 0.5 + 0.5;
    if (projCoords.z > 1.0) return 0.0;

    float bias         = max(0.005 * (1.0 - dot(normal, lightDir)), 0.001);
    float receiverDepth = projCoords.z - bias;

    float lightSize   = 0.18;
    float blockerDist = findBlockerDistance(projCoords.xy, receiverDepth, lightSize);
    if (blockerDist < 0.0) return 0.0;

    float penumbra    = (receiverDepth - blockerDist) * lightSize / blockerDist;

    float shadow      = 0.0;
    int   samples     = 16;
    float step        = penumbra / 2.0;
    for (int x = -2; x <= 2; ++x)
    for (int y = -2; y <= 2; ++y) {
        float d = texture(shadowMap, projCoords.xy + vec2(x,y) * step * 0.01).r;
        shadow += (receiverDepth > d) ? 1.0 : 0.0;
    }
    return shadow / float(samples + 1);
}

// ─── Cook-Torrance GGX BRDF helpers ─────────────────────────────────────────
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

// ─── Main lighting ───────────────────────────────────────────────────────────
void main() {
    vec2 uv = TexCoord;

    // Unpack G-buffer
    vec4  posAO    = texture(gPosition,   uv);
    vec3  FragPos  = posAO.rgb;
    float baked_ao = posAO.a;

    vec4  normMet  = texture(gNormal,     uv);
    vec3  N        = normalize(normMet.rgb);
    float metallic = normMet.a;

    vec4  albRough  = texture(gAlbedoSpec, uv);
    vec3  Albedo    = albRough.rgb;
    float roughness = albRough.a;

    // AO: SSAO from geometry × baked AO from texture
    float ssao_val = useAO ? texture(ssaoTexture, uv).r : 1.0;
    float ao       = ssao_val * baked_ao;

    vec3 V   = normalize(viewPos - FragPos);
    float NdotV = max(dot(N, V), 0.0);

    // Shadow — computed once from lightPositions[0] (the central shadow caster).
    // Only gates light[0]'s contribution; lights 1-3 are unshadowed fill.
    float shadow0 = 0.0;
    if (useShadows) {
        vec3 shadowL           = normalize(lightPositions[0] - FragPos);
        vec4 fragPosLightSpace = lightSpaceMatrix * vec4(FragPos, 1.0);
        shadow0 = useSoftShadows
                  ? shadowPCSS(fragPosLightSpace, N, shadowL)
                  : shadowPCF (fragPosLightSpace, N, shadowL);
    }

    // PBR base reflectance
    vec3 F0 = mix(vec3(0.04), Albedo, metallic);

    // Accumulate contributions from all ceiling lights
    vec3 Lo = vec3(0.0);
    for (int i = 0; i < numLights; ++i) {
        vec3  Li     = normalize(lightPositions[i] - FragPos);
        vec3  Hi     = normalize(V + Li);
        float NdotLi = max(dot(N, Li), 0.0);
        float NdotHi = max(dot(N, Hi), 0.0);
        float HdotVi = max(dot(Hi, V), 0.0);

        // Windowed quadratic attenuation
        float dist_i = length(lightPositions[i] - FragPos);
        float att2_i = dist_i * dist_i / (lightRadius * lightRadius);
        float att_i  = max(0.0, 1.0 - att2_i);
        att_i        = att_i * att_i;

        vec3  Fi      = F_Schlick(HdotVi, F0);
        float Di      = D_GGX(NdotHi, roughness);
        float Gi      = G_SmithGGX(NdotV, NdotLi, roughness);

        vec3 specBRDF = Di * Gi * Fi / max(4.0 * NdotV * NdotLi, 0.001);
        vec3 kDi      = (vec3(1.0) - Fi) * (1.0 - metallic);
        vec3 diffBRDF = kDi * Albedo / PI;

        vec3 radiance = lightColors[i] * att_i * lightIntensities[i];

        // Shadow modulates only light[0]; lights 1-3 are unshadowed fill
        float s = (i == 0) ? shadow0 : 0.0;
        Lo += (diffBRDF + specBRDF) * radiance * NdotLi * (1.0 - s);
    }

    // AO gates the full direct illumination sum
    Lo *= ao;

    // Emissive — additive, bypasses shadow/AO/BRDF, feeds bloom
    vec3 emissive = texture(gEmissive, uv).rgb;

    vec3 lighting = Lo + emissive;

    // Planar floor reflection — only on upward-facing fragments near Y=0
    if (reflectionEnable && N.y > 0.99 && FragPos.y < 0.01) {
        vec4 reflClip = reflProjView * vec4(FragPos, 1.0);
        if (reflClip.w > 0.0) {
            vec2 reflUV = (reflClip.xy / reflClip.w) * 0.5 + 0.5;

            vec3 reflColor;
            if (glossyBlur > 0.0) {
                vec2 blurStep = vec2(glossyBlur) / vec2(textureSize(reflectionTex, 0));
                reflColor = vec3(0.0);
                for (int dx = -1; dx <= 1; ++dx)
                for (int dy = -1; dy <= 1; ++dy)
                    reflColor += texture(reflectionTex, reflUV + vec2(float(dx), float(dy)) * blurStep).rgb;
                reflColor /= 9.0;
            } else {
                reflColor = texture(reflectionTex, reflUV).rgb;
            }

            // Schlick Fresnel (F0 = 0.03 for non-metallic polished floor)
            float cosTheta = max(dot(N, V), 0.0);
            float F0_refl  = 0.03;
            float fresnel  = F0_refl + (1.0 - F0_refl) * pow(1.0 - cosTheta, 5.0);

            lighting += reflColor * fresnel * reflectivity * (1.0 - shadow0);
        }
    }

    FragColor = vec4(lighting, 1.0);
}
