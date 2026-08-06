# ADR 0005: Dedicated ppm prefix with PATH-based reachability

Status: Accepted

## Context

ADR 0001 unpacked the release tarball straight into `$BIN_DIR`, a directory the
installer picks from PATH (default `~/.local/bin`, `/opt/homebrew/bin`,
`/usr/local/bin`). Two follow-on fixes shipped the runtime assets the CLIs need
— `shaders/` (listed and rendered by `ppmr`) and `include/` (the shader API
headers `ppmr` compiles against) — by placing them one directory above the
binaries, because `ppmr` resolves its root as the parent of its own directory.

That placement is correct in a checkout but wrong in a Homebrew tree: with
`$BIN_DIR=/opt/homebrew/bin`, the assets land in `/opt/homebrew/shaders`,
`/opt/homebrew/include`, and the compile cache in `/opt/homebrew/.cache` —
polluting a prefix ppm does not own. The CLIs also had to self-locate from the
parent of the binary, which is fragile across layouts.

Findings that shape the design:

- The release tarball mirrors the repo layout (`bin/`, `shaders/`, `include/`).
  What is missing is a stable, ppm-owned place to put it.
- Mainstream installers split into two reachability models: (a) keep binaries
  in the tool's own directory and add that directory to PATH (rustup/cargo,
  pyenv, asdf); (b) install into an already-on-PATH bin dir via symlinks
  (Homebrew, pipx). We choose (a).
- Model (a) needs no symlink-resolution code in the CLIs: when a CLI is run via
  PATH, `$0`/`argv[0]` is its real path under the prefix, so the existing root
  and ffmpeg lookups resolve unchanged.

## Decision

- **PPM_DIR** — a dedicated prefix that holds everything ppm ships: the CLIs at
  `bin/`, the bundled ffmpeg/ffprobe pair at `bin/ffmpeg/`, `shaders/`,
  `include/`, and the compile cache at `.cache/`. Resolution order:
  `$PPM_DIR` env → `$XDG_DATA_HOME/ppm` → `~/.local/share/ppm`.
- **Reachability via PATH, not symlinks or copies.** `install.sh` appends a
  marker-delimited block
  `export PATH="$PPM_DIR/bin:$PATH"` to the detected shell rc —
  `~/.zshrc` for zsh, `~/.bashrc` for bash — deduplicated and replaced on
  reinstall. For other shells it prints a one-liner (`fish_add_path ...`).
- **No BIN_DIR.** The `pick_bin_dir` selection and the "not on PATH" warning are
  removed. The prefix is always writable because it lives under `$HOME`.
- **No CLI self-location code.** No readlink/realpath changes; `ROOT` and the
  bundled-pair lookup (`exe_dir/ffmpeg`) work because the invoked path is real.
- **Compile cache moves into the prefix** (`.cache/`), removing the writability
  risk a shared prefix like `/opt/homebrew` posed.
- **Legacy layouts are not cleaned** by the installer; the prepended PATH entry
  shadows any older binaries.

## Consequences

- No ppm assets ever land outside `$PPM_DIR`; the Homebrew prefix stays
  untouched.
- The installer is simpler (no bin-dir selection), but a new shell is required
  for PATH to take effect — the installer prints that explicitly.
- Upgrades replace the prefix in place; the marker block keeps PATH deduplicated
  even when `$PPM_DIR` changes.
- `make test`'s install-from-release seam is reworked to assert the prefix
  layout, the rc block, and invocation via PATH.
- The tarball contents (`bin/`, `shaders/`, `include/`) are unchanged; only
  where they are unpacked and how the CLIs are reached changes.
