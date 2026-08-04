// ===========================================================================
//  ppm-prompt -- generate a clip from a text prompt.
//
//  CLI 2 of three. Semantics: PROMPT IN, VIDEO OUT.
//
//      USER_PROMPT ----+
//                      |--[model]--> scene parameters --> synthesize --> frames
//      SYSTEM_PROMPT --+                                                    |
//        (built from docs/*.json)                       output.mp4 <--[ffmpeg]
//
//  The model produces PARAMETERS, not code. That is the load-bearing decision:
//  nothing generated here is ever compiled or executed, so a bad generation is a
//  rejected JSON file rather than an arbitrary program running on the machine.
//  It also means a generated scene is the same kind of object as one derived from
//  analysis, so the two can be diffed, edited, and re-rendered identically.
//
//  The system prompt is assembled from docs/scene-schema.json and
//  docs/design-tokens.json at run time. To change how generations behave, edit
//  the docs -- not this file.
// ===========================================================================
#include "derive.hpp"
#include "net.hpp"
#include "pipe.hpp"
#include "scene.hpp"

#include <thread>

using namespace ppm;

// ---------------------------------------------------------------------------
// system prompt assembly
// ---------------------------------------------------------------------------

static std::string read_text_file(const std::string &path, bool &ok) {
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) { ok = false; return ""; }
    std::string out;
    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    fclose(f);
    ok = true;
    return out;
}

/// Locate docs/ relative to the executable, so the tool works from any cwd.
static std::string docs_dir(const char *argv0) {
    const std::string d = exe_dir(argv0);
    const char *candidates[] = {"../docs", "docs", "../../docs"};
    for (const char *c : candidates) {
        const std::string p = d.empty() ? std::string(c) : join_path(d, c);
        if (is_dir(p)) return p;
    }
    return "docs";
}

static void append_line(std::string &s, const std::string &line) {
    s += line;
    s += '\n';
}

/// Walk a JSON subtree and flatten it into readable "key: value" lines.
///
/// The schema doc is written for humans, and its prose is what a model needs; but
/// dumping raw JSON wastes tokens on punctuation and nesting. Flattening keeps the
/// content and drops the syntax.
static void flatten(const json::Value &v, const std::string &prefix,
                    std::string &out, int depth) {
    if (depth > 6) return;
    if (v.is_str()) {
        append_line(out, prefix.empty() ? v.string : prefix + ": " + v.string);
    } else if (v.is_num() || v.type == json::Value::Bool) {
        std::string s;
        json::dump(v, s, 0);
        append_line(out, prefix + ": " + s);
    } else if (v.is_arr()) {
        bool scalar = true;
        for (size_t i = 0; i < v.size(); ++i)
            if (v[i].is_obj() || v[i].is_arr()) { scalar = false; break; }
        if (scalar) {
            std::string s;
            json::dump(v, s, 0);
            append_line(out, prefix + ": " + s);
        } else {
            for (size_t i = 0; i < v.size(); ++i) flatten(v[i], prefix, out, depth + 1);
        }
    } else if (v.is_obj()) {
        for (const auto &kv : v.object) {
            const std::string key = prefix.empty() ? kv.first : prefix + "." + kv.first;
            flatten(kv.second, key, out, depth + 1);
        }
    }
}

static std::string build_system_prompt(const std::string &docs, const Log &log) {
    bool ok = false;
    json::Value schema, tokens;
    std::string err;

    if (!json::parse_file(join_path(docs, "scene-schema.json"), schema, err))
        log.die("cannot read docs/scene-schema.json: " + err +
                "\n  The system prompt is built from the docs; run from the repo, or pass --system FILE.");
    (void)json::parse_file(join_path(docs, "design-tokens.json"), tokens, err);
    (void)ok;

    std::string s;
    append_line(s, "You generate SCENE PARAMETERS for a CPU software renderer.");
    append_line(s, "");
    append_line(s, "Reply with ONE JSON object and nothing else. No prose, no");
    append_line(s, "explanation, no markdown fence around it if you can avoid it.");
    append_line(s, "The object IS the scene. It is data: it will be validated and");
    append_line(s, "rendered, never executed as code.");
    append_line(s, "");
    append_line(s, "=== FORMAT ===");
    flatten(schema["animation"], "animation", s, 0);
    append_line(s, "");
    flatten(schema["structure"], "", s, 0);
    append_line(s, "");
    append_line(s, "=== RULES ===");
    append_line(s, "1. \"output\" and \"layers\" are required. Everything else is optional.");
    append_line(s, "2. Do NOT use the \"reconstruct\" layer type. It needs measured");
    append_line(s, "   essence grids from a source image, which a prompt has none of.");
    append_line(s, "3. Any numeric parameter may be a constant or");
    append_line(s, "   {\"from\":a,\"to\":b,\"easing\":\"...\",\"loop\":bool}.");
    append_line(s, "4. Animate at least one parameter, or the result is a still image.");
    append_line(s, "5. If any layer blends with \"add\" or \"screen\", set");
    append_line(s, "   tone.tonemap to \"tanh\" or \"reinhard\".");
    append_line(s, "6. Colours are [r,g,b] floats in 0..1, not hex, not 0..255.");
    append_line(s, "");
    append_line(s, "=== GUIDANCE ===");
    flatten(schema["authoring_guidance"]["items"], "", s, 0);

    if (!tokens.is_null()) {
        append_line(s, "");
        append_line(s, "=== PALETTES (cosine: col = bias + amp*cos(TAU*(freq*t + phase))) ===");
        append_line(s, "Sample these into 2-4 [r,g,b] stops for a layer's \"colors\" ramp.");
        const json::Value &cos = tokens["palettes"]["cosine"];
        for (const auto &kv : cos.object) {
            std::string line = "  " + kv.first + ": ";
            std::string tmp;
            json::dump(kv.second["bias"], tmp, 0);
            line += "bias=" + tmp;
            tmp.clear();
            json::dump(kv.second["amp"], tmp, 0);
            line += " amp=" + tmp;
            tmp.clear();
            json::dump(kv.second["phase"], tmp, 0);
            line += " phase=" + tmp + "  (" + kv.second["mood"].text() + ")";
            append_line(s, line);
        }
        append_line(s, "");
        append_line(s, "=== TONE DEFAULTS ===");
        flatten(tokens["tone"], "tone", s, 0);
    }

    append_line(s, "");
    append_line(s, "=== EXAMPLE (a complete, valid scene) ===");
    append_line(s, json::dump(schema["example"]));
    return s;
}

// ---------------------------------------------------------------------------
// response handling
// ---------------------------------------------------------------------------

/// Pull a JSON object out of a reply that may be fenced or padded with prose.
///
/// Models wrap output in ```json fences even when told not to, and sometimes add
/// a sentence before it. Rather than fail, find the outermost balanced object --
/// counting braces while skipping string literals, so a brace inside a string
/// cannot throw off the count.
static std::string extract_json_object(const std::string &text) {
    const size_t start = text.find('{');
    if (start == std::string::npos) return "";
    int depth = 0;
    bool in_string = false, escaped = false;
    for (size_t i = start; i < text.size(); ++i) {
        const char c = text[i];
        if (in_string) {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') in_string = false;
            continue;
        }
        if (c == '"') in_string = true;
        else if (c == '{') ++depth;
        else if (c == '}') {
            if (--depth == 0) return text.substr(start, i - start + 1);
        }
    }
    return "";
}

/// Build the chat-completions request body.
static std::string build_request(const std::string &model, const std::string &system,
                                const std::string &user, float temperature,
                                int max_tokens, bool json_mode) {
    std::string body = "{\"model\":\"";
    json::escape(model, body);
    body += "\",\"messages\":[{\"role\":\"system\",\"content\":\"";
    json::escape(system, body);
    body += "\"},{\"role\":\"user\",\"content\":\"";
    json::escape(user, body);
    body += "\"}],\"temperature\":";
    json::write_number(temperature, body);
    if (max_tokens > 0) body += ",\"max_tokens\":" + std::to_string(max_tokens);
    // Ask for JSON explicitly where supported; harmless where it is not, since an
    // unknown field is ignored by most OpenAI-compatible servers.
    if (json_mode) body += ",\"response_format\":{\"type\":\"json_object\"}";
    body += "}";
    return body;
}

static void usage(const char *prog) {
    printf(
        "%s -- generate a clip from a text prompt\n"
        "\n"
        "USAGE\n"
        "  %s \"PROMPT\" [options]\n"
        "  %s --params FILE [options]      re-render existing parameters\n"
        "\n"
        "Calls an OpenAI-compatible chat-completions endpoint, which returns scene\n"
        "PARAMETERS (never code), then renders them on CPU.\n"
        "\n"
        "OPTIONS\n"
        "  -o, --out DIR         output directory (default: out/<slug>)\n"
        "      --size WxH        output resolution (default 1280x720)\n"
        "      --fps N           frame rate (default 30)\n"
        "      --frames N        frame count (default 180)\n"
        "      --duration SEC    set frame count from a duration instead\n"
        "      --samples N       supersampling, 1..8 (default 1)\n"
        "      --model NAME      model name (or $PPM_MODEL)\n"
        "      --base-url URL    API base (default $PPM_BASE_URL or OpenAI)\n"
        "      --api-key-env VAR env var holding the key (default PPM_API_KEY)\n"
        "      --system FILE     use FILE as the system prompt instead of docs/\n"
        "      --params FILE     skip generation and render this parameters file\n"
        "      --temperature F   default 0.9\n"
        "      --max-tokens N    default 4096\n"
        "      --timeout SEC     default 180\n"
        "      --retries N       retries when the reply is not a valid scene (default 2)\n"
        "      --no-json-mode    do not request response_format=json_object\n"
        "      --print-system-prompt   print the assembled prompt and exit\n"
        "      --dry-run         show what would be sent, without sending\n"
        "      --emit-only       write parameters.json and stop\n"
        "      --keep-frames     also write the frames as PPM\n"
        "      --video-ext EXT   mp4 (default), mkv, gif\n"
        "      --ffmpeg PATH     ffmpeg binary to use\n"
        "  -j, --threads N       worker threads (default: all cores)\n"
        "  -q, --quiet\n"
        "  -h, --help\n"
        "\n"
        "ENDPOINTS\n"
        "  http://  spoken natively over sockets, no dependency. This is the path\n"
        "           for a local model server on a GPU-less box, e.g.\n"
        "           --base-url http://127.0.0.1:11434/v1\n"
        "  https:// delegated to the curl binary, so certificate validation is not\n"
        "           reimplemented here.\n"
        "\n"
        "EXAMPLES\n"
        "  %s \"a slow aurora over dark water\" --model gpt-4o\n"
        "  %s \"molten metal, tight loop\" --duration 4 --base-url http://127.0.0.1:11434/v1 --model llama3\n"
        "  %s --print-system-prompt\n"
        "  %s --params out/aurora/parameters.json --size 1920x1080 -o out/aurora-hd\n"
        "\n"
        "EXIT STATUS\n"
        "  0 success    1 failure    2 usage error\n",
        prog, prog, prog, prog, prog, prog, prog);
}

static std::string slugify(const std::string &in) {
    std::string s;
    int words = 0;
    bool dash = false;
    for (char c : in) {
        if (isalnum((unsigned char)c)) {
            s.push_back((char)tolower((unsigned char)c));
            dash = false;
        } else if (!dash && !s.empty()) {
            if (++words >= 4) break;
            s.push_back('-');
            dash = true;
        }
    }
    while (!s.empty() && s.back() == '-') s.pop_back();
    return s.empty() ? "scene" : s;
}

int main(int argc, char **argv) {
    Log log{"ppm-prompt", false};

    std::string prompt, out_dir, model, base_url, api_key_env = "PPM_API_KEY";
    std::string system_file, params_file, ffmpeg_override, video_ext = "mp4";
    int out_w = 1280, out_h = 720, frames = 180, samples = 1, max_tokens = 4096;
    int timeout = 180, retries = 2, threads = 0;
    float fps = 30.0f, temperature = 0.9f, duration = 0.0f;
    bool dry_run = false, print_prompt = false, emit_only = false;
    bool keep_frames = false, json_mode = true;
    bool size_set = false;

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
            size_set = true;
        }
        else if (a == "--fps") fps = arg_float(log, "--fps", next());
        else if (a == "--frames") frames = arg_int(log, "--frames", next());
        else if (a == "--duration") duration = arg_float(log, "--duration", next());
        else if (a == "--samples") samples = arg_int(log, "--samples", next());
        else if (a == "--model") model = next();
        else if (a == "--base-url") base_url = next();
        else if (a == "--api-key-env") api_key_env = next();
        else if (a == "--system") system_file = next();
        else if (a == "--params") params_file = next();
        else if (a == "--temperature") temperature = arg_float(log, "--temperature", next());
        else if (a == "--max-tokens") max_tokens = arg_int(log, "--max-tokens", next());
        else if (a == "--timeout") timeout = arg_int(log, "--timeout", next());
        else if (a == "--retries") retries = arg_int(log, "--retries", next());
        else if (a == "-j" || a == "--threads") threads = arg_int(log, "--threads", next());
        else if (a == "--video-ext") video_ext = next();
        else if (a == "--ffmpeg") ffmpeg_override = next();
        else if (a == "--no-json-mode") json_mode = false;
        else if (a == "--dry-run") dry_run = true;
        else if (a == "--print-system-prompt") print_prompt = true;
        else if (a == "--emit-only") emit_only = true;
        else if (a == "--keep-frames") keep_frames = true;
        else if (a == "-q" || a == "--quiet") log.quiet = true;
        else if (!a.empty() && a[0] == '-') log.usage_die("unknown option '" + a + "'");
        else if (prompt.empty()) prompt = a;
        else prompt += " " + a;   // allow an unquoted multi-word prompt
    }

    if (duration > 0.0f) frames = (int)(duration * fps + 0.5f);
    if (frames <= 0) log.usage_die("--frames must be positive");
    if (samples < 1 || samples > 8) log.usage_die("--samples must be 1..8");
    if (retries < 0) log.usage_die("--retries must be >= 0");
    if (threads <= 0) {
        const unsigned hc = std::thread::hardware_concurrency();
        threads = hc ? (int)hc : 1;
    }

    // --- system prompt -----------------------------------------------------
    const std::string docs = docs_dir(argv[0]);
    std::string system_prompt;
    if (!system_file.empty()) {
        bool ok = false;
        system_prompt = read_text_file(system_file, ok);
        if (!ok) log.die("cannot read " + system_file);
    } else if (const char *env = getenv("SYSTEM_PROMPT"); env && *env) {
        system_prompt = env;
    } else {
        system_prompt = build_system_prompt(docs, log);
    }

    if (print_prompt) {
        fputs(system_prompt.c_str(), stdout);
        return EX_OK;
    }

    if (prompt.empty()) {
        if (const char *env = getenv("USER_PROMPT"); env && *env) prompt = env;
    }
    if (prompt.empty() && params_file.empty())
        log.usage_die("no prompt given (pass one as an argument, or set USER_PROMPT, or use --params)");

    // --- config ------------------------------------------------------------
    if (base_url.empty()) {
        const char *env = getenv("PPM_BASE_URL");
        base_url = env && *env ? env : "https://api.openai.com/v1";
    }
    if (model.empty()) {
        const char *env = getenv("PPM_MODEL");
        if (env && *env) model = env;
    }

    const std::string url = base_url + (base_url.empty() || base_url.back() == '/' ? "" : "/") +
                            "chat/completions";

    if (dry_run) {
        printf("POST %s\n", url.c_str());
        printf("model:       %s\n", model.empty() ? "(not set)" : model.c_str());
        printf("temperature: %g\n", temperature);
        printf("json_mode:   %s\n", json_mode ? "yes" : "no");
        printf("system:      %zu bytes, assembled from %s\n", system_prompt.size(), docs.c_str());
        printf("user:        %s\n", prompt.c_str());
        printf("output:      %dx%d, %d frames at %g fps\n", out_w, out_h, frames, fps);
        return EX_OK;
    }

    const std::string dir = exe_dir(argv[0]);
    const std::string FFMPEG = find_tool("ffmpeg", ffmpeg_override, dir);
    if (FFMPEG.empty())
        log.die("no ffmpeg found. Build the vendored copy with 'make ffmpeg', or pass --ffmpeg PATH.");

    // --- obtain a scene ----------------------------------------------------
    json::Value params;
    std::string err;

    if (!params_file.empty()) {
        if (!json::parse_file(params_file, params, err))
            log.die("cannot read " + params_file + ": " + err);
        log.note("rendering parameters from " + params_file);
    } else {
        if (model.empty())
            log.usage_die("no model specified. Pass --model NAME or set PPM_MODEL.");
        const char *key = getenv(api_key_env.c_str());
        std::string api_key = key ? key : "";
        // A local server usually needs no key; a remote one always does.
        if (api_key.empty() && url.compare(0, 5, "https") == 0)
            log.die("$" + api_key_env + " is not set (required for an https endpoint)");

        std::string user = prompt;
        char hint[256];
        snprintf(hint, sizeof(hint),
                 "\n\nTarget output: %dx%d, %d frames at %g fps.",
                 out_w, out_h, frames, fps);
        user += hint;

        bool got = false;
        for (int attempt = 0; attempt <= retries && !got; ++attempt) {
            log.note(attempt == 0 ? "requesting scene from " + model
                                  : "retrying (" + std::to_string(attempt) + ")");
            const std::string body =
                build_request(model, system_prompt, user, temperature, max_tokens, json_mode);

            net::Response resp;
            std::string nerr;
            if (!net::post_json(url, body, api_key, timeout, resp, nerr)) log.die(nerr);
            if (resp.status < 200 || resp.status >= 300)
                log.die("API returned HTTP " + std::to_string(resp.status) + ": " +
                        resp.body.substr(0, 600));

            json::Value envelope = json::parse(resp.body, err);
            if (!err.empty())
                log.die("API reply is not valid JSON: " + err + "\n  " + resp.body.substr(0, 300));

            const std::string content =
                envelope["choices"][0]["message"]["content"].text();
            if (content.empty()) {
                // Surface the API's own error message rather than a generic one.
                const std::string api_err = envelope["error"]["message"].text();
                log.die(api_err.empty()
                            ? "reply contained no message content: " + resp.body.substr(0, 400)
                            : "API error: " + api_err);
            }

            const std::string obj = extract_json_object(content);
            if (obj.empty()) {
                log.warn("reply contained no JSON object");
                continue;
            }
            params = json::parse(obj, err);
            if (!err.empty()) {
                log.warn("generated JSON is malformed: " + err);
                continue;
            }
            std::string verr;
            scene::Scene probe = scene::Scene::from_json(params, verr);
            if (!verr.empty()) {
                log.warn("generated scene is invalid: " + verr);
                continue;
            }
            if (probe.layers.empty()) {
                log.warn("generated scene has no layers");
                continue;
            }
            got = true;
        }
        if (!got) log.die("could not obtain a valid scene after " +
                          std::to_string(retries + 1) + " attempt(s)");
    }

    // --- reconcile requested output with the generated scene ---------------
    // The model is asked for a size but need not honour it, and the CLI flags are
    // the user's explicit intent, so they win.
    {
        json::Value out = params["output"];
        if (out.is_null()) out = json::Value::obj();
        if (size_set || !out.has("width")) {
            out.set("width", json::Value((double)out_w));
            out.set("height", json::Value((double)out_h));
        }
        if (!out.has("frames") || frames != 180) out.set("frames", json::Value((double)frames));
        if (!out.has("fps") || fps != 30.0f) out.set("fps", json::Value((double)fps));
        if (samples != 1 || !out.has("samples")) out.set("samples", json::Value((double)samples));
        params.set("output", out);
    }

    std::string serr;
    scene::Scene sc = scene::Scene::from_json(params, serr);
    if (!serr.empty()) log.die("scene is invalid: " + serr);

    OutputSet outs;
    outs.dir = out_dir.empty()
                   ? join_path("out", params_file.empty() ? slugify(prompt) : basename_no_ext(params_file))
                   : out_dir;
    outs.name = params_file.empty() ? slugify(prompt) : basename_no_ext(params_file);
    outs.video_ext = video_ext;
    outs.keep_frames = keep_frames;
    if (!outs.prepare(err)) log.die(err);

    if (params_file.empty()) {
        json::Value prov = json::Value::obj();
        prov.set("tool", json::Value(std::string("ppm-prompt")));
        prov.set("prompt", json::Value(prompt));
        prov.set("model", json::Value(model));
        prov.set("base_url", json::Value(base_url));
        params.set("provenance", prov);
    }
    if (!json::write_file(outs.params_path(), params))
        log.die("cannot write " + outs.params_path());
    log.note("wrote " + outs.params_path());

    if (emit_only) return EX_OK;

    // --- render ------------------------------------------------------------
    // No essence grids exist for a generated scene; a "reconstruct" layer would
    // have nothing to sample and is skipped by the renderer.
    scene::ReconstructSource src;

    EncodeOptions eopt;
    eopt.fps = sc.fps;
    Encoder enc;
    if (!enc.open(FFMPEG, outs.video_path(), sc.width, sc.height, eopt, err)) log.die(err);

    {
        char buf[200];
        snprintf(buf, sizeof(buf), "rendering %d frames at %dx%d, %zu layer(s), %d thread(s)",
                 sc.frames, sc.width, sc.height, sc.layers.size(), threads);
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
    return EX_OK;
}
