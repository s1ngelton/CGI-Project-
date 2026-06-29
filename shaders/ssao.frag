#version 410 core
out float FragColor;

in vec2 TexCoord;

uniform sampler2D gPosition;   // world-space position
uniform sampler2D gNormal;     // world-space normal
uniform sampler2D texNoise;    // 4×4 random rotation around z

uniform vec3  samples[64];
uniform mat4  view;
uniform mat4  projection;
uniform vec2  noiseScale;      // screenSize / 4.0

const int   KERNEL_SIZE = 64;
const float RADIUS      = 0.5;
const float BIAS        = 0.025;

void main() {
    vec2 uv = TexCoord;

    vec3 fragPosW = texture(gPosition, uv).rgb;
    vec3 normalW  = normalize(texture(gNormal, uv).rgb);

    // Transform to view space so depth comparison is camera-relative
    vec3 fragPosVS = vec3(view * vec4(fragPosW, 1.0));
    // View matrix is a rigid transform, so mat3(view) is its own inverse-transpose
    vec3 normalVS  = normalize(mat3(view) * normalW);

    // Random rotation vector from tiling noise — stays in view-space xy plane
    vec3 rvec = normalize(texture(texNoise, uv * noiseScale).rgb);

    // Gram–Schmidt: build a TBN in view space oriented along the surface normal
    vec3 tangent   = normalize(rvec - normalVS * dot(rvec, normalVS));
    vec3 bitangent = cross(normalVS, tangent);
    mat3 TBN       = mat3(tangent, bitangent, normalVS);

    float occlusion = 0.0;
    for (int i = 0; i < KERNEL_SIZE; ++i) {
        // Tangent → view space, offset from fragment
        vec3 sampleVS = fragPosVS + TBN * samples[i] * RADIUS;

        // Project to screen-space UV
        vec4 offset = projection * vec4(sampleVS, 1.0);
        offset.xyz /= offset.w;
        offset.xy   = offset.xy * 0.5 + 0.5;

        // Fetch the actual geometry at that UV and bring it to view space
        vec3  occluderW  = texture(gPosition, offset.xy).rgb;
        float occluderZ  = (view * vec4(occluderW, 1.0)).z;

        // Range check so distant geometry doesn't contribute
        float rangeCheck = smoothstep(0.0, 1.0, RADIUS / abs(fragPosVS.z - occluderZ));
        occlusion += (occluderZ >= sampleVS.z + BIAS ? 1.0 : 0.0) * rangeCheck;
    }

    // Output inverted: 1.0 = fully lit, 0.0 = fully occluded
    FragColor = 1.0 - (occlusion / float(KERNEL_SIZE));
}
