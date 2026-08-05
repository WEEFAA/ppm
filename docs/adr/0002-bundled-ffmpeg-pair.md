# ADR 0002: Ship a bundled ffmpeg/ffprobe pair in every release

Status: Accepted

## Context

The CLIs are not standalone. They shell out to `ffmpeg` (codec) and `ffprobe`
(probe) as subprocesses (`include/pipe.hpp`), and the vendored ffmpeg is pinned
to a commit in `ffmpeg.lock` because analysis output is only comparable across
runs when the decoder is byte-identical (Makefile:77-82). A binary-only install
has no repo root, so the `../ffmpeg/` candidate cannot work.

## Decision

Every release tarball bundles the vendored pair at `bin/ffmpeg/ffmpeg` and
`bin/ffmpeg/ffprobe`, so an installed CLI is self-contained and byte-identical
across machines.

`find_tool` (`include/pipe.hpp:180-183`) already prefers `exe_dir/ffmpeg` before
PATH, so the bundled pair wins by default with zero configuration.

## Consequences

- Install tarballs are larger and require building the vendored ffmpeg during
  release.
- ffmpeg and ffprobe are always shipped and treated as a matched pair; the CLIs
  must never mix one bundled decoder with one system decoder (see ADR 0003).
