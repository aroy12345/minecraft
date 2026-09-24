#version 330

uniform mat4 u_Model;       // Model matrix for the chunk being drawn
uniform mat4 u_ModelInvTr;  // Inverse transpose of the model matrix, for normals
uniform mat4 u_ViewProj;    // Camera view-projection matrix
uniform float u_Time;       // Seconds since launch, drives fluid animation

in vec4 vs_Pos;             // Vertex position (chunk-local space)
in vec4 vs_Nor;             // Vertex normal
in vec4 vs_Col;             // Vertex color; alpha < 1 marks water
in vec4 vs_UV;              // xy: atlas UV, z: 1 = animated fluid, w: 1 = water, 2 = lava

out vec4 fs_Pos;            // World-space position (used for fog)
out vec4 fs_Nor;
out vec4 fs_Col;
out vec2 fs_UV;
out vec2 fs_Flags;          // vs_UV.zw: animated-fluid and fluid-type flags

void main()
{
    vec2 uv = vs_UV.xy;
    vec3 nor = normalize(mat3(u_ModelInvTr) * vec3(vs_Nor));
    vec4 worldPos = u_Model * vs_Pos;

    // Water and lava scroll their UVs within the atlas tile over time
    if (vs_UV.z > 0.5) {
        uv += vec2((sin(u_Time * 0.6) * 0.5 + 0.5) * (1.0 / 16.0), 0.0);
    }

    if (vs_UV.w > 0.5 && vs_UV.w < 1.5) {
        // Water: every water vertex rides a world-position-based wave, so
        // adjacent faces stay sealed while the surface undulates. The
        // constant offset keeps the surface below the block top; the
        // downward bias hides water bottoms inside the floor below.
        float phaseX = worldPos.x * 0.65 + u_Time * 1.8;
        float phaseZ = worldPos.z * 0.50 + u_Time * 1.3;
        worldPos.y += sin(phaseX) * 0.06 + cos(phaseZ) * 0.06 - 0.15;
        if (vs_Nor.y > 0.5) {
            // Tilt the top-surface normal to match the wave slope so the
            // Blinn-Phong highlight tracks the moving water
            float dydx = cos(phaseX) * 0.06 * 0.65;
            float dydz = -sin(phaseZ) * 0.06 * 0.50;
            nor = normalize(vec3(-dydx, 1.0, -dydz));
        }
    }
    // Lava fills its cell flush with the surrounding blocks. (It used to sit
    // 0.1 below the block top, which left a thin gap wherever a mined wall
    // met the lava surface underground.)

    fs_Pos = worldPos;
    fs_Nor = vec4(nor, 0.0);
    fs_Col = vs_Col;
    fs_UV = uv;
    fs_Flags = vs_UV.zw;

    gl_Position = u_ViewProj * worldPos;
}
