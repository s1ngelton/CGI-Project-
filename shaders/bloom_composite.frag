#version 410 core
out vec4 FragColor;
in vec2 TexCoord;

uniform sampler2D hdrBuffer;   // <-- ADD THIS (the sharp image)
uniform sampler2D bloomBlur;   // the blurred glow
uniform float bloomIntensity;

void main() {
    vec3 hdr = texture(hdrBuffer, TexCoord).rgb;   // sharp original
    vec3 bloom = texture(bloomBlur, TexCoord).rgb; // blurred glow
    vec3 result = hdr + bloom * bloomIntensity;    // ADD them
    FragColor = vec4(result, 1.0);
}