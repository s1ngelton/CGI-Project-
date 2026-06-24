#version 410 core
out vec4 FragColor;

in vec2 TexCoords;

uniform sampler2D gPosition;
uniform sampler2D gNormal; // rgb = Normal, a = Metallic
uniform sampler2D gAlbedoSpec; // rgb = Albedo, a = Roughness
uniform sampler2D shadowMap;
uniform sampler2D ssao;

uniform sampler2D prevFrameColor;
uniform mat4 view;
uniform mat4 projection;

uniform vec3 viewPos;
uniform vec3 lightPos;
uniform vec3 lightColor;
uniform mat4 lightSpaceMatrix;
uniform bool shadowsEnabled;
uniform bool pcssEnabled;
uniform bool ssaoEnabled;
uniform bool lightningActive;
uniform vec3 tvLightColor;

const float PI = 3.14159265359;

// --- Linearize Depth Helper ---
float LinearizeDepth(float depth) {
    float near = 0.1;
    float far = 25.0;
    return (2.0 * near * far) / (far + near - (depth * 2.0 - 1.0) * (far - near));
}

// --- PCSS Blocker Search ---
void FindBlocker(out float avgBlockerDepth, out float numBlockers, vec2 uv, float zReceiver, float searchWidth) {
    float blockerSum = 0.0;
    int blockers = 0;
    
    // 9-sample grid for blocker search
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            vec2 offset = vec2(x, y) * searchWidth;
            float shadowMapDepth = texture(shadowMap, uv + offset).r;
            if (shadowMapDepth < zReceiver) {
                blockers++;
                blockerSum += LinearizeDepth(shadowMapDepth);
            }
        }
    }
    
    numBlockers = float(blockers);
    avgBlockerDepth = blockerSum / max(numBlockers, 1.0);
}

// --- True PCSS Soft Shadow Calculation ---
float PCSS_Shadow(vec3 fragPos, vec3 normal) {
    vec4 fragPosLightSpace = lightSpaceMatrix * vec4(fragPos, 1.0);
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords = projCoords * 0.5 + 0.5; // Transform to [0,1]
    
    if (projCoords.z > 1.0) return 0.0;
    
    float currentDepth = projCoords.z;
    float linearReceiver = LinearizeDepth(currentDepth);
    
    // Dynamic bias based on slope
    vec3 lightDir = normalize(lightPos - fragPos);
    float bias = max(0.005 * (1.0 - dot(normal, lightDir)), 0.001);
    
    float shadow = 0.0;
    vec2 texelSize = 1.0 / textureSize(shadowMap, 0);
    
    if (pcssEnabled) {
        // Step 1: Blocker search
        float avgBlockerDepth = 0.0;
        float numBlockers = 0.0;
        float searchWidth = 5.0 * texelSize.x; // Blocker search region
        
        FindBlocker(avgBlockerDepth, numBlockers, projCoords.xy, currentDepth - bias, searchWidth);
        
        if (numBlockers < 1.0) return 0.0; // No shadow
        
        // Step 2: Penumbra estimation
        float lightSize = 0.5; // Size of our virtual light source
        float penumbraWidth = (linearReceiver - avgBlockerDepth) * lightSize / avgBlockerDepth;
        penumbraWidth = clamp(penumbraWidth, 0.0, 1.0);
        
        // Step 3: Filtering (PCF using penumbra width)
        float filterWidth = penumbraWidth * 20.0 * texelSize.x; // Scale filter size
        filterWidth = max(filterWidth, texelSize.x); // Clamp to at least 1 texel
        
        int samples = 0;
        for (int x = -2; x <= 2; ++x) {
            for (int y = -2; y <= 2; ++y) {
                vec2 offset = vec2(x, y) * filterWidth;
                float pcfDepth = texture(shadowMap, projCoords.xy + offset).r;
                shadow += (currentDepth - bias > pcfDepth) ? 1.0 : 0.0;
                samples++;
            }
        }
        shadow /= float(samples);
    } else {
        // Fallback to simple hard/PCF shadow
        float closestDepth = texture(shadowMap, projCoords.xy).r;
        shadow = (currentDepth - bias > closestDepth) ? 1.0 : 0.0;
    }
    
    return shadow;
}

// --- PBR Cook-Torrance BRDF Helpers ---
float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    
    float nom   = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;
    
    return nom / max(denom, 0.000001);
}

float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    
    float nom   = NdotV;
    float denom = NdotV * (1.0 - k) + k;
    
    return nom / denom;
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);
    return ggx1 * ggx2;
}

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 WorldSpaceSSR(vec3 fragPos, vec3 normal, float roughness, out float hitMask) {
    hitMask = 0.0;
    // Only reflect glossy surfaces (roughness <= 0.6)
    if (roughness > 0.6) return vec3(0.0);
    
    vec3 V = normalize(fragPos - viewPos);
    vec3 R = reflect(V, normal);
    
    int maxSteps = 50;
    float stepSize = 0.12;
    float threshold = 0.15;
    
    vec3 currentPos = fragPos + R * 0.1; // Offset to prevent self-reflection
    
    for (int i = 0; i < maxSteps; ++i) {
        currentPos += R * stepSize;
        
        vec4 proj = projection * view * vec4(currentPos, 1.0);
        vec3 ndc = proj.xyz / proj.w;
        vec2 uv = ndc.xy * 0.5 + 0.5;
        
        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) break;
        
        vec3 gPos = texture(gPosition, uv).rgb;
        if (gPos == vec3(0.0)) continue;
        
        float rayDist = length(currentPos - viewPos);
        float geomDist = length(gPos - viewPos);
        
        if (rayDist >= geomDist) {
            if (abs(rayDist - geomDist) < threshold) {
                hitMask = 1.0 - roughness;
                
                // Edge fading
                vec2 dCoords = smoothstep(0.0, 0.08, uv) * (1.0 - smoothstep(0.92, 1.0, uv));
                float edgeFade = dCoords.x * dCoords.y;
                hitMask *= edgeFade;
                
                return texture(prevFrameColor, uv).rgb;
            }
        }
    }
    return vec3(0.0);
}

void main() {
    vec3 FragPos = texture(gPosition, TexCoords).rgb;
    vec4 gNorm = texture(gNormal, TexCoords);
    vec3 Normal = gNorm.rgb;
    float Metallic = gNorm.a;
    
    vec4 albedoRoughness = texture(gAlbedoSpec, TexCoords);
    vec3 Albedo = pow(albedoRoughness.rgb, vec3(2.2)); // Linearize albedo
    float Roughness = max(albedoRoughness.a, 0.05); // Prevent 0 roughness singularities

    // Pitch-black shadow monster (absorbs all light, remains black even under lightning flashes)
    if (albedoRoughness.r < 0.0001 && albedoRoughness.g < 0.0001 && albedoRoughness.b < 0.0001) {
        FragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    
    // Retrieve SSAO
    float AmbientOcclusion = ssaoEnabled ? texture(ssao, TexCoords).r : 1.0;
    
    // View & Normal vectors
    vec3 N = normalize(Normal);
    vec3 V = normalize(viewPos - FragPos);
    
    // Base reflectivity for dielectrics (0.04) and metals (Albedo)
    vec3 F0 = vec3(0.04); 
    F0 = mix(F0, Albedo, Metallic);
    
    // Light vectors & attenuation
    vec3 L = normalize(lightPos - FragPos);
    vec3 H = normalize(V + L);
    
    float distance = length(lightPos - FragPos);
    float attenuation = 1.0 / (distance * distance); // Inverse square law for PBR realism
    if (lightningActive) {
        attenuation = 0.35; // Keep lightning flashes bright across the whole room (acting as global sky flash)
    }
    vec3 radiance = lightColor * attenuation;
    
    // Cook-Torrance BRDF
    float NDF = DistributionGGX(N, H, Roughness);   
    float G   = GeometrySmith(N, V, L, Roughness);    
    vec3 F    = fresnelSchlick(max(dot(H, V), 0.0), F0);           
    
    vec3 numerator    = NDF * G * F; 
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001; // prevent divide by zero
    vec3 specular = numerator / denominator;
    
    // kS is Fresnel factor (specular reflection)
    vec3 kS = F;
    // kD is diffuse refraction (conservation of energy)
    vec3 kD = vec3(1.0) - kS;
    kD *= 1.0 - Metallic;     
    
    float NdotL = max(dot(N, L), 0.0);        
    
    // Shadows
    float shadow = shadowsEnabled ? PCSS_Shadow(FragPos, N) : 0.0;
    
    // Outgoing radiance
    vec3 Lo = (kD * Albedo / PI + specular) * radiance * NdotL * (1.0 - shadow);
    
    // TV light contribution (PBR Point/Spot light at TV position)
    if (length(tvLightColor) > 0.01) {
        vec3 tvLightPos = vec3(2.0, 1.0, 4.2); // Slightly in front of screen Z=3.675
        vec3 tvL = normalize(tvLightPos - FragPos);
        float tvDist = length(tvLightPos - FragPos);
        
        vec3 tvH = normalize(V + tvL);
        float tvAttenuation = 1.0 / (tvDist * tvDist + 0.1);
        vec3 tvRadiance = tvLightColor * 4.0 * tvAttenuation; // PBR intensity
        
        // Spot-like directional cutoff (only shine towards the room (+Z), not behind the TV)
        float tvCosTheta = dot(tvL, vec3(0.0, 0.0, 1.0)); // TV faces +Z
        if (tvCosTheta > 0.0) {
            float tvNDF = DistributionGGX(N, tvH, Roughness);
            float tvG = GeometrySmith(N, V, tvL, Roughness);
            vec3 tvF = fresnelSchlick(max(dot(tvH, V), 0.0), F0);
            
            vec3 tvNumerator = tvNDF * tvG * tvF;
            float tvDenominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, tvL), 0.0) + 0.0001;
            vec3 tvSpecular = tvNumerator / tvDenominator;
            
            vec3 tvKS = tvF;
            vec3 tvKD = vec3(1.0) - tvKS;
            tvKD *= 1.0 - Metallic;
            
            float tvNdotL = max(dot(N, tvL), 0.0);
            Lo += (tvKD * Albedo / PI + tvSpecular) * tvRadiance * tvNdotL * tvCosTheta;
        }
    }
    
    // Ambient term
    vec3 ambient = vec3(0.03) * Albedo * AmbientOcclusion;
    bool isCeilingLightOn = (length(lightColor) > 0.01) && !lightningActive;
    bool isTVLightOn = (length(tvLightColor) > 0.01);
    if (!isCeilingLightOn && !lightningActive && !isTVLightOn) {
        ambient = vec3(0.0); // Absolute pitch black darkness (dunkel mode)
    } else if (lightningActive) {
        ambient = vec3(0.08, 0.1, 0.12) * Albedo; // Atmospheric scatter under lightning flash (hell mode)
    }
    
    // Final composite color before tonemapping
    vec3 color = ambient + Lo;
    
    // Screen Space Reflections (SSR)
    float ssrMask = 0.0;
    vec3 ssrColor = WorldSpaceSSR(FragPos, N, Roughness, ssrMask);
    if (ssrMask > 0.0) {
        vec3 F_ssr = fresnelSchlick(max(dot(N, V), 0.0), F0);
        color = mix(color, ssrColor, F_ssr * ssrMask);
    }
    
    FragColor = vec4(color, 1.0);
}
