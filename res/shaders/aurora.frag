#version 440

// Aurora: a direct port of the web concept's FWAmbientViz "aurora" mode. A 2D
// domain-warped fbm cloud tinted by the album accent, drifting slowly, with two
// aurora-specific touches layered on top of the base wash: drifting vertical
// bands and a channel-rotated complementary tint (accent.gbr) blended into the
// densest folds. Output is opaque (it supersedes the static wash.frag while the
// visualizer is on) and dithered to kill 8-bit banding.
//
// Reactivity model: motion is driven by flowTime, a QML-integrated clock whose
// *speed* swells with loudness (∫ 1 + k·energy dt). Because energy modulates
// velocity rather than being added to the phase, the field accelerates and
// eases but can never run backwards when the envelope decays — adding energy to
// the phase made the whole field reverse on every beat. All remaining reactive
// terms ride the envelope *value* (never its derivative): bass swells cloud
// coverage, loudness widens the bright wisps and gently lifts brightness.

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec4 accent;       // album accent (rgb used)
    float washEnergy;  // slow EMA of overall loudness, 0..1
    float washBass;    // slow EMA of low-band energy, 0..1
    float flowTime;    // music-paced clock, monotonic (integrated in QML)
    float phaseOffset; // current random offset, re-seeded on every track change
    float prevPhaseOffset; // offset being cross-faded out; == phaseOffset at rest
    float phaseBlend;  // 0 = prevPhaseOffset, 1 = phaseOffset; ramps on track change
    vec2 resolution;   // item size in px (for aspect correction)
};

const float INTENSITY = 1.0;

const float ENERGY_BLOOM = 0.22; // gentle global brightness swell with loudness
const float ENERGY_WISP  = 0.20; // bright-wisp widening with loudness
const float BASS_SWELL   = 0.12; // cloud coverage rise on the low end
const float BASS_GLOW    = 0.35; // wisp glow on the low end

float hash(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

float noise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    float a = hash(i), b = hash(i + vec2(1.0, 0.0));
    float c = hash(i + vec2(0.0, 1.0)), d = hash(i + vec2(1.0, 1.0));
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(a, b, u.x) + (c - a) * u.y * (1.0 - u.x) + (d - b) * u.x * u.y;
}

float fbm(vec2 p) {
    float v = 0.0, a = 0.5;
    mat2 m = mat2(1.6, 1.2, -1.2, 1.6);
    for (int i = 0; i < 6; i++) { v += a * noise(p); p = m * p; a *= 0.5; }
    return v;
}

// The full billowing-cloud field for one phase value. Evaluated twice (old and
// new phase) only while phaseBlend is mid-transition; at rest main() calls this
// once, same cost as before phase cross-fading existed.
vec3 aurora_field(vec2 p, vec2 uv, float asp, float t) {
    // Two-step domain warp for that slow billowing-smoke motion.
    vec2 q = vec2(fbm(p + t), fbm(p + vec2(5.2, 1.3) - t * 0.8));
    vec2 r = vec2(fbm(p + 1.8 * q + vec2(1.7, 9.2) + t * 0.6),
                  fbm(p + 1.8 * q + vec2(8.3, 2.8) - t * 0.4));
    float f = fbm(p + 1.8 * r);

    // A full low end raises the field like a swell: coverage grows, folds bloom.
    float cloud = smoothstep(0.12, 1.05, f + 0.15 * r.x + BASS_SWELL * washBass);

    vec3 dark = accent.rgb * 0.045;                 // near-black accent base
    vec3 mid  = accent.rgb * 0.42;
    vec3 hi   = mix(accent.rgb, vec3(1.0), 0.22);   // bright wisp highlight

    vec3 col = dark;
    col = mix(col, mid, smoothstep(0.0, 0.72, cloud));
    col = mix(col, hi,  smoothstep(0.58, 1.0, cloud) * (0.7 + ENERGY_WISP * washEnergy));

    // Aurora: drifting vertical bands + channel-rotated complementary tint.
    float band = sin(uv.x * asp * 3.0 + r.y * 4.0 + flowTime * 0.25) * 0.5 + 0.5;
    vec3 comp = accent.gbr;
    col = mix(col, mix(accent.rgb, comp, band) * 1.15, smoothstep(0.4, 1.0, cloud) * 0.6);

    // Ambient brightness swells: overall bloom + a bass swell in the bright
    // wisps. The 0.82 floor keeps quiet passages dim so loud ones visibly lift.
    col *= 0.82 + ENERGY_BLOOM * washEnergy;
    col += hi * BASS_GLOW * washBass * smoothstep(0.4, 1.0, cloud);
    return col;
}

void main() {
    // The concept uses GL's bottom-left fragcoord convention; flip y so the
    // orientation (and the top→bottom fall-off below) matches the page.
    vec2 uv  = vec2(qt_TexCoord0.x, 1.0 - qt_TexCoord0.y);
    float asp = resolution.x / max(resolution.y, 1.0);
    vec2 p = vec2(uv.x * asp, uv.y) * 2.2;

    vec3 col = aurora_field(p, uv, asp, flowTime * 0.04 + phaseOffset);
    if (phaseBlend < 0.999) {
        vec3 prevCol = aurora_field(p, uv, asp, flowTime * 0.04 + prevPhaseOffset);
        col = mix(prevCol, col, phaseBlend);
    }

    col *= INTENSITY;
    col *= mix(1.0, 0.66, uv.y * 0.45);             // gentle top→bottom fall-off

    // Triangular-PDF dither, ~±1 LSB, before the framebuffer quantises.
    float r1 = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453);
    float r2 = fract(sin(dot(gl_FragCoord.xy, vec2(39.3468, 11.1357))) * 24634.6345);
    col += vec3((r1 + r2 - 1.0) / 255.0);

    fragColor = vec4(col, 1.0) * qt_Opacity;
}
