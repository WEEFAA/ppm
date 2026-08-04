// ===========================================================================
//  ppmshader.hpp -- CPU fragment-shader runtime.
//
//  A shader is a single .cpp file that includes this header and defines one
//  function:
//
//      #include "ppmshader.hpp"
//
//      vec4 mainImage(vec2 FC, const Uniforms &u) {
//          vec2 p = (FC * 2.0f - u.resolution) / u.resolution.y;
//          return vec4(p.x, p.y, 0.5f + 0.5f * sinf(u.time), 1.0f);
//      }
//
//  This header supplies main(): argument parsing, a threaded render loop,
//  supersampling, and a binary PPM (P6) writer. The resulting binary streams
//  concatenated P6 frames to stdout, which ffmpeg reads via `-f ppm_pipe`.
//
//  Design notes:
//    * mainImage must be a PURE function of (FC, u). Purity is what makes
//      both threading and supersampling free -- no locks, no ordering.
//    * FC follows the GLSL convention: pixel centres at +0.5, origin at the
//      BOTTOM-left, y increasing upward. PPM rows are written top-down, so the
//      runtime flips. Pass --top-left-origin to disable.
// ===========================================================================
#ifndef PPMSHADER_HPP
#define PPMSHADER_HPP

#include "pgl.hpp"

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include <sys/stat.h>
#include <unistd.h>

using namespace pgl;

// ---------------------------------------------------------------------------
// exit codes (see docs/cli-design.json)
// ---------------------------------------------------------------------------
enum {
    PPM_EXIT_OK    = 0,
    PPM_EXIT_ERROR = 1,
    PPM_EXIT_USAGE = 2,
};

// ---------------------------------------------------------------------------
// uniforms
// ---------------------------------------------------------------------------

/// Per-frame state handed to every shader invocation. Read-only.
struct Uniforms {
    vec2  resolution;   ///< frame size in pixels
    float time;         ///< seconds since frame 0 (frame / fps)
    float phase;        ///< loop position in [0,1). Use phase*TAU to loop seamlessly.
    float aspect;       ///< resolution.x / resolution.y
    int   frame;        ///< 0-based frame index
    int   frames;       ///< total frames in this render
    float fps;          ///< frames per second
};

// ---------------------------------------------------------------------------
// tweakable parameters
// ---------------------------------------------------------------------------

/// A named float knob, settable from the command line with `--set name=value`.
///
///     static Param zoom{"zoom", 1.0f, "camera zoom"};
///     ...  vec2 p = uv * zoom;      // implicit conversion to float
///
/// Declaring a Param also documents it: `--help` lists every one with its
/// default and description. This is how a shader exposes its tweakables
/// without the CLI needing to know anything about the shader.
struct Param;
inline std::vector<Param *> &param_registry() {
    static std::vector<Param *> v;
    return v;
}

struct Param {
    const char *name;
    const char *help;
    float value;
    float default_value;

    Param(const char *name_, float default_, const char *help_ = "")
        : name(name_), help(help_), value(default_), default_value(default_) {
        param_registry().push_back(this);
    }

    operator float() const { return value; }
};

// ---------------------------------------------------------------------------
// the shader entry point, provided by the shader file
// ---------------------------------------------------------------------------

/// Return linear RGB in [0,1]. Alpha is ignored (PPM has no alpha channel).
/// Values outside [0,1] are clamped, not wrapped.
vec4 mainImage(vec2 FC, const Uniforms &u);

/// Optional: shaders may override this to name themselves in --help.
#ifndef SHADER_NAME
#define SHADER_NAME "shader"
#endif
#ifndef SHADER_DESCRIPTION
#define SHADER_DESCRIPTION ""
#endif

// ---------------------------------------------------------------------------
// render configuration
// ---------------------------------------------------------------------------

namespace ppmrt {

struct Config {
    int         width      = 1280;
    int         height     = 720;
    int         frames     = 240;
    float       fps        = 60.0f;
    int         samples    = 1;      ///< NxN supersamples per pixel
    int         threads    = 0;      ///< 0 = hardware_concurrency
    int         at         = -1;     ///< >=0: emit only this frame index

    bool        flip_y     = true;   ///< GLSL bottom-left origin
    bool        quiet      = false;
    std::string output     = "-";    ///< "-" is stdout
    bool        output_set = false;  ///< true if --output was given explicitly
    std::string seq_prefix = "";     ///< non-empty: also write numbered files
    int         seq_pad    = 4;

    /// Whether to emit the concatenated stream at all.
    ///
    /// --seq and --output are independent sinks, so a caller can ask for both
    /// (keep the frames AND pipe to ffmpeg). But a bare --seq should not also
    /// dump megabytes of PPM onto a terminal, so the stream is suppressed when
    /// --seq is used and --output was not asked for.
    bool stream() const { return seq_prefix.empty() || output_set; }
};

/// mkdir -p, for the directory holding `path`.
///
/// A local copy rather than the one in pipe.hpp: this header is the standalone
/// shader runtime and a shader must compile against it alone. Keeping it
/// self-contained is the point, so a few duplicated lines are the right trade.
inline bool make_parent_dirs(const std::string &path) {
    const size_t slash = path.find_last_of('/');
    if (slash == std::string::npos || slash == 0) return true;
    const std::string dir = path.substr(0, slash);

    std::string acc;
    size_t i = 0;
    if (dir[0] == '/') { acc = "/"; i = 1; }
    while (i <= dir.size()) {
        size_t j = dir.find('/', i);
        if (j == std::string::npos) j = dir.size();
        const std::string part = dir.substr(i, j - i);
        if (!part.empty()) {
            if (!acc.empty() && acc.back() != '/') acc += '/';
            acc += part;
            struct stat st;
            const bool exists = stat(acc.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
            if (!exists && mkdir(acc.c_str(), 0755) != 0) {
                // A concurrent creation is fine; anything else is not.
                if (!(stat(acc.c_str(), &st) == 0 && S_ISDIR(st.st_mode))) return false;
            }
        }
        if (j == dir.size()) break;
        i = j + 1;
    }
    return true;
}

inline void die_usage(const char *msg, const char *arg = nullptr) {
    if (arg)
        fprintf(stderr, "%s: %s: %s\n", SHADER_NAME, msg, arg);
    else
        fprintf(stderr, "%s: %s\n", SHADER_NAME, msg);
    fprintf(stderr, "Try '%s --help' for more information.\n", SHADER_NAME);
    exit(PPM_EXIT_USAGE);
}

inline void print_help() {
    printf("%s -- CPU fragment shader\n", SHADER_NAME);
    if (SHADER_DESCRIPTION[0]) printf("%s\n", SHADER_DESCRIPTION);
    printf(
        "\n"
        "Streams concatenated binary PPM (P6) frames to stdout.\n"
        "\n"
        "USAGE\n"
        "  %s [options] > frames.ppm\n"
        "  %s [options] | ffmpeg -f ppm_pipe -framerate 60 -i - out.mp4\n"
        "\n"
        "OPTIONS\n"
        "      --size WxH        frame size (default 1280x720)\n"
        "      --frames N        number of frames (default 240)\n"
        "      --fps N           frames per second (default 60)\n"
        "      --duration SEC    set --frames from a duration instead\n"
        "  -s, --samples N       NxN supersamples per pixel (default 1)\n"
        "  -j, --threads N       worker threads (default: all cores)\n"
        "      --at N            emit only frame N, timed as frame N of --frames\n"
        "      --set NAME=VALUE  set a shader parameter (repeatable)\n"
        "  -o, --output PATH     write to PATH instead of stdout\n"
        "      --seq PREFIX      write numbered PREFIX0000.ppm files\n"
        "      --top-left-origin put FC origin at top-left (default: bottom-left)\n"
        "  -q, --quiet           suppress the progress line\n"
        "  -h, --help            show this help\n",
        SHADER_NAME, SHADER_NAME);

    const std::vector<Param *> &ps = param_registry();
    if (!ps.empty()) {
        printf("\nSHADER PARAMETERS (--set NAME=VALUE)\n");
        for (Param *p : ps) {
            printf("      %-16s %-8g %s\n", p->name, p->default_value, p->help);
        }
    }
    printf("\nEXIT STATUS\n  0 success   1 error   2 usage error\n");
}

inline float parse_float(const char *s, const char *flag) {
    char *end = nullptr;
    float v = strtof(s, &end);
    if (end == s || (end && *end != '\0')) die_usage("not a number", s);
    (void)flag;
    return v;
}

inline int parse_int(const char *s, const char *flag) {
    char *end = nullptr;
    long v = strtol(s, &end, 10);
    if (end == s || (end && *end != '\0')) die_usage("not an integer", s);
    (void)flag;
    return (int)v;
}

inline Config parse_args(int argc, char **argv) {
    Config c;
    bool have_duration = false;
    float duration = 0.0f;

    for (int i = 1; i < argc; ++i) {
        const char *a = argv[i];
        auto next = [&](void) -> const char * {
            if (i + 1 >= argc) die_usage("missing value for", a);
            return argv[++i];
        };

        if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
            print_help();
            exit(PPM_EXIT_OK);
        } else if (!strcmp(a, "--size")) {
            const char *v = next();
            int w = 0, h = 0;
            if (sscanf(v, "%dx%d", &w, &h) != 2 || w <= 0 || h <= 0)
                die_usage("expected WxH, e.g. 1920x1080", v);
            c.width = w;
            c.height = h;
        } else if (!strcmp(a, "--frames")) {
            c.frames = parse_int(next(), a);
        } else if (!strcmp(a, "--fps")) {
            c.fps = parse_float(next(), a);
        } else if (!strcmp(a, "--duration")) {
            duration = parse_float(next(), a);
            have_duration = true;
        } else if (!strcmp(a, "-s") || !strcmp(a, "--samples")) {
            c.samples = parse_int(next(), a);
        } else if (!strcmp(a, "-j") || !strcmp(a, "--threads")) {
            c.threads = parse_int(next(), a);
        } else if (!strcmp(a, "--at")) {
            c.at = parse_int(next(), a);
            if (c.at < 0) die_usage("--at must be >= 0");
        } else if (!strcmp(a, "-o") || !strcmp(a, "--output")) {
            c.output = next();
            c.output_set = true;
        } else if (!strcmp(a, "--seq")) {
            c.seq_prefix = next();
        } else if (!strcmp(a, "--top-left-origin")) {
            c.flip_y = false;
        } else if (!strcmp(a, "-q") || !strcmp(a, "--quiet")) {
            c.quiet = true;
        } else if (!strcmp(a, "--set")) {
            const char *v = next();
            const char *eq = strchr(v, '=');
            if (!eq) die_usage("expected NAME=VALUE", v);
            std::string name(v, eq - v);
            Param *found = nullptr;
            for (Param *p : param_registry())
                if (name == p->name) found = p;
            if (!found) {
                fprintf(stderr, "%s: unknown parameter '%s'\n", SHADER_NAME, name.c_str());
                if (param_registry().empty()) {
                    fprintf(stderr, "  this shader declares no parameters\n");
                } else {
                    fprintf(stderr, "  known parameters:");
                    for (Param *p : param_registry()) fprintf(stderr, " %s", p->name);
                    fprintf(stderr, "\n");
                }
                exit(PPM_EXIT_USAGE);
            }
            found->value = parse_float(eq + 1, a);
        } else {
            die_usage("unknown option", a);
        }
    }

    if (have_duration) c.frames = (int)(duration * c.fps + 0.5f);
    if (c.frames <= 0) die_usage("--frames must be positive");
    if (c.samples <= 0) die_usage("--samples must be positive");
    if (c.threads < 0) die_usage("--threads must be >= 0");
    if (c.threads == 0) {
        unsigned hc = std::thread::hardware_concurrency();
        c.threads = hc ? (int)hc : 1;
    }
    return c;
}

// ---------------------------------------------------------------------------
// rendering
// ---------------------------------------------------------------------------

/// Quantise a linear [0,1] float to an 8-bit sample.
///
/// Note the clamp: the naive `(unsigned char)(v * 255)` used in most toy PPM
/// writers wraps on out-of-range values, which turns an over-bright highlight
/// into a dark speckle. Shaders that tonemap with tanh() rarely exceed 1, but
/// clamping costs nothing and removes a whole class of artefact.
inline unsigned char to_byte(float v) {
    if (v <= 0.0f) return 0;
    if (v >= 1.0f) return 255;
    return (unsigned char)(v * 255.0f + 0.5f);
}

/// Render one frame's worth of rows [y0, y1) into `rgb`.
inline void render_rows(unsigned char *rgb, const Config &c, const Uniforms &u,
                        int y0, int y1) {
    const int   S    = c.samples;
    const float inv  = 1.0f / (float)S;
    const float invN = 1.0f / (float)(S * S);
    const float h    = (float)c.height;

    for (int row = y0; row < y1; ++row) {
        unsigned char *out = rgb + (size_t)row * (size_t)c.width * 3;
        // Base y in GLSL space: flip so row 0 is the top of the image.
        const float ybase = c.flip_y ? (h - 1.0f - (float)row) : (float)row;

        for (int x = 0; x < c.width; ++x) {
            vec4 acc(0.0f);
            for (int sy = 0; sy < S; ++sy) {
                const float oy = ((float)sy + 0.5f) * inv;
                for (int sx = 0; sx < S; ++sx) {
                    const float ox = ((float)sx + 0.5f) * inv;
                    acc += mainImage(vec2((float)x + ox, ybase + oy), u);
                }
            }
            acc = acc * invN;
            *out++ = to_byte(acc.x);
            *out++ = to_byte(acc.y);
            *out++ = to_byte(acc.z);
        }
    }
}

inline void render_frame(unsigned char *rgb, const Config &c, const Uniforms &u) {
    if (c.threads <= 1) {
        render_rows(rgb, c, u, 0, c.height);
        return;
    }
    // Row-striped, one contiguous band per worker. Frame-level parallelism
    // would be slightly more efficient but would require reordering output.
    std::vector<std::thread> pool;
    pool.reserve(c.threads);
    const int band = (c.height + c.threads - 1) / c.threads;
    for (int t = 0; t < c.threads; ++t) {
        int y0 = t * band;
        int y1 = y0 + band;
        if (y1 > c.height) y1 = c.height;
        if (y0 >= y1) break;
        pool.emplace_back([=]() { render_rows(rgb, c, u, y0, y1); });
    }
    for (std::thread &th : pool) th.join();
}

/// Write a binary PPM header. maxval 255 means one byte per sample.
inline void write_ppm_header(FILE *f, int w, int h) {
    // A single '\n' after each field is the most widely accepted layout, and
    // is what ffmpeg's pnm parser scans for when splitting a piped stream.
    fprintf(f, "P6\n%d %d\n255\n", w, h);
}

} // namespace ppmrt

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char **argv) {
    using namespace ppmrt;

    // If the downstream consumer (ffmpeg) exits first, a write would kill us
    // with SIGPIPE and no diagnostic. Handle it as a normal write error.
    signal(SIGPIPE, SIG_IGN);

    Config c = parse_args(argc, argv);

    FILE *out = nullptr;
    if (c.stream()) {
        if (c.output == "-") {
            out = stdout;
        } else {
            make_parent_dirs(c.output);
            out = fopen(c.output.c_str(), "wb");
            if (!out) {
                fprintf(stderr, "%s: cannot write '%s': %s\n", SHADER_NAME,
                        c.output.c_str(), strerror(errno));
                return PPM_EXIT_ERROR;
            }
        }
    }

    // --seq takes a path PREFIX, not a directory, so the directory part has to be
    // created here; the per-frame fopen below would otherwise fail on the first
    // frame with nothing but "cannot write".
    if (!c.seq_prefix.empty() && !make_parent_dirs(c.seq_prefix)) {
        fprintf(stderr, "%s: cannot create the directory for '%s'\n", SHADER_NAME,
                c.seq_prefix.c_str());
        return PPM_EXIT_ERROR;
    }

    const size_t frame_bytes = (size_t)c.width * (size_t)c.height * 3;
    std::vector<unsigned char> rgb(frame_bytes);

    const bool tty = isatty(fileno(stderr));

    // --at renders a single frame but keeps its position within the nominal
    // sequence, so `time` and `phase` match what a full render would produce at
    // that index. Rendering a still therefore never changes what you see.
    const int first = c.at >= 0 ? c.at : 0;
    const int last  = c.at >= 0 ? c.at + 1 : c.frames;

    for (int i = first; i < last; ++i) {
        Uniforms u;
        u.resolution = vec2((float)c.width, (float)c.height);
        u.frame      = i;
        u.frames     = c.frames;
        u.fps        = c.fps;
        u.time       = (float)i / c.fps;
        u.phase      = (float)i / (float)c.frames;
        u.aspect     = (float)c.width / (float)c.height;

        render_frame(rgb.data(), c, u);

        // Two independent sinks: the numbered file and the stream. Both get the
        // same bytes, so --frames-dir can be combined with piping to ffmpeg.
        auto emit = [&](FILE *dst, const char *what) -> bool {
            write_ppm_header(dst, c.width, c.height);
            if (fwrite(rgb.data(), 1, frame_bytes, dst) != frame_bytes) {
                fprintf(stderr, "\n%s: write to %s failed at frame %d: %s\n",
                        SHADER_NAME, what, i, strerror(errno));
                return false;
            }
            return true;
        };

        if (!c.seq_prefix.empty()) {
            char path[1024];
            snprintf(path, sizeof(path), "%s%0*d.ppm", c.seq_prefix.c_str(),
                     c.seq_pad, i);
            FILE *seq = fopen(path, "wb");
            if (!seq) {
                fprintf(stderr, "\n%s: cannot write '%s': %s\n", SHADER_NAME,
                        path, strerror(errno));
                return PPM_EXIT_ERROR;
            }
            const bool ok = emit(seq, path);
            fclose(seq);
            if (!ok) return PPM_EXIT_ERROR;
        }

        if (out && !emit(out, c.output == "-" ? "stdout" : c.output.c_str()))
            return PPM_EXIT_ERROR;

        if (!c.quiet && c.at < 0) {
            // Progress goes to stderr so it never contaminates piped frames.
            fprintf(stderr, "%s: frame %d/%d (%.0f%%)%s", SHADER_NAME, i + 1,
                    c.frames, 100.0f * (float)(i + 1) / (float)c.frames,
                    tty ? "\r" : "\n");
            fflush(stderr);
        }
    }

    if (out && out != stdout) fclose(out);
    else if (out) fflush(out);
    if (!c.quiet && tty) fprintf(stderr, "\n");

    return PPM_EXIT_OK;
}

#endif // PPMSHADER_HPP
