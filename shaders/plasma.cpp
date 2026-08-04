// ===========================================================================
//  plasma -- turbulent accumulation plasma.
//
//  A direct port of XorDev's code-golfed GLSL shader
//  (https://x.com/XorDev/status/1894123951401378051), by way of the
//  CPU/PPM version in Tsoding's gist:
//  https://gist.github.com/rexim/ef86bf70918034a5a57881456c0a0ccf
//
//  Kept in its original dense form on purpose: it is the reference test that
//  our pgl.hpp really is drop-in for GLSL. The only edits needed were adding
//  parentheses to the swizzles (v.xyyx -> v.xyyx()) and declaring the
//  accumulators. See docs/shader-authoring.json for what each line is doing.
// ===========================================================================
#define SHADER_NAME "plasma"
#define SHADER_DESCRIPTION "Turbulent accumulation plasma (after XorDev)."

#include "ppmshader.hpp"

static Param octaves{"octaves", 8.0f, "turbulence iterations; higher = more detail"};
static Param warp{"warp", 0.7f, "per-iteration domain-warp bias"};
static Param gain{"gain", 5.0f, "exposure fed into the tanh tonemap"};

vec4 mainImage(vec2 FC, const Uniforms &u) {
    vec4 o;                       // accumulator, starts at 0
    const vec2 r = u.resolution;
    const float t = u.phase * TAU; // full turn over the render => seamless loop

    // Normalised coordinates: origin at centre, y spanning [-1, 1], x scaled by
    // aspect. This is the single most important convention in shader code --
    // dividing by r.y (not r) is what keeps the image from stretching.
    vec2 p = (FC * 2.0f - r) / r.y, l, i;

    // A radial lens term. `l` is stashed because the tonemap below reuses it as
    // a brightness envelope, which is why it is a vector being abused as a
    // scalar carrier.
    vec2 v = p * (l += 4.0f - 4.0f * abs(0.7f - dot(p, p)));

    // Turbulence: each iteration warps the sample point by a cosine of its own
    // swizzle, with amplitude falling as 1/i. That 1/i falloff is what makes
    // this read as fractal detail rather than noise.
    for (; i.y++ < octaves;
         o += (sin(v.xyyx()) + 1.0f) * abs(v.x - v.y))
        v += cos(v.yx() * i.y + i + t) / i.y + warp;

    // tanh() as a tonemapper: the accumulation above is unbounded, and tanh
    // squashes any magnitude into [0,1) while keeping bright regions from
    // flat-clipping to white. The per-channel vec4(-1,1,2,0) offsets shift the
    // exponential differently for R, G and B, which is where the colour comes
    // from -- there is no palette anywhere in this shader.
    o = tanh(gain * exp(l.x - 4.0f - p.y * vec4(-1, 1, 2, 0)) / o);
    return o;
}
