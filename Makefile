# Build the three CLIs. Each is one translation unit over header-only code, so
# there is no link step, no object graph and no configure stage.

CXX      ?= c++
CXXFLAGS ?= -std=c++17 -O3 -ffp-contract=off -Wall -Wextra -Wno-unused-parameter
INCLUDE  := -Iinclude
LDFLAGS  := -pthread

CLIS     := ppm-video ppm-media ppm-prompt
BINS     := $(addprefix bin/,$(CLIS))
HEADERS  := $(wildcard include/*.hpp)
SHADERS  := $(wildcard shaders/*.cpp)
FFMPEG   := ffmpeg/ffmpeg
NPROC    := $(shell (nproc 2>/dev/null || sysctl -n hw.ncpu) 2>/dev/null || echo 4)

.PHONY: all help check shaders ffmpeg pgl release clean distclean test

all: $(BINS)

help:
	@printf 'Targets:\n'
	@printf '  all        build the three CLIs into bin/ (default)\n'
	@printf '  ffmpeg     clone and build the vendored ffmpeg\n'
	@printf '  test       self-check: generate, analyse, regenerate, verify\n'
	@printf '  check      compile every shader in shaders/\n'
	@printf '  release    assemble the per-platform release tarball\n'
	@printf '  pgl        regenerate include/pgl.hpp from tools/gen_pgl.py\n'
	@printf '  clean      remove built binaries and the shader cache\n'
	@printf '  distclean  also remove out/ and the ffmpeg build\n'
	@printf '\n'
	@printf 'The three CLIs:\n'
	@printf '  ppm-video  VIDEO  in -> analyse every frame -> regenerate -> video\n'
	@printf '  ppm-media  IMAGE  in -> analyse -> infer a camera move -> video\n'
	@printf '  ppm-prompt PROMPT in -> model returns parameters -> video\n'
	@printf '\n'
	@printf 'Each writes frames, a video, and parameters.json into one directory.\n'

bin/%: src/%.cpp $(HEADERS)
	@mkdir -p bin
	$(CXX) $(CXXFLAGS) $(INCLUDE) $(LDFLAGS) -o $@ $<

# Vendored ffmpeg, used only as a codec. ffprobe is built from the same tree, so
# there is one version to pin, not two.
#
# --disable-autodetect refuses every optional external library, so the build
# cannot silently pick up whatever happens to be installed. zlib has to be
# re-enabled explicitly afterwards: without it there is no PNG encoder, and
# still-image output fails with a confusing "encoder is probably disabled".
FFMPEG_URL    := $(shell sed -n 's/^url=//p' ffmpeg.lock)
FFMPEG_COMMIT := $(shell sed -n 's/^commit=//p' ffmpeg.lock)

# The verify step is its own phony prerequisite so it runs even when the binary
# already exists. Folded into the build rule it would be skipped exactly when it
# matters most: an existing checkout that has drifted off the pin.
ffmpeg: ffmpeg-verify $(FFMPEG)

.PHONY: ffmpeg-verify
ffmpeg-verify:
	@test -n "$(FFMPEG_COMMIT)" || { echo 'ffmpeg.lock has no commit= line'; exit 1; }
	@if [ -d ffmpeg/.git ]; then \
	  have=$$(git -C ffmpeg rev-parse HEAD); \
	  if [ "$$have" != "$(FFMPEG_COMMIT)" ]; then \
	    printf 'ffmpeg is at %s\nffmpeg.lock pins  %s\nRun: git -C ffmpeg checkout %s\n' \
	      "$$have" "$(FFMPEG_COMMIT)" "$(FFMPEG_COMMIT)"; exit 1; \
	  fi; \
	fi

$(FFMPEG):
	@test -n "$(FFMPEG_COMMIT)" || { echo 'ffmpeg.lock has no commit= line'; exit 1; }
	@if [ ! -d ffmpeg/.git ]; then \
	  echo "fetching ffmpeg at $(FFMPEG_COMMIT)"; \
	  git init -q ffmpeg && git -C ffmpeg remote add origin $(FFMPEG_URL) && \
	  { git -C ffmpeg fetch -q --depth 1 origin $(FFMPEG_COMMIT) \
	    || git -C ffmpeg fetch -q origin; } && \
	  git -C ffmpeg checkout -q $(FFMPEG_COMMIT); \
	fi
	@# Refuse to build anything other than the pinned revision. Analysis output is
	@# only comparable across runs if the decoder is byte-identical.
	@have=$$(git -C ffmpeg rev-parse HEAD); \
	 if [ "$$have" != "$(FFMPEG_COMMIT)" ]; then \
	   printf 'ffmpeg is at %s\nffmpeg.lock pins  %s\nRun: git -C ffmpeg checkout %s\n' \
	     "$$have" "$(FFMPEG_COMMIT)" "$(FFMPEG_COMMIT)"; exit 1; \
	 fi
	cd ffmpeg && ./configure \
	  --disable-doc --disable-network --disable-autodetect --disable-debug \
	  --enable-zlib $(if $(filter Darwin,$(shell uname -s)),--enable-videotoolbox,) \
	  && $(MAKE) -j$(NPROC)

# Assemble the per-platform release tarball that install.sh downloads. The
# layout mirrors bin/ at the top level (no bin/ prefix): install.sh unpacks it
# straight into the install directory, where find_tool's exe_dir/ffmpeg lookup
# and ppmr's dirname($0)/ffmpeg lookup both find the bundled pair.
#
# The version must match the release tag exactly (e.g. v0.1.0), because
# install.sh builds the download URL from it. Default to the newest tag; pass
# RELEASE_VERSION=v0.1.1 to override without tagging.
VERSION  := $(shell git describe --tags --abbrev=0 2>/dev/null || echo dev)
RELEASE_VERSION ?= $(VERSION)

# Same os-arch scheme as install.sh: darwin/linux, arm64/x64.
OS   := $(shell uname -s | tr '[:upper:]' '[:lower:]')
ARCH := $(shell uname -m | sed -e 's/aarch64/arm64/; s/x86_64/x64/; s/amd64/x64/')
PLATFORM := $(OS)-$(ARCH)

release: all ffmpeg
	@rm -rf out/release
	@mkdir -p out/release/bin/ffmpeg out/release/shaders
	@cp bin/ppm-video bin/ppm-media bin/ppm-prompt bin/ppmr out/release/bin/
	@cp ffmpeg/ffmpeg ffmpeg/ffprobe out/release/bin/ffmpeg/
	@cp $(SHADERS) out/release/shaders/
	@tar -czf "out/release/ppm-$(RELEASE_VERSION)-$(PLATFORM).tar.gz" -C out/release bin shaders
	@printf 'release tarball: out/release/ppm-%s-%s.tar.gz\n' "$(RELEASE_VERSION)" "$(PLATFORM)"
	@printf 'upload with:     gh release upload %s out/release/ppm-%s-%s.tar.gz\n' \
	  "$(RELEASE_VERSION)" "$(RELEASE_VERSION)" "$(PLATFORM)"

# Move the pin to the current upstream HEAD. Deliberately a separate target: it
# changes measured output, so it should never happen as a side effect of `make`.
.PHONY: ffmpeg-update
ffmpeg-update:
	git -C ffmpeg fetch origin
	@sha=$$(git -C ffmpeg rev-parse origin/master); \
	 git -C ffmpeg checkout -q $$sha; \
	 sed -i.bak "s/^commit=.*/commit=$$sha/" ffmpeg.lock && rm -f ffmpeg.lock.bak; \
	 printf 'pinned to %s -- rebuild with `make ffmpeg`, then re-check figures in docs/\n' "$$sha"

# End-to-end self-check. Renders a source clip with the shader runtime, then puts
# it through analysis and regeneration and asserts the outputs exist and are sane.
test: all
	@set -e; \
	tmp=$$(mktemp -d); \
	printf 'test: rendering a source clip\n'; \
	./bin/ppmr shaders/plasma.cpp --size 320x180 --frames 16 --fps 12 -o $$tmp/src.mp4 -q; \
	printf 'test: ppm-video\n'; \
	./bin/ppm-video $$tmp/src.mp4 -o $$tmp/regen --essence 32 -q; \
	test -s $$tmp/regen/parameters.json || { echo 'FAIL: no parameters.json'; exit 1; }; \
	test -s $$tmp/regen/src-regen.mp4  || { echo 'FAIL: no video'; exit 1; }; \
	printf 'test: ppm-media\n'; \
	./bin/ppmr shaders/flow.cpp --preview 0 --size 320x200 -o $$tmp/still.png -q; \
	./bin/ppm-media $$tmp/still.png -o $$tmp/clip --frames 8 -q; \
	test -s $$tmp/clip/still-clip.mp4 || { echo 'FAIL: no clip'; exit 1; }; \
	printf 'test: ppm-prompt (offline paths only)\n'; \
	./bin/ppm-prompt --print-system-prompt > $$tmp/sys.txt; \
	test -s $$tmp/sys.txt || { echo 'FAIL: empty system prompt'; exit 1; }; \
	./bin/ppm-prompt --params $$tmp/regen/parameters.json -o $$tmp/rerender --frames 4 -q; \
	test -s $$tmp/rerender/parameters.mp4 || { echo 'FAIL: no re-render'; exit 1; }; \
	rm -rf $$tmp; \
	printf 'test: install-from-release\n'; \
	sh tools/test_install.sh; \
	printf 'test: all passed\n'

check shaders:
	@for s in $(SHADERS); do \
	  printf '%-32s' "$$s"; \
	  if $(CXX) $(CXXFLAGS) $(INCLUDE) $(LDFLAGS) -fsyntax-only $$s 2>/tmp/ppm-check.log; then \
	    printf 'ok\n'; \
	  else \
	    printf 'FAILED\n'; cat /tmp/ppm-check.log; exit 1; \
	  fi; \
	done

pgl:
	python3 tools/gen_pgl.py > include/pgl.hpp
	@printf 'regenerated include/pgl.hpp (%s lines)\n' "$$(wc -l < include/pgl.hpp | tr -d ' ')"

clean:
	rm -rf $(BINS) .cache

distclean: clean
	rm -rf out
	@test -d ffmpeg && $(MAKE) -C ffmpeg distclean || true
