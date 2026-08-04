// ===========================================================================
//  scene.hpp -- synthesis. Turns parameters into frames.
//
//  The inverse of analyze.hpp, and the other half of the core API. A Scene is a
//  stack of layers, a global transform, and a tone curve, all described by plain
//  JSON. Every tool here renders through this one path:
//
//    ppm-video   analysis -> parameters (+ essence grids) -> Scene -> frames
//    ppm-media   analysis -> parameters (+ one essence grid) -> Scene -> frames
//    ppm-prompt  model    -> parameters                      -> Scene -> frames
//
//  Two consequences of routing everything through parameters rather than through
//  code:
//
//    * Nothing executes model output. A generated scene is data, validated on
//      load, so a bad generation is a rejected file rather than a running
//      program.
//    * Every result is reproducible and editable. The parameters that produced a
//      render are written next to it, so it can be re-rendered at a different
//      resolution, or hand-tweaked, without re-running analysis or a model.
//
//  Any parameter may be a constant or an animation (see Anim), so the whole
//  scene is a function of frame index -- which keeps synthesis pure, and
//  therefore threadable per row exactly as the shader runtime is.
// ===========================================================================
#ifndef PPM_SCENE_HPP
#define PPM_SCENE_HPP

#include "frame.hpp"
#include "json.hpp"
#include "pgl.hpp"
#include "pnoise.hpp"

#include <algorithm>
#include <string>
#include <thread>
#include <vector>

namespace ppm {
namespace scene {

using pgl::vec2;
using pgl::vec3;
using pgl::TAU;

// ---------------------------------------------------------------------------
// easing
// ---------------------------------------------------------------------------

inline float ease(const std::string &name, float t) {
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    if (name == "linear")           return t;
    if (name == "smoothstep")       return t * t * (3.0f - 2.0f * t);
    if (name == "smootherstep")     return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
    if (name == "ease_in_quad")     return t * t;
    if (name == "ease_out_quad")    return 1.0f - (1.0f - t) * (1.0f - t);
    if (name == "ease_in_cubic")    return t * t * t;
    if (name == "ease_out_cubic")   { const float u = 1.0f - t; return 1.0f - u * u * u; }
    if (name == "ease_in_out_sine") return 0.5f - 0.5f * cosf(3.14159265f * t);
    if (name == "pulse")            return 0.5f - 0.5f * cosf(TAU * t);
    if (name == "constant")         return 0.0f;
    return t;   // unknown easing degrades to linear rather than failing
}

/// A parameter that may be constant or animated.
///
/// Accepts either a bare number, or {"from":a,"to":b,"easing":"...","loop":bool}.
/// Reading both shapes through one type is what lets the whole schema stay flat:
/// no separate "animations" block that has to be kept in sync with the layers it
/// refers to by path.
struct Anim {
    float a = 0.0f, b = 0.0f;
    std::string easing = "linear";
    bool animated = false;
    bool loop = false;    ///< ping-pong, so the value returns to `a` at phase 1

    float at(float phase) const {
        if (!animated) return a;
        float t = phase;
        if (loop) t = 1.0f - fabsf(2.0f * phase - 1.0f);   // triangle wave
        return a + (b - a) * ease(easing, t);
    }
};

inline Anim read_anim(const json::Value &v, float fallback) {
    Anim out;
    out.a = out.b = fallback;
    if (v.is_num()) {
        out.a = out.b = v.flt(fallback);
    } else if (v.is_obj()) {
        out.a = v["from"].flt(fallback);
        out.b = v["to"].flt(out.a);
        out.easing = v["easing"].text("linear");
        out.loop = v["loop"].flag(false);
        out.animated = v.has("to");
    }
    return out;
}

// ---------------------------------------------------------------------------
// tone
// ---------------------------------------------------------------------------

struct Tone {
    Anim exposure, contrast, saturation, black_floor, vignette;
    std::string tonemap = "none";   ///< "none" | "tanh" | "reinhard"
    float temperature = 0.0f;

    static Tone from_json(const json::Value &v) {
        Tone t;
        t.exposure    = read_anim(v["exposure"], 1.0f);
        t.contrast    = read_anim(v["contrast"], 1.0f);
        t.saturation  = read_anim(v["saturation"], 1.0f);
        t.black_floor = read_anim(v["black_floor"], 0.0f);
        t.vignette    = read_anim(v["vignette"], 0.0f);
        t.tonemap     = v["tonemap"].text("none");
        t.temperature = v["temperature"].flt(0.0f);
        return t;
    }
};

/// Apply the tone curve. `r` is the normalised radius from centre, for vignette.
inline void apply_tone(const Tone &t, float phase, float radius,
                       float &cr, float &cg, float &cb) {
    const float exposure = t.exposure.at(phase);
    cr *= exposure; cg *= exposure; cb *= exposure;

    if (t.temperature != 0.0f) {
        // Cheap warm/cool trim: push red and blue in opposition.
        cr *= 1.0f + t.temperature * 0.2f;
        cb *= 1.0f - t.temperature * 0.2f;
    }

    if (t.tonemap == "tanh") {
        cr = tanhf(cr); cg = tanhf(cg); cb = tanhf(cb);
    } else if (t.tonemap == "reinhard") {
        cr = cr / (1.0f + cr); cg = cg / (1.0f + cg); cb = cb / (1.0f + cb);
    }

    const float sat = t.saturation.at(phase);
    if (sat != 1.0f) {
        const float l = luma(cr, cg, cb);
        cr = l + (cr - l) * sat;
        cg = l + (cg - l) * sat;
        cb = l + (cb - l) * sat;
    }

    const float con = t.contrast.at(phase);
    if (con != 1.0f) {
        cr = (cr - 0.5f) * con + 0.5f;
        cg = (cg - 0.5f) * con + 0.5f;
        cb = (cb - 0.5f) * con + 0.5f;
    }

    const float vig = t.vignette.at(phase);
    if (vig != 0.0f) {
        const float k = 1.0f - vig * radius * radius;
        cr *= k; cg *= k; cb *= k;
    }

    // Lift the floor last, so it is not then multiplied away. A pure-black floor
    // reads as a hole in the image and hides banding that a small lift exposes.
    const float floor_v = t.black_floor.at(phase);
    if (floor_v > 0.0f) {
        cr = floor_v + cr * (1.0f - floor_v);
        cg = floor_v + cg * (1.0f - floor_v);
        cb = floor_v + cb * (1.0f - floor_v);
    }

    cr = pgl::sat(cr); cg = pgl::sat(cg); cb = pgl::sat(cb);
}

// ---------------------------------------------------------------------------
// layers
// ---------------------------------------------------------------------------

struct Layer {
    std::string type = "gradient";
    std::string blend = "normal";
    Anim opacity;
    json::Value params;     ///< type-specific, read lazily

    // Cached common parameters, so the hot loop never touches the JSON tree.
    Anim scale, warp, angle, count, thickness, detail, seed, speed;
    std::vector<vec3> colors;

    static Layer from_json(const json::Value &v) {
        Layer L;
        L.type = v["type"].text("gradient");
        L.blend = v["blend"].text("normal");
        L.opacity = read_anim(v["opacity"], 1.0f);
        L.params = v["params"];

        L.scale     = read_anim(L.params["scale"], 2.0f);
        L.warp      = read_anim(L.params["warp"], 0.0f);
        L.angle     = read_anim(L.params["angle"], 0.0f);
        L.count     = read_anim(L.params["count"], 6.0f);
        L.thickness = read_anim(L.params["thickness"], 0.3f);
        L.detail    = read_anim(L.params["detail"], 0.0f);
        L.seed      = read_anim(L.params["seed"], 0.0f);
        L.speed     = read_anim(L.params["speed"], 1.0f);

        const json::Value &cols = L.params["colors"];
        for (size_t i = 0; i < cols.size(); ++i) {
            const json::Value &c = cols[i];
            if (c.size() >= 3)
                L.colors.push_back(vec3(c[0].flt(), c[1].flt(), c[2].flt()));
        }
        if (L.colors.empty()) {
            L.colors.push_back(vec3(0.05f, 0.05f, 0.08f));
            L.colors.push_back(vec3(0.9f, 0.9f, 0.95f));
        }
        return L;
    }

    /// Sample the layer's colour ramp. Interpolates in linear light, because
    /// blending display-encoded endpoints produces a dark band in the middle of
    /// what should be a smooth gradient.
    vec3 ramp(float t) const {
        t = pgl::sat(t);
        if (colors.size() == 1) return colors[0];
        const float s = t * (float)(colors.size() - 1);
        size_t i = (size_t)s;
        if (i >= colors.size() - 1) i = colors.size() - 2;
        const float f = s - (float)i;
        const vec3 &A = colors[i], &B = colors[i + 1];
        return vec3(
            to_srgb(to_linear(A.x) + (to_linear(B.x) - to_linear(A.x)) * f),
            to_srgb(to_linear(A.y) + (to_linear(B.y) - to_linear(A.y)) * f),
            to_srgb(to_linear(A.z) + (to_linear(B.z) - to_linear(A.z)) * f));
    }
};

inline vec3 blend_pixel(const std::string &mode, const vec3 &base, const vec3 &top, float alpha) {
    vec3 c = top;
    if (mode == "add")           c = base + top;
    else if (mode == "multiply") c = base * top;
    else if (mode == "screen")   c = vec3(1.0f) - (vec3(1.0f) - base) * (vec3(1.0f) - top);
    else if (mode == "overlay") {
        auto ov = [](float b, float t) {
            return b < 0.5f ? 2.0f * b * t : 1.0f - 2.0f * (1.0f - b) * (1.0f - t);
        };
        c = vec3(ov(base.x, top.x), ov(base.y, top.y), ov(base.z, top.z));
    }
    return base + (c - base) * alpha;
}

// ---------------------------------------------------------------------------
// reconstruction source
// ---------------------------------------------------------------------------

/// The essence grids a "reconstruct" layer draws from.
///
/// One grid per frame for video regeneration; a single grid for a still, which
/// every frame then samples through the scene's own motion.
struct ReconstructSource {
    std::vector<Frame> essence;
    std::vector<float> high_freq;   ///< per grid; how much detail was discarded

    bool empty() const { return essence.empty(); }
    const Frame &at(int frame) const {
        if (essence.size() == 1) return essence[0];
        const int i = std::max(0, std::min((int)essence.size() - 1, frame));
        return essence[(size_t)i];
    }
    float hf(int frame) const {
        if (high_freq.empty()) return 0.0f;
        if (high_freq.size() == 1) return high_freq[0];
        const int i = std::max(0, std::min((int)high_freq.size() - 1, frame));
        return high_freq[(size_t)i];
    }
};

// ---------------------------------------------------------------------------
// scene
// ---------------------------------------------------------------------------

struct Transform {
    Anim pan_x, pan_y, zoom, rotate;

    static Transform from_json(const json::Value &v) {
        Transform t;
        t.pan_x  = read_anim(v["pan_x"], 0.0f);
        t.pan_y  = read_anim(v["pan_y"], 0.0f);
        t.zoom   = read_anim(v["zoom"], 1.0f);
        t.rotate = read_anim(v["rotate"], 0.0f);
        return t;
    }
};

struct Scene {
    int width = 1280, height = 720, frames = 240;
    float fps = 30.0f;
    int samples = 1;
    vec3 background = vec3(0.0f);
    Tone tone;
    Transform transform;
    std::vector<Layer> layers;

    static Scene from_json(const json::Value &v, std::string &error) {
        Scene s;
        const json::Value &out = v["output"];
        s.width   = out["width"].integer(1280);
        s.height  = out["height"].integer(720);
        s.frames  = out["frames"].integer(240);
        s.fps     = out["fps"].flt(30.0f);
        s.samples = out["samples"].integer(1);

        if (s.width <= 0 || s.height <= 0) { error = "output.width/height must be positive"; return s; }
        if (s.frames <= 0) { error = "output.frames must be positive"; return s; }
        if (s.samples <= 0 || s.samples > 8) { error = "output.samples must be 1..8"; return s; }
        // A generated scene can name an absurd resolution; refuse it here rather
        // than after allocating gigabytes.
        if ((long long)s.width * s.height > 80000000LL) {
            error = "output resolution exceeds 80 MPix";
            return s;
        }

        const json::Value &bg = v["background"];
        if (bg.size() >= 3) s.background = vec3(bg[0].flt(), bg[1].flt(), bg[2].flt());

        s.tone = Tone::from_json(v["tone"]);
        s.transform = Transform::from_json(v["transform"]);

        const json::Value &ls = v["layers"];
        for (size_t i = 0; i < ls.size(); ++i) s.layers.push_back(Layer::from_json(ls[i]));
        if (s.layers.empty() && v["layers"].is_null())
            error = "scene has no \"layers\" array";
        return s;
    }
};

// ---------------------------------------------------------------------------
// layer evaluation
// ---------------------------------------------------------------------------

/// Evaluate one layer at a point. `p` is aspect-corrected and centred (y in
/// [-1,1]); `uv` is in [0,1] with the origin top-left, for grid sampling.
inline vec3 eval_layer(const Layer &L, const ReconstructSource &src, int frame,
                       float phase, vec2 p, vec2 uv, bool &opaque) {
    opaque = true;
    const float t = phase * TAU;

    if (L.type == "reconstruct") {
        if (src.empty()) { opaque = false; return vec3(0.0f); }
        const Frame &g = src.at(frame);
        float r, gg, b;
        // Bicubic, not bilinear: bilinear upsampling of a coarse grid shows
        // diamond facets because its derivative jumps at cell boundaries.
        sample_bicubic_linear(g, uv.x, uv.y, r, gg, b);
        vec3 c(r, gg, b);

        // Inject procedural detail to stand in for the high frequencies the
        // grid discarded, modulated by the local gradient so texture lands on
        // edges and structure rather than across flat sky.
        const float amount = L.detail.at(phase);
        if (amount > 0.0f) {
            const float e = 1.0f / (float)std::max(2, g.width);
            float r2, g2, b2, r3, g3, b3;
            sample_bicubic_linear(g, uv.x + e, uv.y, r2, g2, b2);
            sample_bicubic_linear(g, uv.x, uv.y + e, r3, g3, b3);
            const float grad = fabsf(luma(r2, g2, b2) - luma(r, gg, b)) +
                               fabsf(luma(r3, g3, b3) - luma(r, gg, b));
            const float freq = std::max(1.0f, L.scale.at(phase)) * 40.0f;
            const float n = pgl::vnoise(vec3(p.x * freq, p.y * freq,
                                             L.seed.at(phase) + (float)frame * 0.05f)) - 0.5f;
            const float k = amount * src.hf(frame) * (0.25f + 4.0f * grad);
            c += vec3(n * k);
        }
        return c;
    }

    if (L.type == "gradient") {
        const float a = L.angle.at(phase) * TAU;
        const vec2 dir(cosf(a), sinf(a));
        // Project onto the axis and remap from [-1,1] to [0,1].
        return L.ramp(pgl::dot(p, dir) * 0.5f + 0.5f);
    }

    if (L.type == "radial") {
        return L.ramp(pgl::length(p) * L.scale.at(phase));
    }

    if (L.type == "fbm") {
        const float sc = L.scale.at(phase);
        const float w = L.warp.at(phase);
        vec2 q = p * sc;
        if (w > 0.0f) {
            // Domain warping: displace the lookup by another noise lookup. The
            // large offsets decorrelate the two, without which the warp
            // degenerates into a diagonal smear.
            const vec2 d(pgl::loop_noise(q, phase, 0.9f),
                         pgl::loop_noise(q + vec2(5.2f, 1.3f), phase, 0.9f));
            q += d * w * 2.0f;
        }
        float f = pgl::fbm(vec3(q.x, q.y, L.seed.at(phase) + cosf(t) * 0.5f),
                           std::max(1, (int)L.count.at(phase)));
        f = pgl::sat((f - 0.5f) * (1.0f + L.detail.at(phase)) + 0.5f);
        return L.ramp(f);
    }

    if (L.type == "worley") {
        const float sc = L.scale.at(phase);
        const float d = pgl::worley(p * sc + vec2(cosf(t), sinf(t)) * L.speed.at(phase));
        return L.ramp(pgl::sat(d * L.scale.at(phase) * 0.5f));
    }

    if (L.type == "rings") {
        const float n = std::max(1.0f, L.count.at(phase));
        const float radius = pgl::length(p);
        const float ph = radius * n - phase * L.speed.at(phase) * n;
        const float band = fabsf(pgl::fract(ph) - 0.5f) * 2.0f;
        // Smooth the band edge over roughly a pixel's worth of the ramp so
        // 1-sample renders do not stair-step.
        const float edge = pgl::smoothstep(L.thickness.at(phase) + 0.03f,
                                           L.thickness.at(phase) - 0.03f, band);
        return L.ramp(edge);
    }

    if (L.type == "grain") {
        const float amount = std::max(0.0f, L.detail.at(phase));
        // Reseeded per frame: grain that does not change between frames reads as
        // a dirty lens rather than as film.
        const float n = pgl::hash31(vec3(p.x * 733.1f, p.y * 911.7f,
                                         (float)frame + L.seed.at(phase))) - 0.5f;
        return vec3(n * amount);
    }

    if (L.type == "vignette") {
        const float k = 1.0f - L.detail.at(phase) * pgl::dot(p, p);
        return vec3(pgl::sat(k));
    }

    opaque = false;
    return vec3(0.0f);
}

// ---------------------------------------------------------------------------
// rendering
// ---------------------------------------------------------------------------

inline void render_rows(const Scene &s, const ReconstructSource &src, int frame,
                        Frame &out, int y0, int y1) {
    const float phase = s.frames > 1 ? (float)frame / (float)s.frames : 0.0f;
    const float W = (float)s.width, H = (float)s.height;

    // Global transform, resolved once per frame rather than per pixel.
    const float zoom = std::max(1e-3f, s.transform.zoom.at(phase));
    const float rot = s.transform.rotate.at(phase) * TAU;
    const float cs = cosf(rot), sn = sinf(rot);
    const float px = s.transform.pan_x.at(phase);
    const float py = s.transform.pan_y.at(phase);

    const int S = std::max(1, s.samples);
    const float inv = 1.0f / (float)S;
    const float invN = 1.0f / (float)(S * S);

    for (int y = y0; y < y1; ++y) {
        for (int x = 0; x < s.width; ++x) {
            float ar = 0, ag = 0, ab = 0;
            for (int sy = 0; sy < S; ++sy) {
                for (int sx = 0; sx < S; ++sx) {
                    const float fx = (float)x + ((float)sx + 0.5f) * inv;
                    const float fy = (float)y + ((float)sy + 0.5f) * inv;

                    // Centred, aspect-corrected. Dividing by H (not by both axes)
                    // is what keeps the image from stretching with aspect ratio.
                    vec2 p((fx * 2.0f - W) / H, (fy * 2.0f - H) / H);

                    // Apply zoom/rotate about the centre, then pan. Inverse
                    // mapping: we transform the sample point, not the content.
                    p = vec2(p.x * cs - p.y * sn, p.x * sn + p.y * cs) / zoom;
                    p += vec2(-px, -py) * 2.0f;

                    // uv for grid sampling, tracking the same transform.
                    vec2 uv((p.x * H / W) * 0.5f + 0.5f, p.y * 0.5f + 0.5f);

                    vec3 col = s.background;
                    for (const Layer &L : s.layers) {
                        bool opaque = true;
                        const vec3 c = eval_layer(L, src, frame, phase, p, uv, opaque);
                        if (!opaque) continue;
                        col = blend_pixel(L.blend, col, c, pgl::sat(L.opacity.at(phase)));
                    }

                    const float radius = pgl::length(vec2((fx * 2.0f - W) / H,
                                                          (fy * 2.0f - H) / H));
                    apply_tone(s.tone, phase, radius, col.x, col.y, col.z);
                    ar += col.x; ag += col.y; ab += col.z;
                }
            }
            out.set(x, y, ar * invN, ag * invN, ab * invN);
        }
    }
}

inline void render_frame(const Scene &s, const ReconstructSource &src, int frame,
                         Frame &out, int threads) {
    if (out.width != s.width || out.height != s.height) out.resize(s.width, s.height);
    if (threads <= 1) {
        render_rows(s, src, frame, out, 0, s.height);
        return;
    }
    std::vector<std::thread> pool;
    pool.reserve((size_t)threads);
    const int band = (s.height + threads - 1) / threads;
    for (int t = 0; t < threads; ++t) {
        const int y0 = t * band;
        int y1 = y0 + band;
        if (y1 > s.height) y1 = s.height;
        if (y0 >= y1) break;
        pool.emplace_back([&s, &src, frame, &out, y0, y1]() {
            render_rows(s, src, frame, out, y0, y1);
        });
    }
    for (std::thread &th : pool) th.join();
}

// ---------------------------------------------------------------------------
// scene construction helpers (used by the CLIs)
// ---------------------------------------------------------------------------

inline json::Value anim_json(float from, float to, const char *easing, bool loop = false) {
    json::Value v = json::Value::obj();
    v.set("from", json::Value((double)from));
    v.set("to", json::Value((double)to));
    v.set("easing", json::Value(std::string(easing)));
    if (loop) v.set("loop", json::Value(true));
    return v;
}

inline json::Value colors_json(const std::vector<float> &rgb_triples) {
    json::Value out = json::Value::arr();
    for (size_t i = 0; i + 2 < rgb_triples.size(); i += 3)
        out.push(json::rgb_array(rgb_triples[i], rgb_triples[i + 1], rgb_triples[i + 2]));
    return out;
}

} // namespace scene
} // namespace ppm

#endif // PPM_SCENE_HPP
