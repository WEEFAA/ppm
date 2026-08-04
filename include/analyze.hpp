// ===========================================================================
//  analyze.hpp -- frame analysis. Turns pixels into parameters.
//
//  This is the measurement half of the project. Given a sequence of frames it
//  produces a compact, inspectable description: what is in each frame, how it
//  changes over time, and the numbers a synthesiser needs to rebuild something
//  faithful to it.
//
//  The discipline it follows (docs/frames.json is the long form):
//
//    1. MEASURE BEFORE INTERPRETING. Every derived judgement -- "this is a cut",
//       "this fades", "this pans left" -- is computed from a stated statistic
//       with a stated threshold, and the statistic is reported alongside the
//       judgement so it can be disputed.
//
//    2. SEPARATE ESSENCE FROM DETAIL. A frame decomposes into a low-frequency
//       colour field (the essence: composition and light, which carries almost
//       all of the perceived content) and high-frequency detail (texture, which
//       is better regenerated than stored). The split point is the single
//       fidelity dial.
//
//    3. SCALE-INVARIANT UNITS. Positions are normalised to [0,1], motion is in
//       fractions of frame width, times are in fractions of the sequence.
//       Analysis of a 480p clip must be usable to render at 4K.
//
//    4. NO HIDDEN STATE. Analysis is a pure function of the frames.
// ===========================================================================
#ifndef PPM_ANALYZE_HPP
#define PPM_ANALYZE_HPP

#include "frame.hpp"
#include "json.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace ppm {

// ---------------------------------------------------------------------------
// configuration
// ---------------------------------------------------------------------------

struct AnalyzeConfig {
    int essence_w = 48;      ///< essence grid width; the fidelity dial
    int essence_h = 27;
    int hist_bins = 32;      ///< reported histogram resolution
    int palette_size = 6;    ///< dominant colours to extract
    int motion_cols = 12;    ///< coarse motion field
    int motion_rows = 7;
    int motion_radius = 6;   ///< search radius, in essence-grid pixels
    float cut_threshold = 0.35f;   ///< normalised histogram L1 distance
    float fade_threshold = 0.04f;  ///< luma below this counts as black

    /// Compute the reconstruction error per frame.
    ///
    /// Off for the measuring pass, where the grid built is the small motion grid:
    /// its error is not the fidelity error and is discarded, so computing it is a
    /// full-frame bicubic pass of pure waste.
    bool compute_error = true;

    /// Derive an essence grid that preserves the source aspect ratio.
    void fit_essence(int src_w, int src_h, int target_w) {
        if (src_w <= 0 || src_h <= 0) return;
        essence_w = std::max(2, target_w);
        essence_h = std::max(2, (int)lroundf((float)target_w * (float)src_h / (float)src_w));
    }
};

// ---------------------------------------------------------------------------
// per-frame statistics
// ---------------------------------------------------------------------------

struct Palette {
    std::vector<float> colors;   ///< 3 per entry, display-encoded
    std::vector<float> weights;  ///< fraction of pixels, sums to 1
};

struct FrameStats {
    int index = 0;
    int width = 0, height = 0;

    // Tone
    float luma_mean = 0, luma_min = 0, luma_max = 0, luma_stddev = 0;
    float luma_p05 = 0, luma_p50 = 0, luma_p95 = 0;
    float contrast = 0;        ///< p95 - p05, robust to outliers
    float dynamic_range = 0;   ///< max - min

    // Colour
    float rgb_mean[3] = {0, 0, 0};
    float saturation_mean = 0;
    float colorfulness = 0;    ///< spread in the chroma plane
    float temperature = 0;     ///< -1 cool .. +1 warm, from R-B
    Palette palette;

    // Structure
    float edge_density = 0;    ///< mean Sobel magnitude; a detail proxy
    float edge_p95 = 0;
    float high_freq_ratio = 0; ///< energy lost to the essence grid, in [0,1]

    // Composition
    float centroid_x = 0.5f, centroid_y = 0.5f;  ///< luma-weighted, normalised
    float vignette = 0;        ///< 1 - edge/centre luma ratio; >0 means darker edges
    float horizon_y = -1.0f;   ///< strongest horizontal luma boundary, normalised; -1 if none

    // Colour design. These exist to answer questions a colour designer would
    // ask, not questions the renderer needs answered -- see
    // docs/frame-metadata.json. They do not affect synthesis.
    float shadow_saturation = 0;    ///< mean saturation of the darkest quartile
    float shadow_hue = 0;           ///< dominant shadow hue, degrees
    float shadow_luma = 0;          ///< luma boundary of the shadow quartile
    float midtone_saturation = 0;
    float highlight_saturation = 0;
    float hue_mean = 0;             ///< circular mean hue, degrees
    float hue_spread = 0;           ///< 0 = one hue, 1 = hues evenly distributed
    float dark_mass = 0;            ///< fraction of the frame below 0.5 luma
    float air = 0;                  ///< contrast falloff with luminance; >0 = aerial depth
    float grain = 0;                ///< residual noise measured in FLAT regions only
    float flat_fraction = 0;        ///< share of the frame that is flat enough to judge

    std::vector<float> luma_hist;   ///< hist_bins, normalised to sum 1

    Frame essence;             ///< low-frequency colour field
};

// ---------------------------------------------------------------------------
// palette extraction (median cut)
// ---------------------------------------------------------------------------
//
// Median cut rather than k-means: it is deterministic (no seeding, so no
// frame-to-frame flicker in the extracted palette), single-pass, and its
// failure mode is a slightly suboptimal split rather than a wrong answer.

namespace detail {

struct RgbBox {
    std::vector<size_t> idx;   ///< indices into the sample array
    float lo[3] = {1, 1, 1};
    float hi[3] = {0, 0, 0};

    int widest_axis() const {
        const float e[3] = {hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]};
        // Weight the axes by luma contribution: an equal numeric spread in green
        // is far more visible than the same spread in blue.
        const float wt[3] = {e[0] * 0.2126f, e[1] * 0.7152f, e[2] * 0.0722f};
        if (wt[0] >= wt[1] && wt[0] >= wt[2]) return 0;
        return wt[1] >= wt[2] ? 1 : 2;
    }
    float extent() const {
        const float e[3] = {(hi[0] - lo[0]) * 0.2126f, (hi[1] - lo[1]) * 0.7152f,
                            (hi[2] - lo[2]) * 0.0722f};
        return std::max(e[0], std::max(e[1], e[2]));
    }
};

inline void box_bounds(RgbBox &b, const std::vector<float> &s) {
    b.lo[0] = b.lo[1] = b.lo[2] = 1.0f;
    b.hi[0] = b.hi[1] = b.hi[2] = 0.0f;
    for (size_t i : b.idx) {
        for (int k = 0; k < 3; ++k) {
            const float v = s[i * 3 + k];
            b.lo[k] = std::min(b.lo[k], v);
            b.hi[k] = std::max(b.hi[k], v);
        }
    }
}

} // namespace detail

/// Extract up to `k` dominant colours from a frame.
inline Palette extract_palette(const Frame &f, int k, int stride = 3) {
    Palette out;
    if (f.empty() || k <= 0) return out;

    // Subsample: a few tens of thousands of points settle the palette, and the
    // cost is linear in the sample count.
    std::vector<float> s;
    s.reserve(f.pixels() / (size_t)(stride * stride) * 3 + 3);
    for (int y = 0; y < f.height; y += stride) {
        for (int x = 0; x < f.width; x += stride) {
            const size_t i = f.at(x, y);
            s.push_back(f.rgb[i]);
            s.push_back(f.rgb[i + 1]);
            s.push_back(f.rgb[i + 2]);
        }
    }
    const size_t n = s.size() / 3;
    if (n == 0) return out;

    std::vector<detail::RgbBox> boxes(1);
    boxes[0].idx.reserve(n);
    for (size_t i = 0; i < n; ++i) boxes[0].idx.push_back(i);
    detail::box_bounds(boxes[0], s);

    while ((int)boxes.size() < k) {
        // Split the box with the largest perceptual extent.
        size_t best = 0;
        float best_extent = -1.0f;
        for (size_t i = 0; i < boxes.size(); ++i) {
            if (boxes[i].idx.size() < 2) continue;
            const float e = boxes[i].extent();
            if (e > best_extent) { best_extent = e; best = i; }
        }
        if (best_extent <= 0.0f) break;   // every box is a single colour

        detail::RgbBox &b = boxes[best];
        const int axis = b.widest_axis();
        std::nth_element(b.idx.begin(), b.idx.begin() + (long)(b.idx.size() / 2),
                         b.idx.end(), [&](size_t a, size_t c) {
                             return s[a * 3 + axis] < s[c * 3 + axis];
                         });
        detail::RgbBox nb;
        const size_t mid = b.idx.size() / 2;
        nb.idx.assign(b.idx.begin() + (long)mid, b.idx.end());
        b.idx.erase(b.idx.begin() + (long)mid, b.idx.end());
        detail::box_bounds(b, s);
        detail::box_bounds(nb, s);
        boxes.push_back(std::move(nb));
    }

    // Each box contributes its mean colour, averaged in linear light.
    struct Entry { float c[3]; float w; };
    std::vector<Entry> entries;
    for (const auto &b : boxes) {
        if (b.idx.empty()) continue;
        double acc[3] = {0, 0, 0};
        for (size_t i : b.idx)
            for (int kk = 0; kk < 3; ++kk) acc[kk] += to_linear(s[i * 3 + kk]);
        const double inv = 1.0 / (double)b.idx.size();
        Entry e;
        for (int kk = 0; kk < 3; ++kk) e.c[kk] = to_srgb((float)(acc[kk] * inv));
        e.w = (float)b.idx.size() / (float)n;
        entries.push_back(e);
    }
    std::sort(entries.begin(), entries.end(),
              [](const Entry &a, const Entry &b) { return a.w > b.w; });

    // Merge near-duplicates.
    //
    // Median cut splits on pixel COUNT, so a strongly skewed image -- a small
    // bright subject on a large flat background -- keeps re-splitting the
    // background, because the median along every axis lands inside it. The
    // result is several palette entries holding the same colour and the actual
    // subject colours crowded into what is left. Merging afterwards recovers the
    // slots without abandoning median cut's determinism, which is what keeps the
    // palette from flickering between frames.
    //
    // The threshold is a luma-weighted distance, so it is stricter in green than
    // in blue, matching how visible the difference actually is.
    const float merge_dist = 0.04f;
    std::vector<Entry> merged;
    for (const Entry &e : entries) {
        bool absorbed = false;
        for (Entry &m : merged) {
            const float dr = (e.c[0] - m.c[0]) * 0.2126f;
            const float dg = (e.c[1] - m.c[1]) * 0.7152f;
            const float db = (e.c[2] - m.c[2]) * 0.0722f;
            if (sqrtf(dr * dr + dg * dg + db * db) < merge_dist) {
                // Weighted mean, so the dominant member keeps its colour.
                const float tw = m.w + e.w;
                if (tw > 0.0f)
                    for (int k = 0; k < 3; ++k)
                        m.c[k] = (m.c[k] * m.w + e.c[k] * e.w) / tw;
                m.w = tw;
                absorbed = true;
                break;
            }
        }
        if (!absorbed) merged.push_back(e);
    }
    std::sort(merged.begin(), merged.end(),
              [](const Entry &a, const Entry &b) { return a.w > b.w; });
    entries.swap(merged);

    for (const Entry &e : entries) {
        out.colors.push_back(e.c[0]);
        out.colors.push_back(e.c[1]);
        out.colors.push_back(e.c[2]);
        out.weights.push_back(e.w);
    }
    return out;
}

// ---------------------------------------------------------------------------
// single-frame analysis
// ---------------------------------------------------------------------------

/// Mean and 95th-percentile Sobel gradient magnitude of the luma channel.
/// This is the detail measure that tells the synthesiser how much texture to
/// regenerate in place of the high frequencies the essence grid discarded.
inline void sobel_stats(const Frame &f, float &mean_out, float &p95_out) {
    mean_out = p95_out = 0.0f;
    if (f.width < 3 || f.height < 3) return;

    std::vector<float> mags;
    mags.reserve((size_t)(f.width - 2) * (size_t)(f.height - 2));
    for (int y = 1; y < f.height - 1; ++y) {
        for (int x = 1; x < f.width - 1; ++x) {
            const float l00 = f.luma_at(x - 1, y - 1), l01 = f.luma_at(x, y - 1), l02 = f.luma_at(x + 1, y - 1);
            const float l10 = f.luma_at(x - 1, y),                              l12 = f.luma_at(x + 1, y);
            const float l20 = f.luma_at(x - 1, y + 1), l21 = f.luma_at(x, y + 1), l22 = f.luma_at(x + 1, y + 1);
            const float gx = (l02 + 2.0f * l12 + l22) - (l00 + 2.0f * l10 + l20);
            const float gy = (l20 + 2.0f * l21 + l22) - (l00 + 2.0f * l01 + l02);
            mags.push_back(sqrtf(gx * gx + gy * gy));
        }
    }
    if (mags.empty()) return;
    double sum = 0;
    for (float m : mags) sum += m;
    mean_out = (float)(sum / (double)mags.size());
    const size_t k = (size_t)((double)(mags.size() - 1) * 0.95);
    std::nth_element(mags.begin(), mags.begin() + (long)k, mags.end());
    p95_out = mags[k];
}

// ---------------------------------------------------------------------------
// colour design measurement
// ---------------------------------------------------------------------------

/// Hue in degrees [0,360) and saturation in [0,1], from display-encoded RGB.
inline void rgb_to_hue_sat(float r, float g, float b, float &hue, float &sat) {
    const float mx = std::max(r, std::max(g, b));
    const float mn = std::min(r, std::min(g, b));
    const float c = mx - mn;
    sat = mx > 1e-6f ? c / mx : 0.0f;
    if (c < 1e-6f) { hue = 0.0f; return; }
    float h;
    if (mx == r)      h = fmodf((g - b) / c, 6.0f);
    else if (mx == g) h = (b - r) / c + 2.0f;
    else              h = (r - g) / c + 4.0f;
    h *= 60.0f;
    if (h < 0.0f) h += 360.0f;
    hue = h;
}

/// Measure the properties a colour designer would inspect.
///
/// These come from the working principles of animation colour design rather than
/// from anything synthesis needs. The most useful is shadow chroma: shadows that
/// default to grey or black flatten an image, and shadows carrying their own hue
/// are what give it depth. That is a checkable property, so it is checked.
///
/// Takes the luma quartile boundaries, which the caller already computed.
///
/// NOTE on the comparisons below. The quartiles come from a 256-bin histogram, so
/// the value returned is the QUANTISED lower edge of a bin, which can sit a
/// fraction below the actual luma of the pixels in it. Comparing a full-precision
/// luma against that with `l <= p25` therefore excludes the very pixels the
/// quartile was meant to select -- in testing it excluded ALL of them and every
/// shadow measurement read zero. Comparing in bin space instead, using the same
/// quantisation the histogram used, is exact.
inline void measure_color_design(const Frame &f, float p25, float p75, FrameStats &st) {
    if (f.empty()) return;

    auto to_bin = [](float v) {
        int b = (int)(v * 255.0f + 0.5f);
        return b < 0 ? 0 : (b > 255 ? 255 : b);
    };
    const int bin25 = to_bin(p25), bin75 = to_bin(p75);

    double sh_sat = 0, mid_sat = 0, hi_sat = 0;
    double sh_hx = 0, sh_hy = 0;          // circular mean of shadow hue
    double all_hx = 0, all_hy = 0;        // circular mean of all hue
    double hue_weight = 0;
    size_t sh_n = 0, mid_n = 0, hi_n = 0, dark_n = 0;

    // Aerial perspective: brighter regions in a scene with atmosphere carry less
    // local contrast. Accumulate gradient magnitude separately for dark and
    // bright pixels and compare.
    double grad_dark = 0, grad_bright = 0;
    size_t gd_n = 0, gb_n = 0;

    for (int y = 0; y < f.height; ++y) {
        for (int x = 0; x < f.width; ++x) {
            const size_t i = f.at(x, y);
            const float r = f.rgb[i], g = f.rgb[i + 1], b = f.rgb[i + 2];
            const float l = luma(r, g, b);

            float hue, sat;
            rgb_to_hue_sat(r, g, b, hue, sat);
            const float rad = hue * 0.01745329f;

            if (l < 0.5f) ++dark_n;

            const int lb = to_bin(l);
            if (lb <= bin25) {
                sh_sat += sat;
                sh_hx += cos(rad) * sat;   // weight by saturation: an unsaturated
                sh_hy += sin(rad) * sat;   // pixel has no meaningful hue to average
                ++sh_n;
            } else if (lb >= bin75) {
                hi_sat += sat;
                ++hi_n;
            } else {
                mid_sat += sat;
                ++mid_n;
            }

            all_hx += cos(rad) * sat;
            all_hy += sin(rad) * sat;
            hue_weight += sat;

            // Cheap 2-tap gradient; a full Sobel is computed elsewhere and this
            // only needs to rank bands against each other.
            if (x + 1 < f.width && y + 1 < f.height) {
                const float gx = fabsf(f.luma_at(x + 1, y) - l);
                const float gy = fabsf(f.luma_at(x, y + 1) - l);
                const float gm = gx + gy;
                if (l <= p25) { grad_dark += gm; ++gd_n; }
                else if (l >= p75) { grad_bright += gm; ++gb_n; }
            }
        }
    }

    const double n = (double)f.pixels();
    st.dark_mass = (float)((double)dark_n / n);
    if (sh_n) st.shadow_saturation = (float)(sh_sat / (double)sh_n);
    if (mid_n) st.midtone_saturation = (float)(mid_sat / (double)mid_n);
    if (hi_n) st.highlight_saturation = (float)(hi_sat / (double)hi_n);
    st.shadow_luma = p25;

    if (sh_hx != 0.0 || sh_hy != 0.0) {
        double h = atan2(sh_hy, sh_hx) * 57.29578;
        if (h < 0) h += 360.0;
        st.shadow_hue = (float)h;
    }
    if (hue_weight > 1e-6) {
        double h = atan2(all_hy, all_hx) * 57.29578;
        if (h < 0) h += 360.0;
        st.hue_mean = (float)h;
        // Resultant length of the weighted unit vectors: 1 when every pixel
        // shares a hue, 0 when hues cancel. Reported inverted, as a spread.
        const double resultant = sqrt(all_hx * all_hx + all_hy * all_hy) / hue_weight;
        st.hue_spread = (float)std::max(0.0, std::min(1.0, 1.0 - resultant));
    }

    if (gd_n && gb_n) {
        const double d = grad_dark / (double)gd_n;
        const double b = grad_bright / (double)gb_n;
        if (d + b > 1e-9) st.air = (float)((d - b) / (d + b));
    }
}

// ---------------------------------------------------------------------------
// the fidelity metric, and choosing a grid width from it
// ---------------------------------------------------------------------------

/// Mean absolute luma error between a frame and its reconstruction from `grid`.
///
/// This is the project's honest measure of what the fidelity dial costs: it
/// reconstructs exactly as the renderer will and compares against the real
/// pixels. IMPORTANT: it is only truthful if `f` is at the SOURCE resolution.
/// Measuring against a downscaled proxy understates the loss, because the
/// high frequencies the grid throws away were already gone from the proxy.
inline float essence_error(const Frame &f, const Frame &grid) {
    if (f.empty() || grid.empty()) return 0.0f;
    double diff = 0;
    for (int y = 0; y < f.height; ++y) {
        const float v = ((float)y + 0.5f) / (float)f.height;
        for (int x = 0; x < f.width; ++x) {
            const float u = ((float)x + 0.5f) / (float)f.width;
            float r, g, b;
            sample_bicubic_linear(grid, u, v, r, g, b);
            diff += fabs((double)luma(r, g, b) - (double)f.luma_at(x, y));
        }
    }
    return (float)(diff / (double)f.pixels());
}

inline float essence_error_at(const Frame &f, int w) {
    if (f.empty() || w < 2) return 1.0f;
    const int h = std::max(2, (int)lroundf((float)w * (float)f.height / (float)f.width));
    return essence_error(f, downsample_box(f, w, h));
}

/// Smallest essence width whose reconstruction error meets `target`.
///
/// Exists because a single default cannot serve both continuous-tone and
/// hard-edged content: the same grid width that is faithful on a gradient is
/// six times too narrow for line art. Rather than ask the user to guess, measure
/// the actual frame and let the content decide.
///
/// Steps geometrically and stops at the first candidate that qualifies, so the
/// cost is a handful of reconstruction passes on ONE frame. Error falls
/// monotonically with width, so a linear scan is sufficient and needs no search.
inline int choose_essence_width(const Frame &probe, float target,
                                int min_w, int max_w, float *achieved = nullptr) {
    if (probe.empty()) return min_w;
    max_w = std::min(max_w, probe.width);
    min_w = std::max(8, std::min(min_w, max_w));

    int last = min_w;
    float last_err = 1.0f;
    for (int w = min_w;; w = (int)(w * 1.5f)) {
        if (w > max_w) w = max_w;
        const float e = essence_error_at(probe, w);
        last = w;
        last_err = e;
        if (e <= target || w >= max_w) break;
    }
    if (achieved) *achieved = last_err;
    return last;
}

/// Measure film-grain-like noise, and only that.
///
/// The obvious proxy -- overall edge density -- is WRONG, and wrongly enough that
/// it was a real bug: line art has very high edge density and no noise at all, so
/// deriving grain from edges added heavy grain to clean animation. Worse, the
/// injected noise inflated the project's own sharpness metric, making the
/// reconstruction look twice as faithful as it was.
///
/// Grain is distinguished from detail by WHERE it lives: real noise is present
/// everywhere including flat areas, while drawn or rendered detail is confined to
/// edges. So this measures the residual against a local mean, restricted to
/// pixels whose neighbourhood is flat. A flat-shaded cel scores ~0; film scores
/// well above zero.
inline void measure_grain(const Frame &f, float &grain_out, float &flat_out) {
    grain_out = flat_out = 0.0f;
    if (f.width < 3 || f.height < 3) return;

    // A neighbourhood counts as flat when its peak-to-peak luma is below this.
    // Loose enough to admit gently shaded regions, tight enough to exclude any
    // pixel adjacent to a drawn edge.
    const float flat_ptp = 0.035f;

    double acc = 0;
    size_t flat_n = 0, total = 0;
    for (int y = 1; y < f.height - 1; ++y) {
        for (int x = 1; x < f.width - 1; ++x) {
            float lo = 1.0f, hi = 0.0f, sum = 0.0f;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const float l = f.luma_at(x + dx, y + dy);
                    sum += l;
                    if (l < lo) lo = l;
                    if (l > hi) hi = l;
                }
            }
            ++total;
            if (hi - lo > flat_ptp) continue;      // an edge lives here, not grain
            const float mean = sum / 9.0f;
            acc += fabsf(f.luma_at(x, y) - mean);
            ++flat_n;
        }
    }
    if (total) flat_out = (float)((double)flat_n / (double)total);
    // Require a meaningful flat area before claiming a noise level; a frame that
    // is edges everywhere gives no evidence either way.
    if (flat_n > 64 && flat_out > 0.02f) grain_out = (float)(acc / (double)flat_n);
}

/// Estimate a horizon: the row with the strongest sustained horizontal luma
/// boundary. A landmark a compositor would look for; -1 when no row stands out.
inline float estimate_horizon(const Frame &f) {
    if (f.height < 8) return -1.0f;
    std::vector<float> row_delta((size_t)f.height, 0.0f);
    for (int y = 1; y < f.height; ++y) {
        double d = 0;
        for (int x = 0; x < f.width; ++x)
            d += fabsf(f.luma_at(x, y) - f.luma_at(x, y - 1));
        row_delta[(size_t)y] = (float)(d / (double)f.width);
    }
    int best = -1;
    float best_v = 0.0f, mean = 0.0f;
    for (int y = 1; y < f.height; ++y) mean += row_delta[(size_t)y];
    mean /= (float)std::max(1, f.height - 1);
    for (int y = 1; y < f.height; ++y) {
        if (row_delta[(size_t)y] > best_v) { best_v = row_delta[(size_t)y]; best = y; }
    }
    // Require the winner to stand well clear of the average row, or there is no
    // horizon and saying there is one would be a fabrication.
    if (best < 0 || mean <= 1e-6f || best_v < mean * 4.0f) return -1.0f;
    return ((float)best + 0.5f) / (float)f.height;
}

inline FrameStats analyze_frame(const Frame &f, const AnalyzeConfig &cfg, int index) {
    FrameStats st;
    st.index = index;
    st.width = f.width;
    st.height = f.height;
    if (f.empty()) return st;

    // --- tone and colour, single pass -------------------------------------
    const int FINE = 256;   // fine histogram for percentiles
    std::vector<double> fine((size_t)FINE, 0.0);
    double lsum = 0, lsq = 0;
    double rs = 0, gs = 0, bs = 0, satsum = 0;
    double cx = 0, cy = 0, wsum = 0;
    float lmin = 1.0f, lmax = 0.0f;
    double chroma_a = 0, chroma_b = 0, chroma_a2 = 0, chroma_b2 = 0;

    for (int y = 0; y < f.height; ++y) {
        for (int x = 0; x < f.width; ++x) {
            const size_t i = f.at(x, y);
            const float r = f.rgb[i], g = f.rgb[i + 1], b = f.rgb[i + 2];
            const float l = luma(r, g, b);

            lsum += l;
            lsq += (double)l * l;
            lmin = std::min(lmin, l);
            lmax = std::max(lmax, l);
            int bin = (int)(l * (float)(FINE - 1) + 0.5f);
            if (bin < 0) bin = 0;
            if (bin >= FINE) bin = FINE - 1;
            fine[(size_t)bin] += 1.0;

            rs += r; gs += g; bs += b;
            const float mx = std::max(r, std::max(g, b));
            const float mn = std::min(r, std::min(g, b));
            satsum += (mx > 0.0f) ? (double)((mx - mn) / mx) : 0.0;

            // Simple opponent-colour axes; enough to quantify colourfulness
            // without a full CIELAB conversion.
            const double oa = (double)r - (double)g;
            const double ob = 0.5 * ((double)r + (double)g) - (double)b;
            chroma_a += oa; chroma_b += ob;
            chroma_a2 += oa * oa; chroma_b2 += ob * ob;

            cx += (double)x * l;
            cy += (double)y * l;
            wsum += l;
        }
    }

    const double n = (double)f.pixels();
    st.luma_mean = (float)(lsum / n);
    st.luma_min = lmin;
    st.luma_max = lmax;
    const double var = std::max(0.0, lsq / n - (double)st.luma_mean * st.luma_mean);
    st.luma_stddev = (float)sqrt(var);
    st.dynamic_range = lmax - lmin;

    st.rgb_mean[0] = (float)(rs / n);
    st.rgb_mean[1] = (float)(gs / n);
    st.rgb_mean[2] = (float)(bs / n);
    st.saturation_mean = (float)(satsum / n);
    st.temperature = st.rgb_mean[0] - st.rgb_mean[2];

    const double va = std::max(0.0, chroma_a2 / n - (chroma_a / n) * (chroma_a / n));
    const double vb = std::max(0.0, chroma_b2 / n - (chroma_b / n) * (chroma_b / n));
    // Hasler-Suesstrunk style colourfulness: chroma spread plus a fraction of
    // the chroma offset from grey.
    st.colorfulness = (float)(sqrt(va + vb) +
                              0.3 * sqrt((chroma_a / n) * (chroma_a / n) + (chroma_b / n) * (chroma_b / n)));

    if (wsum > 1e-9) {
        st.centroid_x = (float)(cx / wsum / (double)f.width);
        st.centroid_y = (float)(cy / wsum / (double)f.height);
    }

    // --- percentiles from the fine histogram ------------------------------
    auto percentile = [&](double frac) {
        const double target = frac * n;
        double acc = 0;
        for (int i = 0; i < FINE; ++i) {
            acc += fine[(size_t)i];
            if (acc >= target) return (float)i / (float)(FINE - 1);
        }
        return 1.0f;
    };
    st.luma_p05 = percentile(0.05);
    st.luma_p50 = percentile(0.50);
    st.luma_p95 = percentile(0.95);
    st.contrast = st.luma_p95 - st.luma_p05;

    // --- reported histogram -----------------------------------------------
    st.luma_hist.assign((size_t)cfg.hist_bins, 0.0f);
    for (int i = 0; i < FINE; ++i) {
        int b = i * cfg.hist_bins / FINE;
        if (b >= cfg.hist_bins) b = cfg.hist_bins - 1;
        st.luma_hist[(size_t)b] += (float)(fine[(size_t)i] / n);
    }

    // --- structure ---------------------------------------------------------
    sobel_stats(f, st.edge_density, st.edge_p95);

    // --- colour design and landmarks --------------------------------------
    // Inspection metadata only; nothing below feeds synthesis.
    measure_color_design(f, percentile(0.25), percentile(0.75), st);
    measure_grain(f, st.grain, st.flat_fraction);
    st.horizon_y = estimate_horizon(f);

    // --- vignette ----------------------------------------------------------
    // Compare a central region against a border ring. Positive means the edges
    // are darker, which is both a common stylistic choice and something the
    // synthesiser must reproduce or the reconstruction looks flat.
    {
        double centre = 0, edge = 0;
        int cn = 0, en = 0;
        for (int y = 0; y < f.height; ++y) {
            for (int x = 0; x < f.width; ++x) {
                const float u = ((float)x + 0.5f) / (float)f.width - 0.5f;
                const float v = ((float)y + 0.5f) / (float)f.height - 0.5f;
                const float rr = sqrtf(u * u + v * v);
                if (rr < 0.18f) { centre += f.luma_at(x, y); ++cn; }
                else if (rr > 0.45f) { edge += f.luma_at(x, y); ++en; }
            }
        }
        if (cn && en && centre > 1e-6) {
            st.vignette = 1.0f - (float)((edge / en) / (centre / cn));
            st.vignette = std::max(-1.0f, std::min(1.0f, st.vignette));
        }
    }

    // --- palette and essence ----------------------------------------------
    st.palette = extract_palette(f, cfg.palette_size);
    st.essence = downsample_box(f, cfg.essence_w, cfg.essence_h);
    if (cfg.compute_error) st.high_freq_ratio = essence_error(f, st.essence);
    return st;
}

// ---------------------------------------------------------------------------
// motion
// ---------------------------------------------------------------------------

/// SIGN CONVENTION: dx/dy are the displacement OF THE CONTENT, as a fraction of
/// frame width/height. Content moving right gives dx > 0.
///
/// Worth stating explicitly because the block search naturally produces the
/// opposite: it finds the offset at which the PREVIOUS frame matches the
/// current one, which is the negation. The search result is negated at the point
/// of measurement so that every consumer sees the intuitive sense, and a
/// synthesiser can pan by dx directly rather than having to remember to flip it.
struct MotionVector {
    float x = 0, y = 0;    ///< cell centre, normalised
    float dx = 0, dy = 0;  ///< content displacement, fraction of frame size
    float confidence = 0;  ///< 0..1; how much better the best match was
};

struct MotionEstimate {
    float dx = 0, dy = 0;        ///< global translation, fraction of frame size
    float confidence = 0;
    float energy = 0;            ///< mean abs luma difference between frames
    float residual = 0;          ///< difference remaining after compensation
    float divergence = 0;        ///< >0 means outward flow, i.e. a zoom in
    std::vector<MotionVector> field;
};

namespace detail {

/// Sum of absolute luma differences for `cur` shifted by (ox,oy) against `prev`,
/// restricted to a rectangle. Returns -1 if the overlap is too small to judge.
inline float sad_shift(const Frame &prev, const Frame &cur, int x0, int y0,
                       int x1, int y1, int ox, int oy) {
    double sum = 0;
    int count = 0;
    for (int y = y0; y < y1; ++y) {
        const int sy = y + oy;
        if (sy < 0 || sy >= prev.height) continue;
        for (int x = x0; x < x1; ++x) {
            const int sx = x + ox;
            if (sx < 0 || sx >= prev.width) continue;
            sum += fabsf(cur.luma_at(x, y) - prev.luma_at(sx, sy));
            ++count;
        }
    }
    if (count < 8) return -1.0f;
    return (float)(sum / count);
}

} // namespace detail

/// Estimate motion between two essence grids by block matching.
///
/// Deliberately coarse: run on the essence grid, not the full frame. At 48x27 an
/// exhaustive +-6 search is 169 comparisons of ~1300 pixels, which is cheap, and
/// global camera motion is a low-frequency signal that survives downsampling
/// intact. Sub-pixel accuracy would be false precision here.
inline MotionEstimate estimate_motion(const Frame &prev, const Frame &cur,
                                      const AnalyzeConfig &cfg) {
    MotionEstimate m;
    if (prev.empty() || cur.empty() ||
        prev.width != cur.width || prev.height != cur.height)
        return m;

    // Raw inter-frame difference, before any compensation.
    m.energy = detail::sad_shift(prev, cur, 0, 0, cur.width, cur.height, 0, 0);
    if (m.energy < 0) m.energy = 0;

    // --- global search -----------------------------------------------------
    const int R = cfg.motion_radius;
    float best = 1e30f, second = 1e30f;
    int bx = 0, by = 0;
    for (int oy = -R; oy <= R; ++oy) {
        for (int ox = -R; ox <= R; ++ox) {
            const float s = detail::sad_shift(prev, cur, 0, 0, cur.width, cur.height, ox, oy);
            if (s < 0) continue;
            if (s < best) { second = best; best = s; bx = ox; by = oy; }
            else if (s < second) { second = s; }
        }
    }
    // Negated: see the sign convention on MotionVector.
    m.dx = -(float)bx / (float)cur.width;
    m.dy = -(float)by / (float)cur.height;
    m.residual = best > 0 ? best : 0.0f;
    // Confidence is how much the winner beat the runner-up. A flat search
    // surface (uniform or textureless content) gives a near-zero score, which is
    // the correct answer: the displacement is not determinable.
    m.confidence = (second < 1e29f && second > 1e-9f)
                       ? std::max(0.0f, std::min(1.0f, (second - best) / second))
                       : 0.0f;

    // --- per-cell field ----------------------------------------------------
    const int cols = std::max(1, cfg.motion_cols), rows = std::max(1, cfg.motion_rows);
    m.field.reserve((size_t)cols * (size_t)rows);
    double div = 0;
    int div_n = 0;
    for (int cyi = 0; cyi < rows; ++cyi) {
        for (int cxi = 0; cxi < cols; ++cxi) {
            const int x0 = cxi * cur.width / cols, x1 = (cxi + 1) * cur.width / cols;
            const int y0 = cyi * cur.height / rows, y1 = (cyi + 1) * cur.height / rows;
            float cbest = 1e30f, csecond = 1e30f;
            int cbx = 0, cby = 0;
            for (int oy = -R; oy <= R; ++oy) {
                for (int ox = -R; ox <= R; ++ox) {
                    const float s = detail::sad_shift(prev, cur, x0, y0, x1, y1, ox, oy);
                    if (s < 0) continue;
                    if (s < cbest) { csecond = cbest; cbest = s; cbx = ox; cby = oy; }
                    else if (s < csecond) { csecond = s; }
                }
            }
            MotionVector v;
            v.x = ((float)x0 + (float)x1) * 0.5f / (float)cur.width;
            v.y = ((float)y0 + (float)y1) * 0.5f / (float)cur.height;
            v.dx = -(float)cbx / (float)cur.width;
            v.dy = -(float)cby / (float)cur.height;
            v.confidence = (csecond < 1e29f && csecond > 1e-9f)
                               ? std::max(0.0f, std::min(1.0f, (csecond - cbest) / csecond))
                               : 0.0f;
            // Radial component relative to frame centre: consistently outward
            // flow is a zoom-in, inward is a zoom-out.
            const float rx = v.x - 0.5f, ry = v.y - 0.5f;
            const float rl = sqrtf(rx * rx + ry * ry);
            if (rl > 0.05f && v.confidence > 0.05f) {
                div += ((v.dx - m.dx) * rx + (v.dy - m.dy) * ry) / rl;
                ++div_n;
            }
            m.field.push_back(v);
        }
    }
    if (div_n) m.divergence = (float)(div / div_n);
    return m;
}

// ---------------------------------------------------------------------------
// transitions
// ---------------------------------------------------------------------------

struct Transition {
    int frame = 0;
    std::string kind;     ///< "cut" | "fade_out" | "fade_in" | "dissolve"
    float strength = 0;
    int length = 1;       ///< frames spanned; 1 for a cut
};

/// Normalised L1 distance between two histograms, in [0,1].
inline float hist_distance(const std::vector<float> &a, const std::vector<float> &b) {
    if (a.size() != b.size() || a.empty()) return 0.0f;
    double d = 0;
    for (size_t i = 0; i < a.size(); ++i) d += fabs((double)a[i] - (double)b[i]);
    return (float)(d * 0.5);   // both sum to 1, so the max L1 distance is 2
}

// ---------------------------------------------------------------------------
// easing / curve fitting
// ---------------------------------------------------------------------------

/// Identify which standard easing best describes a monotonic-ish sequence.
///
/// Reported for two reasons: it compresses a long curve into a name plus an
/// error, and a named easing can be re-synthesised at any frame rate, whereas
/// sampled keyframes cannot.
struct CurveFit {
    std::string easing = "linear";
    float rmse = 0;
    float start = 0, end = 0;
    bool monotonic = false;
};

inline CurveFit fit_curve(const std::vector<float> &y) {
    CurveFit out;
    if (y.size() < 3) return out;
    out.start = y.front();
    out.end = y.back();

    const float span = out.end - out.start;
    if (fabsf(span) < 1e-4f) { out.easing = "constant"; return out; }

    // Normalise to [0,1] on both axes so the shape is compared, not the scale.
    std::vector<float> t(y.size()), v(y.size());
    for (size_t i = 0; i < y.size(); ++i) {
        t[i] = (float)i / (float)(y.size() - 1);
        v[i] = (y[i] - out.start) / span;
    }
    out.monotonic = true;
    for (size_t i = 1; i < v.size(); ++i)
        if (v[i] + 1e-3f < v[i - 1]) { out.monotonic = false; break; }

    struct Cand { const char *name; float (*f)(float); };
    static const Cand cands[] = {
        {"linear",          [](float x) { return x; }},
        {"smoothstep",      [](float x) { return x * x * (3.0f - 2.0f * x); }},
        {"smootherstep",    [](float x) { return x * x * x * (x * (x * 6.0f - 15.0f) + 10.0f); }},
        {"ease_in_quad",    [](float x) { return x * x; }},
        {"ease_out_quad",   [](float x) { return 1.0f - (1.0f - x) * (1.0f - x); }},
        {"ease_in_cubic",   [](float x) { return x * x * x; }},
        {"ease_out_cubic",  [](float x) { const float u = 1.0f - x; return 1.0f - u * u * u; }},
        {"ease_in_out_sine",[](float x) { return 0.5f - 0.5f * cosf(3.14159265f * x); }},
    };

    float best = 1e30f;
    for (const Cand &c : cands) {
        double se = 0;
        for (size_t i = 0; i < t.size(); ++i) {
            const float d = c.f(t[i]) - v[i];
            se += (double)d * d;
        }
        const float rmse = (float)sqrt(se / (double)t.size());
        if (rmse < best) { best = rmse; out.easing = c.name; }
    }
    out.rmse = best;
    return out;
}

// ---------------------------------------------------------------------------
// sequence analysis
// ---------------------------------------------------------------------------

struct SequenceAnalysis {
    int width = 0, height = 0;
    float fps = 0;
    std::vector<FrameStats> frames;
    std::vector<MotionEstimate> motion;   ///< size frames.size(); [0] is empty
    std::vector<Transition> transitions;

    // Sequence-level summaries
    float motion_energy_mean = 0;
    float global_pan_x = 0, global_pan_y = 0;   ///< mean per-frame translation
    float zoom_rate = 0;
    CurveFit luma_curve;
    Palette global_palette;
    bool loops = false;          ///< first and last frame are near-identical
    float loop_error = 0;
};

/// Classify transitions from per-frame statistics.
inline void detect_transitions(SequenceAnalysis &a, const AnalyzeConfig &cfg) {
    const size_t n = a.frames.size();
    if (n < 2) return;

    std::vector<float> hd(n, 0.0f);
    for (size_t i = 1; i < n; ++i)
        hd[i] = hist_distance(a.frames[i - 1].luma_hist, a.frames[i].luma_hist);

    for (size_t i = 1; i < n; ++i) {
        // A cut is a histogram discontinuity that is also a local maximum. The
        // local-maximum test is what separates a cut from a fast dissolve, which
        // produces a sustained plateau of moderate distances instead of a spike.
        const bool spike = hd[i] > cfg.cut_threshold;
        const bool local_max = (i + 1 >= n || hd[i] >= hd[i + 1]) && hd[i] >= hd[i - 1];
        if (spike && local_max) {
            Transition t;
            t.frame = (int)i;
            t.kind = "cut";
            t.strength = hd[i];
            a.transitions.push_back(t);
        }
    }

    // Fades: a run over which mean luma moves monotonically into or out of black.
    //
    // Two things make this harder than it looks.
    //
    // First, the magnitude test must be RELATIVE, not absolute. A small bright
    // subject on a dark field has a mean luma of a few percent, so a complete
    // fade to black only moves the mean by ~0.04. An absolute threshold of 0.08
    // would score a full fade as no fade at all.
    //
    // Second, the range it is relative to must be measured PER SHOT, not across
    // the whole sequence. A dark shot that fades out, followed by a cut to a
    // bright shot, has a sequence-wide luma range dominated by the bright shot --
    // so a sequence-wide threshold is set far too high and the fade is missed.
    // Cuts are therefore detected first and used as shot boundaries here.
    std::vector<size_t> bounds;
    bounds.push_back(0);
    for (const Transition &t : a.transitions) bounds.push_back((size_t)t.frame);
    bounds.push_back(n);

    auto scan_fade = [&](size_t s0, size_t s1, float min_drop, bool into_black) {
        size_t i = s0 + 1;
        while (i < s1) {
            const float prev = a.frames[i - 1].luma_mean;
            const float cur = a.frames[i].luma_mean;
            const bool moving = into_black ? (cur < prev - 1e-4f) : (cur > prev + 1e-4f);
            if (!moving) { ++i; continue; }
            size_t j = i;
            while (j + 1 < s1) {
                const float p = a.frames[j].luma_mean, c = a.frames[j + 1].luma_mean;
                if (into_black ? (c < p - 1e-4f) : (c > p + 1e-4f)) ++j;
                else break;
            }
            const float lo = into_black ? a.frames[j].luma_mean : a.frames[i - 1].luma_mean;
            const float hi = into_black ? a.frames[i - 1].luma_mean : a.frames[j].luma_mean;
            if (lo < cfg.fade_threshold && hi - lo > min_drop && j - i + 1 >= 3) {
                Transition t;
                t.frame = (int)(into_black ? i - 1 : i);
                t.kind = into_black ? "fade_out" : "fade_in";
                t.strength = hi - lo;
                t.length = (int)(j - i + 2);
                a.transitions.push_back(t);
            }
            i = j + 1;
        }
    };

    for (size_t b = 0; b + 1 < bounds.size(); ++b) {
        const size_t s0 = bounds[b], s1 = bounds[b + 1];
        if (s1 - s0 < 4) continue;   // too short to hold a fade
        float lo = 1.0f, hi = 0.0f;
        for (size_t i = s0; i < s1; ++i) {
            lo = std::min(lo, a.frames[i].luma_mean);
            hi = std::max(hi, a.frames[i].luma_mean);
        }
        const float min_drop = std::max(0.015f, 0.5f * (hi - lo));
        scan_fade(s0, s1, min_drop, true);
        scan_fade(s0, s1, min_drop, false);
    }

    std::sort(a.transitions.begin(), a.transitions.end(),
              [](const Transition &x, const Transition &y) { return x.frame < y.frame; });
}

inline void summarise(SequenceAnalysis &a) {
    const size_t n = a.frames.size();
    if (n == 0) return;

    a.width = a.frames[0].width;
    a.height = a.frames[0].height;

    std::vector<float> lumas(n);
    for (size_t i = 0; i < n; ++i) lumas[i] = a.frames[i].luma_mean;
    a.luma_curve = fit_curve(lumas);

    double e = 0, px = 0, py = 0, z = 0;
    int mn = 0;
    for (size_t i = 1; i < a.motion.size(); ++i) {
        e += a.motion[i].energy;
        // Weight by confidence: an undeterminable displacement should not drag
        // the mean toward zero as if it were a measured standstill.
        px += (double)a.motion[i].dx * a.motion[i].confidence;
        py += (double)a.motion[i].dy * a.motion[i].confidence;
        z += a.motion[i].divergence;
        ++mn;
    }
    if (mn) {
        a.motion_energy_mean = (float)(e / mn);
        double wsum = 0;
        for (size_t i = 1; i < a.motion.size(); ++i) wsum += a.motion[i].confidence;
        if (wsum > 1e-6) {
            a.global_pan_x = (float)(px / wsum);
            a.global_pan_y = (float)(py / wsum);
        }
        a.zoom_rate = (float)(z / mn);
    }

    // Does it loop? Compare the essence of the first and last frames.
    if (n >= 3 && !a.frames.front().essence.empty()) {
        const Frame &f0 = a.frames.front().essence;
        const Frame &fl = a.frames.back().essence;
        if (f0.width == fl.width && f0.height == fl.height) {
            double d = 0;
            for (size_t i = 0; i < f0.rgb.size(); ++i)
                d += fabs((double)f0.rgb[i] - (double)fl.rgb[i]);
            a.loop_error = (float)(d / (double)f0.rgb.size());
            a.loops = a.loop_error < 0.02f;
        }
    }
}

// ---------------------------------------------------------------------------
// JSON serialisation
// ---------------------------------------------------------------------------

inline json::Value palette_json(const Palette &p) {
    json::Value out = json::Value::arr();
    for (size_t i = 0; i < p.weights.size(); ++i) {
        json::Value e = json::Value::obj();
        e.set("rgb", json::rgb_array(p.colors[i * 3], p.colors[i * 3 + 1], p.colors[i * 3 + 2]));
        e.set("weight", json::Value((double)p.weights[i]));
        out.push(e);
    }
    return out;
}

inline json::Value frame_json(const FrameStats &s, bool include_histogram) {
    json::Value f = json::Value::obj();
    f.set("index", json::Value((double)s.index));

    json::Value tone = json::Value::obj();
    tone.set("luma_mean", json::Value((double)s.luma_mean));
    tone.set("luma_p05", json::Value((double)s.luma_p05));
    tone.set("luma_p50", json::Value((double)s.luma_p50));
    tone.set("luma_p95", json::Value((double)s.luma_p95));
    tone.set("luma_stddev", json::Value((double)s.luma_stddev));
    tone.set("contrast", json::Value((double)s.contrast));
    tone.set("dynamic_range", json::Value((double)s.dynamic_range));
    tone.set("vignette", json::Value((double)s.vignette));
    f.set("tone", tone);

    json::Value col = json::Value::obj();
    col.set("rgb_mean", json::rgb_array(s.rgb_mean[0], s.rgb_mean[1], s.rgb_mean[2]));
    col.set("saturation", json::Value((double)s.saturation_mean));
    col.set("colorfulness", json::Value((double)s.colorfulness));
    col.set("temperature", json::Value((double)s.temperature));
    col.set("palette", palette_json(s.palette));
    f.set("color", col);

    json::Value str = json::Value::obj();
    str.set("edge_density", json::Value((double)s.edge_density));
    str.set("edge_p95", json::Value((double)s.edge_p95));
    str.set("high_freq_ratio", json::Value((double)s.high_freq_ratio));
    f.set("structure", str);

    json::Value comp = json::Value::obj();
    comp.set("centroid", json::xy_array(s.centroid_x, s.centroid_y));
    f.set("composition", comp);

    if (include_histogram) f.set("luma_histogram", json::num_array(s.luma_hist));
    return f;
}

inline json::Value motion_json(const MotionEstimate &m, bool include_field) {
    json::Value v = json::Value::obj();
    v.set("dx", json::Value((double)m.dx));
    v.set("dy", json::Value((double)m.dy));
    v.set("confidence", json::Value((double)m.confidence));
    v.set("energy", json::Value((double)m.energy));
    v.set("residual", json::Value((double)m.residual));
    v.set("divergence", json::Value((double)m.divergence));
    if (include_field) {
        json::Value fld = json::Value::arr();
        for (const MotionVector &mv : m.field) {
            json::Value e = json::Value::arr();
            e.push(json::Value((double)mv.x));
            e.push(json::Value((double)mv.y));
            e.push(json::Value((double)mv.dx));
            e.push(json::Value((double)mv.dy));
            e.push(json::Value((double)mv.confidence));
            fld.push(e);
        }
        v.set("field", fld);
    }
    return v;
}

/// Serialise the analysis. `detail_level` trades size for completeness:
///   0 = summary only, 1 = per-frame stats, 2 = plus histograms and motion fields.
inline json::Value analysis_json(const SequenceAnalysis &a, const AnalyzeConfig &cfg,
                                int detail_level) {
    json::Value root = json::Value::obj();

    json::Value src = json::Value::obj();
    src.set("width", json::Value((double)a.width));
    src.set("height", json::Value((double)a.height));
    src.set("fps", json::Value((double)a.fps));
    src.set("frames", json::Value((double)a.frames.size()));
    root.set("source", src);

    json::Value acfg = json::Value::obj();
    acfg.set("essence_width", json::Value((double)cfg.essence_w));
    acfg.set("essence_height", json::Value((double)cfg.essence_h));
    acfg.set("histogram_bins", json::Value((double)cfg.hist_bins));
    acfg.set("palette_size", json::Value((double)cfg.palette_size));
    acfg.set("motion_grid", json::int_pair(cfg.motion_cols, cfg.motion_rows));
    acfg.set("motion_radius", json::Value((double)cfg.motion_radius));
    acfg.set("cut_threshold", json::Value((double)cfg.cut_threshold));
    root.set("analysis_config", acfg);

    json::Value sum = json::Value::obj();
    sum.set("motion_energy_mean", json::Value((double)a.motion_energy_mean));
    sum.set("global_pan", json::xy_array(a.global_pan_x, a.global_pan_y));
    sum.set("zoom_rate", json::Value((double)a.zoom_rate));
    sum.set("loops", json::Value(a.loops));
    sum.set("loop_error", json::Value((double)a.loop_error));

    json::Value curve = json::Value::obj();
    curve.set("easing", json::Value(a.luma_curve.easing));
    curve.set("rmse", json::Value((double)a.luma_curve.rmse));
    curve.set("start", json::Value((double)a.luma_curve.start));
    curve.set("end", json::Value((double)a.luma_curve.end));
    curve.set("monotonic", json::Value(a.luma_curve.monotonic));
    sum.set("luma_curve", curve);
    sum.set("palette", palette_json(a.global_palette));
    root.set("summary", sum);

    json::Value trs = json::Value::arr();
    for (const Transition &t : a.transitions) {
        json::Value e = json::Value::obj();
        e.set("frame", json::Value((double)t.frame));
        e.set("kind", json::Value(t.kind));
        e.set("strength", json::Value((double)t.strength));
        e.set("length", json::Value((double)t.length));
        trs.push(e);
    }
    root.set("transitions", trs);

    if (detail_level >= 1) {
        json::Value fs = json::Value::arr();
        for (const FrameStats &s : a.frames) fs.push(frame_json(s, detail_level >= 2));
        root.set("frames", fs);

        json::Value ms = json::Value::arr();
        for (size_t i = 0; i < a.motion.size(); ++i)
            ms.push(motion_json(a.motion[i], detail_level >= 2));
        root.set("motion", ms);
    }
    return root;
}

} // namespace ppm

#endif // PPM_ANALYZE_HPP
