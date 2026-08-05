# ADR 0003: Skill-level ffmpeg probe, ask, and matched-pair override

Status: Accepted

## Context

A user may already have `ffmpeg`/`ffprobe` on their machine. The installed
skill (`skills/media/SKILL.md`) drives first use. We want to honour the user's
own tools without ever silently mixing decoders.

Two hazards:

- `find_tool` (`include/pipe.hpp:180-183`) has an override flag for `ffmpeg`
  only (`--ffmpeg`); `ffprobe` has none. Forgetting one side of an override
  silently mixes a bundled decoder with a system one, breaking the byte-identical
  invariant of ADR 0002.
- Offer to use the machine's tools only when the pair is complete; half a pair
  is a trap.

## Decision

- **Probe before install.** `skills/media/SKILL.md` Step 1 runs
  `command -v ffmpeg` and `command -v ffprobe` before the install script.
- **All-or-nothing pair rule.** Offer "use your ffmpeg/ffprobe?" only when
  **both** resolve on PATH. If either is missing, the bundled pair is used for
  both — never a system ffmpeg with a bundled ffprobe.
- **Override both sides.** Add a `--ffprobe PATH` flag to `ppm-video` and
  `ppm-media` (ppm-prompt shells only ffmpeg; no change). When the user opts
  into their own pair, the skill passes **both** `--ffmpeg PATH --ffprobe PATH`
  on every invocation so the pair stays matched.
- **install.sh always bundles.** The probe/ask is a skill-layer concern; the
  installer ships the pair regardless (ADR 0002).

## Consequences

- The installer never prompts interactively; the ask lives in the skill, where
  a human (or agent driving the skill) is present to answer.
- The CLI surface grows one flag in two binaries.
- Runs are either all-bundled or all-system; a mixed decoder pair is impossible.
