#version 410 core
out vec4 FragColor;

in vec2 TexCoord;

uniform sampler2D image;
uniform bool      horizontal;

// 9-tap separable Gaussian (center + 4 offsets each side).
// Weights are normalized: sum = weight[0] + 2*(weight[1]+weight[2]+weight[3]+weight[4]) = 1.0
const float weight[5] = float[](
    0.2270270270,
    0.1945945946,
    0.1216216216,
    0.0540540541,
    0.0162162162
);

void main() {
    vec2 texel = 1.0 / vec2(textureSize(image, 0));
    vec3 result = texture(image, TexCoord).rgb * weight[0];

    if (horizontal) {
        for (int i = 1; i < 5; ++i) {
            result += texture(image, TexCoord + vec2(texel.x * float(i), 0.0)).rgb * weight[i];
            result += texture(image, TexCoord - vec2(texel.x * float(i), 0.0)).rgb * weight[i];
        }
    } else {
        for (int i = 1; i < 5; ++i) {
            result += texture(image, TexCoord + vec2(0.0, texel.y * float(i))).rgb * weight[i];
            result += texture(image, TexCoord - vec2(0.0, texel.y * float(i))).rgb * weight[i];
        }
    }

    FragColor = vec4(result, 1.0);
}
