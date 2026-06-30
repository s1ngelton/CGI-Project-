#version 410 core
out vec4 FragColor;

in vec2 TexCoord;

uniform sampler2D hdrBuffer;
uniform float     threshold;   // luminance cutoff (typically 1.0)
uniform float     knee;        // soft-knee half-width in luminance units

void main() {
    vec3  color = texture(hdrBuffer, TexCoord).rgb;
    float luma  = dot(color, vec3(0.2126, 0.7152, 0.0722));

    // Soft-knee curve (Unity-style):
    //   below (threshold - knee)  → weight = 0
    //   within knee region        → smooth quadratic ramp
    //   above (threshold + knee)  → weight → 1 for bright pixels
    float rq = clamp(luma - (threshold - knee), 0.0, 2.0 * knee);
    rq = (knee > 0.00001) ? (0.25 / knee) * rq * rq : 0.0;

    float weight = max(rq, luma - threshold) / max(luma, 0.00001);

    FragColor = vec4(color * weight, 1.0);
}
