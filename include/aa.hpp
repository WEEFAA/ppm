// ===========================================================================
//  aa.hpp -- antialiasing for CPU shaders.
//
//  A GPU gets screen-space derivatives for almost free: fragments are shaded in
//  2x2 quads, so the hardware can subtract neighbours to get dFdx/dFdy, and
//  fwidth() falls out of that. A scalar CPU kernel shading one point at a time
//  has no neighbours, so none of those functions can exist here.
//
//  That leaves three strategies, cheapest first:
//
//    1. Supersampling (`--samples N`). Brute force: evaluate NxN points per
//       pixel and average. Requires nothing from the shader and handles every
//       case, including discontinuities that break derivative methods. Cost is
//       N^2. On a CPU renderer that is not running in real time this is usually
//       the right answer, and it is why the runtime offers it as a flag.
//
//    2. Analytic AA (`aa_sdf`). If you know the signed distance to the edge,
//       one clamp gives a perfect gradient at zero extra cost. Always prefer
//       this for shapes you have an SDF for.
//
//    3. Finite-difference AA (`aa_grad` + `grad_fd`). Recovers a gradient by
//       sampling the field at neighbouring points, then normalises the edge by
//       it. This is the CPU stand-in for fwidth(), and it costs 2 extra field
//       evaluations rather than being free -- so use it on the cheap scalar
//       field that defines the edge, not on the whole shader.
//
//  Sign convention: every function here takes an INSIDE-POSITIVE value, i.e.
//  positive inside the shape and negative outside. A standard SDF is
//  outside-positive, so negate it: aa_sdf(-d, texel).
// ===========================================================================
#ifndef PPM_AA_HPP
#define PPM_AA_HPP

#include "pgl.hpp"

namespace pgl {

// ---------------------------------------------------------------------------
// 2. analytic AA for a known distance field
// ---------------------------------------------------------------------------

/// Coverage in [0,1] for an inside-positive distance `d_inside`, where `texel`
/// is the size of one pixel in the same units as `d_inside`.
///
/// The 0.5 offset is what makes this correct rather than merely smooth: when
/// the edge passes exactly through the pixel centre, half the pixel is covered,
/// so coverage must be 0.5. Full coverage is reached half a pixel further in.
inline float aa_sdf(float d_inside, float texel) {
    return sat(d_inside / texel + 0.5f);
}

/// `texel` for the common normalised coordinate system
/// `p = (FC * 2 - resolution) / resolution.y`, where the visible y range is
/// [-1, 1] regardless of resolution.
inline float texel_from_height(float height) { return 2.0f / height; }

// ---------------------------------------------------------------------------
// 3. finite-difference gradient -- the CPU stand-in for dFdx/dFdy
// ---------------------------------------------------------------------------

/// Partial derivatives of a scalar field, by forward difference.
///
/// `f` is any callable `float(vec2)`. `h` is the step in the same units as `p`;
/// pass the pixel size so the gradient is measured per pixel, which is what the
/// AA maths below expects. Costs 2 extra evaluations of `f` (3 total).
template <class F>
inline vec2 grad_fd(F f, vec2 p, float h) {
    const float c = f(p);
    return vec2(f(p + vec2(h, 0.0f)) - c, f(p + vec2(0.0f, h)) - c) / h;
}

/// Equivalent of GLSL fwidth(): the L1 norm of the gradient.
template <class F>
inline float fwidth_fd(F f, vec2 p, float h) {
    const vec2 g = grad_fd(f, p, h);
    return (fabsf(g.x) + fabsf(g.y)) * h;
}

/// Coverage for an inside-positive value whose gradient you supply.
///
/// Use when the field is not a true distance field -- noise, a warped SDF, a
/// threshold on an arbitrary continuous function -- so its slope varies from
/// place to place and a single constant `texel` would make some edges mushy and
/// others hard. Normalising by the local gradient magnitude keeps edge
/// thickness uniform.
///
/// The 0.7 factor slightly widens the ramp relative to a strict half-pixel;
/// it reads as cleaner than 1.0 because a single linear ramp under-estimates
/// the true pixel coverage integral near corners.
inline float aa_grad(float d_inside, vec2 dxy) {
    const float w = length(dxy);
    // A zero gradient means a flat region with no edge in it. Snapping the
    // scale to something huge makes the clamp resolve to a hard 0 or 1 rather
    // than producing NaN.
    const float scale = w > 0.0f ? 1.0f / w : 1e7f;
    return sat(0.5f + 0.7f * scale * d_inside);
}

/// One-call form: threshold a scalar field at `level` with automatic AA.
///
/// Note the field is sampled 3 times, so keep `f` cheap. If the field is
/// discontinuous (anything built on floor/fract/mod), the derivative is
/// meaningless -- either rebuild it continuously, or just use `--samples`.
template <class F>
inline float aa_threshold(F f, vec2 p, float level, float h) {
    const float d = level - f(p);
    return aa_grad(d, grad_fd(f, p, h) * h);
}

// ---------------------------------------------------------------------------
// signed distance fields, all outside-positive (negate for the aa_* helpers)
// ---------------------------------------------------------------------------

inline float sd_circle(vec2 p, float r) { return length(p) - r; }

inline float sd_box(vec2 p, vec2 b) {
    const vec2 d = abs(p) - b;
    return length(max(d, 0.0f)) + fminf(fmaxf(d.x, d.y), 0.0f);
}

inline float sd_segment(vec2 p, vec2 a, vec2 b) {
    const vec2 pa = p - a, ba = b - a;
    const float h = sat(dot(pa, ba) / dot(ba, ba));
    return length(pa - ba * h);
}

/// Regular n-gon of circumradius r, centred on the origin.
inline float sd_ngon(vec2 p, float r, int n) {
    const float a = atan2f(p.y, p.x);
    const float seg = TAU / (float)n;
    const float b = seg * floorf(a / seg + 0.5f);
    const vec2 q = rot(-b) * p;
    return q.x - r * cosf(seg * 0.5f);
}

/// Smooth minimum: unions two fields with a rounded seam of width k.
inline float smin(float a, float b, float k) {
    const float h = sat(0.5f + 0.5f * (b - a) / k);
    return mix(b, a, h) - k * h * (1.0f - h);
}

/// Smooth maximum, for rounded intersections.
inline float smax(float a, float b, float k) {
    const float h = sat(0.5f - 0.5f * (b - a) / k);
    return mix(b, a, h) + k * h * (1.0f - h);
}

} // namespace pgl

#endif // PPM_AA_HPP

