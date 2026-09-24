#version 330
// Textured HUD elements (hotbar block icons from the atlas)

uniform sampler2D u_Texture;

in vec2 fs_UV;

out vec4 out_Col;

void main() {
    out_Col = texture(u_Texture, fs_UV);
}
