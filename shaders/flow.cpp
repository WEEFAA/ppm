// ===========================================================================
//  flow -- domain-warped fbm, seamlessly looping.
//
//  Demonstrates two things the CPU is genuinely good at:
//
//    * Deep fbm. This evaluates noise ~30 times per sample. On a GPU that is
//      a real budget; here it just costs wall-clock, and wall-clock is what we
//      have. Turning `octaves` up is the whole point.
//    * A true seamless loop. Because time enters only through `u.phase`, and
//      only via loop_noise's circular path, the last frame joins the first
//      exactly -- no crossfade, no visible seam.
// ===========================================================================
#define SHADER_NAME "flow"
#define SHADER_DESCRIPTION "Domain-warped fbm that loops seamlessly."

#include "ppmshader.hpp"
#include "pnoise.hpp"

static Param scale{"scale", 2.4f, "noise frequency"};
static Param octaves{"octaves", 5.0f, "fbm octaves; cost scales linearly"};
static Param warp{"warp", 0.85f, "domain-warp strength"};
static Param loop_radius{"loop_radius", 0.9f, "how much the field evolves per loop"};
static Param contrast{"contrast", 1.35f, "final contrast"};

vec4 mainImage(vec2 FC, const Uniforms &u) {
    const vec2 p = (FC * 2.0f - u.resolution) / u.resolution.y;
    const int oct = (int)octaves;

    // Warp field. Two decorrelated noise lookups displace the sample point;
    // the offsets (5.2, 1.3) are arbitrary but must be large enough that the
    // two lookups do not correlate, or the warp collapses to a diagonal smear.
    const vec2 q = vec2(loop_noise(p * scale, u.phase, loop_radius),
                        loop_noise(p * scale + vec2(5.2f, 1.3f), u.phase, loop_radius));

    // Second warp stage. Warping the *warp* is what produces the filament
    // structure that single-stage warping never gets to.
    const vec2 s = vec2(loop_noise(p * scale + q * warp * 2.0f, u.phase, loop_radius),
                        loop_noise(p * scale + q * warp * 2.0f + vec2(1.7f, 9.2f),
                                   u.phase, loop_radius));

    float f = fbm(p * scale + s * warp, oct);

    // Ridged detail layered on top, masked by the base field so the creases
    // only appear in the bright regions.
    const float detail = ridged(p * scale * 2.0f + s, 4);
    f = mix(f, detail, 0.35f * f);

    // Contrast around the midpoint, then a palette.
    f = sat((f - 0.5f) * contrast + 0.5f);

    // Cosine palette: three phase-shifted cosines. Cheap, always in gamut, and
    // easy to retune -- the four vec3s are bias, amplitude, frequency, phase.
    const vec3 bias(0.5f, 0.45f, 0.55f);
    const vec3 amp(0.5f, 0.45f, 0.5f);
    const vec3 freq(1.0f, 1.0f, 1.0f);
    const vec3 phase(0.0f, 0.25f, 0.55f);
    vec3 col = bias + amp * cos(TAU * (freq * f + phase));

    // Vignette, and a soft lift so the darks do not crush to pure black.
    col = col * (1.0f - 0.35f * dot(p, p)) + 0.02f;

    return vec4(sat(col), 1.0f);
}
