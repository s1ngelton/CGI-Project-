#version 410 core
out float FragColor;

in vec2 TexCoord;

uniform sampler2D ssaoInput;

void main() {
    vec2  texel  = 1.0 / vec2(textureSize(ssaoInput, 0));
    float result = 0.0;

    // 4×4 box blur — matches the 4×4 noise tile, removing the repetition pattern
    for (int x = -2; x < 2; ++x)
    for (int y = -2; y < 2; ++y)
        result += texture(ssaoInput, TexCoord + vec2(x, y) * texel).r;

    FragColor = result / 16.0;
}
