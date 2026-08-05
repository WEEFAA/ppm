# ADR 0001: Release-based binary install (never clone)

Status: Accepted

## Context

The installer (`install.sh`) tries to download pre-built binaries from a GitHub
release, then falls back to cloning the repository and building from source when
the download fails:

```
curl: (56) The requested URL returned error: 404
no pre-built release found for this platform; building from source...
cloning ppm...
Username for 'https://github.com':
Password for 'https://github.com':
```

This fallback is broken in practice:

- The v0.1.0 release had no assets, so the per-binary tarball 404s.
- The install script's `REPO` default (`aelfestijo/ppm`) did not match the real
  remote (`WEEFAA/ppm`). Cloning a nonexistent repo over HTTPS makes git prompt
  for credentials — the reported failure.
- Even when a repo exists, an interactive `curl | sh` pipeline must not prompt
  for credentials; that is a hostile experience.

## Decision

The install strategy is **download binaries from a GitHub release only**. There
is no git clone and no source-build fallback in `install.sh`.

- Each release ships one tarball per platform: `ppm-<version>-<os>-<arch>.tar.gz`.
- The tarball is unpacked straight into `$BIN_DIR` and mirrors the repo layout:
  - `bin/ppm-video`, `bin/ppm-media`, `bin/ppm-prompt` (built by `make`)
  - `bin/ppmr` (a checked-in shell script, not a `make` artifact)
  - `bin/ffmpeg/ffmpeg`, `bin/ffmpeg/ffprobe` (vendored, pinned by `ffmpeg.lock`)
- `find_tool` (`include/pipe.hpp:180-183`) finds ffmpeg at `$BIN_DIR/ffmpeg/`
  via its `exe_dir/ffmpeg` candidate, so the bundled pair is used with no config.
- On download failure the installer exits with a clear message distinguishing
  unsupported platform, missing release, and network failure, and prints the
  manual source-build instructions (`git clone ... && make`) without running them.
- Canonical repo is `WEEFAA/ppm` on branch `master`; `REPO` defaults and the
  raw install URLs in README/SKILL must all agree on it.

## Consequences

- Users never see a credential prompt from the installer.
- Install requires a network and a release asset for the platform; exotic
  platforms get a clear "no asset" error plus manual build steps.
- Releasing requires building, packaging, and uploading the platform tarball —
  see ADR 0002 for the bundle contents.
