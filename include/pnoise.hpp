// ===========================================================================
//  pnoise.hpp -- hashes and noise for CPU shaders.
//
//  Everything here is deterministic and stateless: the same coordinate always
//  yields the same value, on every thread, in every frame. That is required,
//  not merely convenient -- the runtime shades pixels in parallel and may
//  evaluate the same pixel several times for supersampling, so any hidden
//  state or use of rand() would produce seams and flicker.
//
//  The hashes are the usual sin-fract construction. It is cheap and adequate
//  for visuals, but it is not a good PRNG: quality degrades at large
//  coordinates because sinf() loses precision there. Keep inputs roughly
//  within +-1000, or add an integer hash if you need more.
// ===========================================================================
#ifndef PPM_NOISE_HPP
#define PPM_NOISE_HPP

#include "pgl.hpp"

namespace pgl {

// ---------------------------------------------------------------------------
// hashes -- uniform in [0,1)
// ---------------------------------------------------------------------------

inline float hash11(float p) {
    return fract(sinf(p * 12.9898f) * 43758.5453123f);
}

inline float hash21(vec2 p) {
    return fract(sinf(dot(p, vec2(12.9898f, 78.233f))) * 43758.5453123f);
}

inline vec2 hash22(vec2 p) {
    return vec2(hash21(p), hash21(p + vec2(37.19f, 11.71f)));
}

inline float hash31(vec3 p) {
    return fract(sinf(dot(p, vec3(12.9898f, 78.233f, 37.719f))) * 43758.5453123f);
}

inline vec3 hash33(vec3 p) {
    return vec3(hash31(p), hash31(p + vec3(19.19f, 3.71f, 7.13f)),
                hash31(p + vec3(41.7f, 23.3f, 5.9f)));
}

// ---------------------------------------------------------------------------
// value noise
// ---------------------------------------------------------------------------

/// Quintic fade. Smoother than the cubic smoothstep: its second derivative is
/// zero at the endpoints too, which removes the faint grid creases that show up
/// when value noise is used as a displacement.
inline float fade(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }

inline float vnoise(vec2 p) {
    const vec2 i = floor(p);
    const vec2 f = p - i;
    const float u = fade(f.x), v = fade(f.y);
    const float a = hash21(i);
    const float b = hash21(i + vec2(1.0f, 0.0f));
    const float c = hash21(i + vec2(0.0f, 1.0f));
    const float d = hash21(i + vec2(1.0f, 1.0f));
    return mix(mix(a, b, u), mix(c, d, u), v);
}

inline float vnoise(vec3 p) {
    const vec3 i = floor(p);
    const vec3 f = p - i;
    const float u = fade(f.x), v = fade(f.y), w = fade(f.z);
    auto h = [&](float dx, float dy, float dz) {
        return hash31(i + vec3(dx, dy, dz));
    };
    const float z0 = mix(mix(h(0, 0, 0), h(1, 0, 0), u), mix(h(0, 1, 0), h(1, 1, 0), u), v);
    const float z1 = mix(mix(h(0, 0, 1), h(1, 0, 1), u), mix(h(0, 1, 1), h(1, 1, 1), u), v);
    return mix(z0, z1, w);
}

// ---------------------------------------------------------------------------
// fractal sums
// ---------------------------------------------------------------------------

/// Fractal Brownian motion: octaves of noise at doubling frequency and halving
/// amplitude. Normalised so the result stays in [0,1].
inline float fbm(vec2 p, int octaves = 5, float lacunarity = 2.0f, float gain = 0.5f) {
    float sum = 0.0f, amp = 1.0f, norm = 0.0f;
    for (int i = 0; i < octaves; ++i) {
        sum += amp * vnoise(p);
        norm += amp;
        p = p * lacunarity;
        amp *= gain;
    }
    return norm > 0.0f ? sum / norm : 0.0f;
}

inline float fbm(vec3 p, int octaves = 5, float lacunarity = 2.0f, float gain = 0.5f) {
    float sum = 0.0f, amp = 1.0f, norm = 0.0f;
    for (int i = 0; i < octaves; ++i) {
        sum += amp * vnoise(p);
        norm += amp;
        p = p * lacunarity;
        amp *= gain;
    }
    return norm > 0.0f ? sum / norm : 0.0f;
}

/// Ridged variant: folds each octave around its midpoint to make creases.
inline float ridged(vec2 p, int octaves = 5) {
    float sum = 0.0f, amp = 1.0f, norm = 0.0f;
    for (int i = 0; i < octaves; ++i) {
        sum += amp * (1.0f - fabsf(vnoise(p) * 2.0f - 1.0f));
        norm += amp;
        p = p * 2.0f;
        amp *= 0.5f;
    }
    return norm > 0.0f ? sum / norm : 0.0f;
}

// ---------------------------------------------------------------------------
// cellular
// ---------------------------------------------------------------------------

/// Worley / Voronoi F1 distance: distance to the nearest of one feature point
/// per cell. Returns roughly [0, 1].
inline float worley(vec2 p) {
    const vec2 ip = floor(p);
    const vec2 fp = p - ip;
    float best = 8.0f;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            const vec2 g = vec2((float)dx, (float)dy);
            const vec2 o = hash22(ip + g);
            const float d = length(g + o - fp);
            best = fminf(best, d);
        }
    }
    return best;
}

/// Seamlessly looping 2D noise.
///
/// Scrolling noise cannot loop: after one period the field has translated, so
/// the last frame does not match the first. Instead this traces a *circle*
/// through a 3D noise field using two extra dimensions, so phase 0 and phase 1
/// are literally the same point. `radius` sets how much the field changes over
/// one loop -- larger means more variation, smaller means a subtler breathing.
inline float loop_noise(vec2 p, float phase, float radius = 1.0f) {
    const float a = phase * TAU;
    return vnoise(vec3(p.x + radius * cosf(a), p.y, radius * sinf(a)));
}

// ---------------------------------------------------------------------------
// domain warping
// ---------------------------------------------------------------------------

/// Feeds noise back into its own coordinates. This is the single cheapest way
/// to turn bland fbm into something that looks like smoke, marble or fluid.
inline float warped_fbm(vec2 p, float strength = 1.0f, int octaves = 4) {
    const vec2 q = vec2(fbm(p, octaves), fbm(p + vec2(5.2f, 1.3f), octaves));
    return fbm(p + q * strength, octaves);
}

} // namespace pgl

#endif // PPM_NOISE_HPP
