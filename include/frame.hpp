// ===========================================================================
//  frame.hpp -- the frame asset: an RGB float buffer, plus PPM read/write.
//
//  A FRAME is the unit of currency in this project. Everything -- ingesting a
//  video, analysing it, synthesising a new one -- is a transformation on a
//  sequence of frames. This header is deliberately the only place that knows
//  how a frame is laid out in memory or on the wire.
//
//  Representation: float RGB in [0,1], row-major, top row first, three floats
//  per pixel. Float rather than bytes because analysis and resampling both need
//  headroom: an 8-bit intermediate would quantise twice and visibly band.
//
//  COLOUR SPACE. Values decoded from a PPM are display-encoded (sRGB-ish), which
//  is what video carries. That is the right space for histograms and palette
//  extraction, because it is perceptually spaced. It is the WRONG space for
//  averaging: the mean of sRGB 0.0 and 1.0 is not the colour half as bright, so
//  a naive downsample darkens midtones and haloes edges. Anything that averages
//  pixels therefore converts to linear first. See to_linear / to_srgb.
// ===========================================================================
#ifndef PPM_FRAME_HPP
#define PPM_FRAME_HPP

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace ppm {

// ---------------------------------------------------------------------------
// colour space
// ---------------------------------------------------------------------------

/// sRGB electro-optical transfer function, display-encoded -> linear light.
inline float to_linear(float c) {
    return c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
}

/// Linear light -> display-encoded sRGB.
inline float to_srgb(float c) {
    if (c <= 0.0f) return 0.0f;
    if (c >= 1.0f) return 1.0f;
    return c <= 0.0031308f ? c * 12.92f : 1.055f * powf(c, 1.0f / 2.4f) - 0.055f;
}

/// Rec.709 luma weights, applied to display-encoded values.
inline float luma(float r, float g, float b) {
    return 0.2126f * r + 0.7152f * g + 0.0722f * b;
}

// ---------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------

struct Frame {
    int width = 0;
    int height = 0;
    std::vector<float> rgb;   ///< width*height*3, display-encoded [0,1]

    Frame() = default;
    Frame(int w, int h) : width(w), height(h), rgb((size_t)w * h * 3, 0.0f) {}

    bool empty() const { return width <= 0 || height <= 0; }
    size_t pixels() const { return (size_t)width * (size_t)height; }

    void resize(int w, int h) {
        width = w;
        height = h;
        rgb.assign((size_t)w * h * 3, 0.0f);
    }

    /// Index of pixel (x,y). No bounds checking; callers clamp.
    size_t at(int x, int y) const { return ((size_t)y * (size_t)width + (size_t)x) * 3; }

    void get(int x, int y, float &r, float &g, float &b) const {
        const size_t i = at(x, y);
        r = rgb[i]; g = rgb[i + 1]; b = rgb[i + 2];
    }

    void set(int x, int y, float r, float g, float b) {
        const size_t i = at(x, y);
        rgb[i] = r; rgb[i + 1] = g; rgb[i + 2] = b;
    }

    /// Edge-clamped fetch, so filter kernels need no special-casing at borders.
    void get_clamped(int x, int y, float &r, float &g, float &b) const {
        if (x < 0) x = 0; else if (x >= width) x = width - 1;
        if (y < 0) y = 0; else if (y >= height) y = height - 1;
        get(x, y, r, g, b);
    }

    float luma_at(int x, int y) const {
        const size_t i = at(x, y);
        return luma(rgb[i], rgb[i + 1], rgb[i + 2]);
    }
};

// ---------------------------------------------------------------------------
// PPM reading
// ---------------------------------------------------------------------------
//
// Reads the P6 subset that matters, but reads it strictly. The header rules are
// easy to get subtly wrong, and a wrong header silently shifts every pixel:
//   * fields are separated by any run of space/LF/CR/TAB
//   * '#' starts a comment that runs to end of line -- in the HEADER only
//   * exactly ONE whitespace byte separates maxval from the pixel data
//   * maxval <= 255 is one byte per sample; 256..65535 is two, big-endian

namespace detail {

/// Read the next header token, skipping whitespace and comments.
/// Returns false on EOF.
inline bool ppm_token(FILE *f, std::string &out) {
    out.clear();
    int c;
    // Skip leading whitespace and comment lines.
    for (;;) {
        c = fgetc(f);
        if (c == EOF) return false;
        if (c == '#') {
            while (c != '\n' && c != EOF) c = fgetc(f);
            continue;
        }
        if (c != ' ' && c != '\n' && c != '\r' && c != '\t') break;
    }
    // Accumulate until the next whitespace. The terminating byte is consumed,
    // which is exactly the single separator the format requires before data.
    for (;;) {
        out.push_back((char)c);
        c = fgetc(f);
        if (c == EOF || c == ' ' || c == '\n' || c == '\r' || c == '\t') break;
    }
    return !out.empty();
}

} // namespace detail

/// Read one P6 frame. Returns false at clean EOF (no more frames), and sets
/// `error` non-empty only for malformed input.
inline bool read_ppm(FILE *f, Frame &out, std::string &error) {
    error.clear();
    std::string tok;

    if (!detail::ppm_token(f, tok)) return false;      // clean EOF
    if (tok != "P6") {
        error = "expected P6 magic, got '" + tok + "'";
        return false;
    }
    int w = 0, h = 0, maxval = 0;
    if (!detail::ppm_token(f, tok)) { error = "truncated header (width)"; return false; }
    w = atoi(tok.c_str());
    if (!detail::ppm_token(f, tok)) { error = "truncated header (height)"; return false; }
    h = atoi(tok.c_str());
    if (!detail::ppm_token(f, tok)) { error = "truncated header (maxval)"; return false; }
    maxval = atoi(tok.c_str());

    if (w <= 0 || h <= 0) { error = "bad dimensions"; return false; }
    if (maxval <= 0 || maxval > 65535) { error = "maxval out of range"; return false; }

    out.resize(w, h);
    const size_t n = out.pixels() * 3;
    const float inv = 1.0f / (float)maxval;

    if (maxval <= 255) {
        std::vector<uint8_t> buf(n);
        if (fread(buf.data(), 1, n, f) != n) { error = "truncated pixel data"; return false; }
        for (size_t i = 0; i < n; ++i) out.rgb[i] = (float)buf[i] * inv;
    } else {
        std::vector<uint8_t> buf(n * 2);
        if (fread(buf.data(), 1, n * 2, f) != n * 2) { error = "truncated pixel data"; return false; }
        for (size_t i = 0; i < n; ++i) {
            const unsigned v = ((unsigned)buf[i * 2] << 8) | buf[i * 2 + 1];  // big-endian
            out.rgb[i] = (float)v * inv;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// PPM writing
// ---------------------------------------------------------------------------

/// Quantise to 8 bits. Clamped, and rounded rather than truncated.
///
/// The clamp is not paranoia: an unclamped cast wraps modulo 256, turning an
/// over-bright pixel into a dark speckle. The +0.5 matters too -- truncation
/// biases every channel down half a level, darkening the whole image slightly.
inline uint8_t to_byte(float v) {
    if (v <= 0.0f) return 0;
    if (v >= 1.0f) return 255;
    return (uint8_t)(v * 255.0f + 0.5f);
}

inline bool write_ppm(FILE *f, const Frame &fr) {
    if (fprintf(f, "P6\n%d %d\n255\n", fr.width, fr.height) < 0) return false;
    const size_t n = fr.pixels() * 3;
    std::vector<uint8_t> buf(n);
    for (size_t i = 0; i < n; ++i) buf[i] = to_byte(fr.rgb[i]);
    return fwrite(buf.data(), 1, n, f) == n;
}

inline bool write_ppm_path(const std::string &path, const Frame &fr) {
    FILE *f = fopen(path.c_str(), "wb");
    if (!f) return false;
    const bool ok = write_ppm(f, fr);
    fclose(f);
    return ok;
}

// ---------------------------------------------------------------------------
// resampling
// ---------------------------------------------------------------------------

/// Box-average `src` down to w x h, averaging in LINEAR light.
///
/// Averaging display-encoded values is the classic downscale bug: it makes
/// midtones drift and puts dark fringes around bright detail. Converting to
/// linear first costs two transfer-function calls per sample and removes it.
inline Frame downsample_box(const Frame &src, int w, int h) {
    Frame dst(w, h);
    if (src.empty()) return dst;

    for (int y = 0; y < h; ++y) {
        const int y0 = (int)((int64_t)y * src.height / h);
        int y1 = (int)((int64_t)(y + 1) * src.height / h);
        if (y1 <= y0) y1 = y0 + 1;
        for (int x = 0; x < w; ++x) {
            const int x0 = (int)((int64_t)x * src.width / w);
            int x1 = (int)((int64_t)(x + 1) * src.width / w);
            if (x1 <= x0) x1 = x0 + 1;

            double ar = 0, ag = 0, ab = 0;
            int count = 0;
            for (int sy = y0; sy < y1 && sy < src.height; ++sy) {
                for (int sx = x0; sx < x1 && sx < src.width; ++sx) {
                    const size_t i = src.at(sx, sy);
                    ar += to_linear(src.rgb[i]);
                    ag += to_linear(src.rgb[i + 1]);
                    ab += to_linear(src.rgb[i + 2]);
                    ++count;
                }
            }
            if (count) {
                const double inv = 1.0 / count;
                dst.set(x, y, to_srgb((float)(ar * inv)), to_srgb((float)(ag * inv)),
                        to_srgb((float)(ab * inv)));
            }
        }
    }
    return dst;
}

/// Smooth bilinear sample with normalised coordinates in [0,1], in linear light.
/// Used by the reconstruction layer to expand a coarse grid back to full size.
inline void sample_bilinear_linear(const Frame &src, float u, float v,
                                   float &r, float &g, float &b) {
    if (src.empty()) { r = g = b = 0.0f; return; }
    const float fx = u * (float)src.width - 0.5f;
    const float fy = v * (float)src.height - 0.5f;
    const int x0 = (int)floorf(fx), y0 = (int)floorf(fy);
    const float tx = fx - (float)x0, ty = fy - (float)y0;

    float c[4][3];
    const int xs[2] = {x0, x0 + 1}, ys[2] = {y0, y0 + 1};
    for (int j = 0; j < 2; ++j) {
        for (int i = 0; i < 2; ++i) {
            float rr, gg, bb;
            src.get_clamped(xs[i], ys[j], rr, gg, bb);
            c[j * 2 + i][0] = to_linear(rr);
            c[j * 2 + i][1] = to_linear(gg);
            c[j * 2 + i][2] = to_linear(bb);
        }
    }
    float acc[3];
    for (int k = 0; k < 3; ++k) {
        const float top = c[0][k] + (c[1][k] - c[0][k]) * tx;
        const float bot = c[2][k] + (c[3][k] - c[2][k]) * tx;
        acc[k] = top + (bot - top) * ty;
    }
    r = to_srgb(acc[0]);
    g = to_srgb(acc[1]);
    b = to_srgb(acc[2]);
}

/// Bicubic (Catmull-Rom) sample, in linear light.
///
/// Bilinear upsampling of a coarse grid produces visible diamond-shaped
/// facets because its first derivative is discontinuous at sample boundaries.
/// Catmull-Rom is C1 continuous, which is what makes a 40x23 grid read as a
/// smooth field rather than as a grid.
inline void sample_bicubic_linear(const Frame &src, float u, float v,
                                  float &r, float &g, float &b) {
    if (src.empty()) { r = g = b = 0.0f; return; }
    const float fx = u * (float)src.width - 0.5f;
    const float fy = v * (float)src.height - 0.5f;
    const int x0 = (int)floorf(fx), y0 = (int)floorf(fy);
    const float tx = fx - (float)x0, ty = fy - (float)y0;

    auto w0 = [](float t) { return ((-0.5f * t + 1.0f) * t - 0.5f) * t; };
    auto w1 = [](float t) { return (1.5f * t - 2.5f) * t * t + 1.0f; };
    auto w2 = [](float t) { return ((-1.5f * t + 2.0f) * t + 0.5f) * t; };
    auto w3 = [](float t) { return (0.5f * t - 0.5f) * t * t; };

    const float wx[4] = {w0(tx), w1(tx), w2(tx), w3(tx)};
    const float wy[4] = {w0(ty), w1(ty), w2(ty), w3(ty)};

    float acc[3] = {0, 0, 0};
    for (int j = 0; j < 4; ++j) {
        for (int i = 0; i < 4; ++i) {
            float rr, gg, bb;
            src.get_clamped(x0 - 1 + i, y0 - 1 + j, rr, gg, bb);
            const float wgt = wx[i] * wy[j];
            acc[0] += to_linear(rr) * wgt;
            acc[1] += to_linear(gg) * wgt;
            acc[2] += to_linear(bb) * wgt;
        }
    }
    // Catmull-Rom overshoots on sharp transitions; clamp before re-encoding so
    // the negative lobe cannot produce NaN in to_srgb's pow().
    r = to_srgb(acc[0] < 0.0f ? 0.0f : acc[0]);
    g = to_srgb(acc[1] < 0.0f ? 0.0f : acc[1]);
    b = to_srgb(acc[2] < 0.0f ? 0.0f : acc[2]);
}

} // namespace ppm

#endif // PPM_FRAME_HPP
