#!/bin/sh
# ppm installer: downloads pre-built binaries from a GitHub release.
# Never clones, never builds -- the release tarball is self-contained
# (CLIs + ppmr + the bundled ffmpeg/ffprobe pair).
#
#   curl -fsSL https://raw.githubusercontent.com/WEEFAA/ppm/master/install.sh | sh
#   PPM_VERSION=v0.1.0 sh install.sh   # pin a specific release
set -eu

REPO="${PPM_REPO:-WEEFAA/ppm}"
BASE_URL="${PPM_BASE_URL:-https://github.com}/$REPO"

die() { echo "ppm install: $*" >&2; exit 1; }

on_path() {
    case ":$PATH:" in *":$1:"*) return 0 ;; *) return 1 ;; esac
}

pick_bin_dir() {
    saved_ifs=$IFS; IFS=:
    set -- ${PPM_BIN_CANDIDATES:-$HOME/.local/bin:/opt/homebrew/bin:/usr/local/bin}
    IFS=$saved_ifs
    fallback=$1
    for dir; do
        on_path "$dir" || continue
        if [ -d "$dir" ] && [ -w "$dir" ]; then printf '%s\n' "$dir"; return; fi
        if [ "$dir" = "$fallback" ] && [ ! -e "$dir" ]; then printf '%s\n' "$dir"; return; fi
    done
    printf '%s\n' "$fallback"
}

BIN_DIR="${PPM_BIN_DIR:-$(pick_bin_dir)}"

# --- platform detection -----------------------------------------------------
# Emits os-arch (darwin-arm64, linux-x64, ...) or fails for a platform with no
# release assets.
detect_platform() {
    os=$(uname -s | tr '[:upper:]' '[:lower:]')
    arch=$(uname -m)
    case "$os" in
        darwin|linux) ;;
        *) return 1 ;;
    esac
    case "$arch" in
        arm64|aarch64) arch="arm64" ;;
        x86_64|amd64)  arch="x64" ;;
        *) return 1 ;;
    esac
    printf '%s-%s' "$os" "$arch"
}

manual_build() {
    # Emitted to stdout so callers can embed it in a die() message.
    echo "  Build from source instead:"
    echo "    git clone https://github.com/$REPO.git ppm && cd ppm"
    echo "    make ffmpeg && make -j"
    echo "    cp bin/* ~/.local/bin/ && cp -r bin/ffmpeg ~/.local/bin/"
}

# --- resolve version --------------------------------------------------------
version="${PPM_VERSION:-}"
if [ -z "$version" ]; then
    version=$(curl -fsSLI -o /dev/null -w '%{url_effective}' \
        "$BASE_URL/releases/latest" 2>/dev/null \
        | sed 's|.*/releases/tag/||') || true
fi
case "$version" in
    ''|*/*)
        die "could not determine the latest release of $REPO.
  Pin a release explicitly with: PPM_VERSION=<tag> sh install.sh"
        ;;
esac

# --- detect platform --------------------------------------------------------
if ! platform=$(detect_platform); then
    die "no pre-built release for this platform ($(uname -s)/$(uname -m)).
$(manual_build)"
fi

# --- download the release tarball ------------------------------------------
url="$BASE_URL/releases/download/$version/ppm-$version-$platform.tar.gz"
tmpdir=$(mktemp -d)
trap 'rm -rf "$tmpdir"' EXIT

status=$(curl -sSL -o "$tmpdir/ppm.tar.gz" -w '%{http_code}' "$url" 2>/dev/null) || status=000
case "$status" in
    200) ;;
    404) die "no release asset for $platform at $version.
  $url
  Release assets are one tarball per platform. If $platform is missing, the
  release was not built for it.
$(manual_build)" ;;
    000) die "network error while downloading:
  $url" ;;
    *)   die "download failed (HTTP $status):
  $url" ;;
esac

# --- install ---------------------------------------------------------------
mkdir -p "$BIN_DIR"
tar -xzf "$tmpdir/ppm.tar.gz" -C "$BIN_DIR"
printf 'ppm %s installed to %s\n' "$version" "$BIN_DIR"

# --- warn about PATH --------------------------------------------------------
if ! on_path "$BIN_DIR"; then
    printf 'note: %s is not on your PATH. Add it:\n' "$BIN_DIR" >&2
    case "${SHELL:-}" in
        */zsh)
            printf '  echo '\''export PATH="%s:$PATH"'\'' >> ~/.zshrc && source ~/.zshrc\n' "$BIN_DIR" >&2 ;;
        *)
            printf '  echo '\''export PATH="%s:$PATH"'\'' >> ~/.bashrc && source ~/.bashrc\n' "$BIN_DIR" >&2 ;;
    esac
fi

printf 'next: npx skills add https://github.com/%s --skill media\n' "$REPO"
