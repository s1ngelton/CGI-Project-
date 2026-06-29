#version 410 core

// ─── G-Buffer geometry pass fragment shader ──────────────────────────────────
// Writes world-space data for later deferred lighting passes

layout (location = 0) out vec3 gPosition;
layout (location = 1) out vec3 gNormal;
layout (location = 2) out vec4 gAlbedoSpec;
layout (location = 3) out vec2 gVelocity;   // for motion blur (attachment unbound until MB lands)
layout (location = 4) out vec3 gEmissive;

in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoord;
in vec4 ClipPosCurr;
in vec4 ClipPosPrev;

uniform sampler2D texture_diffuse1;
uniform sampler2D texture_specular1;
uniform bool      hasTexture;
uniform vec3      albedoColor;      // fallback if no texture
uniform vec3      emissiveColor;    // emissive tint (default black)
uniform float     emissiveStrength; // multiplier — 0 = no emission

void main() {
    gPosition    = FragPos;
    gNormal      = normalize(Normal);

    vec3 albedo  = hasTexture
                   ? texture(texture_diffuse1, TexCoord).rgb
                   : albedoColor;
    float spec   = hasTexture
                   ? texture(texture_specular1, TexCoord).r
                   : 0.5;
    gAlbedoSpec  = vec4(albedo, spec);
    gEmissive    = emissiveColor * emissiveStrength;

    // Velocity in NDC space (used by motion blur pass)
    vec2 currNDC = (ClipPosCurr.xy / ClipPosCurr.w) * 0.5 + 0.5;
    vec2 prevNDC = (ClipPosPrev.xy / ClipPosPrev.w) * 0.5 + 0.5;
    gVelocity    = currNDC - prevNDC;
}
