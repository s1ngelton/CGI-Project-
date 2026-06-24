#version 410 core
out vec4 FragColor;

in vec2 TexCoords;

uniform sampler2D screenTexture;
uniform sampler2D gDepth;

uniform mat4 currentViewProjInverse;
uniform mat4 prevViewProj;
uniform int blurSamples;

void main() {
    vec2 texCoords = TexCoords;
    float zOverW = texture(gDepth, texCoords).r;
    
    // H pos
    vec4 H = vec4(texCoords.x * 2.0 - 1.0, texCoords.y * 2.0 - 1.0, zOverW * 2.0 - 1.0, 1.0);
    
    // Transform by inverse view-proj
    vec4 D = currentViewProjInverse * H;
    vec4 worldPos = D / D.w;
    
    // Transform by previous view-proj
    vec4 currentPos = H;
    vec4 previousPos = prevViewProj * worldPos;
    previousPos /= previousPos.w;
    
    // Velocity vector
    vec2 velocity = (currentPos.xy - previousPos.xy) / 2.0;
    
    vec4 color = texture(screenTexture, texCoords);
    texCoords += velocity;
    
    // Simple motion blur
    for(int i = 1; i < blurSamples; ++i, texCoords += velocity) {
        color += texture(screenTexture, texCoords);
    }
    FragColor = color / float(blurSamples);
}
