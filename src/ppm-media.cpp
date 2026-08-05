// ===========================================================================
//  ppm-media -- analyse a still image, then generate a moving clip from it.
//
//  CLI 3 of three. Semantics: ONE IMAGE IN, VIDEO OUT.
//
//      image.jpg --[ffmpeg]--> 1 frame --> analyze --> parameters
//                                                         |
//      output.mp4 <--[ffmpeg]-- frames <-- synthesize <----+
//
//  The difference from ppm-video is where the motion comes from. There, motion
//  was measured -- one essence grid per frame already contains it. Here there is
//  a single grid and no motion to measure, so the camera move has to be
//  INFERRED from the picture. See scene_from_still in derive.hpp for the
//  reasoning; the short version is that the move is derived from where the light
//  sits in the frame, and every choice is written into parameters.json so it can
//  be overridden rather than guessed at again.
// ===========================================================================
#include "analyze.hpp"
#include "derive.hpp"
#include "inspector_html.hpp"
#include "metadata.hpp"
#include "pipe.hpp"
#include "scene.hpp"

#include <thread>

using namespace ppm;

static void usage(const char *prog) {
    printf(
        "%s -- turn a still image into a generated clip\n"
        "\n"
        "USAGE\n"
        "  %s INPUT [options]\n"
        "\n"
        "INPUT may be any image, or a video (use --at to pick the frame).\n"
        "Writes frames, a video, and the parameters that produced them.\n"
        "\n"
        "OPTIONS\n"
        "  -o, --out DIR         output directory (default: out/<input name>)\n"
        "      --size WxH        output resolution (default: source)\n"
        "      --fps N           frame rate (default 30)\n"
        "      --frames N        frame count (default 120)\n"
        "      --duration SEC    set frame count from a duration instead\n"
        "      --at N            take frame N of a video input (default 0)\n"
        "  -f, --fidelity NAME   draft | balanced | faithful (default) | max\n"
        "      --essence N       grid width explicitly, overriding --fidelity\n"
        "      --detail F        procedural detail restored (default: from preset)\n"
        "      --grain F         grain amount; omit to derive it from the source\n"
        "      --move F          camera move amount, 0..2 (default 1; 0 = static)\n"
        "      --no-loop         do not ping-pong the move back to its start\n"
        "      --samples N       supersampling, 1..8 (default 1)\n"
        "      --stylize         grade using the measured character of the source\n"
        "      --analyze-only    write parameters.json and stop\n"
        "      --report LEVEL    parameters detail: 0 summary, 1 per-frame, 2 all\n"
        "      --keep-frames     also write the generated frames as PPM\n"
        "      --inspect         also emit a browsable frame inspector\n"
        "      --flat            also emit ffprobe-style dotted key=value files\n"
        "      --keep-essence    also write the essence grid as PPM\n"
        "      --video-ext EXT   mp4 (default), mkv lossless, gif\n"
        "      --ffmpeg PATH     ffmpeg binary to use\n"
        "      --ffprobe PATH    ffprobe binary to use\n"
        "  -j, --threads N       worker threads (default: all cores)\n"
        "  -q, --quiet\n"
        "  -h, --help\n"
        "\n"
        "NOTES\n"
        "  A fidelity preset is a target reconstruction ERROR; the grid width is\n"
        "  measured from the content and reported. A still can afford a wide grid:\n"
        "  the cost is one grid, not one per frame.\n"
        "  The clip loops seamlessly by default, because the move eases out and\n"
        "  back rather than travelling in one direction.\n"
        "\n"
        "EXAMPLES\n"
        "  %s photo.jpg\n"
        "  %s photo.jpg --duration 6 --move 1.5 --essence 160 -o out/photo\n"
        "  %s clip.mp4 --at 42 --frames 90 --no-loop\n"
        "\n"
        "EXIT STATUS\n"
        "  0 success    1 failure    2 usage error\n",
        prog, prog, prog, prog, prog);
}

int main(int argc, char **argv) {
    Log log{"ppm-media", false};

    std::string input, out_dir, ffmpeg_override, ffprobe_override, video_ext = "mp4";
    std::string fidelity_name = "faithful";
    int out_w = 0, out_h = 0, frames = 120, at = 0, samples = 1, report = 1, threads = 0;
    int essence_override = 0;
    float fps = 30.0f, detail_override = -1.0f, grain = -1.0f, move = 1.0f, duration = 0.0f;
    bool loop = true, stylize = false, analyze_only = false;
    bool keep_frames = false, keep_essence = false, inspect = false, flat_out = false;

    if (argc < 2) { usage(log.prog.c_str()); return EX_USAGE; }

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> const char * {
            if (i + 1 >= argc) log.usage_die("missing value for " + a);
            return argv[++i];
        };
        if (a == "-h" || a == "--help") { usage(log.prog.c_str()); return EX_OK; }
        else if (a == "-o" || a == "--out") out_dir = next();
        else if (a == "--size") {
            const char *v = next();
            if (!parse_size(v, out_w, out_h)) log.usage_die(std::string("--size expects WxH, got '") + v + "'");
        }
        else if (a == "--fps") fps = arg_float(log, "--fps", next());
        else if (a == "--frames") frames = arg_int(log, "--frames", next());
        else if (a == "--duration") duration = arg_float(log, "--duration", next());
        else if (a == "--at") at = arg_int(log, "--at", next());
        else if (a == "-f" || a == "--fidelity") fidelity_name = next();
        else if (a == "--essence") essence_override = arg_int(log, "--essence", next());
        else if (a == "--detail") detail_override = arg_float(log, "--detail", next());
        else if (a == "--grain") grain = arg_float(log, "--grain", next());
        else if (a == "--move") move = arg_float(log, "--move", next());
        else if (a == "--samples") samples = arg_int(log, "--samples", next());
        else if (a == "--report") report = arg_int(log, "--report", next());
        else if (a == "-j" || a == "--threads") threads = arg_int(log, "--threads", next());
        else if (a == "--video-ext") video_ext = next();
        else if (a == "--ffmpeg") ffmpeg_override = next();
        else if (a == "--ffprobe") ffprobe_override = next();
        else if (a == "--no-loop") loop = false;
        else if (a == "--stylize") stylize = true;
        else if (a == "--analyze-only") analyze_only = true;
        else if (a == "--keep-frames") keep_frames = true;
        else if (a == "--keep-essence") keep_essence = true;
        else if (a == "--inspect") inspect = true;
        else if (a == "--flat") flat_out = true;
        else if (a == "-q" || a == "--quiet") log.quiet = true;
        else if (!a.empty() && a[0] == '-') log.usage_die("unknown option '" + a + "'");
        else if (input.empty()) input = a;
        else log.usage_die("unexpected argument '" + a + "'");
    }

    if (input.empty()) log.usage_die("no input given");
    if (!file_exists(input)) log.die("no such file: " + input);
    if (duration > 0.0f) frames = (int)(duration * fps + 0.5f);
    if (frames <= 0) log.usage_die("--frames must be positive");
    if (essence_override != 0 && (essence_override < 4 || essence_override > 4096))
        log.usage_die("--essence must be 4..4096");
    const Fidelity *fid = find_fidelity(fidelity_name);
    if (!fid) {
        std::string known;
        size_t fn = 0;
        const Fidelity *fp = fidelity_presets(fn);
        for (size_t i = 0; i < fn; ++i) { known += " "; known += fp[i].name; }
        log.usage_die("unknown fidelity '" + fidelity_name + "'; known:" + known);
    }
    if (move < 0.0f || move > 4.0f) log.usage_die("--move must be 0..4");
    if (samples < 1 || samples > 8) log.usage_die("--samples must be 1..8");
    if (at < 0) log.usage_die("--at must be >= 0");
    if (threads <= 0) {
        const unsigned hc = std::thread::hardware_concurrency();
        threads = hc ? (int)hc : 1;
    }

    const std::string dir = exe_dir(argv[0]);
    const std::string FFMPEG = find_tool("ffmpeg", ffmpeg_override, dir);
    const std::string FFPROBE = find_tool("ffprobe", ffprobe_override, dir);
    if (FFMPEG.empty())
        log.die("no ffmpeg found. Build the vendored copy with 'make ffmpeg', or pass --ffmpeg PATH.");

    MediaInfo info;
    std::string err;
    if (!FFPROBE.empty()) {
        if (!probe_media(FFPROBE, input, info, err)) log.die(err);
        char buf[256];
        snprintf(buf, sizeof(buf), "input %dx%d %s%s", info.width, info.height,
                 info.codec.c_str(), info.is_still ? " (still)" : "");
        log.note(buf);
    }

    OutputSet outs;
    outs.dir = out_dir.empty() ? join_path("out", basename_no_ext(input)) : out_dir;
    outs.name = basename_no_ext(input) + "-clip";
    outs.video_ext = video_ext;
    outs.keep_frames = keep_frames;
    outs.keep_essence = keep_essence;
    outs.inspect = inspect;
    if (!outs.prepare(err)) log.die(err);

    // --- decode one frame --------------------------------------------------
    AnalyzeConfig cfg;
    DecodeOptions dopt;
    dopt.max_frames = at + 1;   // decode up to the wanted frame, then stop

    Decoder dec;
    if (!dec.open(FFMPEG, input, dopt, err)) log.die(err);

    Frame frame, wanted;
    int seen = 0;
    for (;;) {
        std::string derr;
        if (!dec.next(frame, derr)) {
            if (!derr.empty()) log.die("decode failed: " + derr);
            break;
        }
        if (seen == at) wanted = frame;
        ++seen;
        if (seen > at) break;
    }
    dec.close();

    if (wanted.empty()) {
        if (seen == 0) log.die("no frames decoded from '" + input + "'");
        log.die("--at " + std::to_string(at) + " is out of range; input has " +
                std::to_string(seen) + " frame(s)");
    }
    // A still is always analysed at native size: there is only one frame, so
    // there is nothing to gain by decoding it small, and the error metric is only
    // truthful when measured against full-resolution pixels.
    //
    // The grid width is chosen from the measured content rather than fixed,
    // because no single width serves both continuous-tone and hard-edged sources.
    // A still can also afford a wide grid: the cost is one grid, not one per frame.
    int chosen = 0;
    float achieved_err = 0.0f;
    if (essence_override > 0) {
        chosen = std::min(essence_override, wanted.width);
    } else if (fid->target_error <= 0.0f) {
        chosen = wanted.width;   // "max": grid is the frame, reconstruction is exact
    } else {
        chosen = choose_essence_width(wanted, fid->target_error, 32, wanted.width, nullptr);
    }
    cfg.fit_essence(wanted.width, wanted.height, chosen);
    achieved_err = essence_error_at(wanted, cfg.essence_w);
    {
        char buf[256];
        snprintf(buf, sizeof(buf),
                 "using frame %d (%dx%d), fidelity %s: essence grid %dx%d, measured error %.4f",
                 at, wanted.width, wanted.height,
                 essence_override > 0 ? "(explicit)" : fid->name,
                 cfg.essence_w, cfg.essence_h, achieved_err);
        log.note(buf);
    }

    // --- analyse -----------------------------------------------------------
    SequenceAnalysis seq;
    seq.fps = fps;
    seq.frames.push_back(analyze_frame(wanted, cfg, 0));
    seq.motion.resize(1);
    seq.global_palette = extract_palette(wanted, cfg.palette_size, 2);
    summarise(seq);

    const FrameStats &s0 = seq.frames[0];
    if (keep_essence)
        write_ppm_path(numbered(outs.essence_dir(), "e", 0, "ppm"), s0.essence);
    {
        char buf[256];
        snprintf(buf, sizeof(buf),
                 "measured: luma %.3f, contrast %.3f, saturation %.3f, edges %.4f, "
                 "centroid (%.2f, %.2f), detail lost %.4f",
                 s0.luma_mean, s0.contrast, s0.saturation_mean, s0.edge_density,
                 s0.centroid_x, s0.centroid_y, s0.high_freq_ratio);
        log.note(buf);
    }

    // --- derive ------------------------------------------------------------
    DeriveOptions dopts;
    dopts.out_width = out_w > 0 ? out_w : (info.width > 0 ? info.width : wanted.width);
    dopts.out_height = out_h > 0 ? out_h : (info.height > 0 ? info.height : wanted.height);
    dopts.fps = fps;
    dopts.frames = frames;
    dopts.samples = samples;
    dopts.detail = detail_override >= 0.0f ? detail_override : fid->detail;
    dopts.stylize = stylize;
    dopts.grain = grain;

    json::Value params = scene_from_still(seq, cfg, dopts, move, loop);
    params.set("analysis", analysis_json(seq, cfg, report));

    // Same shape as ppm-video's, so the inspector and any downstream consumer can
    // read fidelity from one place regardless of which tool produced the output.
    json::Value fidj = json::Value::obj();
    fidj.set("preset", json::Value(essence_override > 0 ? std::string("explicit")
                                                        : std::string(fid->name)));
    fidj.set("target_error", json::Value((double)fid->target_error));
    fidj.set("achieved_error", json::Value((double)achieved_err));
    fidj.set("essence", json::int_pair(cfg.essence_w, cfg.essence_h));
    fidj.set("measured_at", json::int_pair(wanted.width, wanted.height));
    fidj.set("measured_at_native", json::Value(true));   // a still is never downscaled
    params.set("fidelity", fidj);

    json::Value prov = json::Value::obj();
    prov.set("tool", json::Value(std::string("ppm-media")));
    prov.set("input", json::Value(input));
    prov.set("source_frame", json::Value((double)at));
    params.set("provenance", prov);

    if (!json::write_file(outs.params_path(), params))
        log.die("cannot write " + outs.params_path());
    log.note("wrote " + outs.params_path());
    if (flat_out) {
        const std::string fp = join_path(outs.dir, "parameters.flat");
        if (json::write_flat_file(fp, params, "parameters")) log.note("wrote " + fp);
        else log.warn("cannot write " + fp);
    }

    // Frame metadata is a pure observer: it never feeds synthesis, so it is
    // always generated and kept in its own file. Mixing it into parameters.json
    // would bury the renderable scene under inspection data.
    {
        const json::Value md = meta::sequence_metadata(seq, meta::MetaConfig());
        if (!json::write_file(outs.metadata_path(), md))
            log.die("cannot write " + outs.metadata_path());
        if (flat_out) {
            const std::string fp = join_path(outs.dir, "metadata.flat");
            if (json::write_flat_file(fp, md, "metadata")) log.note("wrote " + fp);
            else log.warn("cannot write " + fp);
        }
        const json::Value &sm = md["summary"];
        char buf[256];
        if (sm["effective_fps"].is_num())
            snprintf(buf, sizeof(buf),
                     "metadata: %d shot(s), %s (effective %.1f fps), %.0f%% holds, %d key candidate(s)",
                     sm["shots"].integer(), sm["step_label"].text().c_str(),
                     sm["effective_fps"].num(), 100.0 * sm["hold_fraction"].num(),
                     sm["key_candidates"].integer());
        else
            snprintf(buf, sizeof(buf),
                     "metadata: %d shot(s), exposure undetermined (%.0f%% holds), %d key candidate(s)",
                     sm["shots"].integer(), 100.0 * sm["hold_fraction"].num(),
                     sm["key_candidates"].integer());
        log.note(buf);
        const json::Value &rv = sm["review"];
        if (rv["frames_with_neutral_shadows"].num() > 0.5)
            log.note("  note: shadows read as near-neutral in most frames");
        if (rv["frames_murky"].num() > 0.5)
            log.note("  note: palette is muted with little value separation");
        if (rv["frames_clipped"].num() > 0.25)
            log.note("  note: highlights clip in a quarter of frames or more");
        log.note("wrote " + outs.metadata_path());
    }

    {
        const json::Value &d = params["derived_motion"];
        char buf[200];
        snprintf(buf, sizeof(buf), "inferred move: pan direction (%+.2f, %+.2f), amount %.2f%s",
                 d["pan_direction"][0].flt(), d["pan_direction"][1].flt(),
                 d["move_amount"].flt(), loop ? ", looping" : "");
        log.note(buf);
    }

    if (analyze_only) return EX_OK;

    // --- synthesise --------------------------------------------------------
    std::string serr;
    scene::Scene sc = scene::Scene::from_json(params, serr);
    if (!serr.empty()) log.die("derived scene is invalid: " + serr);

    // One grid, reused for every frame. The motion comes from the scene
    // transform, which samples that single grid at moving coordinates.
    scene::ReconstructSource src;
    src.essence.push_back(s0.essence);
    src.high_freq.push_back(s0.high_freq_ratio);

    EncodeOptions eopt;
    eopt.fps = sc.fps;
    Encoder enc;
    if (!enc.open(FFMPEG, outs.video_path(), sc.width, sc.height, eopt, err)) log.die(err);

    {
        char buf[160];
        snprintf(buf, sizeof(buf), "rendering %d frames at %dx%d, %d thread(s)",
                 sc.frames, sc.width, sc.height, threads);
        log.note(buf);
    }

    Frame out;
    for (int i = 0; i < sc.frames; ++i) {
        scene::render_frame(sc, src, i, out, threads);
        if (!enc.write(out)) {
            enc.close();
            log.die("write to encoder failed at frame " + std::to_string(i));
        }
        if (keep_frames)
            write_ppm_path(numbered(outs.frames_dir(), "frame", i, "ppm"), out);
        log.progress("rendering", i + 1, sc.frames);
    }
    log.progress_done();

    const int rc = enc.close();
    if (rc != 0) log.die("ffmpeg failed while encoding (exit " + std::to_string(rc) + ")");

    log.note("wrote " + outs.video_path());

    if (inspect) {
        // Stills are pulled back out of the video just written, so the inspector
        // shows exactly what was delivered rather than a parallel render.
        std::string ierr;
        if (!export_inspect_frames(FFMPEG, outs.video_path(), outs.inspect_frames_dir(), 4, ierr))
            log.warn(ierr);
        else if (!write_inspector_html(join_path(outs.inspect_dir(), "index.html")))
            log.warn("cannot write the inspector page");
        else
            log.note("wrote " + outs.inspect_dir() + "/index.html  (serve the output "
                     "directory over HTTP; browsers block file:// reads)");
    }
    return EX_OK;
}
