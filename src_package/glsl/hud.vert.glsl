#version 330
// On-screen HUD quads: positions are already in NDC

in vec4 vs_Pos;
in vec4 vs_UV;

out vec2 fs_UV;

void main() {
    fs_UV = vs_UV.xy;
    gl_Position = vec4(vs_Pos.xy, -0.99, 1.0);
}
