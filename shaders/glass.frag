#version 410 core
out vec4 FragColor;

in vec3 WorldPos;
in vec3 WorldNormal;

uniform vec3  viewPos;
uniform float glassOpacity;   // base face-on opacity
uniform vec3  glassColor;     // near-black face-on tint
uniform vec3  fresnelColor;   // warm ceiling sheen at grazing angles

void main() {
    vec3 N = normalize(WorldNormal);
    vec3 V = normalize(viewPos - WorldPos);

    // Two-sided: abs so grazing reads correctly from either face
    float cosTheta = abs(dot(N, V));

    // Schlick Fresnel, F0 = 0.04 (glass/dielectric)
    float F0 = 0.04;
    float fresnel = F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);

    // Face-on: dark transparent; grazing: bright warm sheen
    vec3  color = mix(glassColor, fresnelColor, fresnel);
    float alpha = mix(glassOpacity, 0.92, fresnel);

    FragColor = vec4(color, alpha);
}
