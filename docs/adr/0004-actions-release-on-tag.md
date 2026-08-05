# ADR 0004: GitHub Actions release workflow on tag push

Status: Accepted

## Context

Release assets have to be assembled and uploaded for install.sh to work, but
doing that by hand is exactly the step that got skipped (v0.1.0 shipped with
zero assets). We want a `.github/workflows/` release pipeline. The candidate
triggers were "every push/merge to master" and "tag push"; the tag-push model
won because it makes the version explicit and releases deliberate.

Facts that shape the design:

- `make release` produces `out/release/ppm-<version>-<platform>.tar.gz`, where
  the version resolves from `git describe --tags --abbrev=0` — i.e. from the
  tag itself — and install.sh downloads exactly
  `releases/download/<version>/ppm-<version>-<platform>.tar.gz`. So the tarball
  version must equal a real release tag.
- `gh release upload` requires the release to already exist; `gh` is preinstalled
  on GitHub-hosted runners and needs `permissions: contents: write`.
- `make release` builds the vendored ffmpeg from the pinned `ffmpeg.lock` commit
  on the runner it runs on, so one tarball per runner platform.

## Decision

- **Trigger:** `on: push: tags: ['v*']`. The maintainer ships by pushing a tag
  (`git push origin v0.1.1`); merging to master alone does not release.
- **The workflow owns the whole task** — the maintainer never touches the GitHub
  UI. A create step makes the release if missing, then per-platform build jobs
  assemble the tarball and `gh release upload` it.
- **Platform matrix:** build all four common platforms:
  `linux-x64`, `linux-arm64`, `darwin-arm64`, `darwin-x64` (one matrix job per
  runner). Exact runner labels are an implementation detail; the matrix intent is
  four platforms.
- **Tests gate the release:** each build job runs `make test` (which includes the
  install-from-release seam) before `make release` and upload.
- **Idempotent reruns:** upload uses `--clobber`; a rerun of a failed job
  overwrites its own platform asset rather than failing.
- **Release notes:** `gh release create --generate-notes` so each release carries
  auto-generated notes from the commits since the last tag.

## Consequences

- Releasing is one command (`git push origin <tag>`) and the release object +
  assets are created without manual steps, closing the gap that left v0.1.0
  asset-less.
- Releases are immutable and `releases/latest` auto-advances with each new tag;
  `curl | sh` installs the newest tagged build.
- Release cost is the sum of four vendored-ffmpeg source builds (~40–80 min).
- Releases only happen when a tag is pushed; merges alone never ship.
