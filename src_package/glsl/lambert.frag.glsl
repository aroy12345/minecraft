


#version 330

uniform vec4 u_Color;
uniform sampler2D u_Texture;
uniform float u_Time;

// Sky/time related uniforms
uniform vec3 u_LightDir;        // Direction of the sun/moon
uniform vec3 u_LightColor;      // Color of the light
uniform float u_LightIntensity; // Overall light intensity

in vec4 fs_Pos;
in vec4 fs_Nor;
in vec4 fs_LightVec;
in vec4 fs_Col;
in vec2 fs_UV;

out vec4 out_Col;

void main() {
    // Sample texture color
    vec4 texColor = texture(u_Texture, fs_UV);

    // Material base color (before shading)
    vec4 diffuseColor = texColor;

    // Calculate the diffuse term using the dynamic light direction
    float diffuseTerm = dot(normalize(fs_Nor), normalize(vec4(u_LightDir, 0.0)));

    // Avoid negative lighting values
    diffuseTerm = max(0.0, diffuseTerm);

    // Dynamic ambient term based on time of day
    float ambientTerm = mix(0.2, 0.6, 1.0 - u_LightIntensity);

    // Final lighting calculation with colored light
    float lightIntensity = diffuseTerm * u_LightIntensity + ambientTerm;

    // Calculate sun influence
    float sunInfluence = diffuseTerm * u_LightIntensity;

    // Apply light color to the diffuse color
    // More sun color influence when directly lit, more ambient/sky color in shadows
    vec3 litColor = diffuseColor.rgb * mix(
        // Ambient/shadow color (less saturated in darkness)
        mix(vec3(0.5, 0.5, 0.7), vec3(1.0), u_LightIntensity * 0.7),
        // Sun-lit color
        u_LightColor,
        sunInfluence
    );

    // Night-time slight blue tint in shadows
    if (u_LightDir.y < 0.0) {
        // It's night time, add slightly blue tint to shadows
        litColor = mix(litColor, vec3(0.1, 0.1, 0.3), (1.0 - u_LightIntensity) * 0.3);
    }

    // Set final color with adjusted lighting
    out_Col = vec4(litColor * lightIntensity, diffuseColor.a);
}

// #version 330

// uniform vec4 u_Color;
// uniform sampler2D u_Texture;
// uniform float u_Time;

// // Sky/time related uniforms
// uniform vec3 u_LightDir;        // Direction of the sun/moon
// uniform vec3 u_LightColor;      // Color of the light
// uniform float u_LightIntensity; // Overall light intensity

// // Weather related uniforms
// uniform int u_WeatherType;            // 0 = clear, 1 = rain, 2 = snow, 3 = thunderstorm
// uniform float u_WeatherIntensity;     // Overall weather intensity (0-1)
// uniform float u_FogDistance;          // Distance at which fog starts
// uniform vec3 u_CameraPos;             // Camera position for fog calculation

// in vec4 fs_Pos;
// in vec4 fs_Nor;
// in vec4 fs_LightVec;
// in vec4 fs_Col;
// in vec2 fs_UV;

// out vec4 out_Col;

// void main() {
//     // Sample texture color
//     vec4 texColor = texture(u_Texture, fs_UV);

//     // Material base color (before shading)
//     vec4 diffuseColor = texColor;

//     // Calculate the diffuse term using the dynamic light direction
//     float diffuseTerm = dot(normalize(fs_Nor), normalize(vec4(u_LightDir, 0.0)));

//     // Avoid negative lighting values
//     diffuseTerm = max(0.0, diffuseTerm);

//     // Dynamic ambient term based on time of day
//     float ambientTerm = mix(0.2, 0.6, 1.0 - u_LightIntensity);

//     // Adjust ambient based on weather
//     if (u_WeatherType > 0) {
//         // Increase ambient during bad weather (less direct sunlight)
//         ambientTerm = mix(ambientTerm, 0.7, u_WeatherIntensity * 0.5);
//     }

//     // Final lighting calculation with colored light
//     float lightIntensity = diffuseTerm * u_LightIntensity + ambientTerm;

//     // Calculate sun influence
//     float sunInfluence = diffuseTerm * u_LightIntensity;

//     // Apply light color to the diffuse color
//     // More sun color influence when directly lit, more ambient/sky color in shadows
//     vec3 litColor = diffuseColor.rgb * mix(
//         // Ambient/shadow color (less saturated in darkness)
//         mix(vec3(0.5, 0.5, 0.7), vec3(1.0), u_LightIntensity * 0.7),
//         // Sun-lit color
//         u_LightColor,
//         sunInfluence
//     );

//     // Night-time slight blue tint in shadows
//     if (u_LightDir.y < 0.0) {
//         // It's night time, add slightly blue tint to shadows
//         litColor = mix(litColor, vec3(0.1, 0.1, 0.3), (1.0 - u_LightIntensity) * 0.3);
//     }

//     // Adjust color based on weather type
//     if (u_WeatherType == 1) { // Rain
//         // Desaturate colors slightly and darken
//         litColor = mix(litColor, vec3(dot(litColor, vec3(0.299, 0.587, 0.114))), u_WeatherIntensity * 0.3);
//         litColor *= mix(1.0, 0.8, u_WeatherIntensity);
//     } else if (u_WeatherType == 2) { // Snow
//         // Brighten slightly to simulate snow reflection
//         litColor = mix(litColor, vec3(1.0), u_WeatherIntensity * 0.2);
//     } else if (u_WeatherType == 3) { // Thunderstorm
//         // Darken considerably and add blue tint
//         litColor = mix(litColor, vec3(0.1, 0.1, 0.2), u_WeatherIntensity * 0.5);

//         // Add occasional lightning flashes
//         float lightning = max(0.0, sin(u_Time * 10.0) - 0.95) * 20.0;
//         float lightningEffect = lightning * (fract(sin(dot(fs_Pos.xy, vec2(12.9898, 78.233))) * 43758.5453) > 0.8 ? 1.0 : 0.0);
//         litColor += vec3(lightningEffect) * u_WeatherIntensity;
//     }

//     // Set final color with adjusted lighting
//     vec4 finalColor = vec4(litColor * lightIntensity, diffuseColor.a);

//     // Apply fog based on distance and weather
//     if (u_WeatherType > 0) {
//         // Calculate distance to fragment
//         float dist = length(fs_Pos.xyz - u_CameraPos);

//         // Calculate fog factor
//         float fogFactor = 1.0 - exp(-dist / u_FogDistance);
//         fogFactor = clamp(fogFactor * u_WeatherIntensity, 0.0, 0.9); // Cap fog at 90% to maintain some visibility

//         // Fog color based on weather type
//         vec3 fogColor;
//         if (u_WeatherType == 1) { // Rain
//             fogColor = vec3(0.4, 0.4, 0.45);
//         } else if (u_WeatherType == 2) { // Snow
//             fogColor = vec3(0.8, 0.8, 0.85);
//         } else if (u_WeatherType == 3) { // Thunderstorm
//             fogColor = vec3(0.2, 0.2, 0.25);
//         }

//         // Apply fog to final color
//         finalColor.rgb = mix(finalColor.rgb, fogColor, fogFactor);
//     }

//     out_Col = finalColor;
// }
