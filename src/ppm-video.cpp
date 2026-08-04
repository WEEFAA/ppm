// ===========================================================================
//  ppm-video -- inspect a video frame by frame, then regenerate it on CPU.
//
//  CLI 1 of three. Semantics: VIDEO IN, VIDEO OUT, with everything between
//  measured and rewritten by our own code.
//
//      input.mp4 --[ffmpeg]--> frames --> analyze --> parameters
//                                                        |
//      output.mp4 <--[ffmpeg]-- frames <-- synthesize <---+
//
//  ffmpeg is used only as a codec. Every pixel that comes out was produced by
//  analyze.hpp measuring the input and scene.hpp rebuilding from those
//  measurements -- no frame of the source is ever copied through.
//
//  TWO PASSES, for two reasons that both turned out to matter:
//
//    1. Memory. The essence grid needed for faithful reconstruction is large --
//       on a 920x1200 source a faithful grid is ~4.7 MB per frame, so holding a
//       270-frame sequence would cost 1.26 GB. Streaming decode -> render keeps
//       memory flat regardless of length.
//
//    2. Honesty. The grid width cannot be chosen until the content has been
//       measured, and the error metric is only truthful when measured against
//       full-resolution pixels. Pass 1 measures, pass 2 renders.
//
//  Deliverables, all into one directory: frames, video, parameters, metadata.
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
        "%s -- analyse a video and regenerate it on CPU\n"
        "\n"
        "USAGE\n"
        "  %s INPUT [options]\n"
        "\n"
        "Writes frames, a video, parameters and metadata into one directory.\n"
        "\n"
        "OPTIONS\n"
        "  -o, --out DIR          output directory (default: out/<input name>)\n"
        "  -f, --fidelity NAME    draft | balanced | faithful (default) | max\n"
        "      --essence N        set the grid width explicitly, overriding --fidelity\n"
        "      --detail F         procedural detail restored, 0..1 (default: from preset)\n"
        "      --grain F          grain amount; omit to measure it from the source\n"
        "      --size WxH         output resolution (default: source)\n"
        "      --fps N            output frame rate (default: source)\n"
        "      --frames N         process at most N frames\n"
        "      --analysis-width N cap the resolution frames are measured at (default 1920)\n"
        "      --samples N        supersampling, 1..8 (default 1)\n"
        "      --stylize          grade using the measured character of the source\n"
        "      --analyze-only     write parameters and metadata, then stop\n"
        "      --report LEVEL     parameters detail: 0 summary, 1 per-frame, 2 all\n"
        "      --keep-frames      also write the regenerated frames as PPM\n"
        "      --keep-essence     also write the essence grids as PPM\n"
        "      --inspect          also emit a browsable frame inspector\n"
        "      --flat             also emit ffprobe-style dotted key=value files\n"
        "      --video-ext EXT    mp4 (default), mkv lossless, gif\n"
        "      --ffmpeg PATH      ffmpeg binary to use\n"
        "  -j, --threads N        worker threads (default: all cores)\n"
        "  -q, --quiet\n"
        "  -h, --help\n"
        "\n"
        "FIDELITY\n"
        "  A preset is a target RECONSTRUCTION ERROR, not a grid width, because no\n"
        "  single width serves all content: a grid that is faithful on a gradient is\n"
        "  about six times too narrow for line art. The grid width is measured from\n"
        "  the actual frames and reported.\n"
        "\n"
        "    draft      error <= 0.040   fast, visibly abstracted\n"
        "    balanced   error <= 0.020   soft but recognisable\n"
        "    faithful   error <= 0.008   close to the source (default)\n"
        "    max        exact            grid at source width; pixel-exact\n"
        "\n"
        "EXAMPLES\n"
        "  %s clip.mov\n"
        "  %s clip.mov -f max -o out/exact\n"
        "  %s clip.mov -f draft --frames 30      # quick look\n"
        "  %s clip.mov --analyze-only --report 2\n"
        "\n"
        "EXIT STATUS\n"
        "  0 success    1 failure    2 usage error\n",
        prog, prog, prog, prog, prog, prog);
}

int main(int argc, char **argv) {
    Log log{"ppm-video", false};

    std::string input, out_dir, ffmpeg_override, video_ext = "mp4";
    std::string fidelity_name = "faithful";
    int out_w = 0, out_h = 0, max_frames = 0, samples = 1, report = 1, threads = 0;
    int essence_override = 0, analysis_width = 1920;
    float fps = 0.0f, detail_override = -1.0f, grain = -1.0f;
    bool stylize = false, analyze_only = false, keep_frames = false;
    bool keep_essence = false, inspect = false, flat_out = false;

    if (argc < 2) { usage(log.prog.c_str()); return EX_USAGE; }

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> const char * {
            if (i + 1 >= argc) log.usage_die("missing value for " + a);
            return argv[++i];
        };
        if (a == "-h" || a == "--help") { usage(log.prog.c_str()); return EX_OK; }
        else if (a == "-o" || a == "--out") out_dir = next();
        else if (a == "-f" || a == "--fidelity") fidelity_name = next();
        else if (a == "--essence") essence_override = arg_int(log, "--essence", next());
        else if (a == "--detail") detail_override = arg_float(log, "--detail", next());
        else if (a == "--grain") grain = arg_float(log, "--grain", next());
        else if (a == "--analysis-width") analysis_width = arg_int(log, "--analysis-width", next());
        else if (a == "--size") {
            const char *v = next();
            if (!parse_size(v, out_w, out_h)) log.usage_die(std::string("--size expects WxH, got '") + v + "'");
        }
        else if (a == "--fps") fps = arg_float(log, "--fps", next());
        else if (a == "--frames") max_frames = arg_int(log, "--frames", next());
        else if (a == "--samples") samples = arg_int(log, "--samples", next());
        else if (a == "--report") report = arg_int(log, "--report", next());
        else if (a == "-j" || a == "--threads") threads = arg_int(log, "--threads", next());
        else if (a == "--video-ext") video_ext = next();
        else if (a == "--ffmpeg") ffmpeg_override = next();
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
    if (samples < 1 || samples > 8) log.usage_die("--samples must be 1..8");
    if (report < 0 || report > 2) log.usage_die("--report must be 0, 1 or 2");
    if (analysis_width < 64) log.usage_die("--analysis-width must be at least 64");
    if (essence_override != 0 && (essence_override < 4 || essence_override > 4096))
        log.usage_die("--essence must be 4..4096");

    const Fidelity *fid = find_fidelity(fidelity_name);
    if (!fid) {
        std::string known;
        size_t n = 0;
        const Fidelity *p = fidelity_presets(n);
        for (size_t i = 0; i < n; ++i) { known += " "; known += p[i].name; }
        log.usage_die("unknown fidelity '" + fidelity_name + "'; known:" + known);
    }
    if (threads <= 0) {
        const unsigned hc = std::thread::hardware_concurrency();
        threads = hc ? (int)hc : 1;
    }

    // --- locate ffmpeg -----------------------------------------------------
    const std::string dir = exe_dir(argv[0]);
    const std::string FFMPEG = find_tool("ffmpeg", ffmpeg_override, dir);
    const std::string FFPROBE = find_tool("ffprobe", "", dir);
    if (FFMPEG.empty())
        log.die("no ffmpeg found. Build the vendored copy with 'make ffmpeg', or pass --ffmpeg PATH.");

    // --- probe -------------------------------------------------------------
    MediaInfo info;
    std::string err;
    if (!FFPROBE.empty()) {
        if (!probe_media(FFPROBE, input, info, err)) log.die(err);
        char buf[256];
        snprintf(buf, sizeof(buf), "input %dx%d %s %.3ffps %d frames",
                 info.width, info.height, info.codec.c_str(), info.fps, info.nb_frames);
        log.note(buf);
    } else {
        log.warn("ffprobe not found; input properties will be taken from the decoded stream");
    }

    // Per-frame timings, read from the container rather than inferred. Cheap --
    // no pixels are decoded -- and for variable-rate material the durations are
    // the exposure sheet itself.
    std::vector<FrameTiming> timings;
    if (!FFPROBE.empty()) {
        std::string terr;
        if (!probe_frame_timings(FFPROBE, input, timings, terr))
            log.warn(terr + "; exposure will be inferred from pixels instead");
    }

    OutputSet outs;
    outs.dir = out_dir.empty() ? join_path("out", basename_no_ext(input)) : out_dir;
    outs.name = basename_no_ext(input) + "-regen";
    outs.video_ext = video_ext;
    outs.keep_frames = keep_frames;
    outs.keep_essence = keep_essence;
    outs.inspect = inspect;
    if (!outs.prepare(err)) log.die(err);

    // --- decode options ----------------------------------------------------
    // Analysis resolution is decoupled from the essence grid. It used to be
    // derived from the grid width, which meant a 920-wide source was measured at
    // 384 wide -- so the error metric compared the grid against an already
    // softened proxy and understated the real loss.
    DecodeOptions dopt;
    dopt.max_frames = max_frames;
    if (info.width > analysis_width) {
        dopt.scale_w = analysis_width;
        dopt.scale_h = std::max(2, (int)lroundf((float)analysis_width *
                                                (float)info.height / (float)info.width));
        dopt.scale_h += dopt.scale_h & 1;
        char buf[192];
        snprintf(buf, sizeof(buf),
                 "measuring at %dx%d (source is wider than --analysis-width %d); "
                 "reported error is measured at that size",
                 dopt.scale_w, dopt.scale_h, analysis_width);
        log.warn(buf);
    }

    // =======================================================================
    // PASS 1 -- measure
    // =======================================================================
    // Motion runs on a deliberately small grid, independent of fidelity: camera
    // movement is a low-frequency signal, a coarse grid is more robust and far
    // cheaper, and it means pass 1 never has to hold a large grid at all.
    AnalyzeConfig mcfg;
    mcfg.fit_essence(info.width > 0 ? info.width : 16,
                     info.height > 0 ? info.height : 9, 48);
    // The fidelity error is measured from probe frames at the chosen width, not
    // from the motion grid, so computing it per frame here would be a full-frame
    // reconstruction pass whose result is thrown away.
    mcfg.compute_error = false;

    Decoder dec;
    if (!dec.open(FFMPEG, input, dopt, err)) log.die(err);

    SequenceAnalysis seq;
    seq.fps = info.fps > 0.0f ? info.fps : 30.0f;

    // A handful of full-resolution probe frames, spread through the sequence, are
    // kept for choosing the grid width and measuring the error. Everything else is
    // discarded as soon as it has been measured.
    const int PROBE_MAX = 5;
    std::vector<Frame> probes;
    int probe_stride = 0;

    Frame frame;
    int count = 0;
    const int expect = max_frames > 0 ? max_frames
                                      : (info.nb_frames > 0 ? info.nb_frames : 0);
    if (expect > 0) probe_stride = std::max(1, expect / PROBE_MAX);

    for (;;) {
        std::string derr;
        if (!dec.next(frame, derr)) {
            if (!derr.empty()) log.die("decode failed: " + derr);
            break;
        }
        FrameStats st = analyze_frame(frame, mcfg, count);

        if (keep_essence)
            write_ppm_path(numbered(outs.essence_dir(), "e", count, "ppm"), st.essence);

        if (count > 0)
            seq.motion.push_back(estimate_motion(seq.frames.back().essence, st.essence, mcfg));
        else
            seq.motion.push_back(MotionEstimate());

        if ((int)probes.size() < PROBE_MAX &&
            (probe_stride == 0 || count % std::max(1, probe_stride) == 0))
            probes.push_back(frame);

        seq.frames.push_back(std::move(st));
        ++count;
        log.progress("measuring", count, expect);
        if (max_frames > 0 && count >= max_frames) break;
    }
    log.progress_done();
    dec.close();

    if (seq.frames.empty()) log.die("no frames decoded from '" + input + "'");
    if (probes.empty()) probes.push_back(frame);
    {
        char buf[96];
        snprintf(buf, sizeof(buf), "measured %d frames", count);
        log.note(buf);
    }

    // Motion grids were only needed pairwise; release them now so memory does not
    // scale with sequence length.
    for (FrameStats &s : seq.frames) s.essence = Frame();

    detect_transitions(seq, mcfg);
    seq.global_palette = extract_palette(probes[probes.size() / 2], mcfg.palette_size, 1);
    summarise(seq);

    {
        char buf[256];
        snprintf(buf, sizeof(buf),
                 "motion energy %.4f, pan (%+.4f, %+.4f), zoom %+.4f, %zu transition(s)%s",
                 seq.motion_energy_mean, seq.global_pan_x, seq.global_pan_y,
                 seq.zoom_rate, seq.transitions.size(), seq.loops ? ", loops" : "");
        log.note(buf);
    }
    for (const Transition &t : seq.transitions) {
        char buf[128];
        snprintf(buf, sizeof(buf), "  frame %d: %s (strength %.3f, %d frames)",
                 t.frame, t.kind.c_str(), t.strength, t.length);
        log.note(buf);
    }

    // --- choose the essence grid ------------------------------------------
    const int probe_w = probes[0].width;
    AnalyzeConfig cfg = mcfg;
    float achieved_err = 0.0f;
    int chosen = 0;

    if (essence_override > 0) {
        chosen = std::min(essence_override, probe_w);
        if (chosen != essence_override) {
            char buf[160];
            snprintf(buf, sizeof(buf),
                     "--essence %d exceeds the measured width; clamped to %d",
                     essence_override, chosen);
            log.warn(buf);
        }
    } else if (fid->target_error <= 0.0f) {
        chosen = probe_w;   // "max": the grid is the frame, so reconstruction is exact
    } else {
        // Take the WIDEST width any probe needs, so the hardest frame in the
        // sequence meets the target rather than the average one.
        for (const Frame &p : probes) {
            float e = 0.0f;
            const int w = choose_essence_width(p, fid->target_error, 32, p.width, &e);
            if (w > chosen) chosen = w;
        }
    }
    cfg.fit_essence(probe_w, probes[0].height, chosen);

    // Report the error actually achieved at the chosen width, averaged over probes.
    {
        double acc = 0;
        for (const Frame &p : probes) acc += essence_error_at(p, cfg.essence_w);
        achieved_err = (float)(acc / (double)probes.size());
    }
    {
        char buf[256];
        snprintf(buf, sizeof(buf),
                 "fidelity %s: essence grid %dx%d, measured error %.4f%s",
                 essence_override > 0 ? "(explicit)" : fid->name,
                 cfg.essence_w, cfg.essence_h, achieved_err,
                 (fid->target_error > 0.0f && achieved_err > fid->target_error * 1.05f)
                     ? "  (target not reached; source width is the limit)" : "");
        log.note(buf);
    }

    const float measured_grain =
        detail::mean_over(seq.frames, [](const FrameStats &s) { return s.grain; });
    {
        const float g = grain >= 0.0f ? grain : grain_from_measurement(measured_grain);
        char buf[192];
        snprintf(buf, sizeof(buf), "measured grain %.5f -> layer amount %.4f%s",
                 measured_grain, g, g <= 0.001f ? " (clean source; no grain added)" : "");
        log.note(buf);
    }

    // --- derive parameters -------------------------------------------------
    DeriveOptions dopts;
    dopts.out_width = out_w > 0 ? out_w : (info.width > 0 ? info.width : probe_w);
    dopts.out_height = out_h > 0 ? out_h : (info.height > 0 ? info.height : probes[0].height);
    dopts.fps = fps;
    dopts.samples = samples;
    dopts.detail = detail_override >= 0.0f ? detail_override : fid->detail;
    dopts.stylize = stylize;
    dopts.grain = grain;

    // The per-frame error is not known until pass 2, and it only feeds the detail
    // amount, so the probe mean stands in for it. Writing it into every frame's
    // stats keeps the derived parameters and the report consistent.
    for (FrameStats &s : seq.frames) s.high_freq_ratio = achieved_err;

    json::Value params = scene_from_sequence(seq, cfg, dopts);
    params.set("analysis", analysis_json(seq, cfg, report));

    json::Value fidj = json::Value::obj();
    fidj.set("preset", json::Value(essence_override > 0 ? std::string("explicit")
                                                        : std::string(fid->name)));
    fidj.set("target_error", json::Value((double)fid->target_error));
    fidj.set("achieved_error", json::Value((double)achieved_err));
    fidj.set("essence", json::int_pair(cfg.essence_w, cfg.essence_h));
    fidj.set("measured_at", json::int_pair(probe_w, probes[0].height));
    fidj.set("measured_at_native", json::Value(probe_w >= info.width || info.width <= 0));
    params.set("fidelity", fidj);

    // Container CLAIMS vs what we actually MEASURED, reported separately.
    //
    // Modelled on ffprobe keeping nb_frames (claimed) apart from nb_read_frames
    // (counted), and for the same reason: the claim can be wrong. On one sample
    // here the container claimed 270 frames while a careless decode yielded 563.
    // Collapsing the two into one number is how that stayed hidden.
    json::Value src_claim = json::Value::obj();
    src_claim.set("frames", json::Value((double)info.nb_frames));
    src_claim.set("fps_nominal", json::Value((double)info.nominal_fps));
    src_claim.set("fps_average", json::Value((double)info.fps));
    src_claim.set("duration_seconds", json::Value(info.duration));
    src_claim.set("width", json::Value((double)info.width));
    src_claim.set("height", json::Value((double)info.height));
    src_claim.set("variable_rate", json::Value(info.variable_rate));

    json::Value src_meas = json::Value::obj();
    src_meas.set("frames", json::Value((double)count));
    src_meas.set("frames_timed", json::Value((double)timings.size()));
    src_meas.set("width", json::Value((double)probe_w));
    src_meas.set("height", json::Value((double)probes[0].height));
    src_meas.set("at_native_resolution",
                 json::Value(probe_w >= info.width || info.width <= 0));

    const bool frames_disagree =
        info.nb_frames > 0 && max_frames <= 0 && count != info.nb_frames;
    json::Value srcj = json::Value::obj();
    srcj.set("claimed", src_claim);
    srcj.set("measured", src_meas);
    srcj.set("claims_disagree", json::Value(frames_disagree));
    if (frames_disagree)
        srcj.set("note", json::Value(std::string(
            "the container's frame count and the decoded frame count differ; "
            "the measured value is authoritative")));
    params.set("source_report", srcj);

    if (frames_disagree) {
        char buf[192];
        snprintf(buf, sizeof(buf),
                 "container claims %d frames, decoded %d -- using the decoded count",
                 info.nb_frames, count);
        log.warn(buf);
    }

    json::Value prov = json::Value::obj();
    prov.set("tool", json::Value(std::string("ppm-video")));
    prov.set("input", json::Value(input));
    prov.set("source", json::Value(std::string(info.codec)));
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
    // always generated and kept in its own file.
    {
        // Restrict the timings to the frames actually processed, so --frames N
        // does not report the exposure of footage we never looked at.
        std::vector<double> durations;
        for (size_t i = 0; i < timings.size() && i < seq.frames.size(); ++i)
            durations.push_back(timings[i].duration);
        const meta::ContainerExposure exposure = meta::derive_container_exposure(durations);

        const json::Value md =
            meta::sequence_metadata(seq, meta::MetaConfig(), exposure.available ? &exposure : nullptr);
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
        if (exposure.available) {
            std::string dist;
            for (const auto &pr : exposure.distribution) {
                char d[64];
                snprintf(d, sizeof(d), "%s%dx%d", dist.empty() ? "" : ", ", pr.second, pr.first);
                dist += d;
            }
            char eb[320];
            snprintf(eb, sizeof(eb),
                     "  exposure from container: %.0f fps tick base, %s (%s)%s",
                     exposure.tick_fps, sm["step_label"].text().c_str(), dist.c_str(),
                     md["summary"]["exposure"]["pixel_inference"]["agrees"].flag()
                         ? "" : "  [pixel inference disagrees]");
            log.note(eb);
        }
        if (!json::write_file(outs.metadata_path(), md))
            log.die("cannot write " + outs.metadata_path());
        log.note("wrote " + outs.metadata_path());
        if (flat_out) {
            const std::string fp = join_path(outs.dir, "metadata.flat");
            if (json::write_flat_file(fp, md, "metadata")) log.note("wrote " + fp);
            else log.warn("cannot write " + fp);
        }
    }

    if (analyze_only) return EX_OK;

    // =======================================================================
    // PASS 2 -- render, streaming
    // =======================================================================
    std::string serr;
    scene::Scene sc = scene::Scene::from_json(params, serr);
    if (!serr.empty()) log.die("derived scene is invalid: " + serr);

    Decoder dec2;
    if (!dec2.open(FFMPEG, input, dopt, err)) log.die(err);

    EncodeOptions eopt;
    eopt.fps = sc.fps;
    Encoder enc;
    if (!enc.open(FFMPEG, outs.video_path(), sc.width, sc.height, eopt, err)) log.die(err);

    {
        char buf[192];
        snprintf(buf, sizeof(buf),
                 "rendering %d frames at %dx%d, %d thread(s), %dx supersampling",
                 sc.frames, sc.width, sc.height, threads, sc.samples);
        log.note(buf);
    }

    // One grid at a time. ReconstructSource with a single entry serves every
    // frame index, so the streaming case needs no special handling downstream.
    scene::ReconstructSource src;
    src.essence.resize(1);
    src.high_freq.assign(1, achieved_err);

    Frame out, src_frame;
    int rendered = 0;
    for (int i = 0; i < sc.frames; ++i) {
        std::string derr;
        if (!dec2.next(src_frame, derr)) {
            if (!derr.empty()) log.die("decode failed on the render pass: " + derr);
            break;   // fewer frames than pass 1 saw; stop cleanly
        }
        src.essence[0] = downsample_box(src_frame, cfg.essence_w, cfg.essence_h);

        scene::render_frame(sc, src, i, out, threads);
        if (!enc.write(out)) {
            enc.close();
            log.die("write to encoder failed at frame " + std::to_string(i));
        }
        if (keep_frames)
            write_ppm_path(numbered(outs.frames_dir(), "frame", i, "ppm"), out);
        ++rendered;
        log.progress("rendering", rendered, sc.frames);
    }
    log.progress_done();
    dec2.close();

    const int rc = enc.close();
    if (rc != 0) log.die("ffmpeg failed while encoding (exit " + std::to_string(rc) + ")");
    if (rendered != sc.frames) {
        char buf[128];
        snprintf(buf, sizeof(buf), "rendered %d of %d frames; the decoder ran short",
                 rendered, sc.frames);
        log.warn(buf);
    }

    log.note("wrote " + outs.video_path());
    if (keep_frames) log.note("wrote " + outs.frames_dir() + "/");
    if (keep_essence) log.note("wrote " + outs.essence_dir() + "/");

    if (inspect) {
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
