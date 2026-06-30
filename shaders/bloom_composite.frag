#version 410 core
out vec4 FragColor;

in vec2 TexCoord;

uniform sampler2D bloomBlur;
uniform float     bloomIntensity;

void main() {
    FragColor = vec4(texture(bloomBlur, TexCoord).rgb * bloomIntensity, 1.0);
}
