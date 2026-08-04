// ===========================================================================
//  rings -- concentric rings with analytic antialiasing.
//
//  The point of this shader is the AA, not the picture. It draws hard-edged
//  geometry and still produces clean edges at --samples 1, because the edge
//  coverage is computed from the distance field rather than discovered by
//  sampling. Compare:
//
//      ppmr shaders/rings.cpp -o aa.mp4                 # analytic, 1 sample
//      ppmr shaders/rings.cpp -o none.mp4 --set aa=0    # hard threshold
//
//  The second is 1x the cost and visibly stair-stepped. Getting the same
//  quality by brute force would need --samples 4, i.e. 16x the work.
// ===========================================================================
#define SHADER_NAME "rings"
#define SHADER_DESCRIPTION "Concentric rings demonstrating analytic (SDF) antialiasing."

#include "ppmshader.hpp"
#include "aa.hpp"

static Param count{"count", 9.0f, "number of rings"};
static Param thickness{"thickness", 0.28f, "ring thickness, as a fraction of spacing"};
static Param spin{"spin", 1.0f, "rotations of the dot ring per loop"};
static Param aa{"aa", 1.0f, "1 = analytic AA, 0 = hard threshold (for comparison)"};

vec4 mainImage(vec2 FC, const Uniforms &u) {
    const vec2 p = (FC * 2.0f - u.resolution) / u.resolution.y;
    const float t = u.phase * TAU;

    // One pixel, measured in `p` units. Every analytic AA decision needs this:
    // it is the bridge between world-space distance and screen-space coverage.
    const float texel = texel_from_height(u.resolution.y);

    // Coverage helper: inside-positive distance -> [0,1]. With aa=0 this
    // degenerates to a step(), which is exactly the artefact being illustrated.
    auto cover = [&](float d_inside) {
        return aa > 0.5f ? aa_sdf(d_inside, texel) : (d_inside > 0.0f ? 1.0f : 0.0f);
    };

    vec3 col(0.02f, 0.02f, 0.04f);

    // --- rings -------------------------------------------------------------
    // A pulsing radius offset keeps the rings moving through each other so the
    // edge quality is visible in motion, not just in a still.
    const float radius = length(p);
    const float spacing = 1.0f / count;
    const float phase = radius / spacing - t / TAU * 2.0f;

    // fract() is discontinuous, which would wreck a derivative-based AA. It is
    // fine here because we are not differentiating: the distance to the nearest
    // ring edge is known exactly, in the same units as `p`.
    const float band = fabsf(fract(phase) - 0.5f) * 2.0f;   // 0 at centre of ring
    const float ring_d = (thickness - band) * spacing;       // inside-positive
    const float ring = cover(ring_d);

    // Tint rings by radius. Warm in the middle, cool at the edge.
    const vec3 warm(1.0f, 0.55f, 0.25f);
    const vec3 cool(0.25f, 0.6f, 1.0f);
    col = mix(col, mix(warm, cool, sat(radius * 0.7f)), ring);

    // --- orbiting dots -----------------------------------------------------
    // Circles are the ideal case for analytic AA: the SDF is exact, so the
    // edges are as good as a 16x supersample for the price of one sample.
    const int dots = 6;
    for (int k = 0; k < dots; ++k) {
        const float a = t * spin + TAU * (float)k / (float)dots;
        const vec2 c = vec2(cosf(a), sinf(a)) * 0.62f;
        const float d = sd_circle(p - c, 0.055f);
        col = mix(col, vec3(1.0f, 0.95f, 0.85f), cover(-d));
    }

    // --- a rounded box, to show sd_box + smin ------------------------------
    const float box = sd_box(rot(t * 0.5f) * p, vec2(0.16f, 0.16f)) - 0.03f;
    col = mix(col, vec3(0.1f, 0.9f, 0.7f), cover(-box) * 0.85f);

    return vec4(col, 1.0f);
}
