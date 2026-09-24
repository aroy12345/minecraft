#version 150
// ^ Change this to version 130 if you have compatibility issues

// Refer to the lambert shader files for useful comments

in vec4 fs_Col;

uniform vec4 u_Tint;   // multiplied with the vertex color (set to white
                       // for untinted drawables like the axes/outline)

out vec4 out_Col;

void main()
{
    // Tinted vertex color; there is no shading.
    out_Col = fs_Col * u_Tint;
}
