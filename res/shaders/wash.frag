#version 440

// Ambient Wash background: art-color radial field anchored top-left and blended
// toward black, with output dithering so the dark gradient shows no 8-bit
// banding. Computed in float and dithered at output — the correct fix for
// banding regardless of framebuffer depth. Foundation for future GPU screens.

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec4 color0;     // stop @ 0.00  (accent-heavy)
    vec4 color1;     // stop @ 0.62  (mid)
    vec4 color2;     // stop @ 1.00  (near-black)
    vec2 resolution; // item size in px (for a circular field in pixel space)
};

void main() {
    vec2 px = qt_TexCoord0 * resolution;
    vec2 center = vec2(0.22 * resolution.x, -0.10 * resolution.y);
    float R = 1.25 * max(resolution.x, resolution.y);
    float t = length(px - center) / R;

    vec3 col = (t <= 0.62)
        ? mix(color0.rgb, color1.rgb, t / 0.62)
        : mix(color1.rgb, color2.rgb, clamp((t - 0.62) / 0.38, 0.0, 1.0));

    // Triangular-PDF dither, ~±1 LSB, applied before the framebuffer quantises.
    float r1 = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453);
    float r2 = fract(sin(dot(gl_FragCoord.xy, vec2(39.3468, 11.1357))) * 24634.6345);
    float d = (r1 + r2 - 1.0) / 255.0;

    fragColor = vec4(col + vec3(d), 1.0) * qt_Opacity;
}
