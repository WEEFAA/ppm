# ppm

Frame generation on CPU. Three CLIs that take a video, an image, or a prompt, and
produce **frames, a video, and the parameters that made them** — with no GPU, no
graphics API, and no dependency beyond a C++17 compiler and ffmpeg.

Built for machines with no GPU to allocate: VPS instances, CI runners,
containers, cheap cloud tiers.

```bash
make ffmpeg && make          # build the encoder (once) and the CLIs
./bin/ppm-video clip.mp4     # analyse every frame, then rebuild it
```

## The three CLIs

Each has one input semantic and the same output contract. Separate binaries
rather than subcommands, so each maps cleanly onto a single tool invocation.

| | Input | What it does |
|---|---|---|
| `ppm-video` | a video | Measures every frame, then regenerates the sequence from those measurements |
| `ppm-media` | an image | Measures it, infers a camera move from the picture, generates a clip |
| `ppm-prompt` | text | Asks a model for scene *parameters*, then renders them |

```bash
ppm-video  clip.mp4  -o out/study -f faithful --keep-frames
ppm-media  photo.jpg -o out/photo --duration 6 --move 1.5
ppm-prompt "a slow aurora over dark water" --model gpt-4o
```

All three write into one directory:

```
out/study/
  parameters.json        what was measured, what was derived, what was rendered
  metadata.json          per-frame metadata, in animator's terms
  clip-regen.mp4         the video
  frames/frame0000.ppm   the generated frames      (--keep-frames)
  essence/e0000.ppm      the essence grids         (--keep-essence)
  inspect/index.html     browsable frame inspector (--inspect)
```

`parameters.json` is sufficient to re-render without the original input, so a
result is re-gradable and re-scalable rather than disposable.

## How it works

ffmpeg is used **only as a codec**. Every pixel that comes out was produced by our
own C++ measuring the input and rebuilding from those measurements — no frame of
the source is ever copied through.

```
input --[ffmpeg]--> P6 frames --> analyse --> parameters
                                                 |
output <--[ffmpeg]-- P6 frames <-- synthesize <---+
```

Frames move as concatenated binary PPM over a pipe. ffmpeg's `pnm` parser derives
each frame's length from its own header, so a bare byte stream needs no container,
no separators and no index. A 10-second 4K sequence would be ~25 GB staged as
files, and is 0 bytes this way.

### Frames are the asset

The governing idea, spelled out in [docs/frames.json](docs/frames.json): a frame
decomposes into a **low-frequency colour field** (the *essence* — composition,
light, colour, which carries almost all of the perceived content) and
**high-frequency detail** (texture, which is cheaper to regenerate than to store).

Where you put that split is the single fidelity dial — and it is set by a **target
error**, not a grid width, because no fixed width serves all content. A grid stores
colour samples and cannot express a step edge, so line art needs roughly six times
the width a gradient does. The width is measured from your actual frames:

| `-f, --fidelity` | Target error | Character |
|---|---|---|
| `draft` | 0.040 | fast, visibly abstracted |
| `balanced` | 0.020 | soft but recognisable |
| `faithful` | 0.008 | close to the source (**default**) |
| `max` | exact | grid at source width; pixel-exact |

Measured on 920×1200 flat-shaded animation (original `edge_mean` 0.0995):

| Preset | Chosen grid | Error | `edge_mean` |
|---|---|---|---|
| `draft` | 48×63 | 0.0377 | 0.0231 |
| `balanced` | 162×211 | 0.0176 | 0.0561 |
| `faithful` | 364×475 | 0.0078 | 0.0852 |
| `max` | 920×1200 | 0.0000 | 0.0995 |

The cost is reported, not guessed, and the prediction holds: 0.0377 predicted
against 0.0384 actual. Any lossy step here quantifies its own loss.

### Every frame carries metadata

Alongside the render, each frame gets metadata in the vocabulary practitioners
actually use — `hold`, `on twos`, `key`, `smear`, `low-key`, `notan`,
`aerial perspective`, `pan_left`. It is a **pure observer**: deleting
`metadata.json` cannot change a single output pixel.

Several fields come from animation colour-design practice and are genuinely
checkable. The most useful is **shadow chroma**: shadows that fall back to grey
flatten an image, so the tool measures the hue and saturation of the darkest
quartile and says whether they are `coloured` or `neutral`. Same for `murky` (a
muted palette whose values aren't separated) and `air` (whether brighter regions
hold less contrast, which is what atmosphere looks like).

Nothing is asserted without its measurement beside it, and where a signal is too
weak the label is `undetermined` — a flat frame gives the motion search nothing to
lock onto, which is different from being still.

```bash
ppm-video clip.mp4 --inspect      # then serve the output dir over HTTP
```

`--inspect` emits a dependency-free HTML inspector: a fixed **bench** (shot list
and summary), a **show plane** with landmarks projected onto the frame as labels
and tooltips, a **navigation bar** that scrubs and marks transitions and key
candidates, and a collapsible **tools panel** with grouped metadata, palette
swatches and a histogram.

### Generation produces data, not code

`ppm-prompt` asks the model for a **scene parameters** object, never source code.
Nothing generated is compiled or executed — a bad generation is a rejected JSON
file rather than a running program. It also means an analysis-derived scene and a
generated scene are the same kind of object, so they can be diffed, edited and
re-rendered identically.

Its system prompt is assembled from `docs/*.json` at run time, so the rules
governing hand-authored scenes govern generated ones too:

```bash
ppm-prompt --print-system-prompt              # inspect what gets sent
ppm-prompt "molten metal" --dry-run           # no request made
ppm-prompt --params out/x/parameters.json --size 1920x1080   # re-render
```

HTTP is spoken over raw sockets with no dependency, which is the path that matters
for a GPU-less box running a local model server:

```bash
ppm-prompt "aurora" --base-url http://127.0.0.1:11434/v1 --model llama3
```

`https://` is delegated to the `curl` binary rather than reimplementing TLS
certificate validation.

## Shaders

The lower layer: hand-authored CPU fragment shaders, compiled and streamed to
ffmpeg. This is where the project started, and it is still the escape hatch when
parameters are not expressive enough.

```bash
./bin/ppmr shaders/plasma.cpp -o out.mp4 --size 1920x1080 -s 2
./bin/ppmr --list
```

A shader is one C++ file defining one pure function:

```cpp
#define SHADER_NAME "demo"
#include "ppmshader.hpp"

vec4 mainImage(vec2 FC, const Uniforms &u) {
    vec2 p = (FC * 2.0f - u.resolution) / u.resolution.y;
    return vec4(0.5f + 0.5f * cos(u.phase * TAU + vec3(p.x, p.y, 1.6f)), 1.0f);
}
```

Purity is load-bearing, not stylistic: it is what makes row-parallel threading
lock-free, lets `--samples` evaluate a pixel repeatedly, and lets `--at 900`
render frame 900 without the 899 before it.

**Porting from GLSL** needs two mechanical edits: swizzles take parentheses
(`v.xyyx()`), and `dFdx`/`dFdy`/`fwidth` do not exist — a GPU shades 2×2 fragment
quads and subtracts neighbours; a scalar CPU kernel has no neighbours. Use
`--samples` (which needs nothing from the shader), or the analytic and
finite-difference helpers in `aa.hpp`.

## Headers

Header-only, no link step, no build system required.

| Header | Contents |
|---|---|
| `frame.hpp` | the frame asset: PPM read/write, sRGB↔linear, resampling |
| `analyze.hpp` | measurement: tone, colour, palette, edges, motion, transitions, curves |
| `derive.hpp` | judgement: what measurements imply for synthesis |
| `metadata.hpp` | inspection: measurements → animator's vocabulary (pure observer) |
| `scene.hpp` | synthesis: parameters → frames |
| `inspector_html.hpp` | the embedded inspector page |
| `json.hpp` | JSON reader/writer |
| `net.hpp` | HTTP POST over sockets; curl for TLS |
| `pipe.hpp` | ffmpeg orchestration, output layout, CLI scaffolding |
| `pgl.hpp` | GLSL-compatible vector math (generated — every swizzle exists) |
| `aa.hpp`, `pnoise.hpp` | antialiasing + SDFs; hashes, noise, fbm, worley |
| `ppmshader.hpp` | the shader runtime |

## Documentation

Machine-readable guidelines in `docs/`, also used to build the generator's system
prompt.

| File | Contents |
|---|---|
| [frames.json](docs/frames.json) | **the frame discipline** — extract, analyse, generate, and the parameter vocabulary. Start here. |
| [frame-metadata.json](docs/frame-metadata.json) | the metadata vocabulary — animation and cinematography terms, colour-design principles, the inspector contract |
| [scene-schema.json](docs/scene-schema.json) | the synthesis format: layers, tone, transforms, animation |
| [design-principles.json](docs/design-principles.json) | dependency policy, purity contract, CPU-vs-GPU tradeoffs |
| [design-tokens.json](docs/design-tokens.json) | render presets, cosine palettes, easings, noise ranges |
| [ppm-ffmpeg.json](docs/ppm-ffmpeg.json) | PPM spec and ffmpeg flags, verified against source |
| [shader-authoring.json](docs/shader-authoring.json) | shader API, GLSL porting, idioms, pitfalls |
| [cli-design.json](docs/cli-design.json) | CLI conventions and rationale |

Each records its *reasoning*, and each records its *known gaps* — an undocumented
gap reads as a claim.

### Skill mirrors

`skills/media/` ships a portable copy of the docs the agent skill relies on:
`frames.json`, `frame-metadata.json`, `scene-schema.json`, `design-tokens.json`,
`shader-authoring.json`. **Keep them in sync when editing the originals.** The
rest of `docs/` (`ppm-ffmpeg.json`, `cli-design.json`, `design-principles.json`)
is plumbing the binaries encapsulate, so only the skill's own copy matters.

```sh
cp docs/{frames,frame-metadata,scene-schema,design-tokens,shader-authoring}.json skills/media/
```

## Requirements

- A C++17 compiler
- ffmpeg with the `ppm_pipe` demuxer — `make ffmpeg` builds a pinned minimal one
- `curl`, only for `https://` model endpoints

Nothing else. No math library, no build system, no package manager, no runtime.

```bash
make          # build the CLIs
make test     # end-to-end self-check
make check    # compile every shader
```

## Credits

- The PPM-sequence-to-ffmpeg approach follows a
  [gist by Alexey Kutepov (rexim/Tsoding)](https://gist.github.com/rexim/ef86bf70918034a5a57881456c0a0ccf).
- `shaders/plasma.cpp` is a port of a shader by
  [XorDev](https://github.com/XorDev), via that gist.
- The antialiasing hierarchy and several tonemapping and accumulation idioms come
  from XorDev's tutorials at [fragcoord.xyz](https://fragcoord.xyz).
