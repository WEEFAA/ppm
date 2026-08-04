// ===========================================================================
//  pipe.hpp -- ffmpeg plumbing and shared CLI scaffolding.
//
//  ffmpeg is used as a codec, nothing more: it decodes any container into a raw
//  PPM stream on the way in, and encodes a raw PPM stream into a container on
//  the way out. Everything between those two points is our own C++.
//
//      input.mp4 --[ffmpeg -f image2pipe]--> P6 stream --> analyze
//                                                            |
//      output.mp4 <--[ffmpeg -f ppm_pipe]-- P6 stream <-- synthesize
//
//  Both directions are one-way pipes, so popen() is sufficient and no fork/exec
//  plumbing is needed. Frames never touch disk unless asked for.
// ===========================================================================
#ifndef PPM_PIPE_HPP
#define PPM_PIPE_HPP

#include "frame.hpp"
#include "json.hpp"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <sys/stat.h>
#include <unistd.h>

namespace ppm {

// ---------------------------------------------------------------------------
// exit codes (docs/cli-design.json)
// ---------------------------------------------------------------------------
enum { EX_OK = 0, EX_FAIL = 1, EX_USAGE = 2 };

// ---------------------------------------------------------------------------
// diagnostics
// ---------------------------------------------------------------------------

struct Log {
    std::string prog;
    bool quiet = false;

    /// Everything here goes to stderr. stdout is reserved for requested data, so
    /// that a tool can be piped without its own chatter corrupting the stream.
    void note(const std::string &msg) const {
        if (!quiet) fprintf(stderr, "%s: %s\n", prog.c_str(), msg.c_str());
    }
    void warn(const std::string &msg) const {
        fprintf(stderr, "%s: warning: %s\n", prog.c_str(), msg.c_str());
    }
    void error(const std::string &msg) const {
        fprintf(stderr, "%s: %s\n", prog.c_str(), msg.c_str());
    }
    [[noreturn]] void die(const std::string &msg, int code = EX_FAIL) const {
        error(msg);
        exit(code);
    }
    [[noreturn]] void usage_die(const std::string &msg) const {
        error(msg);
        fprintf(stderr, "Try '%s --help' for more information.\n", prog.c_str());
        exit(EX_USAGE);
    }

    /// Progress is only meaningful to a human watching a terminal. In a log file
    /// or CI capture, a carriage-return progress line becomes thousands of
    /// unreadable lines, so it is suppressed when stderr is not a TTY.
    void progress(const std::string &label, int done, int total) const {
        if (quiet || !isatty(fileno(stderr))) return;
        fprintf(stderr, "\r%s: %s %d/%d (%.0f%%)   ", prog.c_str(), label.c_str(),
                done, total, total > 0 ? 100.0 * done / total : 0.0);
        fflush(stderr);
    }
    void progress_done() const {
        if (!quiet && isatty(fileno(stderr))) fprintf(stderr, "\r\033[K");
    }
};

// ---------------------------------------------------------------------------
// shell quoting
// ---------------------------------------------------------------------------

/// Single-quote a string for /bin/sh. popen runs its argument through a shell,
/// so any path containing a space, quote or semicolon would otherwise be split
/// or executed. Wrapping in single quotes and escaping embedded single quotes is
/// the only fully safe form.
inline std::string shq(const std::string &s) {
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') out += "'\\''";
        else out.push_back(c);
    }
    out += "'";
    return out;
}

// ---------------------------------------------------------------------------
// filesystem
// ---------------------------------------------------------------------------

inline bool file_exists(const std::string &p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0;
}
inline bool is_dir(const std::string &p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}
inline bool is_executable(const std::string &p) {
    return access(p.c_str(), X_OK) == 0;
}

/// mkdir -p
inline bool make_dirs(const std::string &path) {
    if (path.empty()) return true;
    std::string acc;
    size_t i = 0;
    if (path[0] == '/') { acc = "/"; i = 1; }
    while (i <= path.size()) {
        size_t j = path.find('/', i);
        if (j == std::string::npos) j = path.size();
        const std::string part = path.substr(i, j - i);
        if (!part.empty()) {
            if (!acc.empty() && acc.back() != '/') acc += '/';
            acc += part;
            if (!is_dir(acc) && mkdir(acc.c_str(), 0755) != 0 && !is_dir(acc)) return false;
        }
        if (j == path.size()) break;
        i = j + 1;
    }
    return true;
}

/// mkdir -p on the directory holding `path`.
///
/// Called wherever a file is about to be created from a user-supplied path, so
/// `-o out/nested/thing.mp4` works without the caller having made `out/nested`
/// first. A path with no directory component ("thing.mp4") is a no-op.
inline bool make_parent_dirs(const std::string &path) {
    const size_t slash = path.find_last_of('/');
    if (slash == std::string::npos || slash == 0) return true;
    return make_dirs(path.substr(0, slash));
}

inline std::string join_path(const std::string &a, const std::string &b) {
    if (a.empty()) return b;
    if (!a.empty() && a.back() == '/') return a + b;
    return a + "/" + b;
}

inline std::string basename_no_ext(const std::string &p) {
    size_t slash = p.find_last_of('/');
    std::string base = slash == std::string::npos ? p : p.substr(slash + 1);
    size_t dot = base.find_last_of('.');
    if (dot != std::string::npos && dot > 0) base = base.substr(0, dot);
    return base;
}

inline std::string numbered(const std::string &dir, const std::string &prefix,
                           int index, const char *ext) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%s%04d.%s", prefix.c_str(), index, ext);
    return join_path(dir, buf);
}

// ---------------------------------------------------------------------------
// locating ffmpeg
// ---------------------------------------------------------------------------

/// Prefer the vendored build: it is the version this project was developed
/// against, and it is guaranteed to have the ppm_pipe demuxer.
inline std::string find_tool(const std::string &name, const std::string &override_path,
                             const std::string &exe_dir) {
    if (!override_path.empty()) return override_path;

    // exe_dir is .../bin, so the repo root is one level up.
    std::vector<std::string> candidates;
    if (!exe_dir.empty()) {
        candidates.push_back(join_path(join_path(exe_dir, "../ffmpeg"), name));
        candidates.push_back(join_path(exe_dir, name));
    }
    candidates.push_back("./ffmpeg/" + name);
    for (const std::string &c : candidates)
        if (is_executable(c)) return c;

    // Fall back to PATH.
    const char *penv = getenv("PATH");
    if (penv) {
        std::string path(penv);
        size_t i = 0;
        while (i <= path.size()) {
            size_t j = path.find(':', i);
            if (j == std::string::npos) j = path.size();
            const std::string dir = path.substr(i, j - i);
            if (!dir.empty()) {
                const std::string c = join_path(dir, name);
                if (is_executable(c)) return c;
            }
            if (j == path.size()) break;
            i = j + 1;
        }
    }
    return "";
}

/// Directory containing this executable, so the vendored ffmpeg can be found
/// regardless of the current working directory.
inline std::string exe_dir(const char *argv0) {
    if (!argv0) return "";
    std::string p(argv0);
    size_t slash = p.find_last_of('/');
    if (slash == std::string::npos) return "";
    return p.substr(0, slash);
}

// ---------------------------------------------------------------------------
// probing
// ---------------------------------------------------------------------------

struct MediaInfo {
    int width = 0, height = 0;
    int nb_frames = 0;
    float fps = 0.0f;        ///< AVERAGE rate; the one to time output with
    float nominal_fps = 0.0f;///< r_frame_rate; an upper bound, often misleading
    double duration = 0.0;
    std::string codec;
    bool is_still = false;
    bool variable_rate = false;   ///< nominal and average disagree materially
};

/// Read stream properties with ffprobe. Returns false and sets `error` if the
/// input is unreadable, which is the check that keeps every later failure from
/// surfacing as a confusing decode error instead.
inline bool probe_media(const std::string &ffprobe, const std::string &path,
                        MediaInfo &out, std::string &error) {
    const std::string cmd =
        shq(ffprobe) + " -v error -select_streams v:0"
        " -show_entries stream=width,height,nb_frames,r_frame_rate,avg_frame_rate,codec_name"
        " -show_entries format=duration -of default=noprint_wrappers=1 " +
        shq(path) + " 2>/dev/null";

    FILE *f = popen(cmd.c_str(), "r");
    if (!f) { error = "cannot run ffprobe"; return false; }

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        const std::string key = line;
        std::string val = eq + 1;
        while (!val.empty() && (val.back() == '\n' || val.back() == '\r')) val.pop_back();

        if (key == "width") out.width = atoi(val.c_str());
        else if (key == "height") out.height = atoi(val.c_str());
        else if (key == "nb_frames") out.nb_frames = atoi(val.c_str());
        else if (key == "codec_name") out.codec = val;
        else if (key == "duration") out.duration = atof(val.c_str());
        else if (key == "r_frame_rate" || key == "avg_frame_rate") {
            // Rational, e.g. "30000/1001". Guard the denominator: a still image
            // reports 0/0 and would otherwise divide by zero.
            float rate = 0.0f;
            const size_t slash = val.find('/');
            if (slash != std::string::npos) {
                const double num = atof(val.substr(0, slash).c_str());
                const double den = atof(val.substr(slash + 1).c_str());
                rate = den > 0.0 ? (float)(num / den) : 0.0f;
            } else {
                rate = (float)atof(val.c_str());
            }
            if (key == "r_frame_rate") out.nominal_fps = rate;
            else out.fps = rate;
        }
    }

    // Prefer the AVERAGE rate. r_frame_rate is an upper bound on the timebase,
    // not the rate the file actually carries frames at, and for a
    // variable-rate capture the two differ a lot: one sample reported
    // r_frame_rate 120 while genuinely holding 270 frames over 4.69s, an
    // average of 57.5. Timing output at 120 would play it at twice speed.
    if (out.fps <= 0.0f) out.fps = out.nominal_fps;
    if (out.fps > 0.0f && out.nominal_fps > 0.0f)
        out.variable_rate = fabs(out.nominal_fps - out.fps) > out.fps * 0.05f;
    const int rc = pclose(f);
    if (rc != 0 || out.width <= 0 || out.height <= 0) {
        error = "cannot read video stream from '" + path + "'";
        return false;
    }
    // A single-frame input with no meaningful rate is a still image.
    out.is_still = (out.nb_frames == 1) || (out.fps <= 0.0f && out.duration <= 0.0);
    return true;
}

// ---------------------------------------------------------------------------
// per-frame timing, from the container
// ---------------------------------------------------------------------------

/// What the container says about one frame.
///
/// Read rather than derived, and worth reading: for variable-rate material the
/// per-frame duration IS the exposure sheet. A drawing held for two ticks reports
/// twice the base duration, so the timing chart can be recovered exactly instead
/// of inferred from pixel differences -- which is inference about something the
/// decode has usually already flattened away.
struct FrameTiming {
    double pts = 0.0;        ///< presentation time, seconds
    double duration = 0.0;   ///< seconds this frame is shown for
    char pict_type = '?';    ///< 'I', 'P', 'B'
    bool key_frame = false;
};

/// Read per-frame timings with ffprobe. Cheap: no pixels are decoded.
///
/// Uses ffprobe's JSON rather than csv because csv emits fields in ffprobe's own
/// internal order, not the order requested -- so positional parsing silently
/// reads the wrong column. JSON is self-describing, and we already have a parser.
inline bool probe_frame_timings(const std::string &ffprobe, const std::string &path,
                                std::vector<FrameTiming> &out, std::string &error) {
    out.clear();
    const std::string cmd =
        shq(ffprobe) + " -v error -select_streams v:0"
        " -show_entries frame=pts_time,duration_time,pict_type,key_frame"
        " -of json " + shq(path) + " 2>/dev/null";

    FILE *f = popen(cmd.c_str(), "r");
    if (!f) { error = "cannot run ffprobe for frame timings"; return false; }
    std::string text;
    char buf[16384];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) text.append(buf, n);
    const int rc = pclose(f);
    if (rc != 0 || text.empty()) { error = "ffprobe produced no frame timings"; return false; }

    std::string perr;
    const json::Value root = json::parse(text, perr);
    if (!perr.empty()) { error = "cannot parse ffprobe frame JSON: " + perr; return false; }

    const json::Value &frames = root["frames"];
    out.reserve(frames.size());
    for (size_t i = 0; i < frames.size(); ++i) {
        const json::Value &fr = frames[i];
        FrameTiming t;
        // ffprobe emits these as STRINGS, so as_num is required, not defensive.
        t.pts = fr["pts_time"].as_num();
        t.duration = fr["duration_time"].as_num();
        const std::string pt = fr["pict_type"].text();
        t.pict_type = pt.empty() ? '?' : pt[0];
        t.key_frame = fr["key_frame"].as_num() != 0.0;
        out.push_back(t);
    }
    if (out.empty()) { error = "ffprobe reported no frames"; return false; }
    return true;
}

// ---------------------------------------------------------------------------
// decoding
// ---------------------------------------------------------------------------

struct DecodeOptions {
    int scale_w = 0;        ///< 0 = native
    int scale_h = 0;
    float fps = 0.0f;       ///< 0 = native
    int max_frames = 0;     ///< 0 = all
};

/// Reads a media file as a stream of frames.
class Decoder {
public:
    ~Decoder() { close(); }

    bool open(const std::string &ffmpeg, const std::string &path,
              const DecodeOptions &opt, std::string &error) {
        std::string filters;
        if (opt.fps > 0.0f) {
            char buf[64];
            snprintf(buf, sizeof(buf), "fps=%g", opt.fps);
            filters = buf;
        }
        if (opt.scale_w > 0 && opt.scale_h > 0) {
            char buf[128];
            // force_original_aspect_ratio=decrease + pad would letterbox; plain
            // scale is right here because analysis is aspect-normalised anyway.
            snprintf(buf, sizeof(buf), "scale=%d:%d:flags=lanczos", opt.scale_w, opt.scale_h);
            if (!filters.empty()) filters += ",";
            filters += buf;
        }

        std::string cmd = shq(ffmpeg) + " -v error -nostdin -i " + shq(path);
        if (!filters.empty()) cmd += " -vf " + shq(filters);
        if (opt.max_frames > 0) {
            char buf[32];
            snprintf(buf, sizeof(buf), " -frames:v %d", opt.max_frames);
            cmd += buf;
        }
        // -fps_mode passthrough emits exactly the frames the file contains.
        //
        // Without it ffmpeg conforms the output to a constant rate taken from
        // r_frame_rate, DUPLICATING frames to fill the gaps. On a
        // variable-rate capture this is not a small effect: a 270-frame source
        // with r_frame_rate 120 and an average of 57.5 fps decoded as 563
        // frames, doubling all downstream work. Worse, the duplicates read as
        // held frames, so the metadata confidently reported the sequence as
        // animated "on twos" with 55% holds -- describing an artefact of our own
        // decode as if it were the exposure sheet.
        //
        // It is an OUTPUT option and must appear after -i.
        if (opt.fps <= 0.0f) cmd += " -fps_mode passthrough";

        // image2pipe + ppm gives concatenated P6 frames, which our reader splits
        // by parsing each header. No container, no index, no temp files.
        cmd += " -f image2pipe -c:v ppm -";

        f_ = popen(cmd.c_str(), "r");
        if (!f_) { error = "cannot start ffmpeg to decode '" + path + "'"; return false; }
        return true;
    }

    /// Read the next frame. Returns false at end of stream; `error` is non-empty
    /// only on malformed data.
    bool next(Frame &out, std::string &error) {
        if (!f_) return false;
        return read_ppm(f_, out, error);
    }

    int close() {
        if (!f_) return 0;
        const int rc = pclose(f_);
        f_ = nullptr;
        return rc;
    }

private:
    FILE *f_ = nullptr;
};

// ---------------------------------------------------------------------------
// encoding
// ---------------------------------------------------------------------------

struct EncodeOptions {
    float fps = 30.0f;
    std::string bitrate;      ///< empty = derive from resolution
    std::string codec;        ///< empty = choose by container
};

/// True if this ffmpeg build has the named encoder.
inline bool has_encoder(const std::string &ffmpeg, const std::string &name) {
    const std::string cmd = shq(ffmpeg) + " -hide_banner -encoders 2>/dev/null";
    FILE *f = popen(cmd.c_str(), "r");
    if (!f) return false;
    char line[512];
    bool found = false;
    const std::string needle = " " + name + " ";
    while (fgets(line, sizeof(line), f))
        if (strstr(line, needle.c_str())) { found = true; break; }
    pclose(f);
    return found;
}

inline std::string lower_ext(const std::string &path) {
    const size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return "";
    std::string e = path.substr(dot + 1);
    for (char &c : e) c = (char)tolower((unsigned char)c);
    return e;
}

/// Writes frames to a container, choosing an encoder from the extension.
class Encoder {
public:
    ~Encoder() { close(); }

    bool open(const std::string &ffmpeg, const std::string &out_path, int w, int h,
              const EncodeOptions &opt, std::string &error) {
        // ffmpeg will not create a missing directory, and its error for one is
        // opaque, so make the parent here rather than let it fail downstream.
        if (!make_parent_dirs(out_path)) {
            error = "cannot create the directory for '" + out_path + "'";
            return false;
        }
        const std::string ext = lower_ext(out_path);

        std::string bitrate = opt.bitrate;
        if (bitrate.empty()) {
            // ~0.08 bits per pixel per second. Synthetic and reconstructed
            // imagery compresses much better than camera footage, so this is
            // visually clean where the same figure would not be for live action.
            const double bps = (double)w * h * opt.fps * 0.08;
            char buf[32];
            snprintf(buf, sizeof(buf), "%dk", (int)(bps / 1000.0));
            bitrate = buf;
        }

        std::string args;
        if (!opt.codec.empty()) {
            args = " -c:v " + shq(opt.codec) + " -b:v " + bitrate;
        } else if (ext == "mp4" || ext == "mov") {
            if (has_encoder(ffmpeg, "h264_videotoolbox"))
                args = " -c:v h264_videotoolbox -b:v " + bitrate + " -allow_sw 1";
            else if (has_encoder(ffmpeg, "libx264"))
                args = " -c:v libx264 -preset medium -crf 18";
            else if (has_encoder(ffmpeg, "mpeg4"))
                args = " -c:v mpeg4 -b:v " + bitrate;
            else { error = "no encoder available for ." + ext; return false; }
            args += " -pix_fmt yuv420p -color_range tv -movflags +faststart";
        } else if (ext == "mkv") {
            args = " -c:v ffv1 -level 3 -pix_fmt rgb24";
        } else if (ext == "gif") {
            // A palette stage is required; without it GIF falls back to a default
            // palette and every gradient bands severely.
            args = " -filter_complex " +
                   shq("split[a][b];[a]palettegen=stats_mode=diff[p];"
                       "[b][p]paletteuse=dither=bayer:bayer_scale=3");
        } else if (ext == "png") {
            if (!has_encoder(ffmpeg, "png")) {
                error = "this ffmpeg has no png encoder (needs zlib); use .jpg";
                return false;
            }
            args = " -frames:v 1 -c:v png";
        } else if (ext == "jpg" || ext == "jpeg") {
            args = " -frames:v 1 -c:v mjpeg -q:v 2";
        } else if (ext == "ppm") {
            // Passthrough: no encoder, just the raw stream.
            make_parent_dirs(out_path);
            raw_ = fopen(out_path.c_str(), "wb");
            if (!raw_) { error = "cannot write '" + out_path + "'"; return false; }
            return true;
        } else {
            error = "unsupported output format '." + ext + "'";
            return false;
        }

        char fpsbuf[32];
        snprintf(fpsbuf, sizeof(fpsbuf), "%g", opt.fps);
        // -framerate BEFORE -i sets the INPUT rate. Placing it after would set the
        // output rate instead and resample by duplicating frames, lengthening the
        // video: 30 frames in becomes 72 out at the default input rate of 25.
        const std::string cmd = shq(ffmpeg) + " -v error -nostdin -f ppm_pipe -framerate " +
                                fpsbuf + " -i -" + args + " -y " + shq(out_path);
        f_ = popen(cmd.c_str(), "w");
        if (!f_) { error = "cannot start ffmpeg to encode '" + out_path + "'"; return false; }
        return true;
    }

    bool write(const Frame &fr) {
        FILE *dst = f_ ? f_ : raw_;
        if (!dst) return false;
        return write_ppm(dst, fr);
    }

    /// Returns the child's exit status, or 0 for passthrough.
    int close() {
        int rc = 0;
        if (f_) { rc = pclose(f_); f_ = nullptr; }
        if (raw_) { fclose(raw_); raw_ = nullptr; }
        return rc;
    }

private:
    FILE *f_ = nullptr;
    FILE *raw_ = nullptr;
};

// ---------------------------------------------------------------------------
// output layout
// ---------------------------------------------------------------------------

/// The three deliverables every tool produces: frames, video, parameters.
///
///     <dir>/parameters.json      what was measured and what was rendered
///     <dir>/<name>.mp4           the video
///     <dir>/frames/frame0000.ppm regenerated frames, with --keep-frames
///     <dir>/essence/e0000.ppm    the low-frequency fields, with --keep-essence
struct OutputSet {
    std::string dir;
    std::string name = "out";
    std::string video_ext = "mp4";
    bool keep_frames = false;
    bool keep_essence = false;

    bool inspect = false;

    std::string video_path() const { return join_path(dir, name + "." + video_ext); }
    std::string params_path() const { return join_path(dir, "parameters.json"); }
    std::string metadata_path() const { return join_path(dir, "metadata.json"); }
    std::string frames_dir() const { return join_path(dir, "frames"); }
    std::string essence_dir() const { return join_path(dir, "essence"); }
    std::string inspect_dir() const { return join_path(dir, "inspect"); }
    std::string inspect_frames_dir() const { return join_path(inspect_dir(), "frames"); }

    bool prepare(std::string &error) const {
        if (!make_dirs(dir)) { error = "cannot create directory '" + dir + "'"; return false; }
        if (keep_frames && !make_dirs(frames_dir())) {
            error = "cannot create '" + frames_dir() + "'";
            return false;
        }
        if (keep_essence && !make_dirs(essence_dir())) {
            error = "cannot create '" + essence_dir() + "'";
            return false;
        }
        if (inspect && !make_dirs(inspect_frames_dir())) {
            error = "cannot create '" + inspect_frames_dir() + "'";
            return false;
        }
        return true;
    }
};

// ---------------------------------------------------------------------------
// inspector frame export
// ---------------------------------------------------------------------------

/// Export browser-viewable stills from an encoded video.
///
/// The inspector needs frames a browser can display, and PPM is not one of those.
/// Rather than run a second encoder alongside the first, the stills are pulled
/// back out of the video we just wrote: one extra process, no extra render pass.
/// The mp4 round trip costs a little fidelity, but the inspector's job is to show
/// what was delivered, and the video IS what was delivered.
inline bool export_inspect_frames(const std::string &ffmpeg, const std::string &video,
                                  const std::string &dir, int quality,
                                  std::string &error) {
    char q[16];
    snprintf(q, sizeof(q), "%d", quality < 2 ? 2 : (quality > 31 ? 31 : quality));
    if (!make_dirs(dir)) {
        error = "cannot create '" + dir + "'";
        return false;
    }
    const std::string pattern = join_path(dir, "f%04d.jpg");
    const std::string cmd = shq(ffmpeg) + " -v error -nostdin -i " + shq(video) +
                            " -q:v " + q + " -start_number 0 -y " + shq(pattern);
    const int rc = system(cmd.c_str());
    if (rc != 0) {
        error = "could not export inspector frames from '" + video + "'";
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// small argument helpers
// ---------------------------------------------------------------------------

inline bool parse_size(const std::string &s, int &w, int &h) {
    return sscanf(s.c_str(), "%dx%d", &w, &h) == 2 && w > 0 && h > 0;
}

inline int arg_int(const Log &log, const char *flag, const char *val) {
    char *end = nullptr;
    const long v = strtol(val, &end, 10);
    if (!end || *end != '\0') log.usage_die(std::string(flag) + ": not an integer: '" + val + "'");
    return (int)v;
}

inline float arg_float(const Log &log, const char *flag, const char *val) {
    char *end = nullptr;
    const float v = strtof(val, &end);
    if (!end || *end != '\0') log.usage_die(std::string(flag) + ": not a number: '" + val + "'");
    return v;
}

} // namespace ppm

#endif // PPM_PIPE_HPP
