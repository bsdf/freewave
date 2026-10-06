#version 440

// Drop shadow for the Now Playing cover. The cover is an opaque rectangle, so
// its blurred silhouette is a blurred rectangle — the artwork never affects the
// result, and the shadow is analytic: no texture sampling, no render target, no
// blur kernel. Output is premultiplied, as Qt expects.
//
// The falloff is the product of two independent per-axis smoothsteps, not a
// function of distance to the rect. A Gaussian blur is separable, so a blurred
// rectangle's coverage really is f(x)*f(y); computing it radially instead puts a
// gradient discontinuity along each corner diagonal, which shows up as a hard
// crease radiating from the corners.

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec4 shadowColor;    // straight rgba; alpha scales the whole shadow
    vec2 resolution;     // this item's size in px (larger than the caster)
    vec2 boxHalf;        // half-extent of the casting rect, in px
    vec2 shadowOffset;   // caster-to-shadow displacement, in px
    float softness;      // half-width of the falloff, in px
};

void main() {
    vec2 p = (qt_TexCoord0 - 0.5) * resolution - shadowOffset;
    vec2 edge = abs(p) - boxHalf;
    vec2 f = vec2(1.0) - smoothstep(vec2(-softness), vec2(softness), edge);
    float a = f.x * f.y * shadowColor.a * qt_Opacity;
    fragColor = vec4(shadowColor.rgb, 1.0) * a;
}
