#version 410 core
out vec4 FragColor;

in vec2 TexCoord;

uniform sampler2D hdrBuffer;
uniform float     exposure;
uniform int       tonemapOp;   // 0 = ACES filmic, 1 = Reinhard

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

void main() {
    vec3 hdr    = texture(hdrBuffer, TexCoord).rgb * exposure;
    vec3 mapped = (tonemapOp == 0) ? tonemapACES(hdr) : tonemapReinhard(hdr);

    // sRGB gamma (approximation; good enough for realtime)
    mapped = pow(mapped, vec3(1.0 / 2.2));

    FragColor = vec4(mapped, 1.0);
}
