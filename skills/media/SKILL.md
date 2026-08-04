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

Do NOT generate C++ code, shaders, or frame data by hand. Use the binaries.

Pick the CLI by **input type**:

| Input | CLI | What it does |
|---|---|---|
| Video file | `ppm-video` | Measures every frame, regenerates the sequence |
| Image file | `ppm-media` | Measures it, infers a camera move, generates a clip |
| Text description | `ppm-prompt` | Asks a model for scene parameters, then renders |
| C++ shader file | `ppmr` | Compiles and streams a hand-authored CPU shader |

```sh
ppm-video  clip.mp4  -o out/study -f faithful
ppm-media  photo.jpg -o out/photo --duration 6
ppm-prompt "a slow aurora over dark water" -o out/aurora
ppmr shaders/plasma.cpp -o out.mp4
```

**Completion criterion:** The output directory contains `parameters.json` and a video file.

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

One C++ file, one pure function. Porting from GLSL: swizzles take parentheses (`v.xyyx()`), `dFdx`/`dFdy`/`fwidth` do not exist — use `--samples` or helpers in `aa.hpp`.

### Reference files (when inside ppm repo)

- `docs/frames.json` — frame discipline, fidelity, parameter vocabulary
- `docs/frame-metadata.json` — metadata terms, colour-design principles
- `docs/scene-schema.json` — layers, tone, transforms, animation
- `docs/ppm-ffmpeg.json` — PPM spec and ffmpeg flags
- `docs/shader-authoring.json` — shader API, idioms, pitfalls
