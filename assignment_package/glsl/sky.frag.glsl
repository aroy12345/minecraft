#version 330

uniform vec3  u_SunDirection;     // Direction of the sun
uniform vec3  u_SkyZenithColor;   // Color at the zenith
uniform vec3  u_SkyHorizonColor;  // Color at the horizon
uniform vec3  u_SunColor;         // Sun disk color
uniform float u_Time;             // For animating clouds

uniform float u_FogDistance;      // Fog distance
uniform int   u_WeatherType;      // Weather type (0=clear, 1=rain, 2=snow)
uniform float u_WeatherIntensity; // Weather intensity

in  vec4 fs_Pos;                  // Full-screen quad pos
in  vec3 fs_RayDir;               // World-space ray dir

out vec4 out_Col;                 // Final pixel color


// —————————————————————————————————————————————
// Hash + Worley + FBM (unchanged)
vec2 hash2(vec2 p) {
    p = vec2(dot(p, vec2(127.1,311.7)),
             dot(p, vec2(269.5,183.3)));
    return fract(sin(p) * 43758.5453);
}
float worleyNoise(vec2 p) {
    vec2  pi = floor(p);
    vec2 pf = fract(p);
    float m = 1.0;
    for(int y=-1; y<=1; y++){
      for(int x=-1; x<=1; x++){
        vec2 b = vec2(x,y);
        vec2 rp= hash2(pi+b) + b;
        m = min(m, length(rp - pf));
      }
    }
    return m;
}
float fbm(vec2 p){
    float sum = 0.0, amp = 0.5, freq = 1.0;
    for(int i=0; i<7; i++){
      sum += (1.0 - worleyNoise(p*freq)) * amp;
      amp  *= 0.5;
      freq *= 2.0;
    }
    return sum;
}

// —————————————————————————————————————————————
// Cloud pattern, now safe for rayDir.y<0
float cloudPattern(vec3 rd, float time){
    // 1) clamp denominator ∈ [0.05,1.0] to avoid NaNs
    float denom = max(rd.y * 0.5 + 0.5, 0.05);
    vec2  uv    = rd.xz / denom;
    uv += time * 0.01;

    // 2) base FBM
    float c = fbm(uv * 2.0);

    // 3) fade out toward zenith (t=1) smoothly
    float t = clamp(rd.y * 0.5 + 0.5, 0.0, 1.0);
    float fade = smoothstep(0.0, 0.4, 1.0 - t);

    // 4) threshold clouds
    c = smoothstep(0.35, 0.7, c) * fade;
    return c;
}

void main(){
    // —————————————————————————————————————————————
    // 1) normalize ray and build full-range gradient t∈[0,1]
    vec3 rayDir = normalize(fs_RayDir);
    float t     = clamp(rayDir.y * 0.5 + 0.5, 0.0, 1.0);

    // 2) sky gradient (horizon→zenith)
    vec3 skyColor = mix(u_SkyHorizonColor, u_SkyZenithColor, pow(t,0.5));

    // 3) sun disk + glow
    float sunDot  = max(dot(rayDir, normalize(u_SunDirection)), 0.0);
    float sunDisk = pow(sunDot, 64.0);
    float sunGlow = pow(sunDot,  8.0);
    vec3  diskCol = u_SunColor * sunDisk;
    vec3  glowCol = mix(skyColor, u_SunColor, sunGlow * 0.5);

    // 4) combine sun+sky
    vec3 finalCol = max(diskCol, glowCol);

    // 5) nighttime: stars + dark sky
    bool isNight = u_SunDirection.y < 0.0;
    if(isNight){
        finalCol = skyColor;
        float seed = fract(sin(dot(rayDir.xy, vec2(12.9898,78.233))) * 43758.5453);
        float star = smoothstep(0.995,1.0,seed) * mix(0.3,1.0,t);
        finalCol += vec3(star);
    }

    // 6) clouds (only on sky half, but safe for rd.y<0)
    if(!isNight){
        float clouds   = cloudPattern(rayDir, u_Time);
        vec3  cloudCol;
        // simple bright clouds tinted by sun angle
        float bright = max(0.6, sunDot+0.2);
        cloudCol = mix(u_SkyHorizonColor, vec3(1.0), bright);

        // sunrise/sunset tint
        if(u_SunDirection.y < 0.3 && u_SunDirection.y > 0.0){
            float sf = 1.0 - (u_SunDirection.y/0.3);
            cloudCol = mix(cloudCol, u_SunColor, sf * 0.7);
        }

        finalCol = mix(finalCol, cloudCol, clouds);
    }

    // 7) gentle haze near horizon
    finalCol = mix(finalCol, u_SkyHorizonColor, (1.0 - t)*0.2);

    out_Col = vec4(finalCol, 1.0);
}


