#version 330
// The texture containing the rendered scene

// Uniform inputs
uniform sampler2D u_Texture;  // The rendered scene texture
uniform int u_InWater;        // 1 if player is in water, 0 otherwise
uniform int u_InLava;         // 1 if player is in lava, 0 otherwise
uniform float u_Time;         // Time for animating effects

// Input from vertex shader
in vec2 fs_UV;  // Texture coordinates

// Output
out vec4 out_Col;  // Final pixel color

void main() {
    // Sample the rendered scene texture
    vec4 sceneColor = texture(u_Texture, fs_UV);

    // Apply post-processing effects based on player state
    if (u_InWater == 1) {
        // Underwater effect: Blue tint + wavy distortion

        // Create a wavy distortion effect
        float wavyOffset = sin(fs_UV.y * 20.0 + u_Time * 2.0) * 0.005;
        vec2 distortedUV = fs_UV + vec2(wavyOffset, 0.0);

        // Sample with distorted coordinates
        vec4 distortedColor = texture(u_Texture, distortedUV);

        // Apply blue tint
        vec3 underwaterColor = mix(distortedColor.rgb, vec3(0.0, 0.2, 0.8), 0.3);

        // Add caustics/light rays effect
        float caustics = pow(sin(fs_UV.x * 40.0 + u_Time) * 0.5 + 0.5, 2.0) * 0.05;
        underwaterColor += vec3(caustics);

        // Output final underwater color
        out_Col = vec4(underwaterColor, 1.0);
    }
    else if (u_InLava == 1) {
        // Lava effect: Red/orange tint + heat distortion + pulsing

        // Create stronger distortion for lava
        float distortX = sin(fs_UV.y * 30.0 + u_Time * 3.0) * 0.01;
        float distortY = cos(fs_UV.x * 30.0 + u_Time * 2.0) * 0.01;
        vec2 lavaUV = fs_UV + vec2(distortX, distortY);

        // Sample with distorted coordinates
        vec4 distortedColor = texture(u_Texture, lavaUV);

        // Apply orange/red tint and darken
        vec3 lavaColor = mix(distortedColor.rgb, vec3(0.9, 0.3, 0.0), 0.4);

        // Add pulsing glow
        float pulse = sin(u_Time * 1.5) * 0.5 + 0.5;
        lavaColor += vec3(0.2, 0.05, 0.0) * pulse;

        // Output final lava color
        out_Col = vec4(lavaColor, 1.0);
    }
    else {
        // No effect, just output the original scene color
        out_Col = sceneColor;
    }
}
