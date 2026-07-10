#version 430
in vec2 TexCoord;
uniform sampler2D noisyImage;   // m_screenTex
uniform sampler2D guidePos;     // reflPos
uniform sampler2D guideNorm;    // reflNorm

uniform float sigma_depth = 0.5;
uniform float sigma_normal = 0.3;
uniform float sigma_color = 1.0;  // HIGH for HDR values

out vec4 FragColor;

void main() {
    ivec2 size = textureSize(noisyImage, 0);
    vec2 uv = TexCoord;

    vec3 centerPos = texture(guidePos, uv).xyz;
    vec3 centerNorm = texture(guideNorm, uv).xyz;
    vec3 centerCol = texture(noisyImage, uv).rgb;

    // If sky (no guide), keep original color
    if (length(centerPos) < 0.001) {
        FragColor = vec4(centerCol, 1.0);
        return;
    }

    vec3 sum = vec3(0.0);
    float totalWeight = 0.0;

    // 9x9 kernel
    for (int dx = -4; dx <= 4; ++dx) {
        for (int dy = -4; dy <= 4; ++dy) {
            vec2 offset = vec2(dx, dy) / vec2(size);
            vec2 sampleUV = uv + offset;
            
            vec3 samplePos = texture(guidePos, sampleUV).xyz;
            vec3 sampleNorm = texture(guideNorm, sampleUV).xyz;
            vec3 sampleCol = texture(noisyImage, sampleUV).rgb;

            float depthWeight = exp(-pow(length(centerPos - samplePos), 2.0) / (2.0 * sigma_depth * sigma_depth));
            float normalWeight = exp(-pow(1.0 - dot(centerNorm, sampleNorm), 2.0) / (2.0 * sigma_normal * sigma_normal));
            float colorWeight = exp(-pow(length(centerCol - sampleCol), 2.0) / (2.0 * sigma_color * sigma_color));

            float weight = depthWeight * normalWeight * colorWeight;
            
            sum += sampleCol * weight;
            totalWeight += weight;
        }
    }

    FragColor = vec4(sum / max(totalWeight, 0.0001), 1.0);
}