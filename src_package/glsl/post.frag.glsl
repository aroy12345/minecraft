#version 330
// The texture containing the rendered scene

// Uniform inputs
uniform sampler2D u_Texture;  // The rendered scene texture
uniform int u_InWater;        // 1 if player is in water, 0 otherwise
uniform int u_InLava;         // 1 if player is in lava, 0 otherwise
uniform float u_InSolid;      // 0..1 faded, 1 = camera inside a solid block
uniform int u_Crosshair;      // 1 while the mouse is captured for play
uniform int u_WeatherKind;    // 0 clear, 1 rain, 2 snow
uniform float u_WeatherStrength;
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

    // Raindrops / snowflakes landing on the "lens": hashed grid cells
    // spawn splats that grow and fade, rain refracting the scene slightly
    if (u_WeatherKind > 0 && u_WeatherStrength > 0.05) {
        vec2 cellUV = fs_UV * vec2(9.0, 6.0);
        vec2 cell = floor(cellUV);
        vec2 f = fract(cellUV);
        float rnd = fract(sin(dot(cell, vec2(12.9898, 78.233))) * 43758.5453);
        float life = fract(u_Time * 0.3 + rnd);
        vec2 center = vec2(0.25) + 0.5 * vec2(fract(rnd * 7.31), fract(rnd * 13.73));
        float d = length(f - center);
        float radius = 0.04 + 0.10 * life;
        float splat = smoothstep(radius, radius - 0.035, d) * (1.0 - life) * u_WeatherStrength;
        if (splat > 0.001) {
            if (u_WeatherKind == 1) {
                vec2 refr = (f - center) * splat * 0.06;
                out_Col.rgb = mix(out_Col.rgb, texture(u_Texture, fs_UV + refr).rgb, min(splat, 0.85));
                out_Col.rgb += splat * 0.06;
            } else {
                out_Col.rgb = mix(out_Col.rgb, vec3(0.96), splat * 0.55);
            }
        }
    }

    // Black out the view while the camera is buried inside a solid block,
    // so flying through terrain reads as darkness instead of broken x-ray
    out_Col.rgb *= mix(1.0, 0.06, clamp(u_InSolid, 0.0, 1.0));

    // --- Cinematic colour grade (scene only, before the HUD): a gentle
    //     filmic contrast curve, a touch more saturation, a warm-highlight /
    //     cool-shadow split-tone, and a soft vignette. This is what lifts
    //     the look from flat and basic to polished and premium. ---
    {
        vec3 graded = out_Col.rgb;
        float luma = dot(graded, vec3(0.2126, 0.7152, 0.0722));
        graded = mix(vec3(luma), graded, 1.16);                 // saturation
        graded = clamp((graded - 0.5) * 1.07 + 0.5, 0.0, 1.0);  // S-curve contrast
        graded += (luma - 0.5) * vec3(0.035, 0.012, -0.03);     // split-tone
        vec2 vd = fs_UV - 0.5;                                   // soft vignette
        float vig = smoothstep(1.1, 0.35, dot(vd, vd) * 3.0);
        graded *= mix(0.84, 1.0, vig);
        out_Col.rgb = clamp(graded, 0.0, 1.0);
    }

    // Crosshair, drawn only while the mouse is captured for gameplay.
    // Inverting the scene color keeps it visible on any background.
    if (u_Crosshair == 1) {
        vec2 res = vec2(textureSize(u_Texture, 0));
        vec2 px = (fs_UV - vec2(0.5)) * res;
        float arm = res.y * 0.014;
        float thick = max(res.y * 0.0014, 1.5);
        if ((abs(px.x) <= thick && abs(px.y) <= arm) ||
            (abs(px.y) <= thick && abs(px.x) <= arm)) {
            out_Col.rgb = vec3(1.0) - 0.8 * out_Col.rgb;
        }
    }
}
