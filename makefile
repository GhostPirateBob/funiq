# funiq: the C++ tool (the release) plus Rust and Go ports for benchmarking.
# Portable across Linux (x86_64, arm64), macOS, MinGW and Cygwin; needs GNU
# make and a POSIX sh. Override the compiler with `make CXX=...` or the CXX
# environment variable, cargo and go with CARGO=... and GO=....
CXX ?= g++
CXXFLAGS=-Wall -g -std=c++11 -pthread
OPT_FLAG=-O2
CARGO ?= cargo
GO ?= go

ifeq ($(OS),Windows_NT)
  EXE=.exe
  # A bare g++ on Windows may be Cygwin's, whose binaries need cygwin1.dll.
  # Unless a compiler was chosen explicitly, prefer the MinGW triplet name
  # (shipped by winlibs/mingw-w64, also as a Cygwin cross package) so the
  # default build is a standalone native .exe.
  ifeq ($(origin CXX),default)
    ifneq (,$(findstring mingw,$(shell x86_64-w64-mingw32-g++ -dumpmachine 2>/dev/null)))
      CXX := x86_64-w64-mingw32-g++
    endif
  endif
endif

# MinGW: fully static so the .exe runs without libstdc++-6.dll,
# libgcc_s_seh-1.dll or libwinpthread-1.dll (-static-libstdc++ alone still
# leaves winpthread as a dependency on posix-thread toolchains).
# Keyed on the compiler's target, not the OS: Cygwin also sets
# OS=Windows_NT, but its binaries always need cygwin1.dll and a -static
# libstdc++ link fails there (duplicate std::logic_error symbols).
ifneq (,$(findstring mingw,$(shell $(CXX) -dumpmachine)))
  LDFLAGS+=-static
endif

CPP_INCLUDES=-Isrc/cpp -Ilib
OUT=bin/funiq$(EXE)
OUT_TEST=bin/funiq_test$(EXE)
OUT_MSVC=bin/funiq-msvc$(EXE)
OUT_RUST=bin/funiq-rs$(EXE)
OUT_GO=bin/funiq-go$(EXE)
HEADERS=$(wildcard src/cpp/funiq/*.h)

# The languages `make` builds and tests: C++ always, Rust and Go when their
# sources exist and their toolchain answers, so a machine with only a C++
# compiler still builds funiq.
LANGS := cpp
ifneq ($(wildcard src/rust/Cargo.toml),)
  ifneq ($(shell $(CARGO) --version 2>/dev/null),)
    LANGS += rust
  endif
endif
ifneq ($(wildcard src/go/go.mod),)
  ifneq ($(shell $(GO) version 2>/dev/null),)
    LANGS += go
  endif
endif

# Use DESTDIR and PREFIX for flexible install paths, or set BINDIR directly
# (e.g. make install BINDIR=C:/PROGRAMS/__PATH).
# If PREFIX is not set, try ~/.local/bin first (no sudo needed), then
# fall back to /usr/local/bin, and finally /usr/bin as a last resort.
# On Windows always use ~/.local/bin: /usr there would resolve inside
# whichever Git/MSYS/Cygwin sh runs the recipe, not a real system path.
DESTDIR=
ifeq ($(OS),Windows_NT)
  # HOME may be unset or use backslashes, which sh treats as escapes.
  USERHOME := $(subst \,/,$(or $(HOME),$(USERPROFILE)))
  PREFIX ?= $(USERHOME)/.local
else ifndef PREFIX
  ifneq ($(wildcard $(HOME)/.local/bin),)
    PREFIX=$(HOME)/.local
  else ifneq ($(wildcard /usr/local/bin),)
    PREFIX=/usr/local
  else
    PREFIX=/usr
  endif
endif
BINDIR ?= $(DESTDIR)$(PREFIX)/bin

all: build test

build: $(addprefix build-,$(LANGS))

# Build first, then test, so a test failure doesn't prevent the binaries.
test: build $(addprefix test-,$(LANGS))

# Show which compilers, targets and languages this make will use
toolchain:
	@echo "CXX=$(CXX)  target=$(shell $(CXX) -dumpmachine)  LDFLAGS=$(LDFLAGS)  EXE=$(EXE)  LANGS=$(LANGS)"
	@$(CARGO) --version 2>/dev/null && rustc --version 2>/dev/null || echo "cargo: not found"
	@$(GO) version 2>/dev/null || echo "go: not found"

bin:
	mkdir -p bin

# --- C++ ---------------------------------------------------------------------
build-cpp: $(OUT)

$(OUT): src/cpp/funiq.cpp $(HEADERS) | bin
	$(CXX) $(CXXFLAGS) $(OPT_FLAG) $(CPP_INCLUDES) src/cpp/funiq.cpp -o $(OUT) $(LDFLAGS)

$(OUT_TEST): src/cpp/test/test.cpp $(HEADERS) | bin
	$(CXX) $(CXXFLAGS) $(CPP_INCLUDES) src/cpp/test/test.cpp -o $(OUT_TEST) $(LDFLAGS)

test-cpp: $(OUT) $(OUT_TEST)
	$(OUT_TEST)
	bash test/cli_test.sh $(OUT)
	@# the conformance harness against itself: proves the harness runs here
	bash test/conformance.sh $(OUT) $(OUT) > bin/conformance-cpp.log || (tail -20 bin/conformance-cpp.log; exit 1)
	@tail -1 bin/conformance-cpp.log

# Windows only: the same C++ built with Visual Studio's MSVC (/O2, static CRT)
# as bin/funiq-msvc.exe, for comparing toolchains. The release stays MinGW.
build-cpp-msvc: | bin
	MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL='*' cmd.exe /c "tools\\build-msvc.bat"

test-cpp-msvc: build-cpp-msvc
	bash test/cli_test.sh $(OUT_MSVC)

# --- Rust and Go ports (see docs/superpowers/plans) --------------------------
RUST_MANIFEST=--manifest-path src/rust/Cargo.toml

build-rust: | bin
	$(CARGO) build --release $(RUST_MANIFEST)
	cp src/rust/target/release/funiq-rs$(EXE) $(OUT_RUST)

test-rust: build-rust $(OUT)
	$(CARGO) test --release $(RUST_MANIFEST)
	bash test/conformance.sh $(OUT) $(OUT_RUST) > bin/conformance-rust.log || (grep -A1 "^FAIL" bin/conformance-rust.log; tail -1 bin/conformance-rust.log; exit 1)
	@tail -1 bin/conformance-rust.log

build-go: | bin
	cd src/go && CGO_ENABLED=0 $(GO) build -trimpath -o ../../$(OUT_GO) .

test-go: build-go $(OUT)
	cd src/go && $(GO) vet ./... && $(GO) test ./...
	bash test/conformance.sh $(OUT) $(OUT_GO) > bin/conformance-go.log || (grep -A1 "^FAIL" bin/conformance-go.log; tail -1 bin/conformance-go.log; exit 1)
	@tail -1 bin/conformance-go.log

# --- install -----------------------------------------------------------------
# Install every binary that `make build` produces (or one with install-<lang>).
# Hint the user to use sudo if the install fails due to permissions.
install: $(addprefix install-,$(LANGS))

install-cpp: build-cpp
	@$(call install_one,$(OUT),funiq)
install-rust: build-rust
	@$(call install_one,$(OUT_RUST),funiq-rs)
install-go: build-go
	@$(call install_one,$(OUT_GO),funiq-go)

define install_one
install -d $(BINDIR) 2>/dev/null || \
	(echo "Error: Permission denied writing to $(BINDIR). Try: sudo make install" && exit 1); \
install -m 755 $(1) $(BINDIR)/$(2)$(EXE) 2>/dev/null || \
	(echo "Error: Permission denied writing to $(BINDIR). Try: sudo make install" && exit 1); \
echo "Installed $(2) to $(BINDIR)/$(2)$(EXE)"
endef

# Remove every installed funiq binary
uninstall:
	@rm -f $(BINDIR)/funiq$(EXE) $(BINDIR)/funiq-rs$(EXE) $(BINDIR)/funiq-go$(EXE) 2>/dev/null || \
		(echo "Error: Permission denied. Try: sudo make uninstall" && exit 1)
	@echo "Removed funiq, funiq-rs and funiq-go from $(BINDIR)"

# --- benchmark ---------------------------------------------------------------
bench: build
	bash bench/bench.sh $(wildcard $(OUT) $(OUT_MSVC) $(OUT_RUST) $(OUT_GO))

clean:
	rm -f $(OUT) $(OUT_TEST) $(OUT_MSVC) $(OUT_RUST) $(OUT_GO)
	rm -rf bin/msvc-obj
	@if [ -f src/rust/Cargo.toml ] && $(CARGO) --version >/dev/null 2>&1; then $(CARGO) clean --manifest-path src/rust/Cargo.toml; fi

.PHONY: all build test toolchain build-cpp test-cpp build-cpp-msvc test-cpp-msvc build-rust test-rust \
	build-go test-go install install-cpp install-rust install-go uninstall bench clean
