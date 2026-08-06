---
title: "Dedicated ppm prefix with PATH-based reachability"
status: ready-for-agent
category: enhancement
state: ready-for-agent
labels: [enhancement, ready-for-agent]
---

## Problem Statement

As a user who installs ppm from a release, I run one line
(`curl .../install.sh | sh`) and expect ppm to work: `ppmr --list`, `ppmr`
renders, `ppm-video`/`ppm-media`/`ppm-prompt` runs — all with no extra setup.

Today, on a Homebrew machine, the install is *functionally* correct but
*structurally* wrong: the CLIs land in `/opt/homebrew/bin`, and the assets they
need at run time — `shaders/` (listed and rendered by `ppmr`), `include/` (the
shader API headers `ppmr` compiles against), the bundled `ffmpeg`/`ffprobe`
pair, and the compile cache — are unpacked into `/opt/homebrew/` itself. A
prefix we do not own gets polluted with `shaders/`, `include/`, and `.cache/`.
When any asset is missing from that spot, `ppmr` fails with a raw
`cat: /opt/homebrew/include/*.hpp: No such file or directory` — a confusing
failure that depends on where Homebrew lives.

I want ppm's files in **a directory solely for ppm**.

## Solution

Installation moves to a dedicated prefix — **PPM_DIR** — that holds everything
ppm ships and everything ppm produces at run time. The CLIs are made reachable
the way mainstream installers do it (rustup/cargo, pyenv, asdf): by adding
`PPM_DIR/bin` to the shell's PATH — no symlinks, no copies, no binaries
scattered into directories we do not own.

After install, `ppmr --list` finds the shipped shaders, `ppmr` compiles against
the shipped headers, the bundled `ffmpeg`/`ffprobe` pair is used, and the
compile cache is written inside the prefix — all without ppm ever touching a
foreign directory.

## User Stories

1. As a user, I want `curl .../install.sh | sh` to install everything ppm needs
   into one directory, so that no ppm file ever appears in Homebrew's tree.
2. As a user, I want the ppm prefix to be predictable (`~/.local/share/ppm` by
   default), so that I can find and inspect the installation.
3. As a user, I want to override the prefix with a `PPM_DIR` environment
   variable, so that I can place ppm where I want.
4. As a user, I want `$XDG_DATA_HOME/ppm` honoured when set, so that I follow
   the XDG convention.
5. As a zsh user, I want the installer to append `PPM_DIR/bin` to my `~/.zshrc`,
   so that the CLIs are on my PATH with no manual step.
6. As a bash user, I want the installer to append `PPM_DIR/bin` to my
   `~/.bashrc`, so that the CLIs are on my PATH with no manual step.
7. As a fish (or other-shell) user, I want the installer to print the exact
   one-liner to add `PPM_DIR/bin` to my PATH, so that I can finish setup
   myself.
8. As a user, I want the PATH entry **prepended**, so that the freshly
   installed CLIs win over any older install.
9. As a user reinstalling or upgrading, I want the installer to deduplicate the
   PATH entry, so that my shell rc never accumulates duplicate lines.
10. As a user reinstalling with a different `PPM_DIR`, I want the old PATH
    entry replaced by the new one, so that the shell points at the live
    installation.
11. As a user, I want the installer to tell me the PATH change needs a new
    terminal (or to `source` my rc), so that I am not confused when `ppmr`
    isn't found in the current shell.
12. As a user, I want `ppmr --list` to list the shipped shaders right after
    install, so that the documented workflow works out of the box.
13. As a user, I want `ppmr shaders/<name>.cpp` and absolute shader paths to
    compile and render from any directory, so that the documented examples
    work without cd'ing into the repo.
14. As a user, I want `ppmr` to find the bundled `ffmpeg`/`ffprobe` pair inside
    the prefix, so that the matched-pair rule holds with no flags.
15. As a user, I want `ppm-video`, `ppm-media`, and `ppm-prompt` to find the
    bundled pair inside the prefix, so that they work with no configuration.
16. As a user, I want the compile cache to live inside the prefix, so that no
    cache directory appears in a foreign prefix like `/opt/homebrew`.
17. As a user on a machine with an older ppm install, I want the new install to
    take precedence, so that I get the fixed behavior without manually removing
    the old files.
18. As a user, I want the installer to remain non-interactive (`curl | sh`),
    so that the one-liner still works.
19. As a maintainer, I want the installer to stop selecting a bin directory on
    PATH, so that the "which dir did it pick?" logic and its warning disappear.
20. As a maintainer, I want the CLIs to require **no symlink-resolution code**,
    so that the installed CLIs are byte-identical to the repo CLIs.
21. As a maintainer, I want the release tarball contents unchanged
    (`bin/`, `shaders/`, `include/`), so that only the unpack target and PATH
    wiring change.
22. As a maintainer, I want the source-build hint to reflect the new layout,
    so that the fallback instructions stay truthful.
23. As a maintainer, I want `make test` to verify the new install contract end
    to end, so that regressions are caught by the existing test seam.
24. As a maintainer, I want the glossary and ADRs to record the new terms
    (PPM_DIR, PATH block) and retire BIN_DIR, so that specs and tickets speak
    one vocabulary.
25. As a user on a machine that never had ppm, I want the whole flow to work
    with zero manual steps beyond opening a new shell, so that first-run is
    smooth.

## Implementation Decisions

- **PPM_DIR resolution.** The prefix is resolved in order: the `PPM_DIR`
  environment variable, else `$XDG_DATA_HOME/ppm`, else
  `$HOME/.local/share/ppm`. It is always under the user's home, so it is always
  writable; the writable-dir-on-PATH picker is removed from the installer.
- **Prefix layout.** The release tarball is unpacked so the prefix mirrors the
  repo:
  `bin/` (the CLIs and the bundled pair), `shaders/`, `include/`. The compile
  cache is written at `.cache/` inside the prefix. No ppm asset is placed
  outside the prefix.
- **Reachability via PATH.** The installer appends a marker-delimited block
  `export PATH="$PPM_DIR/bin:$PATH"` to the detected shell rc — `~/.zshrc` for
  zsh, `~/.bashrc` for bash — **prepended** within that line. On reinstall the
  block is replaced, so entries deduplicate and a changed `PPM_DIR` leaves no
  stale line. Shells other than zsh/bash get a printed one-liner
  (`fish_add_path ...`) and no rc edit.
- **No CLI changes.** Because a CLI run via PATH is invoked by its real path
  under `PPM_DIR/bin`, the existing root-relative and bundled-pair lookups
  resolve unchanged. No `readlink`/`realpath` logic is added to the CLIs.
- **Legacy handling.** Older installs in foreign prefixes are **not** cleaned or
  migrated. The prepended PATH entry shadows them; the installer prints nothing
  about them.
- **Post-install message.** The installer reports the prefix it used, the shell
  rc it edited, and that a new terminal (or `source` of the rc) is needed,
  including the exact `export PATH=...` line as a manual alternative.
- **Packaging unchanged.** The release tarball still ships `bin/`, `shaders/`,
  and `include/`; only where the installer unpacks it changes.

## Testing Decisions

- **What a good test looks like:** exercise the installer's external contract
  only — where files land, what the shell rc gains, how a CLI invoked via PATH
  behaves. Do not assert internal helper functions or literal file paths inside
  the installer.
- **One seam (existing, highest point):** the install-from-release seam already
  wired into `make test`. It serves a fixture release tarball over a local HTTP
  server, runs the installer with env overrides into a temp home, and asserts
  behavior. It is reworked for the new contract rather than adding a new seam.
- **Scenario:** a temp `$HOME` with a fixture `~/.zshrc`; the installer runs
  with `PPM_DIR` pointed into the temp home; the test then invokes the CLIs by
  name with `PPM_DIR/bin` prepended to PATH, exactly as a new shell would.
- **Assertions:**
  - the prefix contains `bin/` (CLIs + bundled pair), `shaders/`, `include/`
    and nothing ppm owns lands outside it;
  - the rc gains exactly one marker-delimited PATH block, prepending
    `PPM_DIR/bin`, and a second install does not duplicate it (and replaces it
    when `PPM_DIR` changes);
  - `ppmr --list` lists the shipped shaders and `ppmr` compiles a shipped
    shader through the shipped headers and bundled pair;
  - `ppm-video --help` runs and a bundled-pair analysis produces output;
  - a non-zsh/non-bash shell gets the printed one-liner (no rc edit);
  - `git` is never invoked, and the existing failure paths (missing asset,
    unsupported platform) still exit nonzero with clear guidance.
- **Modules under test:** the installer and, via the installed tree, the CLIs'
  run-time behavior. Prior art is the current install-from-release harness.

## Out of Scope

- Cleaning or migrating legacy installs (files left in foreign prefixes are
  simply shadowed).
- Auto-append for fish or other shells (they get a printed one-liner only).
- An uninstaller.
- Multi-version management or versioned prefix layouts.
- Symlink or copy reachability models and any symlink-resolution code in the
  CLIs — explicitly rejected in design grilling in favour of PATH.
- Changing the release tarball contents.
- Windows support.

## Further Notes

- Design produced in a grilling session; see `docs/adr/0005-dedicated-prefix-path-reachability.md`
  and the updated `docs/glossary.md` (the `BIN_DIR` term is retired; `PPM_DIR`
  and `PATH block` are canonical).
- This spec supersedes the "unpack into the bin dir" portion of the
  install-from-release spec; the never-clone and bundled-pair decisions stand.
- The current maintainer machine has a legacy `/opt/homebrew` install; it is
  shadowed by the prepended PATH entry and can be removed by hand at any time.
- After install, PATH takes effect in new shells; the installer prints the
  one-liner so the current shell can be fixed immediately.
