#!/bin/sh
# ppm installer: downloads pre-built binaries or builds from source.
#   curl -fsSL https://raw.githubusercontent.com/aelfestijo/ppm/main/install.sh | sh
#   PPM_VERSION=v0.1.0 sh install.sh   # pin a specific release
set -eu

REPO="${PPM_REPO:-aelfestijo/ppm}"
CANDIDATES="${PPM_BIN_CANDIDATES:-$HOME/.local/bin:/opt/homebrew/bin:/usr/local/bin}"

on_path() {
    case ":$PATH:" in *":$1:"*) return 0 ;; *) return 1 ;; esac
}

pick_bin_dir() {
    saved_ifs=$IFS; IFS=:
    set -- $CANDIDATES
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

# --- platform detection ---
detect_platform() {
    os=$(uname -s | tr '[:upper:]' '[:lower:]')
    arch=$(uname -m)
    case "$os" in
        darwin)  os="darwin" ;;
        linux)   os="linux" ;;
        *)       echo "ppm install: unsupported OS: $os" >&2; exit 1 ;;
    esac
    case "$arch" in
        arm64|aarch64) arch="arm64" ;;
        x86_64|amd64)  arch="x64" ;;
        *)             echo "ppm install: unsupported arch: $arch" >&2; exit 1 ;;
    esac
    printf '%s-%s' "$os" "$arch"
}

# --- try release download ---
try_release() {
    version="${PPM_VERSION:-}"
    if [ -z "$version" ]; then
        version=$(curl -fsSLI -o /dev/null -w '%{url_effective}' "https://github.com/$REPO/releases/latest" \
            | sed 's|.*/releases/tag/||') || true
    fi
    case "$version" in
        ''|*/*) return 1 ;;
    esac

    platform=$(detect_platform)
    base_url="https://github.com/$REPO/releases/download/$version"

    # download each binary
    for bin in ppm-video ppm-media ppm-prompt ppmr; do
        url="$base_url/${bin}-${platform}.tar.gz"
        if ! curl -fsSL "$url" -o /tmp/ppm-$bin.tar.gz 2>/dev/null; then
            rm -f /tmp/ppm-$bin.tar.gz
            return 1
        fi
    done

    # extract all
    mkdir -p "$BIN_DIR"
    for bin in ppm-video ppm-media ppm-prompt ppmr; do
        tar -xzf /tmp/ppm-$bin.tar.gz -C "$BIN_DIR"
        rm -f /tmp/ppm-$bin.tar.gz
    done

    # download ffmpeg if available
    url="$base_url/ffmpeg-${platform}.tar.gz"
    if curl -fsSL "$url" -o /tmp/ppm-ffmpeg.tar.gz 2>/dev/null; then
        mkdir -p "$BIN_DIR/ffmpeg"
        tar -xzf /tmp/ppm-ffmpeg.tar.gz -C "$BIN_DIR/ffmpeg"
        rm -f /tmp/ppm-ffmpeg.tar.gz
    fi

    printf 'ppm %s installed to %s\n' "$version" "$BIN_DIR"
    return 0
}

# --- build from source ---
build_from_source() {
    tmpdir=$(mktemp -d)
    trap 'rm -rf "$tmpdir"' EXIT

    printf 'cloning ppm...\n'
    git clone --depth 1 "https://github.com/$REPO.git" "$tmpdir/ppm"

    printf 'building ffmpeg (vendored)...\n'
    make -C "$tmpdir/ppm" ffmpeg

    printf 'building binaries...\n'
    make -C "$tmpdir/ppm" -j"$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"

    mkdir -p "$BIN_DIR"
    cp "$tmpdir/ppm/bin/"* "$BIN_DIR/"

    # copy vendored ffmpeg alongside binaries
    mkdir -p "$BIN_DIR/ffmpeg"
    cp "$tmpdir/ppm/ffmpeg/ffmpeg" "$BIN_DIR/ffmpeg/"

    printf 'ppm installed to %s\n' "$BIN_DIR"
}

# --- main ---
if try_release; then
    : # installed from release
else
    printf 'no pre-built release found for this platform; building from source...\n'
    build_from_source
fi

# --- warn about PATH ---
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
