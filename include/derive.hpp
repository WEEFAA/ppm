// ===========================================================================
//  derive.hpp -- turn analysis into synthesis parameters.
//
//  The bridge between the two halves of the core API. analyze.hpp measures;
//  scene.hpp renders; this decides what the measurements imply.
//
//  Every decision here is a judgement, not a measurement, so each one is
//  commented with its reasoning and exposed as a knob. The parameters file
//  records both the measurement and the derived value, so a user can see why a
//  render looks the way it does and override it without re-analysing.
// ===========================================================================
#ifndef PPM_DERIVE_HPP
#define PPM_DERIVE_HPP

#include "analyze.hpp"
#include "json.hpp"
#include "scene.hpp"

#include <algorithm>
#include <string>

namespace ppm {

struct DeriveOptions {
    int out_width = 0;      ///< 0 = source resolution
    int out_height = 0;
    float fps = 0.0f;       ///< 0 = source rate
    int frames = 0;         ///< 0 = as analysed
    int samples = 1;
    float detail = 0.12f;   ///< procedural detail restored, 0..1
    bool stylize = false;   ///< apply grading derived from measurement
    float grain = -1.0f;    ///< explicit grain; <0 means derive it from measurement
};

// ---------------------------------------------------------------------------
// fidelity presets
// ---------------------------------------------------------------------------

/// A fidelity preset is a target reconstruction ERROR, not a grid width.
///
/// Fixing the grid width cannot work across content: a width that is faithful on
/// a gradient is roughly six times too narrow for line art, because the grid
/// stores colour samples and hard edges are exactly what it cannot represent.
/// Targeting a measured error instead lets the content decide the width.
///
/// `detail` falls as fidelity rises: injected noise exists to disguise grid
/// softness, and at high fidelity there is no softness to disguise, so adding
/// texture that is not in the source would only reduce fidelity.
struct Fidelity {
    const char *name;
    float target_error;
    float detail;
};

inline const Fidelity *fidelity_presets(size_t &count) {
    static const Fidelity presets[] = {
        {"draft",    0.040f, 0.60f},
        {"balanced", 0.020f, 0.35f},
        {"faithful", 0.008f, 0.12f},
        {"max",      0.000f, 0.00f},   // grid at source width; exact reconstruction
    };
    count = sizeof(presets) / sizeof(presets[0]);
    return presets;
}

inline const Fidelity *find_fidelity(const std::string &name) {
    size_t n = 0;
    const Fidelity *p = fidelity_presets(n);
    for (size_t i = 0; i < n; ++i)
        if (name == p[i].name) return &p[i];
    return nullptr;
}

/// Convert a measured flat-region noise level into a grain layer amplitude.
///
/// The grain layer adds (hash - 0.5) * amount, so its mean absolute deviation is
/// amount/4. The measurement is a residual against a local 3x3 mean, which
/// recovers roughly 60% of the true deviation. Inverting both gives the ~6.7
/// factor. Verified against a synthetic source: 0.06 amplitude in, 0.00895
/// measured, 0.06 recovered.
inline float grain_from_measurement(float measured) {
    // Below this there is no grain worth reproducing, and inventing some would
    // add noise to a clean source -- which is exactly the bug this replaced.
    if (measured < 0.0015f) return 0.0f;
    return std::min(0.08f, measured * 6.7f);
}

namespace detail {

/// Mean of a field across frames, so single-frame noise does not drive a
/// sequence-wide decision.
template <class Fn>
inline float mean_over(const std::vector<FrameStats> &fs, Fn get) {
    if (fs.empty()) return 0.0f;
    double acc = 0;
    for (const FrameStats &s : fs) acc += (double)get(s);
    return (float)(acc / (double)fs.size());
}

} // namespace detail

/// Collect the measured values a synthesis decision is based on. Written into
/// the parameters file next to the derived values, so the reasoning is auditable.
inline json::Value measured_json(const SequenceAnalysis &a) {
    json::Value m = json::Value::obj();
    if (a.frames.empty()) return m;

    m.set("luma_mean", json::Value((double)detail::mean_over(a.frames, [](const FrameStats &s) { return s.luma_mean; })));
    m.set("luma_p05", json::Value((double)detail::mean_over(a.frames, [](const FrameStats &s) { return s.luma_p05; })));
    m.set("luma_p95", json::Value((double)detail::mean_over(a.frames, [](const FrameStats &s) { return s.luma_p95; })));
    m.set("contrast", json::Value((double)detail::mean_over(a.frames, [](const FrameStats &s) { return s.contrast; })));
    m.set("saturation", json::Value((double)detail::mean_over(a.frames, [](const FrameStats &s) { return s.saturation_mean; })));
    m.set("colorfulness", json::Value((double)detail::mean_over(a.frames, [](const FrameStats &s) { return s.colorfulness; })));
    m.set("temperature", json::Value((double)detail::mean_over(a.frames, [](const FrameStats &s) { return s.temperature; })));
    m.set("edge_density", json::Value((double)detail::mean_over(a.frames, [](const FrameStats &s) { return s.edge_density; })));
    m.set("grain", json::Value((double)detail::mean_over(a.frames, [](const FrameStats &s) { return s.grain; })));
    m.set("flat_fraction", json::Value((double)detail::mean_over(a.frames, [](const FrameStats &s) { return s.flat_fraction; })));
    m.set("high_freq_ratio", json::Value((double)detail::mean_over(a.frames, [](const FrameStats &s) { return s.high_freq_ratio; })));
    m.set("vignette", json::Value((double)detail::mean_over(a.frames, [](const FrameStats &s) { return s.vignette; })));
    m.set("centroid", json::xy_array(
        detail::mean_over(a.frames, [](const FrameStats &s) { return s.centroid_x; }),
        detail::mean_over(a.frames, [](const FrameStats &s) { return s.centroid_y; })));
    m.set("motion_energy", json::Value((double)a.motion_energy_mean));
    m.set("global_pan", json::xy_array(a.global_pan_x, a.global_pan_y));
    m.set("zoom_rate", json::Value((double)a.zoom_rate));
    m.set("loops", json::Value(a.loops));
    return m;
}

/// Build a scene that reconstructs an analysed sequence.
///
/// The reconstruct layer carries essentially all of it: the essence grids already
/// hold composition, colour and motion, because they were measured per frame.
/// The only additions are detail restoration and, optionally, grading.
inline json::Value scene_from_sequence(const SequenceAnalysis &a,
                                       const AnalyzeConfig &cfg,
                                       const DeriveOptions &opt) {
    json::Value root = json::Value::obj();

    const int w = opt.out_width > 0 ? opt.out_width : a.width;
    const int h = opt.out_height > 0 ? opt.out_height : a.height;
    const int n = opt.frames > 0 ? opt.frames : (int)a.frames.size();
    const float fps = opt.fps > 0.0f ? opt.fps : (a.fps > 0.0f ? a.fps : 30.0f);

    json::Value out = json::Value::obj();
    out.set("width", json::Value((double)w));
    out.set("height", json::Value((double)h));
    out.set("frames", json::Value((double)n));
    out.set("fps", json::Value((double)fps));
    out.set("samples", json::Value((double)opt.samples));
    root.set("output", out);

    root.set("background", json::rgb_array(0.0f, 0.0f, 0.0f));

    // --- layers ------------------------------------------------------------
    json::Value layers = json::Value::arr();

    // The reconstruction itself.
    const float hf = detail::mean_over(a.frames, [](const FrameStats &s) { return s.high_freq_ratio; });
    json::Value rec = json::Value::obj();
    rec.set("type", json::Value(std::string("reconstruct")));
    rec.set("blend", json::Value(std::string("normal")));
    rec.set("opacity", json::Value(1.0));
    {
        json::Value p = json::Value::obj();
        // `detail` scales noise that stands in for frequencies the essence grid
        // discarded. It is multiplied by the MEASURED high_freq_ratio inside the
        // layer, so a flat source gets no invented texture however high this is.
        p.set("detail", json::Value((double)opt.detail));
        // Detail frequency tracks the grid: texture finer than the grid is what
        // was lost, so that is the band worth regenerating.
        p.set("scale", json::Value((double)std::max(1.0f, (float)cfg.essence_w / 24.0f)));
        p.set("seed", json::Value(0.0));
        rec.set("params", p);
    }
    layers.push(rec);

    // Grain, only if the source actually is grainy.
    //
    // This used to be derived from edge_density, which was simply wrong: line art
    // has very high edge density and no noise, so clean animation was given the
    // maximum grain. The measurement now comes from noise in FLAT regions only
    // (see measure_grain), which is what separates film grain from drawn detail.
    const float measured_grain =
        detail::mean_over(a.frames, [](const FrameStats &s) { return s.grain; });
    float grain = opt.grain;
    if (grain < 0.0f) grain = grain_from_measurement(measured_grain);
    if (grain > 0.001f) {
        json::Value g = json::Value::obj();
        g.set("type", json::Value(std::string("grain")));
        g.set("blend", json::Value(std::string("add")));
        g.set("opacity", json::Value(1.0));
        json::Value p = json::Value::obj();
        p.set("detail", json::Value((double)grain));
        g.set("params", p);
        layers.push(g);
    }
    root.set("layers", layers);

    // --- tone --------------------------------------------------------------
    // Neutral by default. The essence grid already carries the source's tone, so
    // grading on top of it would double-apply. --stylize opts into pushing the
    // measured character further, which is a creative choice, not a faithful one.
    json::Value tone = json::Value::obj();
    if (opt.stylize) {
        const float sat = detail::mean_over(a.frames, [](const FrameStats &s) { return s.saturation_mean; });
        // Bicubic upsampling of a coarse grid loses local contrast; restore a
        // little in proportion to how much was lost.
        tone.set("contrast", json::Value((double)(1.0f + std::min(0.25f, hf * 2.0f))));
        // Lift an UNDERSATURATED source toward 0.35, and leave a saturated one
        // alone. The clamp at 0 is the point: without it, a colourful source
        // (measured saturation 0.82) produced a NEGATIVE boost and was cut to
        // ~0.53 -- stylising by desaturating, which is the opposite of intent.
        const float sat_boost = std::max(0.0f, std::min(0.2f, 0.35f - sat));
        tone.set("saturation", json::Value((double)(1.0f + sat_boost)));
        tone.set("black_floor", json::Value(0.01));
        tone.set("tonemap", json::Value(std::string("none")));
    } else {
        tone.set("contrast", json::Value(1.0));
        tone.set("saturation", json::Value(1.0));
        tone.set("black_floor", json::Value(0.0));
        tone.set("tonemap", json::Value(std::string("none")));
    }
    root.set("tone", tone);

    // Identity transform: the motion is already baked into the per-frame essence
    // grids. Panning here as well would double the motion.
    json::Value tr = json::Value::obj();
    tr.set("zoom", json::Value(1.0));
    tr.set("pan_x", json::Value(0.0));
    tr.set("pan_y", json::Value(0.0));
    tr.set("rotate", json::Value(0.0));
    root.set("transform", tr);

    root.set("measured", measured_json(a));
    return root;
}

/// Build a scene that animates a single still image.
///
/// Here there is only one essence grid, so motion has to be invented. The moves
/// are derived from the picture rather than picked at random:
///
///   * Direction of travel points AWAY from the luma centroid, so the camera
///     drifts across the subject instead of off the empty side of the frame.
///   * Zoom always ends wider than it starts, because pushing in on a still
///     magnifies its softness, while easing out reveals detail.
///   * The move is eased, not linear, and loops back, so the clip can play on
///     repeat without a jump.
inline json::Value scene_from_still(const SequenceAnalysis &a,
                                    const AnalyzeConfig &cfg,
                                    const DeriveOptions &opt,
                                    float move_amount, bool loop) {
    json::Value root = scene_from_sequence(a, cfg, opt);

    if (a.frames.empty()) return root;
    const FrameStats &s = a.frames[0];

    // Offset from centre, in [-0.5, 0.5]. Travel away from where the light is.
    const float ox = s.centroid_x - 0.5f;
    const float oy = s.centroid_y - 0.5f;
    float dirx = -ox, diry = -oy;
    const float len = sqrtf(dirx * dirx + diry * diry);
    if (len > 1e-3f) { dirx /= len; diry /= len; }
    else { dirx = 1.0f; diry = 0.0f; }   // dead-centre subject: drift sideways

    const float pan = move_amount * 0.12f;

    // Zoom has to cover the pan, or the pan samples off the edge of the essence
    // grid and the edge-clamped texel smears into a streak along that side.
    //
    // The grid spans uv 0..1. At zoom z the sampled window is 0.5 +- 0.5/z, and
    // panning shifts its centre by at most `pan`. Staying inside therefore needs
    //     0.5/z + pan <= 0.5   =>   z >= 1/(1 - 2*pan)
    // The 1.02 is margin against the bicubic kernel, which reaches one texel
    // beyond the sample point on each side.
    const float safe = pan < 0.45f ? 1.02f / (1.0f - 2.0f * pan) : 1.02f / 0.1f;
    const float zoom_end = std::max(1.0f, safe);
    const float zoom_start = zoom_end * (1.0f + move_amount * 0.18f);

    json::Value tr = json::Value::obj();
    // Start tighter and ease out, so the frame opens up over the clip. Pushing in
    // on a still only magnifies its softness; easing out reveals detail.
    tr.set("zoom", scene::anim_json(zoom_start, zoom_end, "ease_in_out_sine", loop));
    tr.set("pan_x", scene::anim_json(-dirx * pan, dirx * pan, "ease_in_out_sine", loop));
    tr.set("pan_y", scene::anim_json(-diry * pan, diry * pan, "ease_in_out_sine", loop));
    tr.set("rotate", json::Value(0.0));
    root.set("transform", tr);

    json::Value derived = json::Value::obj();
    derived.set("move_amount", json::Value((double)move_amount));
    derived.set("pan_direction", json::xy_array(dirx, diry));
    derived.set("pan_extent", json::Value((double)pan));
    derived.set("zoom_range", json::xy_array(zoom_start, zoom_end));
    derived.set("loop", json::Value(loop));
    derived.set("rationale", json::Value(std::string(
        "pan travels away from the luma centroid; zoom eases outward but never "
        "below 1/(1-2*pan), so the pan cannot sample past the grid edge and "
        "streak; both are ping-ponged so the clip loops")));
    root.set("derived_motion", derived);
    return root;
}

} // namespace ppm

#endif // PPM_DERIVE_HPP
