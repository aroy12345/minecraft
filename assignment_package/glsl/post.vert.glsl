#version 330
// Inputs the position of the vertex
in vec4 vs_Pos;
// Inputs the UV coordinates of the vertex
in vec4 vs_UV;

// Outputs the UV coordinates to the fragment shader
out vec2 fs_UV;

void main() {
    // Pass the UV coordinates unchanged to the fragment shader
    fs_UV = vs_UV.xy;
    // Set the position of the vertex in clip space
    gl_Position = vs_Pos;
}
