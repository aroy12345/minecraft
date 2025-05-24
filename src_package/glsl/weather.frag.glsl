#version 330

uniform sampler2D u_ParticleTexture;
uniform int u_WeatherType;  // 0 = rain, 1 = snow

in vec2 fs_UV;
in float fs_Life;

out vec4 out_Col;

void main() {
    // Base particle texture (will be a simple dot or flake)
    vec4 baseColor;

    // Different appearance based on weather type
    if (u_WeatherType == 0) {
        // Rain: elongated blue-white drop
        baseColor = vec4(0.7, 0.7, 0.9, 0.8);

        // Make rain particles elongated vertically
        float distFromCenter = length(vec2(fs_UV.x - 0.5, (fs_UV.y - 0.5) * 0.3));

        // Create a simple drop shape
        if (distFromCenter > 0.4) {
            // Outside the drop shape
            discard;
        }

        // Fade alpha at edges
        baseColor.a *= smoothstep(0.4, 0.1, distFromCenter);
    }
    else {
        // Snow: white flake with some detail
        baseColor = vec4(1.0, 1.0, 1.0, 0.9);

        // Calculate snowflake shape
        float distFromCenter = length(fs_UV - 0.5);

        // Create a simple flake shape
        if (distFromCenter > 0.45) {
            // Outside the flake shape
            discard;
        }

        // Add some detail to the snowflake
        float flakeDetail = sin(fs_UV.x * 12.0) * sin(fs_UV.y * 12.0) * 0.2;
        baseColor.rgb += vec3(flakeDetail);

        // Fade alpha at edges
        baseColor.a *= smoothstep(0.45, 0.25, distFromCenter);
    }

    // Adjust opacity based on particle life
    baseColor.a *= fs_Life;

    // Output final color
    out_Col = baseColor;
}
