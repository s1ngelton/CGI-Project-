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

// ─── Light & camera ──────────────────────────────────────────────────────────
uniform vec3  lightPos;
uniform vec3  lightColor;
uniform float lightIntensity;
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

// ─── Shadow: PCF (hard) ──────────────────────────────────────────────────────
float shadowPCF(vec4 fragPosLightSpace, vec3 normal, vec3 lightDir) {
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords      = projCoords * 0.5 + 0.5;
    if (projCoords.z > 1.0) return 0.0;

    float bias      = max(0.005 * (1.0 - dot(normal, lightDir)), 0.001);
    float shadow    = 0.0;
    vec2  texelSize = 1.0 / textureSize(shadowMap, 0);

    for (int x = -1; x <= 1; ++x)
    for (int y = -1; y <= 1; ++y) {
        float pcfDepth = texture(shadowMap, projCoords.xy + vec2(x,y) * texelSize).r;
        shadow += (projCoords.z - bias > pcfDepth) ? 1.0 : 0.0;
    }
    return shadow / 9.0;
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

    float lightSize   = 0.05;
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
    float k   = (roughness + 1.0) * (roughness + 1.0) / 8.0;
    float gv  = NdotV / (NdotV * (1.0 - k) + k);
    float gl  = NdotL / (NdotL * (1.0 - k) + k);
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

    vec4  albRough = texture(gAlbedoSpec, uv);
    vec3  Albedo   = albRough.rgb;
    float roughness = albRough.a;

    // AO: combine SSAO with baked per-mesh AO map
    float ssao_val = useAO ? texture(ssaoTexture, uv).r : 1.0;
    float ao       = ssao_val * baked_ao;

    // Directions
    vec3 V = normalize(viewPos  - FragPos);
    vec3 L = normalize(lightPos - FragPos);
    vec3 H = normalize(V + L);

    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.0);
    float NdotH = max(dot(N, H), 0.0);
    float HdotV = max(dot(H, V), 0.0);

    // Attenuation — smooth windowed falloff
    float dist        = length(lightPos - FragPos);
    const float LIGHT_RADIUS = 5.5;
    float att2        = dist * dist / (LIGHT_RADIUS * LIGHT_RADIUS);
    float attenuation = max(0.0, 1.0 - att2);
    attenuation       = attenuation * attenuation;

    // Shadow
    float shadow = 0.0;
    if (useShadows) {
        vec4 fragPosLightSpace = lightSpaceMatrix * vec4(FragPos, 1.0);
        shadow = useSoftShadows
                 ? shadowPCSS(fragPosLightSpace, N, L)
                 : shadowPCF (fragPosLightSpace, N, L);
    }

    // PBR — metallic-roughness workflow
    vec3 F0 = mix(vec3(0.04), Albedo, metallic);
    vec3 F  = F_Schlick(HdotV, F0);

    float D = D_GGX(NdotH, roughness);
    float G = G_SmithGGX(NdotV, NdotL, roughness);

    vec3 specBRDF = D * G * F / max(4.0 * NdotV * NdotL, 0.001);

    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
    vec3 diffBRDF = kD * Albedo / PI;

    // Radiance from our single point light
    vec3 radiance = lightColor * attenuation * lightIntensity;

    // Direct illumination — shadow and AO both gate it
    vec3 Lo = (diffBRDF + specBRDF) * radiance * NdotL * (1.0 - shadow) * ao;

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

            lighting += reflColor * fresnel * reflectivity * (1.0 - shadow);
        }
    }

    FragColor = vec4(lighting, 1.0);
}
