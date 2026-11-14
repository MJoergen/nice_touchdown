# nice_touchdown: the game of TouchDown for the NICE system.
#
# The NICE toolchain comes from nice_cpu at a pinned release tag. make
# deps fetches it into deps/. NICE_CPU=DIR and NICEEMU=PATH use local
# copies of nice_cpu and niceemu instead. make help lists the targets.

NICE_CPU_TAG    := V2.0
NICEEMU_SHA256  := 7726cfdf6c86513d156ed75e52c287a7d88ef0c82c7bb49fa02558dde0cf7cf1

NICE_CPU ?= deps/nice_cpu-$(NICE_CPU_TAG)
NICEEMU  ?= deps/niceemu-$(NICE_CPU_TAG)-x86_64-linux/niceemu
PYTHON   ?= python3
CC       ?= cc
# The version the front ends show: the release tag on a tagged commit,
# such as V1.0, and git describe's name of the commit between tags.
# tools/runtest.sh replaces it with VERSION in the output, so the tests
# do not depend on it.
VERSION  ?= $(shell git describe --tags --match 'V[0-9]*' --always --dirty 2>/dev/null || echo unknown)
export NICEEMU VERSION

NICEBUILD  = $(PYTHON) $(NICE_CPU)/translator/nicebuild.py
RELEASE_URL = https://github.com/MJoergen/nice_cpu/releases/download/$(NICE_CPU_TAG)
NICEEMU_TGZ = niceemu-$(NICE_CPU_TAG)-x86_64-linux.tar.gz

# The engine: no I/O, no clock, no malloc, so the same files build for
# NICE and for the host.
ENGINE = $(wildcard src/engine/*.c)
# The headers, and the Makefile, whose flags every build takes, so that
# a change of the flags rebuilds.
HEADERS = $(wildcard src/engine/*.h) src/platform/platform.h src/ui/version.h Makefile
# The version, for the front ends (VERSION).
VERSIONC = build/gen/version.c

# NICE: optimized for speed, without asserts, and without the bounds
# checks of wasm's memory accesses, which the host build's asserts
# stand in for. --byte-windows reaches the memory through the byte
# windows of nice_cpu's SYSTEM.md §9 (niceemu and NiceMonMEGA65 from
# V2.0 on).
NICEFLAGS = -O 2 -D NDEBUG --no-bounds-checks --trust-alignment --byte-windows \
            --stack 8192 --symbols --keep
# The MEGA65: the same, built for the monitor, which loads it at 0x8000.
MONFLAGS = $(NICEFLAGS) --monitor

# The host: with asserts.
HOSTFLAGS = -O2 -W -Wall -Wextra

.PHONY: FORCE help all test host-test nice-test perft-deep deps clean distclean
help:
	@echo "nice_touchdown: the game of TouchDown for the NICE system"
	@echo ""
	@echo "  make help          show this text (the default)"
	@echo "  make deps          fetch nice_cpu $(NICE_CPU_TAG) and niceemu $(NICE_CPU_TAG) into deps/"
	@echo "  make all           build the text front end, for niceemu, for the MEGA65, and for the host"
	@echo "  make test          the tests, on the host and in niceemu"
	@echo "  make perft-deep    the perft suite to depth 6, and 10000 games a size, on the host (a minute)"
	@echo "  make clean         remove build/"
	@echo "  make distclean     remove build/ and deps/"
	@echo ""
	@echo "NICE_CPU=DIR and NICEEMU=PATH use local copies of nice_cpu and niceemu."

# The NICE builds depend on the toolchain: its directory and commit, in
# a file that changes only when they do, so that a new pin or another
# NICE_CPU=DIR builds them again.
TOOLCHAIN = build/toolchain.txt
$(TOOLCHAIN): FORCE | $(NICE_CPU)
	@mkdir -p $(@D)
	@t="$(abspath $(NICE_CPU)) $$(git -C $(NICE_CPU) rev-parse HEAD 2>/dev/null)"; \
	  [ "$$t" = "$$(cat $@ 2>/dev/null)" ] || echo "$$t" > $@

$(VERSIONC): FORCE
	@mkdir -p $(@D)
	@t='const char nice_touchdown_version[] = "$(VERSION)";'; \
	  [ "$$t" = "$$(cat $@ 2>/dev/null)" ] || echo "$$t" > $@

FORCE:

all: build/nice/td_text.bin build/mega65/td_text.bin build/host/td_text

build/nice/td_text.bin: $(ENGINE) $(HEADERS) src/ui/text.c src/platform/nice.c $(VERSIONC) $(TOOLCHAIN)
	@mkdir -p $(@D)
	$(NICEBUILD) $(NICEFLAGS) -o $@ $(ENGINE) $(VERSIONC) src/ui/text.c src/platform/nice.c

build/mega65/td_text.bin: $(ENGINE) $(HEADERS) src/ui/text.c src/platform/nice.c $(VERSIONC) $(TOOLCHAIN)
	@mkdir -p $(@D)
	$(NICEBUILD) $(MONFLAGS) -o $@ $(ENGINE) $(VERSIONC) src/ui/text.c src/platform/nice.c

# nice_cpu's stand-in for a monitor at 0, which runs a program at 0x8000
# as the monitor does, so that the MEGA65 builds run in niceemu too,
# joined after it (a .bin file is address and word pairs).
build/nice/monstub.bin: $(NICE_CPU)/runtime/test/monitor.nasm $(TOOLCHAIN)
	@mkdir -p $(@D)
	$(PYTHON) $(NICE_CPU)/assembler/niceasm.py -o $@ $<

build/test/mon_%.bin: build/nice/monstub.bin build/mega65/%.bin
	@mkdir -p $(@D)
	cat $^ > $@

build/host/td_text: $(ENGINE) $(HEADERS) src/ui/text.c src/platform/host.c $(VERSIONC)
	@mkdir -p $(@D)
	$(CC) $(HOSTFLAGS) -o $@ $(ENGINE) $(VERSIONC) src/ui/text.c src/platform/host.c

# The perft suite (tests/perftsuite.c): the engine's move generator
# against a slow one, on every size of board. On NICE, which passes no
# arguments, the depth and the games are set when it is built; the
# checks are slow there, about 40 million steps a game.
build/host/perftsuite: $(ENGINE) $(HEADERS) tests/perftsuite.c
	@mkdir -p $(@D)
	$(CC) $(HOSTFLAGS) -o $@ $(ENGINE) tests/perftsuite.c

build/nice/perftsuite.bin: $(ENGINE) $(HEADERS) tests/perftsuite.c $(TOOLCHAIN)
	@mkdir -p $(@D)
	$(NICEBUILD) $(NICEFLAGS) -D PERFT_DEPTH=2 -D PERFT_GAMES=4 -o $@ $(ENGINE) tests/perftsuite.c

test: host-test nice-test

host-test: build/host/td_text build/host/perftsuite
	@tools/runtest.sh host_text text tests/text.input host build/host/td_text
	@tools/runtest.sh host_perftsuite5 perftsuite5 /dev/null host build/host/perftsuite 5 1000

nice-test: build/nice/td_text.bin build/test/mon_td_text.bin build/nice/perftsuite.bin | $(NICEEMU)
	@tools/runtest.sh nice_text text tests/text.input nice build/nice/td_text.bin
	@tools/runtest.sh mega65_text text tests/text.input nice build/test/mon_td_text.bin
	@tools/runtest.sh nice_perftsuite2 perftsuite2 /dev/null nice build/nice/perftsuite.bin

perft-deep: build/host/perftsuite
	@tools/runtest.sh host_perftsuite6 perftsuite6 /dev/null host build/host/perftsuite 6 10000

deps: $(NICE_CPU) $(NICEEMU)

deps/nice_cpu-$(NICE_CPU_TAG):
	git clone --quiet --depth 1 --branch $(NICE_CPU_TAG) -c advice.detachedHead=false \
	  https://github.com/MJoergen/nice_cpu $@

deps/niceemu-$(NICE_CPU_TAG)-x86_64-linux/niceemu:
	@mkdir -p deps
	curl -sSfL -o deps/$(NICEEMU_TGZ) $(RELEASE_URL)/$(NICEEMU_TGZ)
	echo "$(NICEEMU_SHA256)  deps/$(NICEEMU_TGZ)" | sha256sum -c --quiet
	tar xzf deps/$(NICEEMU_TGZ) -C deps
	rm deps/$(NICEEMU_TGZ)
	@touch $@

clean:
	rm -rf build

distclean: clean
	rm -rf deps
