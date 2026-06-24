#version 410 core
out vec4 FragColor;

in vec2 TexCoords;

uniform sampler2D screenTexture;
uniform sampler2D gDepth;

uniform float focusDistance;
uniform float focusRange;

void main() {
    float depth = texture(gDepth, TexCoords).r;
    // Linearize depth (assuming standard perspective)
    float zNear = 0.1; 
    float zFar  = 100.0; 
    float linearDepth = (2.0 * zNear * zFar) / (zFar + zNear - (depth * 2.0 - 1.0) * (zFar - zNear));
    
    float blur = smoothstep(focusDistance - focusRange, focusDistance, linearDepth) + 
                 smoothstep(focusDistance, focusDistance + focusRange, linearDepth);
    blur = clamp(blur - 1.0, 0.0, 1.0); // 0 at focus, 1 far away
    
    // Simple blur 
    vec2 texelSize = 1.0 / textureSize(screenTexture, 0);
    vec3 result = vec3(0.0);
    
    int radius = int(blur * 3.0); // max 3 pixel radius
    if (radius > 0) {
        float weightSum = 0.0;
        for (int x = -radius; x <= radius; ++x) {
            for (int y = -radius; y <= radius; ++y) {
                result += texture(screenTexture, TexCoords + vec2(x, y) * texelSize).rgb;
                weightSum += 1.0;
            }
        }
        result /= weightSum;
    } else {
        result = texture(screenTexture, TexCoords).rgb;
    }
    
    FragColor = vec4(result, 1.0);
}
