---
title: "Release-based binary install (no clone)"
status: ready-for-human
category: enhancement
state: ready-for-human
labels: [enhancement, ready-for-human]
---

## Problem Statement

As a user running `curl -fsSL https://raw.githubusercontent.com/weefaa/ppm/master/install.sh | sh`, I hit a wall: the installer finds no pre-built binaries, falls back to cloning the repo, and then prompts me for a GitHub username and password. A one-liner install must never ask me for credentials.

The failure chain is:

- The v0.1.0 release has no assets, so every per-binary tarball 404s.
- The installer's `REPO` default (`aelfestijo/ppm`) doesn't match the real remote (`WEEFAA/ppm`); cloning a nonexistent repo over HTTPS makes git prompt for credentials.
- The whole "build from source" fallback is the design flaw: a `curl | sh` installer should only ever download pre-built artifacts.

## Solution

The installer becomes a **release-based binary install**: it resolves a release, downloads one per-platform release tarball, unpacks it into a bin directory, and never clones or builds. Each release tarball is self-contained: the three make-built CLIs, the checked-in `ppmr` script, and the vendored `ffmpeg`/`ffprobe` pair, laid out so the CLIs find the bundled pair with zero configuration.

If a user already has `ffmpeg` and `ffprobe` on their machine, the installed skill probes for them before running the installer and asks the user whether to prefer their own pair; the answer is honoured by passing both `--ffmpeg` and `--ffprobe` overrides on every run, so a bundled and a system decoder are never mixed.

## User Stories

1. As a user, I want `curl | sh` to install ppm without ever asking me for GitHub credentials, so that the one-liner stays a one-liner.
2. As a user, I want the installer to download pre-built binaries from a GitHub release instead of cloning the repository, so that I don't need git, a compiler, or network permissions beyond a release download.
3. As a user on a supported platform, I want the release tarball to contain the three CLIs, `ppmr`, and the bundled `ffmpeg`/`ffprobe` pair, so that the installed tools work out of the box.
4. As a user, I want the installed CLIs to use the bundled `ffmpeg`/`ffprobe` pair by default with no flags, so that first-run "no ffmpeg found" errors disappear.
5. As a user who already has `ffmpeg` and `ffprobe`, I want the skill to detect both and ask me whether to use mine, so that I can keep my preferred tools.
6. As a user who already has `ffmpeg` and `ffprobe`, I want choosing "use mine" to make every run use my pair consistently, so that I never get a silent mix of my decoder and the bundled one.
7. As a user with only one of `ffmpeg`/`ffprobe`, I want the installer's bundled pair used for both, so that a half pair never silently corrupts analysis output.
8. As a user, I want the installer to pass `--ffmpeg PATH` and `--ffprobe PATH` together whenever my pair is chosen, so that decoding and probing never diverge.
9. As a user on an unsupported platform, I want a clear "no asset for your platform" message plus the manual source-build command, so that I know exactly what to do instead of a credential prompt.
10. As a user who hits a download failure, I want a clear error distinguishing network failure from a missing release, so that I can react appropriately.
11. As a user, I want to pin a specific version via an env override, so that I can install a known-good release.
12. As a user, I want the latest release to be resolved automatically when no version is pinned, so that the default path stays simple.
13. As a maintainer, I want a way to assemble the per-platform release tarball from the make artifacts and the vendored ffmpeg, so that releasing is a short, repeatable step.
14. As a maintainer, I want the tarball to mirror the repo's `bin/` layout, so that the CLIs' existing `find_tool` lookup finds the bundled pair with no configuration.
15. As a maintainer, I want the installer's repo/URL defaults to match the real remote (`WEEFAA/ppm`, branch `master`), so that users never hit a nonexistent-repo credential prompt.
16. As a maintainer, I want `ppm-video` and `ppm-media` to accept a `--ffprobe PATH` override, so that the skill can honour a user's chosen pair without mixing decoders.
17. As an agent running the skill, I want Step 1 to probe `command -v ffmpeg` and `command -v ffprobe` before the installer, so that I know whether to ask the user.
18. As an agent running the skill, I want the ask to be all-or-nothing (both present or neither), so that I never offer a broken half-pair option.
19. As a user, I want `make test` to exercise the install-from-release path end to end, so that the no-clone contract is verified by the existing test target.

## Implementation Decisions

- **Release asset format.** Each release carries one tarball per platform:
  `ppm-<version>-<os>-<arch>.tar.gz`. It unpacks directly into the bin dir and
  mirrors the repo layout:
  - `bin/ppm-video`, `bin/ppm-media`, `bin/ppm-prompt` (built by `make`)
  - `bin/ppmr` (checked-in shell script, not a make artifact)
  - `bin/ffmpeg/ffmpeg`, `bin/ffmpeg/ffprobe` (vendored, pinned by `ffmpeg.lock`)
- **Packaging step.** A `release`-style target (in the build tooling) builds the
  three CLIs, builds the vendored ffmpeg, and assembles the tarball. Uploading to
  a GitHub release stays a manual `gh release upload` (or equivalent) for now —
  no CI.
- **Installer rewrite.** The installer:
  - resolves the version (env override, else the `releases/latest` redirect),
  - detects the platform (`os-arch`),
  - downloads the single tarball for that platform,
  - extracts into the bin dir (the existing writable-dir-on-PATH picker stays),
  - **never clones and never builds**; on failure it exits nonzero with a
    message that separates unsupported platform, missing asset, and network
    failure, and prints the manual `git clone ... && make` instructions without
    running them,
  - defaults `REPO` to `WEEFAA/ppm` and keeps curl failures silent except when
    they are the final answer.
- **`--ffprobe` override.** `ppm-video` and `ppm-media` gain a `--ffprobe PATH`
  flag alongside the existing `--ffmpeg PATH`; `find_tool` for ffprobe honours
  it. `ppm-prompt` shells only ffmpeg and is unchanged.
- **Skill probe and ask.** The skill's first step runs
  `command -v ffmpeg && command -v ffprobe` before the installer. Offer
  "use your ffmpeg/ffprobe?" only when both resolve. On acceptance, every
  invocation passes both `--ffmpeg PATH --ffprobe PATH` — the matched-pair rule.
- **Bundled pair by default.** The installer always ships the bundled pair; the
  probe/ask is a skill-layer concern only. `find_tool` already prefers
  `exe_dir/ffmpeg` over PATH, so the bundled pair wins with no flags.
- **Canonical URLs.** Installer defaults, README, and the skill's install command
  all point at `weefaa/ppm`, branch `master`.

## Testing Decisions

- **What a good test looks like:** exercise external behavior only — the
  installer's observable contract (downloads the tarball, unpacks into the bin
  dir, never invokes `git`, exits nonzero with clear guidance on failure) and the
  CLIs' override behavior (`--ffprobe` is accepted and honoured, and a run keeps
  its pair matched). Do not assert internal functions or file paths.
- **One seam:** extend the existing `make test` shell-integration target. Prior
  art is the current `make test` (Makefile `test:`), which is already a shell
  end-to-end harness.
- **Scenario:** build the real artifacts, assemble a fixture tarball exactly as
  the packaging step would, serve it from a local HTTP server (e.g.
  `python3 -m http.server`), and run the installer with env overrides (repo /
  version / bin dir) into a temp dir.
- **Assertions:**
  - binaries and the bundled pair land in the bin dir; a CLI `--help` runs,
  - `git` is never invoked (a stub `git` on PATH that fails if called),
  - a CLI run using the bundled pair produces output,
  - a CLI run with `--ffmpeg`/`--ffprobe` overrides honours them (behaviorally
    distinguishable from the bundled pair),
  - failure paths exit nonzero with the right guidance: forced unsupported
    platform, and a missing asset.
- **Modules under test:** the installer and the two CLIs that gain `--ffprobe`.
  The skill's ask is interactive and covered by a manual/agent run.

## Out of Scope

- CI automation (GitHub Actions) that builds and uploads release tarballs on tag — releases are assembled and pushed manually for now.
- Asset checksums or code signing.
- Windows support.
- Changing `find_tool` lookup priority so system tools win over bundled ones — explicitly rejected in the design grilling (the bundled pair must stay the default).
- An interactive prompt inside the installer — the ask lives in the skill, where a human is present to answer.
- A `--ffprobe` flag on `ppm-prompt` (it never invokes ffprobe).

## Further Notes

- Design produced in a grilling session; see the ADRs and glossary:
  - `docs/adr/0001-release-based-binary-install.md`
  - `docs/adr/0002-bundled-ffmpeg-pair.md`
  - `docs/adr/0003-skill-ffmpeg-probe-and-override.md`
  - `docs/glossary.md`
- The v0.1.0 release currently has no assets. The first cut should upload
  tarballs for the platform(s) the maintainer builds on so the reported 404 path
  is actually exercised.
- The reported trace's credential prompt was a symptom of the wrong default repo
  name plus the clone fallback; both are eliminated by this spec.

---

## Triage — ready-for-human

> *This was generated by AI during triage.*

**State change:** `ready-for-agent` → `ready-for-human`.

**What's established:**

- The release-based binary install is **implemented and committed** (`c42cb6b`):
  `install.sh` never clones and never builds; the `release` target assembles the
  per-platform tarball; `--ffprobe PATH` exists on `ppm-video`/`ppm-media`; the
  skill probes and asks before install; `make test` covers the whole story.
- Verified against the real repo: `curl -fsSL .../install.sh | sh` now resolves
  `v0.1.0`, detects `darwin-arm64`, and fails with the designed
  `no release asset ... release was not built for it` message — **no credential
  prompt, no clone**. The original bug is fixed.
- The **only** remaining reason the install fails is that release `v0.1.0` has
  zero assets uploaded (confirmed via the GitHub API).

**What's left (human, cannot be delegated):**

1. Build the tarball(s) for the platform(s) to publish:
   `make release` → `out/release/ppm-<version>-<platform>.tar.gz`
   (builds the CLIs + vendored ffmpeg and assembles the bundle).
2. Upload to the release: `gh release upload v0.1.0 out/release/ppm-v0.1.0-darwin-arm64.tar.gz`
   (repeat per platform).
3. If a new version is desired instead, tag it first and use
   `make release RELEASE_VERSION=v0.1.1`, then upload to that tag.

**Acceptance:** the exact reported command succeeds end to end:

```sh
curl -fsSL https://raw.githubusercontent.com/WEEFAA/ppm/master/install.sh | sh
```

It should print `ppm v0.1.0 installed to ...`, and `ppm-video --help` should run.

