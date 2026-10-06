#version 440

// Spectrum waterfall (scrolling spectrogram). Reads the same history ring-buffer
// texture the ridgeline uses — x = frequency (bass→treble, left→right), each row a
// past spectrum — but renders it flat as a heat field instead of extruded ridges.
// Newest spectrum is at the bottom; rows march upward and fade as they age, so the
// recent past of the music streams up the screen. `head` is the number of rows
// committed (newest = ring row head-1); `frac` (0..1) slides everything up smoothly
// between commits, in lockstep with `head` from the C++ side. Magnitude drives both
// colour (dark → accent → hot) and opacity, so silence fades into the ambient wash
// and energy lights up. Premultiplied alpha, edge-feathered, dithered.

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec4 accent;     // album accent (rgb used)
    int head;        // committed history rows (newest = ring row head-1)
    float frac;      // sub-row scroll position 0..1
    int histRows;    // history texture height (ring-buffer modulo) = HIST_ROWS
    vec2 resolution; // unused; kept for aspect-aware tuning
};

layout(binding = 1) uniform sampler2D history; // bands × rows ring-buffer spectrogram

const float VISROWS = 180.0; // history rows spanning the visible height (≤ histRows)
const float FLOOR   = 0.05;  // baseline opacity so the field isn't dead in silence

// Sample one ring row (wrapped) at frequency xu.
float rowMag(float xu, float j) {
    float rr = mod(float(head) - 1.0 - j, float(histRows));
    return texture(history, vec2(xu, (rr + 0.5) / float(histRows))).r;
}

// Dark → dim-accent → accent → hot-accent heat ramp.
vec3 heat(float m) {
    vec3 c0 = mix(accent.rgb, vec3(0.0), 0.92);
    vec3 c1 = mix(accent.rgb, vec3(0.0), 0.42);
    vec3 c2 = accent.rgb;
    vec3 c3 = mix(accent.rgb, vec3(1.0), 0.75);
    vec3 c = mix(c0, c1, smoothstep(0.0, 0.32, m));
    c = mix(c, c2, smoothstep(0.28, 0.68, m));
    c = mix(c, c3, smoothstep(0.66, 1.0, m));
    return c;
}

void main() {
    float py = 1.0 - qt_TexCoord0.y;            // 0 bottom (newest) .. 1 top (oldest)
    float xu = clamp(qt_TexCoord0.x, 0.0, 1.0); // frequency

    // Fractional row back from newest, slid smoothly by frac. Interpolate between
    // the two bounding rows *after* wrapping each, so the ring seam (oldest↔newest
    // adjacency) never blends across — a texture y-filter would smear it.
    float jf = py * VISROWS - frac;
    float j0 = floor(jf);
    float t  = jf - j0;
    float mag = mix(rowMag(xu, j0), rowMag(xu, j0 + 1.0), t);

    mag = clamp(mag, 0.0, 1.0);
    mag = pow(mag, 0.82); // mild lift so faint detail reads

    vec3 col = heat(mag);

    // Opacity from magnitude: quiet bins fade to the wash, energy lights up. Older
    // rows quieten as they climb so the top dissolves cleanly.
    float age = smoothstep(1.0, 0.55, py); // 1 near bottom → 0 at top
    float a = (FLOOR + (1.0 - FLOOR) * smoothstep(0.04, 0.8, mag)) * age;

    // Rectangular edge feather → dissolves into the ambient field.
    a *= smoothstep(0.0, 0.04, xu) * (1.0 - smoothstep(0.96, 1.0, xu));
    a *= smoothstep(0.0, 0.03, py);
    a *= 0.95;

    // Triangular-PDF dither, ~±1 LSB, before the framebuffer quantises.
    float d1 = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453);
    float d2 = fract(sin(dot(gl_FragCoord.xy, vec2(39.346, 11.135))) * 24634.6345);
    col += vec3((d1 + d2 - 1.0) / 255.0);

    fragColor = vec4(col * a, a) * qt_Opacity; // premultiplied alpha
}
