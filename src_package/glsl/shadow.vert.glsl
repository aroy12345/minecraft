#version 330
// Depth-only pass from the sun's point of view, for shadow mapping

uniform mat4 u_Model;
uniform mat4 u_ViewProj;   // the light's orthographic view-projection

in vec4 vs_Pos;

void main() {
    gl_Position = u_ViewProj * u_Model * vs_Pos;
}
