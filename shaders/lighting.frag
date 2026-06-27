#version 410 core
out vec4 FragColor;

in vec2 TexCoord;

// ─── G-Buffer inputs ─────────────────────────────────────────────────────────
uniform sampler2D gPosition;
uniform sampler2D gNormal;
uniform sampler2D gAlbedoSpec;
uniform sampler2D ssaoTexture;
uniform sampler2D shadowMap;

// ─── Light & camera ──────────────────────────────────────────────────────────
uniform vec3  lightPos;
uniform vec3  lightColor;
uniform vec3  viewPos;
uniform mat4  lightSpaceMatrix;

// ─── Feature toggles ─────────────────────────────────────────────────────────
uniform bool  useShadows;
uniform bool  useSoftShadows;   // PCSS
uniform bool  useAO;

// ─── Shadow: PCF (hard) ──────────────────────────────────────────────────────
float shadowPCF(vec4 fragPosLightSpace, vec3 normal, vec3 lightDir) {
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords      = projCoords * 0.5 + 0.5;
    if (projCoords.z > 1.0) return 0.0;

    float bias      = max(0.005 * (1.0 - dot(normal, lightDir)), 0.001);
    float shadow    = 0.0;
    vec2  texelSize = 1.0 / textureSize(shadowMap, 0);

    // 3×3 PCF kernel
    for (int x = -1; x <= 1; ++x)
    for (int y = -1; y <= 1; ++y) {
        float pcfDepth = texture(shadowMap, projCoords.xy + vec2(x,y) * texelSize).r;
        shadow += (projCoords.z - bias > pcfDepth) ? 1.0 : 0.0;
    }
    return shadow / 9.0;
}

// ─── Shadow: PCSS (soft) ─────────────────────────────────────────────────────
// Step 1: find average blocker depth in a search region
float findBlockerDistance(vec2 uv, float receiverDepth, float searchWidth) {
    float blockerSum   = 0.0;
    int   numBlockers  = 0;
    float stepSize     = searchWidth / 4.0;

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

    // Step 1: blocker search
    float lightSize    = 0.05;
    float blockerDist  = findBlockerDistance(projCoords.xy, receiverDepth, lightSize);
    if (blockerDist < 0.0) return 0.0;   // fully lit

    // Step 2: penumbra width
    float penumbra     = (receiverDepth - blockerDist) * lightSize / blockerDist;

    // Step 3: PCF with penumbra-sized kernel
    float shadow       = 0.0;
    int   samples      = 16;
    float step         = penumbra / 2.0;
    for (int x = -2; x <= 2; ++x)
    for (int y = -2; y <= 2; ++y) {
        float d = texture(shadowMap, projCoords.xy + vec2(x,y) * step * 0.01).r;
        shadow += (receiverDepth > d) ? 1.0 : 0.0;
    }
    return shadow / float(samples + 1);
}

// ─── Main lighting ───────────────────────────────────────────────────────────
void main() {
    vec2 uv          = TexCoord;
    vec3 FragPos     = texture(gPosition,   uv).rgb;
    vec3 Normal      = texture(gNormal,     uv).rgb;
    vec4 AlbSpec     = texture(gAlbedoSpec, uv);
    vec3 Albedo      = AlbSpec.rgb;
    float Specular   = AlbSpec.a;
    float ao         = useAO ? texture(ssaoTexture, uv).r : 1.0;

    vec3  lightDir   = normalize(lightPos - FragPos);
    vec3  viewDir    = normalize(viewPos  - FragPos);
    vec3  halfDir    = normalize(lightDir + viewDir);
    float dist       = length(lightPos - FragPos);
    float attenuation = 1.0 / (1.0 + 0.09 * dist + 0.032 * dist * dist);

    // Ambient (AO modulated)
    vec3 ambient     = 0.15 * Albedo * ao;

    // Diffuse (Lambertian)
    float diff       = max(dot(Normal, lightDir), 0.0);
    vec3  diffuse    = diff * lightColor * Albedo * attenuation;

    // Specular (Blinn-Phong)
    float spec       = pow(max(dot(Normal, halfDir), 0.0), 32.0) * Specular;
    vec3  specular   = spec * lightColor * attenuation;

    // Shadow
    float shadow     = 0.0;
    if (useShadows) {
        vec4 fragPosLightSpace = lightSpaceMatrix * vec4(FragPos, 1.0);
        shadow = useSoftShadows
                 ? shadowPCSS(fragPosLightSpace, Normal, lightDir)
                 : shadowPCF (fragPosLightSpace, Normal, lightDir);
    }

    vec3 lighting    = ambient + (1.0 - shadow) * (diffuse + specular);
    FragColor        = vec4(lighting, 1.0);
}
