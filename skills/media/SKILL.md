---
name: ppm-cpu-frames
description: Generate, analyse and regenerate video frames on CPU in this repo — no GPU, no graphics API. Use when working with ppm-video (video in, regenerated video out), ppm-media (image in, clip out), ppm-prompt (text in, clip out), or the CPU shader layer (ppmr); when output looks blurry or wrong; when tuning fidelity; when reading frame metadata or the inspector; or when editing analysis, synthesis or scene parameters.
---

# CPU frame generation

ffmpeg is used **only as a codec**. Everything between decode and encode is our own
C++17. Frames move as concatenated binary PPM over a pipe.

## The three CLIs

Each writes `parameters.json`, `metadata.json` and a video into one directory.

```bash
./bin/ppm-video clip.mov -o out/study        # measure every frame, regenerate
./bin/ppm-media photo.jpg --duration 6       # still -> clip with an inferred move
./bin/ppm-prompt "a slow aurora"             # model returns parameters -> render
./bin/ppmr shaders/plasma.cpp -o out.mp4     # hand-authored CPU shader
make && make test                            # build + end-to-end self-check
```

## Fidelity — read this before calling output "blurry"

Fidelity is a **target reconstruction error**, not a grid width. The essence grid
stores colour samples and cannot represent a step edge, so line art needs roughly
six times the width a gradient does. The width is measured from the content.

```bash
-f draft      # error <= 0.040   fast, visibly abstracted
-f balanced   # error <= 0.020   soft but recognisable
-f faithful   # error <= 0.008   default
-f max        # exact            grid at source width; pixel-exact
```

Every run reports the chosen grid and the error achieved; both land in
`parameters.json` under `fidelity`. If output looks soft, **read that number first**
— it is the honest cost, and it predicts reality (0.0377 predicted / 0.0384 actual).

`--essence N` overrides the preset. Prefer not to.

`faithful` costs real time: the grid is ~130× more cells than `draft`, and
`ppm-video` decodes twice. Use `-f draft --frames 30` to iterate.

## Non-obvious rules

These each cost a real bug. Do not undo them.

1. **Decode with `-fps_mode passthrough`.** Otherwise ffmpeg conforms output to
   `r_frame_rate` and *duplicates* frames — one 270-frame variable-rate `.mov`
   decoded as 563. The duplicates read as holds, so metadata reported the clip as
   "on twos, 55% holds" when that was our own decode command. Time output from
   `avg_frame_rate`, never `r_frame_rate`.
2. **Measure at native resolution.** Detail statistics — edge density, grain, the
   reconstruction error — only exist in pixels that still contain those
   frequencies. `--analysis-width` must stay independent of the grid width;
   deriving one from the other is circular and understated the loss.
3. **Grain comes from flat regions only**, never from `edge_density`. Line art has
   high edge density and zero noise; the old signal gave clean animation maximum
   grain, and the injected noise then inflated our own sharpness metric.
4. **Average in linear light.** Averaging sRGB drifts midtones and haloes edges.
   Check: black + white must average to ~0.735, not 0.5.
5. **Motion `dx`/`dy` are content displacement**, reported as camera action.
   Content right = `pan_left`. Block matching yields the negation, so it is negated
   at measurement.
6. **`mainImage` and scene synthesis must stay pure.** No statics, no `rand()`, no
   clock. Purity is what makes threading lock-free and single-frame rendering
   possible.
7. **Cuts before fades.** Fade magnitude is judged per shot, and cuts define shots.

## Debugging

| Symptom | Cause |
|---|---|
| Output soft / blurry | Fidelity preset too low. Check `fidelity.achieved_error`. |
| Frame count ≈ 2× source | Missing `-fps_mode passthrough`. |
| Plays too fast | Timed from `r_frame_rate` instead of `avg_frame_rate`. |
| Fake film noise on clean art | Grain derived from edges, not flat regions. |
| Whole frame black or grey | NaN. `clamp` will not remove it — NaN fails every comparison. |
| Memory blowup on long clips | Essence grids held in RAM; `ppm-video` must stream (two passes). |
| Vertically mirrored (shaders) | Pass `--top-left-origin`. |
| More output frames than rendered | `-r` after `-i`; use `-framerate` before it. |

## Shaders

One C++ file, one pure function. Porting from GLSL needs two edits: swizzles take
parentheses (`v.xyyx()`), and **`dFdx`/`dFdy`/`fwidth` do not exist** — a GPU shades
2×2 quads and subtracts neighbours; a scalar CPU kernel has no neighbours. Use
`--samples` (needs nothing from the shader) or the helpers in `aa.hpp`.

## Reference — read, do not guess

- `docs/frames.json` — the frame discipline; fidelity, extraction, parameter vocabulary
- `docs/frame-metadata.json` — metadata terms, colour-design principles, inspector contract
- `docs/scene-schema.json` — layers, tone, transforms, animation
- `docs/ppm-ffmpeg.json` — PPM spec and ffmpeg flags, verified against source
- `docs/design-principles.json`, `docs/design-tokens.json`, `docs/cli-design.json`
- `docs/shader-authoring.json` — shader API, idioms, pitfalls

`include/pgl.hpp` is **generated** — edit `tools/gen_pgl.py`, run `make pgl`.

## Dependency policy

ffmpeg, a C++17 compiler, the standard library. Nothing is linked but `-pthread`.
`curl` only for `https://` model endpoints; `http://` uses raw sockets. Adding a
dependency is a discussion, not a commit.
