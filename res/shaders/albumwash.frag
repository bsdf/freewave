#version 440

// Album Wash: the "off" state — an ambient field of the album's accent colour,
// like ink diffusing in dark water, with no audio reactivity at all. The clouds
// genuinely morph (form/dissolve/reshape in place) rather than just sliding,
// because the field is a slice through 3D noise with `time` as the third axis —
// `MORPH` evolves the volume, `DRIFT` adds a gentle translation on top. A
// two-level domain warp gives the molten folds. Motion and brightness are fixed
// (time-driven only) so this preset reads as calm/idle regardless of what's
// playing — see auroracalm.frag for the same pattern applied to aurora.frag.
// Brightness is biased toward the top-left and dimmed through the centre so the
// centred hero text stays legible. Output is opaque (it supersedes the static
// wash.frag when the visualizer is on) and dithered so the dark gradient shows
// no 8-bit banding.

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec4 accent;       // album accent (rgb used)
    float time;        // seconds (animated from QML)
    vec2 resolution;   // item size in px (for aspect correction)
};

// ── Tuning surface ───────────────────────────────────────────────────────────
const float SCALE      = 2.6;   // feature size (larger = finer, busier field)
const float MORPH      = 0.075; // shape evolution speed (z through the 3D volume)
const float DRIFT      = 0.012; // translation speed on top of the morph
const float WARP       = 0.65;  // domain-warp depth (the "molten" look)
const float HI_MIX     = 0.45;  // bright-ridge blend strength (fixed, ambient floor)
const float CENTRE_DIM = 0.70;  // centre brightness vs edges (text legibility)

float hash(vec3 p) {
    return fract(sin(dot(p, vec3(127.1, 311.7, 74.7))) * 43758.5453);
}

// Trilinearly-interpolated value noise — a slice at fixed z, animated by z=time,
// morphs in place instead of merely translating.
float vnoise(vec3 p) {
    vec3 i = floor(p), f = fract(p);
    vec3 u = f * f * (3.0 - 2.0 * f);
    float n000 = hash(i + vec3(0.0, 0.0, 0.0));
    float n100 = hash(i + vec3(1.0, 0.0, 0.0));
    float n010 = hash(i + vec3(0.0, 1.0, 0.0));
    float n110 = hash(i + vec3(1.0, 1.0, 0.0));
    float n001 = hash(i + vec3(0.0, 0.0, 1.0));
    float n101 = hash(i + vec3(1.0, 0.0, 1.0));
    float n011 = hash(i + vec3(0.0, 1.0, 1.0));
    float n111 = hash(i + vec3(1.0, 1.0, 1.0));
    return mix(mix(mix(n000, n100, u.x), mix(n010, n110, u.x), u.y),
               mix(mix(n001, n101, u.x), mix(n011, n111, u.x), u.y), u.z);
}

float fbm(vec3 p) {
    float s = 0.0, a = 0.5;
    for (int i = 0; i < 5; i++) {
        s += a * vnoise(p);
        p *= 2.0;
        a *= 0.5;
    }
    return s;
}

void main() {
    float aspect = resolution.x / max(resolution.y, 1.0);
    vec2 uv = qt_TexCoord0;
    vec2 xy = vec2(uv.x * aspect, uv.y) * SCALE;

    float tz = time * MORPH;                         // z axis → shapes evolve
    vec2 drift = vec2(time * DRIFT, time * DRIFT * 0.6);
    vec3 p = vec3(xy + drift, tz);

    // Two-level domain warp; warp layers read different z slices so they evolve
    // independently rather than locking into one rigid pattern.
    vec2 q = vec2(fbm(p), fbm(p + vec3(5.2, 1.3, 2.7)));
    vec2 r = vec2(fbm(p + vec3(WARP * q, 0.0) + vec3(1.7, 9.2, 0.0)),
                  fbm(p + vec3(WARP * q, 0.0) + vec3(8.3, 2.8, 4.1)));
    float f = fbm(p + vec3(WARP * r, 0.0));

    // Map the field through an accent-derived palette: mostly dark, with a
    // brighter ridge in the densest folds.
    vec3 lo  = mix(accent.rgb, vec3(0.0), 0.86);
    vec3 mid = mix(accent.rgb, vec3(0.0), 0.42);
    vec3 hi  = mix(accent.rgb, vec3(1.0), 0.22);
    vec3 col = mix(lo, mid, smoothstep(0.05, 0.65, f));
    col = mix(col, hi, smoothstep(0.6, 1.0, f) * HI_MIX);

    // Compositional bias: brighter top-left (echoes the static wash), dimmed
    // through the centre so the hero text reads cleanly.
    vec2 c = (uv - 0.5); c.x *= aspect;
    float vig = smoothstep(0.0, 0.62, length(c));
    col *= mix(CENTRE_DIM, 1.0, vig);
    col *= 1.0 - 0.28 * smoothstep(0.0, 1.3, length(vec2((uv.x - 0.2) * aspect, uv.y - 0.05)));

    // Triangular-PDF dither, ~±1 LSB, before the framebuffer quantises.
    float r1 = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453);
    float r2 = fract(sin(dot(gl_FragCoord.xy, vec2(39.3468, 11.1357))) * 24634.6345);
    col += vec3((r1 + r2 - 1.0) / 255.0);

    fragColor = vec4(col, 1.0) * qt_Opacity;
}
