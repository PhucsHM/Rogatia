# Rogatia -- GPL-3.0
#
# OpenBench compatibility (hard requirements, do not break these):
#   make EXE=Rogatia-abc1234   -- output binary name MUST be honoured
#   make CXX=clang++           -- compiler MUST be overridable
#   make EVALFILE=net.nnue     -- net path MUST be overridable (used from Phase 6)
#
# Local development:
#   make                  optimized native build   -> ./rogatia
#   make debug            -O0 -g, asserts on, UBSan/ASan
#   make perft            standalone perft test runner
#   make release          portable AVX2 build for distribution (no PEXT)
#   make release-pext     the same, with PEXT -- only for a known-good CPU

EXE       ?= rogatia
CXX       ?= clang++
EVALFILE  ?= none

# Not every toolchain has LTO -- w64devkit's gcc, notably, does not. Autodetect
# and fall back rather than failing the build. Force with LTO=yes / LTO=no.
LTO       ?= $(shell printf 'int main(){return 0;}' > .ltotest.cpp 2>/dev/null && \
               $(CXX) -flto -x c++ .ltotest.cpp -o .ltotest.out > /dev/null 2>&1 \
               && echo yes || echo no; rm -f .ltotest.cpp .ltotest.out)

ifeq ($(LTO),yes)
	LTOFLAGS := -flto
else
	LTOFLAGS :=
endif

SRCDIR    := src
TESTDIR   := tests
BUILDDIR  := build

SOURCES   := $(wildcard $(SRCDIR)/*.cpp) $(wildcard $(SRCDIR)/fathom/*.cpp)
OBJECTS   := $(patsubst $(SRCDIR)/%.cpp,$(BUILDDIR)/%.o,$(SOURCES))
DEPS      := $(OBJECTS:.o=.d)

# Perft runner links every core object except the engine's own main.
PERFT_SRC := $(TESTDIR)/run_perft.cpp
PERFT_OBJ := $(filter-out $(BUILDDIR)/main.o,$(OBJECTS))

WARNINGS  := -Wall -Wextra -Wcast-qual -Wshadow -Wno-unused-parameter
CXXFLAGS  := -std=c++20 $(WARNINGS) -MMD -MP -I$(SRCDIR)/fathom
LDFLAGS   :=

ifneq ($(EVALFILE),none)
	# Single-quoted so a path containing spaces stays one argument -- this
	# repo can live under a directory like "Vs Code".
	CXXFLAGS += -DEVALFILE='"$(EVALFILE)"'
endif

# EVALFILE is a compile flag, not a source file, so make cannot see it change:
# `make EVALFILE=b` after `make EVALFILE=a` rebuilds nothing and silently keeps
# net a.  That is invisible and it ruins exactly the comparison this project
# runs most -- a new net SPRT'd against the old one on identical code, where
# both binaries end up carrying the same net and the result reads 0 Elo.
#
# Record the value in a stamp file and make every object depend on it.  The
# stamp is only rewritten when the value actually changes, so this costs one
# rebuild per net switch and nothing otherwise.
EVALSTAMP := $(BUILDDIR)/.evalfile
$(shell mkdir -p $(BUILDDIR) 2>/dev/null; \
        [ "$$(cat $(EVALSTAMP) 2>/dev/null)" = "$(EVALFILE)" ] \
        || printf '%s' "$(EVALFILE)" > $(EVALSTAMP))

# -march=native is right for dev and for the 7700; release uses a portable
# baseline so the binary runs on other people's machines.
ifeq ($(build),)
	build := native
endif

ifeq ($(build),native)
	CXXFLAGS += -O3 -march=native -DNDEBUG $(LTOFLAGS)
	LDFLAGS  += $(LTOFLAGS)
else ifeq ($(build),release)
	# -DROGATIA_NO_PEXT: BMI2 is part of x86-64-v3, so this target would
	# otherwise compile the PEXT indexer -- and x86-64-v3 is also satisfied by
	# Zen 1, Zen 2 and Excavator, where PEXT is microcoded and much slower than
	# the multiply-shift it replaces.  This is the binary other people run, so
	# it takes the path that is fast everywhere.  `make release-pext` opts in.
	CXXFLAGS += -O3 -march=x86-64-v3 -mtune=generic -DNDEBUG -DROGATIA_NO_PEXT \
	            $(LTOFLAGS) -static
	LDFLAGS  += $(LTOFLAGS) -static
else ifeq ($(build),release-pext)
	CXXFLAGS += -O3 -march=x86-64-v3 -mtune=generic -DNDEBUG $(LTOFLAGS) -static
	LDFLAGS  += $(LTOFLAGS) -static
else ifeq ($(build),debug)
	# -march=native so __BMI2__ is defined and the PEXT indexer is the one that
	# gets checked.  Without it this build takes the black-magic path, and the
	# verify_slider_tables() assert -- the only build that runs it, since native
	# and release both carry -DNDEBUG -- has never once compared PEXT to anything.
	CXXFLAGS += -O0 -g3 -march=native -fsanitize=address,undefined -fno-omit-frame-pointer
	LDFLAGS  += -fsanitize=address,undefined
endif

.PHONY: all debug release release-pext perft bench run-perft nnue-test run-nnue keycheck run-keycheck filter clean format

all: $(EXE)

debug:
	@$(MAKE) build=debug EXE=$(EXE)-debug --no-print-directory

release:
	@$(MAKE) build=release EXE=$(EXE) --no-print-directory

# Same portable target with the PEXT indexer turned back on.  Faster on Intel
# Haswell and later and on Zen 3 and later; much SLOWER on Zen 1, Zen 2 and
# Excavator.  Only build this when you know which CPU will run it.
release-pext:
	@$(MAKE) build=release-pext EXE=$(EXE) --no-print-directory

$(EXE): $(OBJECTS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^

$(BUILDDIR)/%.o: $(SRCDIR)/%.cpp $(EVALSTAMP) | $(BUILDDIR)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILDDIR):
	@mkdir -p $(BUILDDIR)

# ---- perft: the Phase 1 correctness gate -----------------------------------
perft: $(PERFT_SRC) $(PERFT_OBJ) | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -I$(SRCDIR) -o $(BUILDDIR)/run_perft $(PERFT_SRC) $(PERFT_OBJ)

run-perft: perft
	@$(BUILDDIR)/run_perft $(TESTDIR)/perft_suite.txt

# ---- nnue: the accumulator's equivalent of the perft gate ------------------
# Incremental updates duplicate make_move's logic and fail silently, so this
# asserts they match a from-scratch refresh at every node of a small tree.
NNUE_SRC := $(TESTDIR)/run_nnue.cpp

nnue-test: $(NNUE_SRC) $(PERFT_OBJ) | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -I$(SRCDIR) -o $(BUILDDIR)/run_nnue $(NNUE_SRC) $(PERFT_OBJ)

run-nnue: nnue-test
	@$(BUILDDIR)/run_nnue "$(EVALFILE)"

# ---- keys: perft for the Zobrist sets --------------------------------------
# Five key sets are maintained on every piece event and, until 2026-07-28,
# nothing checked four of them.  Perft counts nodes and never compares keys;
# unmake_move restores BoardState by popping, so keys round-trip correctly even
# if update_keys attributes a piece to the wrong set.  A bug there stays
# invisible until something READS pawnKey or nonPawnKey -- which correction
# history now does.  Run this after touching update_keys, make_move, or
# anything that adds a key set.
KEYCHECK_SRC := $(TESTDIR)/run_keycheck.cpp

keycheck: $(KEYCHECK_SRC) $(PERFT_OBJ) | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -I$(SRCDIR) -o $(BUILDDIR)/run_keycheck $(KEYCHECK_SRC) $(PERFT_OBJ)

run-keycheck: keycheck
	@$(BUILDDIR)/run_keycheck

# ---- bench: the determinism fingerprint ------------------------------------
# The node count printed here goes in every commit message.
bench: $(EXE)
	@./$(EXE) bench

# Training corpus filter.  Standalone -- it shares no engine source, so it does
# not need EVALFILE and does not care which net is around.
filter: scripts/filter.cpp
	$(CXX) -std=c++20 -O2 -Wall -Wextra -o filter scripts/filter.cpp

clean:
	@rm -rf $(BUILDDIR) $(EXE) $(EXE)-debug

-include $(DEPS)
