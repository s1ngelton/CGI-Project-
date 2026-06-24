#version 410 core
out vec4 FragColor;

in vec2 TexCoords;

uniform sampler2D hdrBuffer;

void main() {
    vec3 hdrColor = texture(hdrBuffer, TexCoords).rgb;
    
    // Exposure tone mapping
    float exposure = 1.0;
    vec3 mapped = vec3(1.0) - exp(-hdrColor * exposure);
    
    // Gamma correction
    const float gamma = 2.2;
    mapped = pow(mapped, vec3(1.0 / gamma));
    
    FragColor = vec4(mapped, 1.0);
}
