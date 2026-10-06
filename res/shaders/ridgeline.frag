#version 440

// Ridgeline field ("Unknown Pleasures" family), driven by a true scrolling
// spectrogram stored as a ring buffer. The `history` texture holds the last N
// spectra (x = frequency, bass→treble left→right). `head` is the number of rows
// committed; the newest is ring row (head-1), so stacked ridge j reads ring row
// (head-1-j) — a wave is born at the bottom and marches upward unchanged as it
// ages, fading as it climbs. `frac` (0..1) slides every ridge up smoothly between
// commits, kept in lockstep with `head` by the C++ side.
//
// Anti-aliasing: rows are composited front (bottom/newest) to back with a
// screen-space-derivative-feathered coverage (the AA ridge silhouette) and a
// Gaussian crest line (symmetric, so no hard top edge). Fills are translucent so
// the field blends into the ambient wash rather than blacking it out.

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec4 accent;     // album accent (rgb used)
    int head;        // committed history rows (newest = ring row head-1)
    float frac;      // sub-row scroll position 0..1
    int histRows;    // history texture height (ring-buffer modulo) = HIST_ROWS
    vec2 resolution; // unused for now; kept for aspect-aware tuning
};

layout(binding = 1) uniform sampler2D history; // bands × rows ring-buffer spectrogram

void main() {
    const int   VIS = 110;      // visible stacked ridges (≤ histRows)
    const float G   = 1.0 / float(VIS); // baseline spacing (fills the height)
    const float PK  = 0.13;     // peak height scale

    float py = 1.0 - qt_TexCoord0.y; // 0 bottom .. 1 top
    float xu = clamp(qt_TexCoord0.x, 0.0, 1.0);

    // Pixel size in py units → derivative-based AA widths.
    float px = fwidth(py);
    float w = 0.9 * px;            // coverage feather half-width
    float s = max(1.3 * px, 1e-4); // crest-line Gaussian sigma

    vec3 lineC = mix(accent.rgb, vec3(1.0), 0.55);
    vec3 fillC = mix(accent.rgb, vec3(0.015, 0.02, 0.035), 0.82);

    vec3 col = vec3(0.0);
    float a = 0.0;
    float trans = 1.0; // remaining transparency (front-to-back compositing)

    for (int j = 0; j < VIS; j++) {
        float rr = mod(float(head - 1 - j), float(histRows));
        float h = texture(history, vec2(xu, (rr + 0.5) / float(histRows))).r;
        float baseY = (float(j) + frac) * G; // rises smoothly as frac advances
        float rt = baseY + h * PK;           // ridge top (yUp)

        float cov = 1.0 - smoothstep(rt - w, rt + w, py); // AA body coverage
        if (cov <= 0.0)
            continue;                                     // pixel above this ridge

        float d = rt - py;                                // depth below crest
        float line_i = exp(-(d * d) / (2.0 * s * s));     // symmetric AA crest line
        float body = exp(-max(d, 0.0) * 7.0);             // brighter near the crest
        float fade = exp(-float(j) * 0.022);              // older rows quieten upward

        vec3 rc = lineC * line_i + fillC * body * (1.0 - line_i);
        float ra = clamp(0.95 * line_i + 0.45 * body, 0.0, 1.0) * fade;

        col += trans * cov * rc;
        a += trans * cov * ra;
        trans *= 1.0 - cov; // nearer ridge occludes those behind
        if (trans < 0.01)
            break;
    }

    // Soften the extreme top edge so the oldest crest dissolves cleanly.
    a *= 1.0 - smoothstep(0.95, 1.0, py);
    // Feather the side and bottom edges too, so the inset block dissolves into
    // the ambient field instead of terminating on hard rectangular borders.
    a *= smoothstep(0.0, 0.06, xu) * (1.0 - smoothstep(0.94, 1.0, xu));
    a *= smoothstep(0.0, 0.04, py);

    // Triangular-PDF dither, ~±1 LSB, before the framebuffer quantises.
    float p1 = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453);
    float p2 = fract(sin(dot(gl_FragCoord.xy, vec2(39.346, 11.135))) * 24634.6345);
    col += vec3((p1 + p2 - 1.0) / 255.0);

    fragColor = vec4(col * a, a) * qt_Opacity; // premultiplied alpha
}
