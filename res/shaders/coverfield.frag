#version 440

// Album-art reactive field: a full-bleed, darkened, liquid rendition of the cover
// art behind the floating hero. The square cover is center-cropped to fill the
// field, then *deformed by the music* — the reactivity lives in motion, not
// brightness:
//   • bass surges the radial ripples and punches a quick zoom-in (kick = pulse),
//   • presence/treble adds a fine high-frequency shimmer warp on busy passages,
//   • the slow loudness envelope drives a gentle swirl + breathe.
// Brightness is held essentially constant (a faint slow swell only) so it never
// flickers or distracts. Heavily darkened + vignetted + edge-feathered so it stays
// an ambient backdrop the title reads over. Premultiplied alpha, dithered.

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec4 accent;      // album accent (rgb used for a shadow tint)
    vec4 b0;          // spectrum bands 0..3   (sub-bass..bass) — ripple/zoom drive
    vec4 b4;          // spectrum bands 16..19 (presence)        — shimmer drive
    float washEnergy; // slow loudness envelope 0..1 (swirl + breathe + faint swell)
    float washBass;   // slow bass envelope 0..1 (sustained ripple depth)
    float time;       // seconds
    vec2 resolution;  // field size in px (aspect-correct cropping)
};

layout(binding = 1) uniform sampler2D cover;

// ── Tuning surface ───────────────────────────────────────────────────────────
const float RIPPLE_FREQ  = 24.0;  // concentric ripple density
const float RIPPLE_SPEED = 1.4;   // outward travel
const float RIPPLE_BASE  = 0.004; // calm baseline displacement (UV units)
const float RIPPLE_GAIN  = 0.055; // extra ripple depth at full bass (the big one)
const float SHIMMER_GAIN = 0.014; // fine treble warp depth
const float SWIRL        = 0.18;  // max swirl rotation (radians) at full energy
const float ZOOM_PULSE   = 0.11;  // bass zoom-in punch
const float ZOOM_BREATHE = 0.03;  // slow energy breathe
const float BLOOM        = 0.08;  // faint slow brightness swell (keep low)

void main() {
    float bass = clamp(dot(b0, vec4(0.25)), 0.0, 1.0);
    float treb = clamp(dot(b4, vec4(0.25)), 0.0, 1.0);

    vec2 uv = qt_TexCoord0;

    // Center-crop the square cover into the (wider/taller) field.
    float aspect = resolution.x / max(resolution.y, 1.0);
    vec2 cuv = uv;
    if (aspect > 1.0)
        cuv.y = 0.5 + (uv.y - 0.5) / aspect;
    else
        cuv.x = 0.5 + (uv.x - 0.5) * aspect;

    vec2 p = cuv - 0.5;

    // Swirl: a gentle rotation about the centre, oscillating, opening up with
    // loudness so loud passages have rotational life (quiet = still).
    float ang = SWIRL * washEnergy * sin(time * 0.5);
    float ca = cos(ang), sa = sin(ang);
    p = mat2(ca, -sa, sa, ca) * p;

    // Breathing zoom + bass pulse: a kick punches the art inward, then it eases
    // back as the bass envelope decays.
    float zoom = 1.0 - ZOOM_PULSE * bass - ZOOM_BREATHE * washEnergy;
    p *= zoom;

    float r = length(p);

    // Radial ripples — depth dominated by instantaneous bass (so they visibly
    // surge on hits) over a small always-present baseline; a sustained swell rides
    // the slow bass envelope.
    float rAmp = RIPPLE_BASE + RIPPLE_GAIN * bass + 0.35 * RIPPLE_GAIN * washBass;
    float ripple = sin(r * RIPPLE_FREQ - time * RIPPLE_SPEED) * rAmp;
    vec2 disp = normalize(p + 1e-5) * ripple;

    // Treble shimmer — a fine, high-frequency cross warp that only shows up when
    // there's energy up top, so detailed passages make the art "shiver".
    disp += SHIMMER_GAIN * treb
          * vec2(sin(cuv.y * 42.0 + time * 3.1), cos(cuv.x * 42.0 + time * 2.7));

    vec3 art = texture(cover, 0.5 + p + disp).rgb;

    // Ambient styling: slight desaturation, pulled toward a near-black field, with
    // an accent-tinted shadow so coverless/dark art still carries the album color.
    float lum = dot(art, vec3(0.299, 0.587, 0.114));
    art = mix(vec3(lum), art, 0.86);
    art = mix(vec3(0.018, 0.022, 0.038), art, 0.5);
    art = mix(art, art * accent.rgb * 1.7, 0.14);

    // Brightness held near-constant — a faint slow swell only, no fast term.
    art *= 0.94 + BLOOM * washEnergy;

    // Vignette + rectangular edge feather → dissolves into the wash.
    float vr = length(cuv - 0.5);
    float a = smoothstep(1.05, 0.25, vr);
    a *= smoothstep(0.0, 0.05, uv.x) * (1.0 - smoothstep(0.95, 1.0, uv.x));
    a *= smoothstep(0.0, 0.05, uv.y) * (1.0 - smoothstep(0.95, 1.0, uv.y));
    a *= 0.92;

    // Triangular-PDF dither, ~±1 LSB, before the framebuffer quantises.
    float d1 = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453);
    float d2 = fract(sin(dot(gl_FragCoord.xy, vec2(39.346, 11.135))) * 24634.6345);
    art += vec3((d1 + d2 - 1.0) / 255.0);

    fragColor = vec4(art * a, a) * qt_Opacity; // premultiplied alpha
}
