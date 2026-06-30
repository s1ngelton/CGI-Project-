#version 410 core
out vec4 FragColor;

in vec3 WorldPos;
in vec3 WorldNormal;

uniform vec3  viewPos;
uniform float glassOpacity;
uniform vec3  glassColor;     // face-on tint (near-black)
uniform vec3  fresnelColor;   // fallback grazing tint when probe is off

// ── Cubemap reflection probe ──────────────────────────────────────────────────
uniform samplerCube envCubemap;
uniform bool        useProbe;
uniform vec3        probePos;   // world-space centre of the baked probe
uniform vec3        boxMin;     // room AABB min (for parallax correction)
uniform vec3        boxMax;     // room AABB max
uniform float       probeStrength;

// ── Ceiling lights (specular hotspot blobs) ───────────────────────────────────
const int MAX_LIGHTS = 4;
uniform vec3  lightPositions[MAX_LIGHTS];
uniform vec3  lightColors[MAX_LIGHTS];
uniform float lightIntensities[MAX_LIGHTS];
uniform int   numLights;
uniform float lightRadius;

void main() {
    vec3 N = normalize(WorldNormal);
    vec3 V = normalize(viewPos - WorldPos);

    // Two-sided: flip normal to always face the viewer
    vec3 Nf = (dot(N, V) < 0.0) ? -N : N;

    float cosTheta = abs(dot(Nf, V));
    float F0       = 0.04;
    float fresnel  = F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);

    // Reflection vector (used for both cubemap and specular hotspot)
    vec3 R = reflect(-V, Nf);

    // ── Box-projected cubemap reflection ──────────────────────────────────────
    // Intersect ray (WorldPos, R) with the room AABB, then redirect through the
    // probe origin — corrects parallax so large flat walls reflect at proper depth.
    // IEEE-754 ensures R=0 components produce ±inf which max/min handle naturally.
    vec3 envColor = fresnelColor;   // flat fallback when probe isn't ready
    if (useProbe) {
        vec3 firstPlane    = (boxMax - WorldPos) / R;
        vec3 secondPlane   = (boxMin - WorldPos) / R;
        vec3 furthestPlane = max(firstPlane, secondPlane);
        float dist         = min(min(furthestPlane.x, furthestPlane.y), furthestPlane.z);
        dist               = max(dist, 0.0);          // clamp: reflection exits the box
        vec3 correctedR    = normalize(WorldPos + R * dist - probePos);
        envColor           = texture(envCubemap, correctedR).rgb * probeStrength;
    }

    // Fresnel blend: face-on = dark tint, grazing = environment reflection
    vec3  color = mix(glassColor, envColor, fresnel);
    float alpha = mix(glassOpacity, 0.92, fresnel);

    // ── Additive specular hotspot from ceiling lights ─────────────────────────
    // Keeps a tight bright blob where the reflection direction aligns with a light.
    // Complements the cubemap (which gives broad parallax-correct reflections).
    vec3 specular = vec3(0.0);
    for (int i = 0; i < numLights; ++i) {
        vec3  toLight = lightPositions[i] - WorldPos;
        float d       = length(toLight);
        vec3  L       = toLight / d;

        float att2 = (d * d) / (lightRadius * lightRadius);
        float att  = clamp(1.0 - att2, 0.0, 1.0);
        att       *= att;

        float spec = pow(max(dot(R, L), 0.0), 128.0);
        specular  += lightColors[i] * (lightIntensities[i] * spec * att);
    }
    color += specular * 0.35;

    FragColor = vec4(color, alpha);
}
