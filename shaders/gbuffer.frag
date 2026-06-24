#version 410 core
layout (location = 0) out vec3 gPosition;
layout (location = 1) out vec4 gNormal;
layout (location = 2) out vec4 gAlbedoSpec;

in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoords;

uniform vec3 colorOverride;
uniform float roughness;
uniform float metallic;

uniform bool isTVScreen;
uniform float time;

void main() {
    gPosition = FragPos;
    gNormal.rgb = normalize(Normal);
    gNormal.a = metallic;
    
    // Store Albedo (RGB) and Roughness (A)
    if (isTVScreen) {
        // If colorOverride is close to zero, it means the TV is off/blown
        if (length(colorOverride) < 0.01) {
            gAlbedoSpec.rgb = vec3(0.0);
        } else if (time < 7.0) {
            // TV Program Mockup: Shifting color bars / movie scenes
            float shift = sin(time * 1.5) * 0.5 + 0.5;
            vec3 skyColor = mix(vec3(0.1, 0.35, 0.75), vec3(0.85, 0.45, 0.15), shift);
            float mountain = step(0.35 + 0.1 * sin(TexCoords.x * 3.1415), TexCoords.y);
            vec3 movie = mix(vec3(0.05, 0.08, 0.12), skyColor, mountain);
            // Add a small flickering scanline or screen curvature/glow
            movie *= 0.95 + 0.05 * sin(TexCoords.y * 180.0 + time * 12.0);
            gAlbedoSpec.rgb = movie;
        } else {
            // SMPTE / PAL Color bar colors in order: White, Yellow, Cyan, Green, Magenta, Red, Blue
            vec3 colors[7];
            colors[0] = vec3(1.0, 1.0, 1.0); // White
            colors[1] = vec3(1.0, 1.0, 0.0); // Yellow
            colors[2] = vec3(0.0, 1.0, 1.0); // Cyan
            colors[3] = vec3(0.0, 1.0, 0.0); // Green
            colors[4] = vec3(1.0, 0.0, 1.0); // Magenta
            colors[5] = vec3(1.0, 0.0, 0.0); // Red
            colors[6] = vec3(0.0, 0.0, 1.0); // Blue
            
            int colIdx = int(TexCoords.x * 7.0);
            colIdx = clamp(colIdx, 0, 6);
            vec3 barColor = colors[colIdx];
            
            // Add procedural white noise static on top (flickering/animated)
            float n = fract(sin(dot(TexCoords.xy * (time + 1.0), vec2(12.9898, 78.233))) * 43758.5453);
            
            // Blend color bars with noise (e.g. 60% color bars, 40% noise)
            gAlbedoSpec.rgb = mix(barColor, vec3(n), 0.4);
        }
    } else {
        gAlbedoSpec.rgb = colorOverride;
    }
    gAlbedoSpec.a = roughness;
    
    // Note: To fully support MRA (Metallic, Roughness, AO), we might need another render target.
    // For now, we pack roughness in alpha of albedo.
}
