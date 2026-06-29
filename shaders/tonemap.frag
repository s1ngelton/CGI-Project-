#version 410 core
out vec4 FragColor;

in vec2 TexCoord;

uniform sampler2D hdrBuffer;
uniform float     exposure;
uniform int       tonemapOp;   // 0 = ACES filmic, 1 = Reinhard

// ─── Color grade uniforms ────────────────────────────────────────────────────
uniform bool  gradeEnable;
uniform float temperature;      // -1 (cool/blue) .. +1 (warm/orange)
uniform vec3  gradeTint;        // per-channel multiply, default (1,1,1)
uniform float saturation;       // 0 = greyscale, 1 = identity, 2 = hyper
uniform vec3  shadowLift;       // additive tint for dark areas, default (0,0,0)

// ─── Vignette uniforms ───────────────────────────────────────────────────────
uniform float vignetteStrength; // 0 = off, 1 = max
uniform float vignetteSoftness; // controls where the fade starts (0..1)

// ─── Tone-map helpers ────────────────────────────────────────────────────────
// Narkowicz 2015 ACES approximation
vec3 tonemapACES(vec3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

vec3 tonemapReinhard(vec3 x) {
    return x / (x + vec3(1.0));
}

// ─── Color grade (operates in linear HDR space, before tonemapping) ──────────
vec3 colorGrade(vec3 c) {
    // Temperature: blue-orange correlated shift
    c.r *= 1.0 + temperature * 0.3;
    c.b *= 1.0 - temperature * 0.3;
    c    = max(c, 0.0);

    // Per-channel tint
    c *= gradeTint;

    // Shadow lift: additive tint weighted toward dark pixels
    float grey = dot(c, vec3(0.2126, 0.7152, 0.0722));
    c += shadowLift * max(0.0, 1.0 - grey);

    // Saturation
    float luma = dot(c, vec3(0.2126, 0.7152, 0.0722));
    c = mix(vec3(luma), c, saturation);

    return max(c, 0.0);
}

void main() {
    vec3 hdr = texture(hdrBuffer, TexCoord).rgb * exposure;

    // Color grade in linear space (before tonemap)
    if (gradeEnable)
        hdr = colorGrade(hdr);

    // Tone map
    vec3 mapped = (tonemapOp == 0) ? tonemapACES(hdr) : tonemapReinhard(hdr);

    // sRGB gamma — one and only gamma conversion
    mapped = pow(mapped, vec3(1.0 / 2.2));

    // Vignette — multiplicative darkening AFTER gamma (photographic lens look)
    if (vignetteStrength > 0.0) {
        float r        = length(TexCoord - vec2(0.5));
        // 0.7071 ≈ max corner distance from centre in UV space
        float vignette = 1.0 - vignetteStrength *
                         smoothstep(vignetteSoftness * 0.7071, 0.7071, r);
        mapped *= vignette;
    }

    FragColor = vec4(mapped, 1.0);
}
