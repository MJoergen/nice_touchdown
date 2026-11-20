# nice_touchdown: the game of TouchDown for the NICE system.
#
# The NICE toolchain comes from nice_cpu at a pinned release tag, and
# touchdown_db, which has the value of every 4x4 position, at a pinned
# commit. make deps fetches them into deps/. NICE_CPU=DIR and NICEEMU=PATH use local
# copies of nice_cpu and niceemu instead. make help lists the targets.

NICE_CPU_TAG    := V2.0
NICEEMU_SHA256  := 7726cfdf6c86513d156ed75e52c287a7d88ef0c82c7bb49fa02558dde0cf7cf1
TOUCHDOWN_DB_COMMIT := 2e00db4dbc666ff60e656b992d097e92a889d517

NICE_CPU ?= deps/nice_cpu-$(NICE_CPU_TAG)
NICEEMU  ?= deps/niceemu-$(NICE_CPU_TAG)-x86_64-linux/niceemu
NICEMON  ?= ../NiceMonMEGA65
PYTHON   ?= python3
CC       ?= cc
CXX      ?= c++
# The version the front ends show: the release tag on a tagged commit,
# such as V1.0, and git describe's name of the commit between tags.
# tools/runtest.sh replaces it with VERSION in the output, so the tests
# do not depend on it.
VERSION  ?= $(shell git describe --tags --match 'V[0-9]*' --always --dirty 2>/dev/null || echo unknown)
export NICEEMU NICEMON VERSION

# How to reach the board: ETH=IP or PORT=DEVICE, as NiceMonMEGA65's tools
# take them. Only the board targets need one.
BOARDLINK = $(if $(ETH),--eth $(ETH),$(if $(PORT),--port $(PORT),$(error ETH=IP or PORT=DEVICE names the board)))

# The MEGA65 gives a program under the monitor the words 0x8000 to
# 0x1EFFF, 92K, for its image, its linear memory, and the NICE stack,
# which the monitor's routines share and use at most 32 words of
# (NiceMonMEGA65 V1.2.1).
BUDGET = 94176

NICEBUILD  = $(PYTHON) $(NICE_CPU)/translator/nicebuild.py
RELEASE_URL = https://github.com/MJoergen/nice_cpu/releases/download/$(NICE_CPU_TAG)
NICEEMU_TGZ = niceemu-$(NICE_CPU_TAG)-x86_64-linux.tar.gz
TOUCHDOWN_DB_RAW = https://raw.githubusercontent.com/MJoergen/touchdown_db/$(TOUCHDOWN_DB_COMMIT)
TOUCHDOWN_DB_SRC = main.cpp index.cpp board.h index.h tablebase.h

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
# The host, without asserts, for the trainer.
FASTFLAGS = -O2 -W -Wall -Wextra -DNDEBUG

.PHONY: FORCE help all test host-test nice-test perft-deep train weights solve pawns size deps clean distclean dist
.PHONY: board-text board-screen board-test
help:
	@echo "nice_touchdown: the game of TouchDown for the NICE system"
	@echo ""
	@echo "  make help          show this text (the default)"
	@echo "  make deps          fetch nice_cpu $(NICE_CPU_TAG), niceemu $(NICE_CPU_TAG), and touchdown_db into deps/"
	@echo "  make all           build both front ends, for niceemu and for the MEGA65, and the text one for the host"
	@echo "  make test          the tests, on the host and in niceemu"
	@echo "  make perft-deep    the perft suite to depth 6, and 10000 games a size, on the host (a minute)"
	@echo "  make train         learn N-tuple weights by TD self-play on the host, and check them on 4x4"
	@echo "                     (TRAIN_ARGS= options of tools/train.c), for experiments"
	@echo "  make weights       learn the engine's weights into src/engine/eval_weights.h (5 minutes)"
	@echo "  make solve         solve the start of boards on the host: who wins (SOLVE_ARGS= options"
	@echo "                     and sizes of tools/solve.c; by default the sizes solved in under 30 s)"
	@echo "  make size          the size of the MEGA65 builds, against the board's budget"
	@echo "  make pawns         draw the pawns again into src/ui/pawns.h (needs Pillow)"
	@echo "  make dist          the archive of a release, and SHA256SUMS, in build/dist/ (VERSION=V1.0)"
	@echo "  make clean         remove build/"
	@echo "  make distclean     remove build/ and deps/"
	@echo ""
	@echo "On the MEGA65, under NiceMonMEGA65's monitor, with ETH=IP (the board's"
	@echo "Ethernet port) or PORT=/dev/ttyUSBn (its debug UART), and the tools of a"
	@echo "checkout of NiceMonMEGA65 (NICEMON=DIR, ../NiceMonMEGA65 by default):"
	@echo ""
	@echo "  make board-text    load and run the text front end, then talk to it over the line"
	@echo "  make board-screen  load and run the windowed front end, for the screen and keyboard"
	@echo "  make board-test    the tests of the text front end, on the board"
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

all: build/nice/td_text.bin build/nice/td_screen.bin build/mega65/td_text.bin build/mega65/td_screen.bin \
     build/host/td_text

build/nice/td_text.bin: $(ENGINE) $(HEADERS) src/ui/text.c src/platform/nice.c $(VERSIONC) $(TOOLCHAIN)
	@mkdir -p $(@D)
	$(NICEBUILD) $(NICEFLAGS) -o $@ $(ENGINE) $(VERSIONC) src/ui/text.c src/platform/nice.c

build/mega65/td_text.bin: $(ENGINE) $(HEADERS) src/ui/text.c src/platform/nice.c $(VERSIONC) $(TOOLCHAIN)
	@mkdir -p $(@D)
	$(NICEBUILD) $(MONFLAGS) -o $@ $(ENGINE) $(VERSIONC) src/ui/text.c src/platform/nice.c

build/nice/td_screen.bin: $(ENGINE) $(HEADERS) src/ui/screen.c src/ui/pawns.h src/platform/nice.c $(VERSIONC) $(TOOLCHAIN)
	@mkdir -p $(@D)
	$(NICEBUILD) $(NICEFLAGS) -o $@ $(ENGINE) $(VERSIONC) src/ui/screen.c src/platform/nice.c

build/mega65/td_screen.bin: $(ENGINE) $(HEADERS) src/ui/screen.c src/ui/pawns.h src/platform/nice.c $(VERSIONC) $(TOOLCHAIN)
	@mkdir -p $(@D)
	$(NICEBUILD) $(MONFLAGS) -o $@ $(ENGINE) $(VERSIONC) src/ui/screen.c src/platform/nice.c

# The glyphs of the pawns. pawns.h is in git, so that a build needs no
# Pillow; make pawns draws it again after a change to mkpawns.py.
pawns:
	$(PYTHON) tools/mkpawns.py src/ui/pawns.h

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

# The trainer of the evaluation (tools/train.c), on the host. It checks
# its weights against touchdown_db's 4x4 database every --report games.
# make train is for experiments; make weights writes the engine's
# weights, src/engine/eval_weights.h, which is in git. A run is the same
# every time, so the weights are what WEIGHTS_ARGS make.
# The databases they check against (--db): touchdown_db's of 4x4, and
# the solver's of 6x4, 5x5 and 4x6, 1.5 million positions in all, which
# a check takes a few seconds for. TRAIN_DBS can add others, such as
# build/gen/solve_7x4.txt and build/gen/solve_6x5.txt, with 12 and 13
# million positions, for --fit.
TRAIN_DBS ?= build/gen/touchdown4x4.txt build/gen/solve_6x4.txt build/gen/solve_5x5.txt build/gen/solve_4x6.txt
TRAIN_ARGS ?= --sizes all --games 3000000 --report 500000
WEIGHTS_ARGS ?= --sizes all --games 10000000 --seed 1 --report 1000000

build/host/train: $(ENGINE) $(HEADERS) tools/train.c
	@mkdir -p $(@D)
	$(CC) $(FASTFLAGS) -o $@ $(ENGINE) tools/train.c -lm

train: build/host/train $(TRAIN_DBS)
	@mkdir -p build/train
	build/host/train $(addprefix --db ,$(TRAIN_DBS)) --save build/train/weights.f32 $(TRAIN_ARGS)

weights: build/host/train $(TRAIN_DBS)
	@mkdir -p build/train
	build/host/train $(addprefix --db ,$(TRAIN_DBS)) $(WEIGHTS_ARGS) --save build/train/weights.f32 \
	  --header src/engine/eval_weights.h

# The solver (tools/solve.c), on the host: whether the side to move
# wins, by a depth-first search with a table of the positions solved.
SOLVE_ARGS ?= 4x4 5x4 6x4 7x4 8x4 9x4 10x4 4x5 5x5 6x5 7x5 8x5 4x6 5x6 6x6

build/host/solve: $(ENGINE) $(HEADERS) tools/solve.c
	@mkdir -p $(@D)
	$(CC) $(FASTFLAGS) -pthread -o $@ $(ENGINE) tools/solve.c

solve: build/host/solve
	build/host/solve $(SOLVE_ARGS)

# The databases of the solver: every position that can come from the
# start of a size, with its value (tools/db.h), such as
# build/gen/solve_6x4.txt. 7x4 and 6x5, the largest, have 12 and 13
# million positions, and take 10 seconds each.
build/gen/solve_%.txt: build/host/solve
	@mkdir -p $(@D)
	build/host/solve --hash 1024 --dump $@.tmp $* > /dev/null
	mv $@.tmp $@

# touchdown_db, and its database of the 4x4 board: the value of every
# position with at most 4 pawns a side (tests/tablebase.c). It needs
# <cstdint> included, which its sources leave to older compilers, and
# it keeps a file of its work at /tmp/touchdown.known, which the build
# moves into build/.
build/host/touchdown_db: $(addprefix deps/touchdown_db/,$(TOUCHDOWN_DB_SRC))
	@mkdir -p $(@D) build/gen
	sed 's|/tmp/touchdown.known|$(abspath build/gen/touchdown.known)|' deps/touchdown_db/main.cpp > build/gen/touchdown_db.cpp
	$(CXX) -O2 -include cstdint -I deps/touchdown_db -o $@ build/gen/touchdown_db.cpp deps/touchdown_db/index.cpp

build/gen/touchdown4x4.txt: build/host/touchdown_db
	rm -f build/gen/touchdown.tb build/gen/touchdown.known
	build/host/touchdown_db build/gen/touchdown.tb > /dev/null
	build/host/touchdown_db -d build/gen/touchdown.tb > $@.tmp
	mv $@.tmp $@

deps/touchdown_db/%:
	@mkdir -p $(@D)
	curl -sSfL -o $@ $(TOUCHDOWN_DB_RAW)/$*

build/host/tablebase: $(ENGINE) $(HEADERS) tests/tablebase.c
	@mkdir -p $(@D)
	$(CC) $(HOSTFLAGS) -o $@ $(ENGINE) tests/tablebase.c -lm

test: host-test nice-test

host-test: build/host/td_text build/host/perftsuite build/host/tablebase build/gen/touchdown4x4.txt \
           build/host/train build/host/solve build/gen/solve_6x4.txt build/gen/solve_5x5.txt build/gen/solve_4x6.txt
	@tools/runtest.sh host_evalcheck evalcheck /dev/null host build/host/train --check-engine 2700
	@tools/runtest.sh host_text text tests/text.input host build/host/td_text
	@tools/runtest.sh host_perft perft tests/perft.input host build/host/td_text
	@tools/runtest.sh host_perftsuite5 perftsuite5 /dev/null host build/host/perftsuite 5 1000
	@tools/runtest.sh host_tablebase tablebase build/gen/touchdown4x4.txt host build/host/tablebase 24
	@tools/runtest.sh host_tablebase_6x4 tablebase_6x4 build/gen/solve_6x4.txt host build/host/tablebase 32
	@tools/runtest.sh host_tablebase_5x5 tablebase_5x5 build/gen/solve_5x5.txt host build/host/tablebase 32
	@tools/runtest.sh host_evaldb evaldb build/gen/touchdown4x4.txt host build/host/tablebase eval
	@for s in 6x4 5x5 4x6; do \
	  tools/runtest.sh host_evaldb_$$s evaldb_$$s build/gen/solve_$$s.txt host build/host/tablebase eval || exit 1; \
	done
	@tools/runtest.sh host_solve solve /dev/null host build/host/solve --hash 64 --order plain \
	  --check build/gen/touchdown4x4.txt 4x4 5x4 6x4 7x4 4x5 5x5 6x5 4x6 5x6
	@tools/runtest.sh host_search search tests/search.input host build/host/td_text

SCREEN_TESTS = screen screen_size screen_game screen_quit

nice-test: build/nice/td_text.bin build/test/mon_td_text.bin build/nice/perftsuite.bin \
           build/nice/td_screen.bin build/test/mon_td_screen.bin | $(NICEEMU)
	@for t in $(SCREEN_TESTS); do \
	  tools/runtest.sh nice_$$t $$t tests/$$t.keys screen build/nice/td_screen.bin || exit 1; \
	  tools/runtest.sh mega65_$$t $$t tests/$$t.keys screen build/test/mon_td_screen.bin || exit 1; \
	done
	@tools/runtest.sh nice_text text tests/text.input nice build/nice/td_text.bin
	@tools/runtest.sh nice_perft perft tests/perft.input nice build/nice/td_text.bin
	@tools/runtest.sh nice_search search tests/search.input nice build/nice/td_text.bin
	@tools/runtest.sh mega65_text text tests/text.input nice build/test/mon_td_text.bin
	@tools/runtest.sh nice_perftsuite2 perftsuite2 /dev/null nice build/nice/perftsuite.bin

perft-deep: build/host/perftsuite
	@tools/runtest.sh host_perftsuite6 perftsuite6 /dev/null host build/host/perftsuite 6 10000

# The size of the MEGA65 builds, and the peaks of their stacks in a run
# of a search to depth 24 and of the screen's test.
size: build/mega65/td_text.bin build/mega65/td_screen.bin build/test/mon_td_text.bin build/test/mon_td_screen.bin | $(NICEEMU)
	$(PYTHON) tools/size.py --emu $(NICEEMU) --input tests/size.input --run build/test/mon_td_text.bin \
	  --budget $(BUDGET) --check build/mega65/td_text.bin
	$(PYTHON) tools/size.py --emu $(NICEEMU) --input tests/screen.keys --run build/test/mon_td_screen.bin \
	  --budget $(BUDGET) --check build/mega65/td_screen.bin

# The board. load.py loads a program under the monitor and types
# SYS 0x8000, which runs it; what it prints then comes on the line, such as nc IP 4510 or
# picocom -b 2000000 DEVICE (NiceMonMEGA65's tools/README.md).
board-text: build/mega65/td_text.bin
	$(PYTHON) $(NICEMON)/tools/load.py $(BOARDLINK) --monitor $<

board-screen: build/mega65/td_screen.bin
	$(PYTHON) $(NICEMON)/tools/load.py $(BOARDLINK) --monitor $<

# The text front end's tests, on the board. Its searches have a depth,
# not a time, and set their seed, so the board must give what niceemu
# does. The windowed front end's moves among Even ones come from the
# board's clock, so its tests are for niceemu only.
board-test: build/mega65/td_text.bin
	@BOARDLINK="$(BOARDLINK)" tools/runtest.sh board_text text tests/text.input board build/mega65/td_text.bin
	@BOARDLINK="$(BOARDLINK)" tools/runtest.sh board_perft perft tests/perft.input board build/mega65/td_text.bin
	@BOARDLINK="$(BOARDLINK)" tools/runtest.sh board_search search tests/search.input board build/mega65/td_text.bin

# A release (doc/RELEASE.md): the archive nice_touchdown-VERSION.tar.gz,
# whose directory holds the windowed front end, for the MEGA65 as
# touchdown.bin, which the monitor's load reads by its long name, and
# for niceemu, and README.md, RELEASE_NOTES.md, and LICENSE; and
# SHA256SUMS. The text front end is for development and the tests, and
# is not released. The release workflow runs it with VERSION set to
# the tag, after the tests and make size.
DIST = build/dist
dist: build/mega65/td_screen.bin build/nice/td_screen.bin
	rm -rf $(DIST)
	mkdir -p $(DIST)/nice_touchdown-$(VERSION)/mega65 $(DIST)/nice_touchdown-$(VERSION)/niceemu
	cp build/mega65/td_screen.bin $(DIST)/nice_touchdown-$(VERSION)/mega65/touchdown.bin
	cp build/nice/td_screen.bin $(DIST)/nice_touchdown-$(VERSION)/niceemu/
	cp README.md RELEASE_NOTES.md LICENSE $(DIST)/nice_touchdown-$(VERSION)/
	tar czf $(DIST)/nice_touchdown-$(VERSION).tar.gz -C $(DIST) --owner=0 --group=0 nice_touchdown-$(VERSION)
	cd $(DIST) && sha256sum nice_touchdown-$(VERSION).tar.gz > SHA256SUMS
	@echo "$(DIST)/nice_touchdown-$(VERSION).tar.gz"

deps: $(NICE_CPU) $(NICEEMU) $(addprefix deps/touchdown_db/,$(TOUCHDOWN_DB_SRC))

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
