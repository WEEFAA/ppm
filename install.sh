#!/bin/sh
# ppm installer: downloads pre-built binaries from a GitHub release.
# Never clones, never builds -- the release tarball is self-contained
# (the CLIs + ppmr + the bundled ffmpeg/ffprobe pair + shaders/ + include/).
#
# Everything lands in a single ppm-owned prefix (PPM_DIR), and the CLIs are
# made reachable by adding PPM_DIR/bin to the shell's PATH -- no symlinks, no
# copies, and nothing ever placed in a directory we do not own.
#
#   curl -fsSL https://raw.githubusercontent.com/WEEFAA/ppm/master/install.sh | sh
#   PPM_VERSION=v0.1.0 sh install.sh   # pin a specific release
#   PPM_DIR=~/.local/share/ppm sh install.sh   # choose a different prefix
set -eu

REPO="${PPM_REPO:-WEEFAA/ppm}"
BASE_URL="${PPM_BASE_URL:-https://github.com}/$REPO"

die() { echo "ppm install: $*" >&2; exit 1; }

# --- resolve the prefix ------------------------------------------------------
# PPM_DIR holds everything ppm ships and everything ppm produces: bin/ (the
# CLIs and the bundled ffmpeg/ffprobe pair), shaders/, include/, and the
# compile cache .cache/. Resolution order: the PPM_DIR env variable, else
# $XDG_DATA_HOME/ppm, else ~/.local/share/ppm. It is always under the user's
# home, so it is always writable -- no bin-dir picker is needed.
if [ -n "${PPM_DIR:-}" ]; then
    PREFIX="$PPM_DIR"
elif [ -n "${XDG_DATA_HOME:-}" ]; then
    PREFIX="$XDG_DATA_HOME/ppm"
else
    PREFIX="$HOME/.local/share/ppm"
fi

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
    echo "    mkdir -p '$PREFIX' && cp -R bin shaders include '$PREFIX'/"
    echo "    export PATH=\"$PREFIX/bin:\$PATH\"   # then open a new terminal"
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

# --- install into the prefix ------------------------------------------------
# The release tarball mirrors the repo layout (bin/, shaders/, include/), so
# unpacking it at the prefix root gives exactly the tree the CLIs expect:
#   PPM_DIR/bin/      the CLIs and the bundled ffmpeg/ffprobe pair
#   PPM_DIR/shaders/  listed and rendered by ppmr
#   PPM_DIR/include/  the shader API headers ppmr compiles against
# The compile cache (.cache/) is written there too, at run time by ppmr.
# Upgrades unpack over the old prefix; the compile cache survives because it
# is keyed on source, so stale entries are harmless.
mkdir -p "$PREFIX"
tar -xzf "$tmpdir/ppm.tar.gz" -C "$PREFIX"
printf 'ppm %s installed to %s\n' "$version" "$PREFIX"

# --- reachability via PATH --------------------------------------------------
# The CLIs are reached by PATH, not symlinks or copies: the shell rc gains a
# marker-delimited block that prepends PPM_DIR/bin. On reinstall the block is
# replaced, so entries never accumulate and a changed prefix leaves no stale
# line. zsh and bash get the rc edit; every other shell gets a one-liner.
rc=""
case "${SHELL:-}" in
    */zsh)  rc="$HOME/.zshrc" ;;
    */bash) rc="$HOME/.bashrc" ;;
esac

block="# >>> ppm >>>
export PATH=\"$PREFIX/bin:\$PATH\"
# <<< ppm <<<"

if [ -n "$rc" ]; then
    if [ -f "$rc" ]; then
        rest=$(sed -e '/^# >>> ppm >>>$/,/^# <<< ppm <<<$/d' "$rc")
    else
        rest=""
    fi
    if [ -n "$rest" ]; then
        new="$rest
$block"
    else
        new="$block"
    fi
    # Write only when the file actually changes, so a same-prefix reinstall
    # does not touch the rc.
    if [ ! -f "$rc" ] || [ "$(cat "$rc")" != "$new" ]; then
        printf '%s\n' "$new" > "$rc"
    fi
    printf 'added %s/bin to your PATH in %s\n' "$PREFIX" "$rc"
    printf 'open a new terminal, or run: source %s\n' "$rc"
    printf '  export PATH="%s/bin:$PATH"\n' "$PREFIX"
else
    printf 'add %s/bin to your PATH:\n' "$PREFIX"
    case "${SHELL:-}" in
        */fish)
            printf '  fish_add_path %s/bin\n' "$PREFIX" ;;
        *)
            printf '  export PATH="%s/bin:$PATH"\n' "$PREFIX" ;;
    esac
    printf 'then open a new terminal\n'
fi

printf 'next: npx skills add https://github.com/%s --skill media\n' "$REPO"
