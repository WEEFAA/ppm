#!/bin/sh
# tools/test_install.sh -- end-to-end installer check (single seam, run by `make test`).
#
# Exercises install.sh against a local HTTP server serving a fixture release
# tarball built from the real artifacts, and asserts the release-based install
# contract:
#   1. binaries + the bundled ffmpeg/ffprobe pair land in the install dir,
#   2. git is never invoked (a stub git fails loudly if it is),
#   3. a CLI run from the installed tree works using the bundled pair,
#   4. --ffmpeg/--ffprobe overrides are honoured (matched pair, no silent mix),
#   5. installed ppmr resolves the bundled ffmpeg next to itself,
#   6. failure paths exit nonzero with clear guidance: missing asset, and
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

# --- fixture release tarball (real artifacts + bundled pair) ---------------
FIX="$TMP/fix"
mkdir -p "$FIX/bin/ffmpeg"
cp "$ROOT/bin/ppm-video" "$ROOT/bin/ppm-media" "$ROOT/bin/ppm-prompt" "$ROOT/bin/ppmr" "$FIX/bin/"
cp "$ROOT/ffmpeg/ffmpeg" "$ROOT/ffmpeg/ffprobe" "$FIX/bin/ffmpeg/"
tar -czf "$FIX/ppm-$VERSION-$PLATFORM.tar.gz" -C "$FIX/bin" .

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

# --- 1+2: install from release, no clone -----------------------------------
BINDIR="$TMP/installed/bin"
BASE="http://127.0.0.1:$PORT"
PATH="$STUBBIN:$PATH" PPM_REPO="$REPO" PPM_BASE_URL="$BASE" PPM_VERSION="$VERSION" PPM_BIN_DIR="$BINDIR" \
  sh "$ROOT/install.sh" >"$TMP/install.log" 2>&1
grep -q "installed to" "$TMP/install.log" || fail "install did not report success"
for b in ppm-video ppm-media ppm-prompt ppmr; do
  [ -x "$BINDIR/$b" ] || fail "$b not installed"
done
[ -x "$BINDIR/ffmpeg/ffmpeg" ] || fail "bundled ffmpeg not installed"
[ -x "$BINDIR/ffmpeg/ffprobe" ] || fail "bundled ffprobe not installed"
"$BINDIR/ppm-video" --help >/dev/null || fail "ppm-video --help failed"
if grep -q "git was invoked" "$TMP/install.log"; then
  fail "installer cloned (git was invoked)"
fi

# --- 3: a CLI run from the installed tree using the bundled pair -----------
"$ROOT/bin/ppmr" "$ROOT/shaders/plasma.cpp" --size 160x90 --frames 4 --fps 10 -o "$TMP/src.mp4" -q
"$BINDIR/ppm-video" "$TMP/src.mp4" -o "$TMP/regen" --essence 24 -q
[ -s "$TMP/regen/parameters.json" ] || fail "bundled-pair run produced no parameters.json"

# --- 4: overrides honoured (matched pair, no silent mix) -------------------
# Wrappers record that they were invoked, then hand off to the real bundled
# binaries. If the CLIs silently used the bundled pair instead of the override,
# the markers would not appear.
cat > "$TMP/wrap-ffmpeg" <<EOF
#!/bin/sh
echo used >> "$TMP/override-ffmpeg.marker"
exec "$BINDIR/ffmpeg/ffmpeg" "\$@"
EOF
cat > "$TMP/wrap-ffprobe" <<EOF
#!/bin/sh
echo used >> "$TMP/override-ffprobe.marker"
exec "$BINDIR/ffmpeg/ffprobe" "\$@"
EOF
chmod +x "$TMP/wrap-ffmpeg" "$TMP/wrap-ffprobe"
"$BINDIR/ppm-video" "$TMP/src.mp4" -o "$TMP/regen2" --essence 24 \
  --ffmpeg "$TMP/wrap-ffmpeg" --ffprobe "$TMP/wrap-ffprobe" -q
[ -s "$TMP/regen2/parameters.json" ] || fail "override run produced no parameters.json"
[ -f "$TMP/override-ffmpeg.marker" ] || fail "--ffmpeg override was not used"
[ -f "$TMP/override-ffprobe.marker" ] || fail "--ffprobe override was not used"

# --- 5: installed ppmr resolves the bundled ffmpeg next to itself ----------
# The release ships bin/ only, so ppmr cannot compile a shader without headers;
# this check proves only that its ffmpeg lookup finds the bundled pair.
# ppmr's ROOT is one directory above bin, so headers go to <root>/include.
mkdir -p "$TMP/installed/include"
cp "$ROOT"/include/*.hpp "$TMP/installed/include/"
mv "$BINDIR/ffmpeg/ffmpeg" "$BINDIR/ffmpeg/ffmpeg.real"
# The stub records that the bundled location was used, then hands off to the
# real bundled ffmpeg so encoder detection and encoding still work.
cat > "$BINDIR/ffmpeg/ffmpeg" <<EOF
#!/bin/sh
echo used >> "$TMP/ppmr-ffmpeg.marker"
exec "$BINDIR/ffmpeg/ffmpeg.real" "\$@"
EOF
chmod +x "$BINDIR/ffmpeg/ffmpeg"
"$BINDIR/ppmr" "$ROOT/shaders/plasma.cpp" --size 64x36 --frames 2 --fps 5 -o "$TMP/ppmr-test.mp4" -q
[ -f "$TMP/ppmr-ffmpeg.marker" ] || fail "installed ppmr did not use the bundled ffmpeg"
mv "$BINDIR/ffmpeg/ffmpeg.real" "$BINDIR/ffmpeg/ffmpeg"

# --- 6: failure paths ------------------------------------------------------
# missing asset: a version with no tarball on the server
if PPM_REPO="$REPO" PPM_BASE_URL="$BASE" PPM_VERSION=vNOPE PPM_BIN_DIR="$TMP/x1" \
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
if PATH="$STUBBIN:$PATH" PPM_REPO="$REPO" PPM_BASE_URL="$BASE" PPM_VERSION="$VERSION" PPM_BIN_DIR="$TMP/x2" \
    sh "$ROOT/install.sh" >"$TMP/f2.log" 2>&1; then
  fail "unsupported-platform case exited 0"
fi
grep -qi "no pre-built release for this platform" "$TMP/f2.log" || fail "unsupported-platform case gave unclear error"

if [ "$FAIL" -ne 0 ]; then
  echo "test-install: FAILED" >&2
  exit 1
fi
echo "test-install: all passed"
