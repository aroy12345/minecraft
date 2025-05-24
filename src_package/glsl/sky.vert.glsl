#version 330

uniform mat4 u_ViewProj;    // Camera's view * projection matrix
uniform mat4 u_InvViewProj; // Inverse of view * projection matrix

in vec4 vs_Pos;             // Vertex positions (NDC coordinates of full-screen quad)
in vec4 vs_Nor;             // Normals (not used for sky rendering)
in vec4 vs_Col;             // Colors (not used for sky rendering)

out vec4 fs_Pos;
out vec3 fs_RayDir;         // Ray direction for each pixel

void main() {
    // Pass through the vertex position
    fs_Pos = vs_Pos;

    // Calculate the ray direction for this pixel in world space
    // transforms from NDC coordinates back to world space
    vec4 ndcPos = vec4(vs_Pos.xy, 1.0, 1.0);
    vec4 worldPos = u_InvViewProj * ndcPos;
    worldPos /= worldPos.w;

    // Direction from camera position to this pixel in world space
    fs_RayDir = normalize(worldPos.xyz - vec3(u_InvViewProj[3]));

    // Pass through the vertex position unchanged (already in NDC)
    gl_Position = vs_Pos;
}
