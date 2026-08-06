# Glossary

Domain vocabulary for the ppm install/release work. Terms here are canonical;
use them verbatim in specs, tickets, and code.

- **installer** — `install.sh`; the `curl | sh` entry point that puts the CLIs
  on a machine.
- **release asset** — a file attached to a GitHub release. ppm ships one
  **release tarball** per platform: `ppm-<version>-<os>-<arch>.tar.gz`.
- **platform** — the `os-arch` pair the installer resolves (e.g. `darwin-arm64`,
  `linux-x64`).
- **PPM_DIR** — the dedicated ppm prefix that holds everything the CLIs need at
  run time: `bin/` (the CLIs + bundled pair), `shaders/`, `include/`, and the
  compile cache `.cache/`. Resolution order: `$PPM_DIR` env →
  `$XDG_DATA_HOME/ppm` → `~/.local/share/ppm` (ADR 0005).
- **PATH block** — the marker-delimited line `export PATH="$PPM_DIR/bin:$PATH"`
  that `install.sh` appends to `~/.zshrc` / `~/.bashrc` (deduplicated and
  replaced on reinstall) so the CLIs are reachable without symlinks or copies.
- **bundled pair** — the vendored `ffmpeg` + `ffprobe` shipped inside every
  release tarball at `bin/ffmpeg/`.
- **matched pair** — a rule: the ffmpeg and ffprobe used for one run must come
  from the same source (both bundled or both system). A mixed pair is forbidden.
- **byte-identical invariant** — analysis output is only comparable across runs
  when the decoder is byte-identical, hence the pinned vendored ffmpeg
  (`ffmpeg.lock`).
- **skill** — the agent-facing instructions installed by
  `npx skills add ... --skill media` (`skills/media/SKILL.md`). Drives first use
  and the ffmpeg probe/ask.
- **probe** — the skill's `command -v ffmpeg && command -v ffprobe` check run
  before the installer.
- **override** — a `--ffmpeg PATH` / `--ffprobe PATH` CLI flag that replaces the
  `find_tool` lookup for one run.
- **find_tool** — the binary-location helper (`include/pipe.hpp`) that prefers
  `exe_dir/ffmpeg`, then `./ffmpeg/`, then PATH.
- **release tag** — a `v*` git tag pushed to trigger the release workflow; the
  version source of truth for `make release` and for install.sh's download URL.
- **release workflow** — the GitHub Actions pipeline that, on a release tag
  push, runs tests, assembles the per-platform tarballs, and creates/uploads the
  GitHub release.
- **build matrix** — the set of runner platforms the release workflow builds for:
  `linux-x64`, `linux-arm64`, `darwin-arm64`, `darwin-x64`.
