
#version 330

uniform mat4 u_ViewProj;
uniform vec3 u_CameraPos;
uniform float u_Time;
uniform vec3 u_WindDirection;
uniform float u_WindStrength;

// Particle data
in vec3 vs_Position;    // Position in world space
in vec3 vs_Velocity;    // Velocity direction
in vec2 vs_SizeLife;    // x = size, y = life

out vec2 fs_UV;
out float fs_Life;

// Vertex positions for a quad (two triangles)
const vec2 quadPositions[6] = vec2[](
    vec2(-1.0, -1.0),  // Bottom-left
    vec2( 1.0, -1.0),  // Bottom-right
    vec2( 1.0,  1.0),  // Top-right

    vec2(-1.0, -1.0),  // Bottom-left (repeated)
    vec2( 1.0,  1.0),  // Top-right (repeated)
    vec2(-1.0,  1.0)   // Top-left
);

// Corresponding UVs
const vec2 quadUVs[6] = vec2[](
    vec2(0.0, 0.0),  // Bottom-left
    vec2(1.0, 0.0),  // Bottom-right
    vec2(1.0, 1.0),  // Top-right

    vec2(0.0, 0.0),  // Bottom-left (repeated)
    vec2(1.0, 1.0),  // Top-right (repeated)
    vec2(0.0, 1.0)   // Top-left
);

void main() {
    // Get current vertex position from the quad array
    vec2 quadPos = quadPositions[gl_VertexID % 6];
    fs_UV = quadUVs[gl_VertexID % 6];

    // Apply wind and gravity to the position
    vec3 worldPos = vs_Position;
    worldPos += (vs_Velocity + u_WindDirection * u_WindStrength * 0.5) * u_Time * 0.1;

    // Create billboard that always faces camera
    vec3 right = normalize(cross(vec3(0.0, 1.0, 0.0), normalize(worldPos - u_CameraPos)));
    vec3 up = normalize(cross(normalize(worldPos - u_CameraPos), right));

    // Apply size to quad
    vec3 pos = worldPos + right * quadPos.x * vs_SizeLife.x + up * quadPos.y * vs_SizeLife.x;

    // Pass life to fragment shader
    fs_Life = vs_SizeLife.y;

    // Transform to clip space
    gl_Position = u_ViewProj * vec4(pos, 1.0);
}
