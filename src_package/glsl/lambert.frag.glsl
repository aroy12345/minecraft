#version 330

uniform sampler2D u_Texture;
uniform float u_Time;

// Sky/time related uniforms
uniform vec3 u_LightDir;        // Direction of the sun/moon
uniform vec3 u_LightColor;      // Color of the light
uniform float u_LightIntensity; // Overall light intensity

// Fog
uniform vec3 u_CamPos;          // Camera position in world space
uniform vec3 u_FogColor;        // Matches the sky's horizon color
uniform float u_FogDistance;    // Distance at which fog fully covers terrain

// Shadow mapping
uniform mat4 u_ShadowVP;        // The sun's orthographic view-projection
uniform sampler2D u_ShadowMap;  // Depth of the scene as seen by the sun
uniform int u_ShadowsOn;        // 0 while the sun is below the horizon

in vec4 fs_Pos;                 // World-space position
in vec4 fs_Nor;
in vec4 fs_Col;                 // r: baked sky light, g: baked block light,
                                // b: foliage tint flag, a < 1 marks water
in vec2 fs_UV;
in vec2 fs_Flags;               // x: animated fluid, y: 1 = water, 2 = lava

out vec4 out_Col;

void main() {
    vec4 texColor = texture(u_Texture, fs_UV);
    // fs_Col.b: 1.0 brightens the leaf texture; values in (0.05, 0.95)
    // carry the biome climate and tint the grey grass-top tile, blending
    // smoothly from lush cool green to warm dry green across the world
    if (fs_Col.b > 0.97) {
        texColor.rgb *= vec3(1.55, 1.7, 1.2);
    } else if (fs_Col.b > 0.05) {
        float climate = clamp((fs_Col.b - 0.1) / 0.8, 0.0, 1.0);
        vec3 lush = vec3(0.62, 1.22, 0.52);
        vec3 dry  = vec3(1.02, 1.12, 0.42);
        texColor.rgb *= mix(lush, dry, climate);
    }

    vec3 N = normalize(fs_Nor.xyz);
    vec3 L = normalize(u_LightDir);

    // Minecraft-style face shading: bright tops, dimmer sides, dark bottoms.
    float faceLight;
    if (N.y > 0.5)       faceLight = 1.0;
    else if (N.y < -0.5) faceLight = 0.55;
    else if (abs(N.x) > abs(N.z)) faceLight = 0.72;
    else                 faceLight = 0.85;

    // Baked per-vertex lighting: sunlight (scaled by time of day, with a
    // moonlit floor) and lava glow, already smoothed and ambient-occluded
    // by the mesher. This is what carries caves, shadows and soft corners.
    float day = clamp(u_LightIntensity, 0.0, 1.0);
    float lightLevel = max(fs_Col.r * mix(0.2, 1.0, day), fs_Col.g);

    // Held-torch glow: a soft light the player carries, so caves are dark
    // and moody but explorable (only visible where baked light is dimmer)
    float torchDist = length(fs_Pos.xyz - u_CamPos);
    float heldGlow = 0.55 * clamp(1.0 - torchDist / 13.0, 0.0, 1.0);
    lightLevel = max(lightLevel, heldGlow);

    float brightness = (0.10 + 0.90 * lightLevel) * faceLight;

    // Shadow mapping: fragments the sun cannot see lose their direct
    // sunlight (2x2 PCF softens the edges); night is unaffected
    if (u_ShadowsOn == 1) {
        vec4 sc = u_ShadowVP * vec4(fs_Pos.xyz, 1.0);
        vec3 ndc = sc.xyz / sc.w * 0.5 + 0.5;
        if (ndc.x > 0.002 && ndc.x < 0.998 && ndc.y > 0.002 && ndc.y < 0.998 && ndc.z < 1.0) {
            float bias = 0.0028;
            vec2 texel = 1.0 / vec2(textureSize(u_ShadowMap, 0));
            float lit = 0.0;
            for (int sx = 0; sx <= 1; ++sx)
                for (int sy = 0; sy <= 1; ++sy) {
                    float d = texture(u_ShadowMap, ndc.xy + vec2(sx, sy) * texel).r;
                    lit += (ndc.z - bias <= d) ? 1.0 : 0.0;
                }
            lit *= 0.25;
            brightness *= mix(mix(0.55, 1.0, lit), 1.0, 1.0 - day);
        }
    }

    // Subtle warm sun tint on lit faces; cool moonlight at night
    float sunInfluence = max(0.0, dot(N, L)) * day;
    vec3 lightTint = mix(vec3(1.0), u_LightColor, 0.35 * sunInfluence);
    if (u_LightDir.y < 0.0) {
        lightTint = mix(lightTint, vec3(0.75, 0.8, 1.0), 0.5);
    }

    vec3 shaded = texColor.rgb * brightness * lightTint;
    float outAlpha = texColor.a * fs_Col.a;

    if (fs_Flags.y > 1.5) {
        // Emissive surfaces (lava, torches, lit lamps, powered redstone)
        // render full-bright regardless of depth or time of day
        shaded = texColor.rgb * 1.15;
    } else if (fs_Col.a < 0.95) {
        // Water: deepen toward blue and add a Blinn-Phong sun glint that
        // rides the wave-distorted normals
        vec3 V = normalize(u_CamPos - fs_Pos.xyz);
        vec3 H = normalize(L + V);
        float spec = pow(max(dot(N, H), 0.0), 64.0) * day;
        shaded = mix(shaded, vec3(0.1, 0.35, 0.75) * max(brightness, 0.3), 0.5);
        shaded += u_LightColor * spec * 0.7;
        outAlpha = 0.7;
    }

    // Distance fog: terrain stays crisp and fully visible almost all the way
    // out, then fades into the sky's horizon color only in the last slice
    // before the draw edge, purely to hide the boundary. Underground the fog
    // is suppressed so caves recede into darkness instead of bright haze.
    float dist = length(fs_Pos.xz - u_CamPos.xz);
    float fogFactor = smoothstep(u_FogDistance * 0.90, u_FogDistance, dist);
    fogFactor *= clamp((u_CamPos.y - 110.0) / 25.0, 0.0, 1.0);
    shaded = mix(shaded, u_FogColor, fogFactor);

    out_Col = vec4(shaded, outAlpha);
}
