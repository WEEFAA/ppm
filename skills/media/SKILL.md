---
name: ppm-cpu-frames
description: CPU frame generation — no GPU, no graphics API. Use when working with ppm-video, ppm-media, ppm-prompt, or ppmr; when output looks blurry or wrong; when tuning fidelity; when reading frame metadata or the inspector.
---

# CPU frame generation

## Step 1: Ensure binaries are available

Run this command. If it fails or the binaries are not in PATH, run the install:

```sh
command -v ppm-video >/dev/null 2>&1 || curl -fsSL https://raw.githubusercontent.com/aelfestijo/ppm/main/install.sh | sh
```

**Completion criterion:** `ppm-video --help` prints usage without error.

## Step 2: Run the appropriate CLI

Do NOT reimplement the pipeline by hand — no hand-rolled C++ renderers, no PPM
writing, no ffmpeg plumbing. Use the binaries. The one exception is a `ppmr`
shader: a shader IS hand-authored code (see the Shaders section below).

Pick the CLI by **input type**:

| Input | CLI | What it does |
|---|---|---|
| Video file | `ppm-video` | Measures every frame, regenerates the sequence |
| Image file | `ppm-media` | Measures it, infers a camera move, generates a clip |
| Text description | `ppm-prompt` | Asks a model for scene parameters, then renders |
| C++ shader file | `ppmr` | Compiles and streams a hand-authored CPU shader |

**Before writing any shader, READ `shader-authoring.json` (next to this file) — it
is the API contract. Do not guess the API and do not read repo headers.**

```sh
ppm-video  clip.mp4  -o out/study -f faithful
ppm-media  photo.jpg -o out/photo --duration 6
ppm-prompt "a slow aurora over dark water" -o out/aurora
ppmr shaders/plasma.cpp -o out.mp4
```

**Completion criterion:** The output directory contains `parameters.json` and a video file.

**When unsure of a flag, option, or parameter: run `<binary> --help`.** Every
binary carries its full usage — options, fidelity presets, examples, exit codes —
so never guess an option and never invent one. This is the authoritative
reference; there are no man pages. For a shader's own `--set` knobs, run
`ppmr SHADER --help` (they are printed from the shader's `Param` declarations).

## Step 3: Read the output

Check `parameters.json` in the output directory. It contains:
- `fidelity.achieved_error` — the actual reconstruction error
- `fidelity.grid_width`, `fidelity.grid_height` — the chosen grid
- All measured parameters

If output looks wrong, check `fidelity.achieved_error` first — it is the honest cost.

---

## Reference

### Fidelity presets

| Flag | Target error | Character |
|---|---|---|
| `-f draft` | ≤ 0.040 | fast, visibly abstracted |
| `-f balanced` | ≤ 0.020 | soft but recognisable |
| `-f faithful` | ≤ 0.008 | close to source (default) |
| `-f max` | exact | pixel-exact |

`faithful` costs ~130× more cells than `draft`. Use `-f draft --frames 30` to iterate.

### Non-obvious rules

These each cost a real bug. Do not undo them.

1. **Decode with `-fps_mode passthrough`.** Otherwise ffmpeg duplicates frames.
2. **Measure at native resolution.** `--analysis-width` must stay independent of grid width.
3. **Grain comes from flat regions only**, never from `edge_density`.
4. **Average in linear light.** Black + white must average to ~0.735, not 0.5.
5. **Motion `dx`/`dy` are content displacement.** Content right = `pan_left`.
6. **`mainImage` and scene synthesis must stay pure.** No statics, no `rand()`, no clock.
7. **Cuts before fades.** Fade magnitude is judged per shot.

### Debugging

| Symptom | Cause |
|---|---|
| Output soft / blurry | Fidelity too low. Check `fidelity.achieved_error`. |
| Frame count ≈ 2× source | Missing `-fps_mode passthrough`. |
| Plays too fast | Timed from `r_frame_rate` instead of `avg_frame_rate`. |
| Fake film noise on clean art | Grain derived from edges, not flat regions. |
| Whole frame black or grey | NaN. `clamp` will not remove it. |
| Memory blowup on long clips | Essence grids held in RAM; must stream (two passes). |
| Vertically mirrored (shaders) | Pass `--top-left-origin`. |

### Shaders

**READ `shader-authoring.json` (next to this file) BEFORE writing any shader.** It
is the complete API contract: the `mainImage` entry point, the `Uniforms` fields,
the `Param` knob mechanism, the vector-math helpers, and every idiom and pitfall.
Do not guess the API and do not go hunting in the repo headers — the contract is
right here.

The floor contract, so you can write a valid shader immediately:

```cpp
#define SHADER_NAME "my-shader"
#include "ppmshader.hpp"

vec4 mainImage(vec2 FC, const Uniforms &u) {
    // u: resolution, time, phase, aspect, frame, frames, fps
    // return: linear RGB in [0,1]; alpha ignored
    vec2 p = (FC * 2.0f - u.resolution) / u.resolution.y;
    return vec4(p.x, p.y, 0.5f + 0.5f * cosf(u.phase * TAU), 1.0f);
}
```

Expose a tweakable knob as `static Param name{"name", default, "description"};` —
it becomes settable with `--set name=value` and shows in `--help`.

GLSL porting notes (details in `shader-authoring.json`):
- Swizzles take parentheses: `v.xyyx()`
- No `dFdx`/`dFdy`/`fwidth` — use `--samples` or the `aa.hpp` helpers
- `mod()` is floor-based (GLSL semantics), not `fmod`
- `mainImage` MUST be pure: no statics, no `rand()`, no clock

### Reference files

These docs ship with this skill (same folder). Read them, do not guess:

- `shader-authoring.json` — the shader API: contract, uniforms, parameters, idioms, pitfalls
- `frames.json` — the frame discipline; the parameter vocabulary in `parameters.json`
- `frame-metadata.json` — metadata terms, colour-design principles, the inspector contract
- `scene-schema.json` — the synthesis format; what `ppm-prompt` emits and `scene.hpp` accepts
- `design-tokens.json` — named render presets and cosine palettes for shaders

Docs that apply only when working inside the ppm repository:

- `docs/ppm-ffmpeg.json` — PPM spec and ffmpeg flags (handled by the binaries)
- `docs/cli-design.json` — CLI conventions
- `docs/design-principles.json` — dependency policy and standing decisions
