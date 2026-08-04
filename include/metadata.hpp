// ===========================================================================
//  metadata.hpp -- per-frame metadata in the language practitioners use.
//
//  This is a PURE OBSERVER. Nothing here affects rendering, fidelity, or any
//  parameter that synthesis reads. It exists so that a generated or regenerated
//  sequence can be inspected the way an animator, compositor or colour designer
//  would inspect it -- and so a UI can project those observations onto the frame
//  as labels and tooltips.
//
//  Two rules govern what belongs here:
//
//    1. USE THE REAL VOCABULARY. "on twos", "hold", "key", "smear", "low-key",
//       "aerial perspective", "notan" are the terms the work is actually
//       discussed in. A field called `motion_energy_bucket_2` is useless to the
//       person inspecting the frame; `hold` is not.
//
//    2. NEVER FABRICATE CERTAINTY. Every classification is derived from a stated
//       measurement, and the measurement is emitted alongside the label so it
//       can be checked. Where a signal is too weak to classify, the label is
//       "undetermined" -- which is information, not a failure.
//
//  Colour-design fields follow the working principles of animation colour
//  design, in particular that shadows should carry hue rather than defaulting to
//  grey, and that a muted palette can slip into murk if its values are not
//  separated. Both are checkable, so they are checked. See
//  docs/frame-metadata.json.
// ===========================================================================
#ifndef PPM_METADATA_HPP
#define PPM_METADATA_HPP

#include "analyze.hpp"
#include "json.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace ppm {
namespace meta {

// ---------------------------------------------------------------------------
// thresholds
// ---------------------------------------------------------------------------

struct MetaConfig {
    float hold_energy = 0.0015f;     ///< below this, a frame is a hold
    float static_motion = 0.0015f;   ///< |dx|,|dy| below this is not a move
    float zoom_threshold = 0.004f;   ///< |divergence| above this is a push/pull
    float min_confidence = 0.15f;    ///< below this, the move is undetermined
    float clip_level = 0.995f;       ///< luma at or above this counts as clipped
    float crush_level = 0.005f;
    float clip_warn = 0.02f;         ///< fraction of pixels before it is a flag
    float shadow_chroma_min = 0.12f; ///< below this, shadows read as neutral
    float murky_sat_max = 0.40f;
    float murky_separation = 0.16f;  ///< palette spread below this is unseparated
};

// ---------------------------------------------------------------------------
// small helpers
// ---------------------------------------------------------------------------

/// Colour-wheel name for a hue in degrees. Twelve names is enough resolution to
/// be useful and few enough to stay recognisable.
inline const char *hue_name(float deg) {
    static const char *names[12] = {"red", "orange", "yellow", "chartreuse",
                                    "green", "spring green", "cyan", "azure",
                                    "blue", "violet", "magenta", "rose"};
    float d = fmodf(deg, 360.0f);
    if (d < 0.0f) d += 360.0f;
    int i = (int)((d + 15.0f) / 30.0f) % 12;
    return names[i];
}

/// Shortest angular distance between two hues, in degrees [0,180].
inline float hue_distance(float a, float b) {
    float d = fabsf(a - b);
    d = fmodf(d, 360.0f);
    return d > 180.0f ? 360.0f - d : d;
}

inline std::string timecode(int frame, float fps) {
    if (fps <= 0.0f) fps = 24.0f;
    const int total = (int)(frame / fps);
    const int ff = frame - (int)(total * fps + 0.5f);
    char buf[32];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d:%02d",
             total / 3600, (total / 60) % 60, total % 60, ff < 0 ? 0 : ff);
    return buf;
}

/// Ordinal used in the trade for exposure rate: one drawing held N frames.
inline std::string step_label(int step) {
    switch (step) {
        case 1: return "on ones";
        case 2: return "on twos";
        case 3: return "on threes";
        case 4: return "on fours";
        default: break;
    }
    if (step <= 0) return "undetermined";
    return "on " + std::to_string(step) + "s";
}

// ---------------------------------------------------------------------------
// exposure from container timings (delegated measurement)
// ---------------------------------------------------------------------------

/// The exposure sheet, recovered from per-frame durations.
///
/// This is a DELEGATED measurement: ffprobe reads it out of the container and we
/// only interpret it. That is strictly better than the pixel-difference inference
/// below, for a reason worth stating plainly -- inferring holds from pixels means
/// inferring a property that the decode step has usually already destroyed. A
/// variable-rate source carries its timing chart in the durations; flattening to a
/// constant rate throws that away, and then any hold detection is reconstructing
/// what was discarded a moment earlier.
///
/// Takes a plain vector of seconds so this header stays independent of pipe.hpp.
struct ContainerExposure {
    bool available = false;
    double tick_seconds = 0.0;   ///< the base grid: shortest frame duration
    float tick_fps = 0.0f;       ///< 1 / tick_seconds
    int modal_step = 0;          ///< most common duration, in ticks
    /// Frames exposed LONGER THAN THE MODAL STEP -- i.e. actual holds.
    ///
    /// Not "longer than one tick". On material animated uniformly on twos every
    /// frame exceeds one tick, so that definition reports ~100% holds and reads as
    /// though nothing ever moves. A hold is relative to the sequence's own
    /// exposure: at on-twos, a hold is something held longer than two.
    float held_fraction = 0.0f;
    bool uniform = false;        ///< every frame the same length (true CFR)
    std::vector<std::pair<int, int>> distribution;   ///< (ticks, frame count)
};

inline ContainerExposure derive_container_exposure(const std::vector<double> &durations) {
    ContainerExposure e;
    if (durations.size() < 2) return e;

    double tick = 0.0;
    for (double d : durations)
        if (d > 1e-6 && (tick == 0.0 || d < tick)) tick = d;
    if (tick <= 1e-6) return e;

    // Quantise each duration onto the tick grid. Reject the whole reading if the
    // durations are not close to integer multiples: that means the base grid was
    // misidentified, and a confident wrong answer is worse than none.
    std::vector<int> steps;
    steps.reserve(durations.size());
    double worst = 0.0;
    for (double d : durations) {
        if (d <= 1e-6) { steps.push_back(1); continue; }
        const double ratio = d / tick;
        const int k = (int)llround(ratio);
        worst = std::max(worst, fabs(ratio - (double)k));
        steps.push_back(std::max(1, k));
    }
    if (worst > 0.20) return e;

    std::vector<std::pair<int, int>> hist;
    for (int s : steps) {
        bool found = false;
        for (auto &p : hist)
            if (p.first == s) { ++p.second; found = true; break; }
        if (!found) hist.push_back({s, 1});
    }
    std::sort(hist.begin(), hist.end(),
              [](const std::pair<int, int> &a, const std::pair<int, int> &b) {
                  return a.second > b.second;
              });

    const int modal = hist.front().first;
    size_t held = 0;
    for (int s : steps) if (s > modal) ++held;

    e.available = true;
    e.tick_seconds = tick;
    e.tick_fps = (float)(1.0 / tick);
    e.modal_step = modal;
    e.held_fraction = (float)((double)held / (double)steps.size());
    e.uniform = hist.size() == 1;
    e.distribution = hist;
    return e;
}

// ---------------------------------------------------------------------------
// sequence-level derivations
// ---------------------------------------------------------------------------

struct Timing {
    std::vector<bool> hold;          ///< frame is identical to its predecessor
    std::vector<bool> key_candidate; ///< a motion extreme; where a key would sit
    std::vector<bool> smear;         ///< fast motion with detail loss
    int step = 1;                    ///< modal frames-per-drawing
    float hold_fraction = 0.0f;
};

/// Recover exposure timing from the frame differences.
///
/// Animation is rarely shot on ones. A sequence on twos changes every second
/// frame and holds in between, which is directly visible in the motion energy:
/// the intervals between changes cluster at 2. Taking the MODE rather than the
/// mean matters, because a single long pause would drag a mean upward and
/// misreport the whole sequence.
inline Timing derive_timing(const SequenceAnalysis &a, const MetaConfig &cfg) {
    Timing t;
    const size_t n = a.frames.size();
    t.hold.assign(n, false);
    t.key_candidate.assign(n, false);
    t.smear.assign(n, false);
    if (n == 0) return t;

    std::vector<size_t> changes;
    changes.push_back(0);
    size_t holds = 0;
    for (size_t i = 1; i < n && i < a.motion.size(); ++i) {
        if (a.motion[i].energy < cfg.hold_energy) {
            t.hold[i] = true;
            ++holds;
        } else {
            changes.push_back(i);
        }
    }
    t.hold_fraction = n > 1 ? (float)holds / (float)(n - 1) : 0.0f;

    // Modal interval between changes.
    if (changes.size() >= 3) {
        int counts[9] = {0};
        for (size_t i = 1; i < changes.size(); ++i) {
            const size_t d = changes[i] - changes[i - 1];
            if (d >= 1 && d <= 8) ++counts[d];
        }
        int best = 1, best_n = 0;
        for (int d = 1; d <= 8; ++d)
            if (counts[d] > best_n) { best_n = counts[d]; best = d; }
        t.step = best_n > 0 ? best : 1;
    } else {
        // Every frame changed, or nothing did. Either way there is no pattern to
        // report, and claiming "on ones" from two samples would be invention.
        t.step = changes.size() >= 2 ? 1 : 0;
    }

    // Key candidates: a key drawing sits at an extreme of the movement, so look
    // for a reversal in either axis, or a local peak in speed.
    for (size_t i = 1; i + 1 < n && i + 1 < a.motion.size(); ++i) {
        const MotionEstimate &p = a.motion[i];
        const MotionEstimate &q = a.motion[i + 1];
        if (p.confidence < cfg.min_confidence) continue;
        const bool reversal = (p.dx * q.dx < 0.0f && fabsf(p.dx) > cfg.static_motion) ||
                              (p.dy * q.dy < 0.0f && fabsf(p.dy) > cfg.static_motion);
        const float sp = sqrtf(p.dx * p.dx + p.dy * p.dy);
        const float sq = sqrtf(q.dx * q.dx + q.dy * q.dy);
        const float sprev = sqrtf(a.motion[i - 1].dx * a.motion[i - 1].dx +
                                  a.motion[i - 1].dy * a.motion[i - 1].dy);
        const bool peak = sp > sprev && sp > sq && sp > cfg.static_motion * 3.0f;
        if (reversal || peak) t.key_candidate[i] = true;
    }
    // The first and last frames of a shot are always keys by definition.
    t.key_candidate[0] = true;
    if (n > 1) t.key_candidate[n - 1] = true;

    // Smear: a fast frame whose fine detail has dropped relative to its
    // neighbours -- the signature of a motion-blurred or deliberately smeared
    // drawing.
    for (size_t i = 1; i + 1 < n && i < a.motion.size(); ++i) {
        const float e = a.motion[i].energy;
        if (e < cfg.hold_energy * 8.0f) continue;
        const float d = a.frames[i].edge_p95;
        const float around = 0.5f * (a.frames[i - 1].edge_p95 + a.frames[i + 1].edge_p95);
        if (around > 1e-6f && d < around * 0.8f) t.smear[i] = true;
    }
    return t;
}

/// Shot index per frame, from the detected cuts.
inline std::vector<int> derive_shots(const SequenceAnalysis &a) {
    std::vector<int> shot(a.frames.size(), 0);
    int current = 0;
    size_t next_cut = 0;
    std::vector<int> cuts;
    for (const Transition &t : a.transitions)
        if (t.kind == "cut") cuts.push_back(t.frame);

    for (size_t i = 0; i < shot.size(); ++i) {
        while (next_cut < cuts.size() && (int)i >= cuts[next_cut]) {
            ++current;
            ++next_cut;
        }
        shot[i] = current;
    }
    return shot;
}

// ---------------------------------------------------------------------------
// per-frame classification
// ---------------------------------------------------------------------------

/// Camera move, named as the CAMERA's action rather than the content's.
///
/// Note the inversion: if the camera pans left, the scene appears to travel
/// right. Motion is measured as content displacement, so a positive dx means the
/// camera panned left. Reporting content direction instead would be defensible
/// but would read backwards to anyone used to talking about shots.
inline std::string classify_camera(const MotionEstimate &m, const MetaConfig &cfg) {
    if (m.confidence < cfg.min_confidence) {
        // A flat or textureless frame gives the block search nothing to lock
        // onto. That is not the same as being still.
        return fabsf(m.energy) < cfg.hold_energy ? "static" : "undetermined";
    }
    if (fabsf(m.divergence) > cfg.zoom_threshold)
        return m.divergence > 0.0f ? "push_in" : "pull_out";

    const bool movex = fabsf(m.dx) > cfg.static_motion;
    const bool movey = fabsf(m.dy) > cfg.static_motion;
    if (!movex && !movey) return "static";
    if (movex && movey) return "compound";
    if (movex) return m.dx > 0.0f ? "pan_left" : "pan_right";
    return m.dy > 0.0f ? "tilt_up" : "tilt_down";
}

/// Overall value placement: where the bulk of the tones sit.
inline std::string classify_key(const FrameStats &s) {
    if (s.luma_p50 < 0.30f) return "low_key";
    if (s.luma_p50 > 0.68f) return "high_key";
    return "mid_key";
}

/// Colour relationship among the dominant palette entries.
inline std::string classify_harmony(const Palette &p, const FrameStats &s) {
    // Only entries with real presence and real chroma can carry a relationship.
    std::vector<float> hues;
    for (size_t i = 0; i < p.weights.size(); ++i) {
        if (p.weights[i] < 0.04f) continue;
        float h, sat;
        rgb_to_hue_sat(p.colors[i * 3], p.colors[i * 3 + 1], p.colors[i * 3 + 2], h, sat);
        if (sat < 0.12f) continue;   // a near-neutral has no hue to relate
        hues.push_back(h);
    }
    if (hues.empty()) return "achromatic";
    if (hues.size() == 1) return "monochromatic";

    float max_d = 0.0f;
    for (size_t i = 0; i < hues.size(); ++i)
        for (size_t j = i + 1; j < hues.size(); ++j)
            max_d = std::max(max_d, hue_distance(hues[i], hues[j]));

    if (max_d < 30.0f) return "monochromatic";
    if (max_d < 60.0f) return "analogous";
    if (max_d > 150.0f) return "complementary";
    if (max_d > 100.0f) return "triadic";
    return "split_complementary";
    (void)s;
}

/// Mean pairwise separation of the dominant palette entries, luma-weighted.
/// Low separation is what turns a muted palette into a muddy one.
inline float palette_separation(const Palette &p) {
    std::vector<size_t> keep;
    for (size_t i = 0; i < p.weights.size(); ++i)
        if (p.weights[i] >= 0.04f) keep.push_back(i);
    if (keep.size() < 2) return 1.0f;   // nothing to be confused with

    double acc = 0;
    int pairs = 0;
    for (size_t a = 0; a < keep.size(); ++a) {
        for (size_t b = a + 1; b < keep.size(); ++b) {
            const size_t i = keep[a] * 3, j = keep[b] * 3;
            const float dr = (p.colors[i] - p.colors[j]) * 0.2126f;
            const float dg = (p.colors[i + 1] - p.colors[j + 1]) * 0.7152f;
            const float db = (p.colors[i + 2] - p.colors[j + 2]) * 0.0722f;
            acc += sqrt((double)(dr * dr + dg * dg + db * db));
            ++pairs;
        }
    }
    return pairs ? (float)(acc / pairs) : 1.0f;
}

// ---------------------------------------------------------------------------
// assembly
// ---------------------------------------------------------------------------

/// One frame's metadata.
inline json::Value frame_metadata(const SequenceAnalysis &a, const Timing &tm,
                                 const std::vector<int> &shots, size_t i,
                                 const MetaConfig &cfg) {
    const FrameStats &s = a.frames[i];
    const MotionEstimate &m = i < a.motion.size() ? a.motion[i] : MotionEstimate();

    json::Value f = json::Value::obj();
    f.set("frame", json::Value((double)i));
    f.set("timecode", json::Value(timecode((int)i, a.fps)));

    // --- shot -------------------------------------------------------------
    {
        const int idx = shots.empty() ? 0 : shots[i];
        size_t first = i, last = i;
        while (first > 0 && shots[first - 1] == idx) --first;
        while (last + 1 < shots.size() && shots[last + 1] == idx) ++last;
        const int len = (int)(last - first + 1);

        json::Value sh = json::Value::obj();
        sh.set("index", json::Value((double)idx));
        sh.set("frame_in_shot", json::Value((double)(i - first)));
        sh.set("length", json::Value((double)len));
        sh.set("position", json::Value(len > 1 ? (double)(i - first) / (double)(len - 1) : 0.0));
        f.set("shot", sh);
    }

    // --- timing -----------------------------------------------------------
    {
        json::Value t = json::Value::obj();
        t.set("hold", json::Value(tm.hold[i]));
        t.set("key_candidate", json::Value(tm.key_candidate[i]));
        t.set("smear", json::Value(tm.smear[i]));
        t.set("step", json::Value((double)tm.step));
        t.set("step_label", json::Value(step_label(tm.step)));
        t.set("difference", json::Value((double)m.energy));
        f.set("timing", t);
    }

    // --- camera -----------------------------------------------------------
    {
        json::Value c = json::Value::obj();
        c.set("move", json::Value(classify_camera(m, cfg)));
        c.set("confidence", json::Value((double)m.confidence));
        c.set("content_dx", json::Value((double)m.dx));
        c.set("content_dy", json::Value((double)m.dy));
        c.set("divergence", json::Value((double)m.divergence));
        f.set("camera", c);
    }

    // --- exposure ---------------------------------------------------------
    {
        // Clipping is read from the reported histogram, so the fractions are
        // bin-resolution estimates rather than exact pixel counts.
        float clipped = 0.0f, crushed = 0.0f;
        const size_t bins = s.luma_hist.size();
        if (bins) {
            clipped = s.luma_hist[bins - 1];
            crushed = s.luma_hist[0];
        }
        json::Value e = json::Value::obj();
        e.set("key", json::Value(classify_key(s)));
        e.set("contrast", json::Value((double)s.contrast));
        // Ratio, the way a lighting setup is described. Guarded against a black
        // floor, where the ratio is unbounded rather than huge.
        e.set("contrast_ratio",
              json::Value(s.luma_p05 > 0.004f ? (double)(s.luma_p95 / s.luma_p05) : 0.0));
        e.set("contrast_ratio_valid", json::Value(s.luma_p05 > 0.004f));
        e.set("clipped_highlights", json::Value((double)clipped));
        e.set("crushed_blacks", json::Value((double)crushed));
        e.set("dynamic_range", json::Value((double)s.dynamic_range));

        json::Value flags = json::Value::arr();
        if (clipped > cfg.clip_warn) flags.push(json::Value(std::string("clipped_highlights")));
        if (crushed > cfg.clip_warn) flags.push(json::Value(std::string("crushed_blacks")));
        if (s.contrast < 0.12f) flags.push(json::Value(std::string("flat")));
        e.set("flags", flags);
        f.set("exposure", e);
    }

    // --- colour -----------------------------------------------------------
    {
        const float sep = palette_separation(s.palette);
        const bool murky = s.midtone_saturation < cfg.murky_sat_max &&
                           s.midtone_saturation > 0.06f &&
                           sep < cfg.murky_separation;

        json::Value c = json::Value::obj();
        c.set("harmony", json::Value(classify_harmony(s.palette, s)));
        c.set("dominant_hue", json::Value((double)s.hue_mean));
        c.set("dominant_hue_name", json::Value(std::string(hue_name(s.hue_mean))));
        c.set("hue_spread", json::Value((double)s.hue_spread));
        c.set("temperature",
              json::Value(std::string(s.temperature > 0.02f ? "warm"
                                      : (s.temperature < -0.02f ? "cool" : "neutral"))));
        c.set("temperature_value", json::Value((double)s.temperature));
        c.set("saturation", json::Value((double)s.saturation_mean));
        c.set("colorfulness", json::Value((double)s.colorfulness));
        c.set("palette_separation", json::Value((double)sep));

        // Shadow chroma. Shadows that fall back to grey flatten an image; shadows
        // that carry their own hue are what give it depth. Reported as a verdict
        // plus the measurement it came from.
        json::Value sc = json::Value::obj();
        sc.set("saturation", json::Value((double)s.shadow_saturation));
        sc.set("hue", json::Value((double)s.shadow_hue));
        sc.set("hue_name", json::Value(std::string(hue_name(s.shadow_hue))));
        sc.set("verdict", json::Value(std::string(
            s.shadow_saturation >= cfg.shadow_chroma_min ? "coloured" : "neutral")));
        c.set("shadow_chroma", sc);

        c.set("murky", json::Value(murky));
        c.set("air", json::Value((double)s.air));
        c.set("aerial_perspective",
              json::Value(std::string(s.air > 0.08f ? "present"
                                      : (s.air < -0.08f ? "inverted" : "flat"))));

        json::Value notes = json::Value::arr();
        if (s.shadow_saturation < cfg.shadow_chroma_min)
            notes.push(json::Value(std::string(
                "shadows are near-neutral; adding hue to them usually adds depth")));
        if (murky)
            notes.push(json::Value(std::string(
                "muted palette with little value separation; risks reading as murky")));
        if (s.air < -0.08f)
            notes.push(json::Value(std::string(
                "highlights carry more detail than shadows; reads as flat or backlit")));
        c.set("notes", notes);
        f.set("color", c);
    }

    // --- composition ------------------------------------------------------
    {
        // Affinity to the nearest third-line intersection, 1 = on it.
        const float tx = std::min(fabsf(s.centroid_x - 1.0f / 3.0f),
                                  fabsf(s.centroid_x - 2.0f / 3.0f));
        const float ty = std::min(fabsf(s.centroid_y - 1.0f / 3.0f),
                                  fabsf(s.centroid_y - 2.0f / 3.0f));
        const float thirds = std::max(0.0f, 1.0f - (tx + ty) * 3.0f);

        json::Value c = json::Value::obj();
        c.set("centroid", json::xy_array(s.centroid_x, s.centroid_y));
        c.set("thirds_affinity", json::Value((double)thirds));
        c.set("horizon_y", json::Value((double)s.horizon_y));
        c.set("has_horizon", json::Value(s.horizon_y >= 0.0f));
        c.set("dark_mass", json::Value((double)s.dark_mass));
        // Notan: the light/dark massing, judged as a balance rather than a ratio.
        c.set("notan",
              json::Value(std::string(s.dark_mass > 0.65f ? "dark_dominant"
                                      : (s.dark_mass < 0.35f ? "light_dominant" : "balanced"))));
        c.set("vignette", json::Value((double)s.vignette));
        f.set("composition", c);
    }

    // --- landmarks, for projection onto the frame -------------------------
    // Normalised coordinates, so a UI can place them at any display size.
    {
        json::Value lm = json::Value::arr();
        {
            json::Value p = json::Value::obj();
            p.set("kind", json::Value(std::string("point")));
            p.set("id", json::Value(std::string("centroid")));
            p.set("label", json::Value(std::string("luma centroid")));
            p.set("x", json::Value((double)s.centroid_x));
            p.set("y", json::Value((double)s.centroid_y));
            p.set("tooltip", json::Value(std::string(
                "Brightness-weighted centre of the frame. Where the light sits, "
                "not necessarily where the subject is.")));
            lm.push(p);
        }
        if (s.horizon_y >= 0.0f) {
            json::Value h = json::Value::obj();
            h.set("kind", json::Value(std::string("hline")));
            h.set("id", json::Value(std::string("horizon")));
            h.set("label", json::Value(std::string("horizon")));
            h.set("y", json::Value((double)s.horizon_y));
            h.set("tooltip", json::Value(std::string(
                "Strongest sustained horizontal boundary in the frame.")));
            lm.push(h);
        }
        if (i < a.motion.size() && m.confidence >= cfg.min_confidence &&
            (fabsf(m.dx) > cfg.static_motion || fabsf(m.dy) > cfg.static_motion)) {
            json::Value v = json::Value::obj();
            v.set("kind", json::Value(std::string("vector")));
            v.set("id", json::Value(std::string("motion")));
            v.set("label", json::Value(classify_camera(m, cfg)));
            v.set("x", json::Value(0.5));
            v.set("y", json::Value(0.5));
            // Scaled up for visibility: a per-frame displacement is a few
            // thousandths of the frame and would otherwise be invisible.
            v.set("dx", json::Value((double)(m.dx * 12.0f)));
            v.set("dy", json::Value((double)(m.dy * 12.0f)));
            v.set("tooltip", json::Value(std::string(
                "Content displacement, exaggerated 12x. Camera direction is the "
                "opposite of the arrow.")));
            lm.push(v);
        }
        f.set("landmarks", lm);
    }
    return f;
}

/// Sequence-level metadata, plus the per-frame array.
///
/// `exposure` is optional. When the container supplied timings, that reading is
/// authoritative and the pixel-difference inference is reported beside it as a
/// cross-check, with any disagreement flagged rather than hidden.
inline json::Value sequence_metadata(const SequenceAnalysis &a, const MetaConfig &cfg,
                                     const ContainerExposure *exposure = nullptr) {
    json::Value root = json::Value::obj();

    const Timing tm = derive_timing(a, cfg);
    const std::vector<int> shots = derive_shots(a);

    int shot_count = shots.empty() ? 0 : shots.back() + 1;

    json::Value sum = json::Value::obj();
    sum.set("frames", json::Value((double)a.frames.size()));
    sum.set("fps", json::Value((double)a.fps));
    sum.set("duration_seconds",
            json::Value(a.fps > 0.0f ? (double)a.frames.size() / a.fps : 0.0));
    sum.set("shots", json::Value((double)shot_count));

    // --- exposure: delegated reading first, inference second ---------------
    const bool have_container = exposure && exposure->available;
    const int step = have_container ? exposure->modal_step : tm.step;
    const float holds = have_container ? exposure->held_fraction : tm.hold_fraction;

    sum.set("step", json::Value((double)step));
    sum.set("step_label", json::Value(step_label(step)));
    sum.set("hold_fraction", json::Value((double)holds));
    sum.set("exposure_source",
            json::Value(std::string(have_container ? "container_timing"
                                                   : "inferred_from_pixels")));

    json::Value exp = json::Value::obj();
    if (have_container) {
        exp.set("available", json::Value(true));
        exp.set("tick_seconds", json::Value(exposure->tick_seconds));
        exp.set("tick_fps", json::Value((double)exposure->tick_fps));
        exp.set("modal_step", json::Value((double)exposure->modal_step));
        exp.set("held_fraction", json::Value((double)exposure->held_fraction));
        exp.set("constant_rate", json::Value(exposure->uniform));
        json::Value dist = json::Value::arr();
        for (const auto &p : exposure->distribution) {
            json::Value d = json::Value::obj();
            d.set("ticks", json::Value((double)p.first));
            d.set("frames", json::Value((double)p.second));
            d.set("label", json::Value(step_label(p.first)));
            dist.push(d);
        }
        exp.set("distribution", dist);
        // The inference, kept as a check rather than discarded.
        json::Value inf = json::Value::obj();
        inf.set("step", json::Value((double)tm.step));
        inf.set("hold_fraction", json::Value((double)tm.hold_fraction));
        inf.set("agrees", json::Value(tm.step == exposure->modal_step));
        exp.set("pixel_inference", inf);
    } else {
        exp.set("available", json::Value(false));
        exp.set("reason", json::Value(std::string(
            "no container timings supplied; exposure inferred from frame "
            "differences, which is unreliable once a variable-rate source has "
            "been flattened to a constant rate")));
    }
    sum.set("exposure", exp);

    // Null rather than 0 when the exposure rate could not be determined. A
    // sequence that never changes has no rate, and reporting "0 fps" would read
    // as a measurement rather than as an absence of one.
    // Effective rate = the rate at which the IMAGE changes. With container
    // timings this is the tick grid divided by the exposure, which is the figure
    // an animator would state; without them it falls back to the inferred step.
    if (have_container && exposure->tick_fps > 0.0f && step > 0)
        sum.set("effective_fps", json::Value((double)exposure->tick_fps / (double)step));
    else if (!have_container && tm.step > 0)
        sum.set("effective_fps", json::Value((double)a.fps / (double)tm.step));
    else
        sum.set("effective_fps", json::Value());
    sum.set("loops", json::Value(a.loops));

    int keys = 0;
    for (bool k : tm.key_candidate) keys += k ? 1 : 0;
    sum.set("key_candidates", json::Value((double)keys));

    // Roll up per-frame colour verdicts, so a reviewer sees sequence-wide issues
    // without scrubbing every frame.
    int neutral_shadows = 0, murky = 0, clipped = 0;
    for (size_t i = 0; i < a.frames.size(); ++i) {
        const FrameStats &s = a.frames[i];
        if (s.shadow_saturation < cfg.shadow_chroma_min) ++neutral_shadows;
        if (palette_separation(s.palette) < cfg.murky_separation &&
            s.midtone_saturation < cfg.murky_sat_max && s.midtone_saturation > 0.06f)
            ++murky;
        if (!s.luma_hist.empty() && s.luma_hist.back() > cfg.clip_warn) ++clipped;
    }
    const double n = (double)std::max<size_t>(1, a.frames.size());
    json::Value review = json::Value::obj();
    review.set("frames_with_neutral_shadows", json::Value((double)neutral_shadows / n));
    review.set("frames_murky", json::Value((double)murky / n));
    review.set("frames_clipped", json::Value((double)clipped / n));
    sum.set("review", review);

    root.set("summary", sum);

    json::Value frames = json::Value::arr();
    for (size_t i = 0; i < a.frames.size(); ++i)
        frames.push(frame_metadata(a, tm, shots, i, cfg));
    root.set("frames", frames);

    root.set("vocabulary_reference", json::Value(std::string("docs/frame-metadata.json")));
    return root;
}

} // namespace meta
} // namespace ppm

#endif // PPM_METADATA_HPP
