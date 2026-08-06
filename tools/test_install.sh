#!/bin/sh
# tools/test_install.sh -- end-to-end installer check (single seam, run by `make test`).
#
# Exercises install.sh against a local HTTP server serving a fixture release
# tarball built from the real artifacts, and asserts the release-based install
# contract for the dedicated-prefix layout:
#   1. everything lands in one ppm-owned prefix (bin/ + bundled pair, shaders/,
#      include/) and nothing ppm owns lands outside it,
#   2. the shell rc gains exactly one marker-delimited PATH block prepending
#      PPM_DIR/bin; a second install does not duplicate it, and a changed
#      PPM_DIR replaces it,
#   3. a CLI invoked by name with PPM_DIR/bin on PATH (as a fresh shell would)
#      works: ppmr --list lists the shipped shaders, ppmr compiles a shipped
#      shader through the shipped headers and the bundled pair, the compile
#      cache lands inside the prefix, and a bundled-pair analysis produces
#      output,
#   4. a non-zsh/non-bash shell gets a printed one-liner and no rc edit,
#   5. PPM_DIR resolution defaults are honoured (XDG_DATA_HOME/ppm, then
#      ~/.local/share/ppm),
#   6. git is never invoked (a stub git fails loudly if it is),
#   7. failure paths exit nonzero with clear guidance: missing asset, and
#      unsupported platform.
#
# Only external behavior is asserted: what the installer puts on disk, and what
# the installed CLIs do.
set -eu

ROOT=$(cd -- "$(dirname -- "$0")/.." && pwd)
TMP=$(mktemp -d)
FAIL=0

fail() { echo "FAIL: $*" >&2; FAIL=1; }

# --- platform (same mapping as install.sh) ---------------------------------
os=$(uname -s | tr '[:upper:]' '[:lower:]')
arch=$(uname -m)
case "$arch" in
  aarch64) arch=arm64 ;;
  x86_64|amd64) arch=x64 ;;
esac
PLATFORM="$os-$arch"
VERSION=vTEST
REPO=testrepo

# --- fixture release tarball (real artifacts + bundled pair + assets) ------
FIX="$TMP/fix"
mkdir -p "$FIX/bin/ffmpeg" "$FIX/shaders" "$FIX/include"
cp "$ROOT/bin/ppm-video" "$ROOT/bin/ppm-media" "$ROOT/bin/ppm-prompt" "$ROOT/bin/ppmr" "$FIX/bin/"
cp "$ROOT/ffmpeg/ffmpeg" "$ROOT/ffmpeg/ffprobe" "$FIX/bin/ffmpeg/"
cp "$ROOT"/shaders/*.cpp "$FIX/shaders/"
cp "$ROOT"/include/*.hpp "$FIX/include/"
tar -czf "$FIX/ppm-$VERSION-$PLATFORM.tar.gz" -C "$FIX" bin shaders include

# --- local HTTP server -----------------------------------------------------
mkdir -p "$FIX/$REPO/releases/download/$VERSION"
cp "$FIX/ppm-$VERSION-$PLATFORM.tar.gz" "$FIX/$REPO/releases/download/$VERSION/"
PORT=$(python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1",0)); print(s.getsockname()[1]); s.close()')
python3 -m http.server "$PORT" --bind 127.0.0.1 -d "$FIX" >/dev/null 2>&1 &
SERVER=$!
trap 'kill $SERVER 2>/dev/null || true; wait $SERVER 2>/dev/null || true; rm -rf "$TMP"' EXIT
i=0
until curl -fsS -o /dev/null "http://127.0.0.1:$PORT/" 2>/dev/null; do
  i=$((i+1)); [ "$i" -lt 20 ] || { echo "test server did not start" >&2; exit 1; }
  sleep 0.2
done

# --- stub git: the installer must never clone ------------------------------
STUBBIN="$TMP/stubbin"
mkdir -p "$STUBBIN"
cat > "$STUBBIN/git" <<'EOF'
#!/bin/sh
echo "ppm test: git was invoked; the installer must never clone" >&2
exit 99
EOF
chmod +x "$STUBBIN/git"

# --- temp home with a fixture rc -------------------------------------------
HOME_DIR="$TMP/home"
mkdir -p "$HOME_DIR"
printf '# fixture zshrc\n\nexport FOO=bar\n' > "$HOME_DIR/.zshrc"
PREFIX="$HOME_DIR/ppm"
BASE="http://127.0.0.1:$PORT"

# run_install [env assignments...] -- run install.sh with the stub git shadowing
run_install() {
  env PATH="$STUBBIN:$PATH" HOME="$HOME_DIR" SHELL=/usr/bin/zsh \
    PPM_REPO="$REPO" PPM_BASE_URL="$BASE" PPM_VERSION="$VERSION" \
    "$@" sh "$ROOT/install.sh"
}

# --- 1: install from release, no clone -------------------------------------
run_install PPM_DIR="$PREFIX" >"$TMP/install.log" 2>&1
grep -q "installed to" "$TMP/install.log" || fail "install did not report success"

# --- 1a: the prefix holds everything, nothing ppm owns lands outside it ----
for b in ppm-video ppm-media ppm-prompt ppmr; do
  [ -x "$PREFIX/bin/$b" ] || fail "$b not installed in the prefix"
done
[ -x "$PREFIX/bin/ffmpeg/ffmpeg" ] || fail "bundled ffmpeg not installed in the prefix"
[ -x "$PREFIX/bin/ffmpeg/ffprobe" ] || fail "bundled ffprobe not installed in the prefix"
[ -d "$PREFIX/shaders" ] || fail "shaders/ not installed in the prefix"
[ -f "$PREFIX/shaders/plasma.cpp" ] || fail "shipped shaders missing"
[ -d "$PREFIX/include" ] || fail "include/ not installed in the prefix"
[ -f "$PREFIX/include/ppmshader.hpp" ] || fail "shipped headers missing"
for stray in bin shaders include .cache; do
  [ ! -e "$HOME_DIR/$stray" ] || fail "ppm asset landed outside the prefix: $HOME_DIR/$stray"
done

# --- 1b: the rc gains exactly one PATH block prepending PPM_DIR/bin ---------
rc="$HOME_DIR/.zshrc"
[ -f "$rc" ] || fail "installer did not touch the fixture .zshrc"
n=$(grep -c '^# >>> ppm >>>$' "$rc" || true)
[ "$n" -eq 1 ] || fail "expected exactly one PATH block, found $n"
grep -q "^export PATH=\"$PREFIX/bin:\$PATH\"$" "$rc" || fail "PATH block does not prepend PPM_DIR/bin"
grep -q "export FOO=bar" "$rc" || fail "fixture rc content was lost"
grep -q "added .*/bin to your PATH in" "$TMP/install.log" || fail "installer did not report the rc it edited"
grep -q "open a new terminal" "$TMP/install.log" || fail "installer did not say a new terminal is needed"
if grep -q "git was invoked" "$TMP/install.log"; then
  fail "installer cloned (git was invoked)"
fi

# --- 3: a CLI invoked by name, as a fresh shell would -----------------------
PATH="$PREFIX/bin:$PATH" ppmr --list | grep -q "plasma.cpp" \
  || fail "installed ppmr --list found no shipped shaders"
PATH="$PREFIX/bin:$PATH" ppm-video --help >/dev/null || fail "ppm-video --help failed"

# Prove the installed ppmr uses the bundled ffmpeg next to itself: wrap the
# bundled binary so its use is recorded, then restore it.
mv "$PREFIX/bin/ffmpeg/ffmpeg" "$PREFIX/bin/ffmpeg/ffmpeg.real"
cat > "$PREFIX/bin/ffmpeg/ffmpeg" <<EOF
#!/bin/sh
echo used >> "$TMP/bundled-ffmpeg.marker"
exec "$PREFIX/bin/ffmpeg/ffmpeg.real" "\$@"
EOF
chmod +x "$PREFIX/bin/ffmpeg/ffmpeg"

# ppmr compiles a shipped shader through the shipped headers and the bundled
# pair; the compile cache lands inside the prefix.
PATH="$PREFIX/bin:$PATH" ppmr "$PREFIX/shaders/plasma.cpp" --size 64x36 --frames 2 --fps 5 \
  -o "$TMP/ppmr-test.mp4" -q
[ -f "$TMP/bundled-ffmpeg.marker" ] || fail "installed ppmr did not use the bundled ffmpeg"
[ -s "$TMP/ppmr-test.mp4" ] || fail "installed ppmr render produced no output"
[ -d "$PREFIX/.cache" ] || fail "compile cache was not written inside the prefix"
mv "$PREFIX/bin/ffmpeg/ffmpeg.real" "$PREFIX/bin/ffmpeg/ffmpeg"

# a bundled-pair analysis produces output
"$ROOT/bin/ppmr" "$ROOT/shaders/plasma.cpp" --size 160x90 --frames 4 --fps 10 -o "$TMP/src.mp4" -q
PATH="$PREFIX/bin:$PATH" ppm-video "$TMP/src.mp4" -o "$TMP/regen" --essence 24 -q
[ -s "$TMP/regen/parameters.json" ] || fail "bundled-pair run produced no parameters.json"

# --- overrides honoured (matched pair, no silent mix) ----------------------
cat > "$TMP/wrap-ffmpeg" <<EOF
#!/bin/sh
echo used >> "$TMP/override-ffmpeg.marker"
exec "$PREFIX/bin/ffmpeg/ffmpeg" "\$@"
EOF
cat > "$TMP/wrap-ffprobe" <<EOF
#!/bin/sh
echo used >> "$TMP/override-ffprobe.marker"
exec "$PREFIX/bin/ffmpeg/ffprobe" "\$@"
EOF
chmod +x "$TMP/wrap-ffmpeg" "$TMP/wrap-ffprobe"
PATH="$PREFIX/bin:$PATH" ppm-video "$TMP/src.mp4" -o "$TMP/regen2" --essence 24 \
  --ffmpeg "$TMP/wrap-ffmpeg" --ffprobe "$TMP/wrap-ffprobe" -q
[ -s "$TMP/regen2/parameters.json" ] || fail "override run produced no parameters.json"
[ -f "$TMP/override-ffmpeg.marker" ] || fail "--ffmpeg override was not used"
[ -f "$TMP/override-ffprobe.marker" ] || fail "--ffprobe override was not used"

# --- 2: reinstall deduplicates, and a changed PPM_DIR replaces the block ----
run_install PPM_DIR="$PREFIX" >"$TMP/install2.log" 2>&1
n=$(grep -c '^# >>> ppm >>>$' "$rc" || true)
[ "$n" -eq 1 ] || fail "second install duplicated the PATH block"

PREFIX2="$HOME_DIR/ppm2"
run_install PPM_DIR="$PREFIX2" >"$TMP/install3.log" 2>&1
n=$(grep -c '^# >>> ppm >>>$' "$rc" || true)
[ "$n" -eq 1 ] || fail "changed PPM_DIR left a stale block"
grep -q "^export PATH=\"$PREFIX2/bin:\$PATH\"$" "$rc" || fail "PATH block was not replaced for the new PPM_DIR"
if grep -q "export PATH=\"$PREFIX/bin" "$rc"; then
  fail "old PPM_DIR still referenced in the rc"
fi

# --- 4: a non-zsh/non-bash shell gets a one-liner and no rc edit -----------
FISH_HOME="$TMP/home-fish"
mkdir -p "$FISH_HOME"
PATH="$STUBBIN:$PATH" HOME="$FISH_HOME" SHELL=/usr/bin/fish PPM_DIR="$FISH_HOME/ppm" \
  PPM_REPO="$REPO" PPM_BASE_URL="$BASE" PPM_VERSION="$VERSION" \
  sh "$ROOT/install.sh" >"$TMP/install-fish.log" 2>&1
grep -q "fish_add_path" "$TMP/install-fish.log" || fail "fish user got no one-liner"
[ ! -f "$FISH_HOME/.zshrc" ] || fail "fish user got a zshrc edit"
[ ! -f "$FISH_HOME/.bashrc" ] || fail "fish user got a bashrc edit"

# --- 5: default prefix resolution (XDG, then ~/.local/share/ppm) -----------
XDG_HOME="$TMP/home-xdg"
mkdir -p "$XDG_HOME"
printf 'export FOO=bar\n' > "$XDG_HOME/.zshrc"
PATH="$STUBBIN:$PATH" HOME="$XDG_HOME" SHELL=/usr/bin/zsh XDG_DATA_HOME="$TMP/xdg" \
  PPM_REPO="$REPO" PPM_BASE_URL="$BASE" PPM_VERSION="$VERSION" \
  sh "$ROOT/install.sh" >"$TMP/install-xdg.log" 2>&1
[ -x "$TMP/xdg/ppm/bin/ppmr" ] || fail "XDG_DATA_HOME/ppm was not honoured"
grep -q '^# >>> ppm >>>$' "$XDG_HOME/.zshrc" || fail "XDG install did not add a PATH block"

DEF_HOME="$TMP/home-def"
mkdir -p "$DEF_HOME"
printf 'export FOO=bar\n' > "$DEF_HOME/.zshrc"
PATH="$STUBBIN:$PATH" HOME="$DEF_HOME" SHELL=/usr/bin/zsh \
  PPM_REPO="$REPO" PPM_BASE_URL="$BASE" PPM_VERSION="$VERSION" \
  sh "$ROOT/install.sh" >"$TMP/install-def.log" 2>&1
[ -x "$DEF_HOME/.local/share/ppm/bin/ppmr" ] || fail "default prefix was not ~/.local/share/ppm"
grep -q '^# >>> ppm >>>$' "$DEF_HOME/.zshrc" || fail "default install did not add a PATH block"

# --- 7: failure paths ------------------------------------------------------
# missing asset: a version with no tarball on the server
if PATH="$STUBBIN:$PATH" HOME="$HOME_DIR" SHELL=/usr/bin/zsh PPM_DIR="$TMP/x1" \
    PPM_REPO="$REPO" PPM_BASE_URL="$BASE" PPM_VERSION=vNOPE \
    sh "$ROOT/install.sh" >"$TMP/f1.log" 2>&1; then
  fail "missing-asset case exited 0"
fi
grep -qi "no release asset" "$TMP/f1.log" || fail "missing-asset case gave unclear error"

# unsupported platform: stub uname reports an OS/arch with no release assets
cat > "$STUBBIN/uname" <<'EOF'
#!/bin/sh
case "$1" in
  -s) echo plan9 ;;
  -m) echo riscv64 ;;
  *) exec /usr/bin/uname "$@" ;;
esac
EOF
chmod +x "$STUBBIN/uname"
if PATH="$STUBBIN:$PATH" HOME="$HOME_DIR" SHELL=/usr/bin/zsh PPM_DIR="$TMP/x2" \
    PPM_REPO="$REPO" PPM_BASE_URL="$BASE" PPM_VERSION="$VERSION" \
    sh "$ROOT/install.sh" >"$TMP/f2.log" 2>&1; then
  fail "unsupported-platform case exited 0"
fi
grep -qi "no pre-built release for this platform" "$TMP/f2.log" || fail "unsupported-platform case gave unclear error"

if [ "$FAIL" -ne 0 ]; then
  echo "test-install: FAILED" >&2
  exit 1
fi
echo "test-install: all passed"
