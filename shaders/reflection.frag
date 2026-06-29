#version 410 core
out vec4 FragColor;

in vec3 WorldPos;
in vec3 WorldNormal;

uniform vec3  albedoColor;
uniform vec3  emissiveColor;
uniform float emissiveStrength;
uniform vec3  lightPos;
uniform vec3  lightColor;
uniform float lightIntensity;

void main() {
    // Emissive objects glow in the reflection as-is
    if (emissiveStrength > 0.0) {
        FragColor = vec4(emissiveColor * emissiveStrength, 1.0);
        return;
    }

    // Simple Lambertian for non-emissive — no shadows (cheap approximation)
    vec3  N    = normalize(WorldNormal);
    vec3  L    = normalize(lightPos - WorldPos);
    float dist = length(lightPos - WorldPos);

    const float LIGHT_RADIUS = 5.5;
    float att2 = dist * dist / (LIGHT_RADIUS * LIGHT_RADIUS);
    float att  = max(0.0, 1.0 - att2);
    att        = att * att;

    float diff  = max(dot(N, L), 0.0);
    vec3  color = diff * lightColor * albedoColor * att * lightIntensity;
    FragColor   = vec4(color, 1.0);
}
