# Portable across Linux (x86_64, arm64), MinGW and Cygwin. Override the
# compiler with `make CXX=...` or the CXX environment variable.
CXX ?= g++
CXXFLAGS=-Wall -g -std=c++11
OPT_FLAG=-O2

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
OUT=bin/funiq$(EXE)
OUT_TEST=bin/funiq_test$(EXE)
HEADERS=$(wildcard lib/funiq/*.h)

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

# Show which compiler and target this make will use
toolchain:
	@echo "CXX=$(CXX)  target=$(shell $(CXX) -dumpmachine)  LDFLAGS=$(LDFLAGS)  EXE=$(EXE)"

# Ensure bin directory exists before building
$(OUT) $(OUT_TEST): | bin

bin:
	mkdir -p bin

build_test: $(OUT_TEST)

$(OUT_TEST): test/test.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) -Ilib test/test.cpp -o $(OUT_TEST) $(LDFLAGS)

# Build the binary first, then run tests independently so a test failure
# does not prevent the binary from being produced.
test: build_test build
	$(OUT_TEST)
	bash test/cli_test.sh $(OUT)

build: $(OUT)

$(OUT): src/funiq.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) $(OPT_FLAG) -Ilib src/funiq.cpp -o $(OUT) $(LDFLAGS)

# Install the compiled binary to the system path.
# Hint the user to use sudo if the install fails due to permissions.
install: build
	@install -d $(BINDIR) 2>/dev/null || \
		(echo "Error: Permission denied writing to $(BINDIR). Try: sudo make install" && exit 1)
	@install -m 755 $(OUT) $(BINDIR)/funiq$(EXE) 2>/dev/null || \
		(echo "Error: Permission denied writing to $(BINDIR). Try: sudo make install" && exit 1)
	@echo "Installed funiq to $(BINDIR)/funiq$(EXE)"

# Remove the installed binary
uninstall:
	@rm -f $(BINDIR)/funiq$(EXE) 2>/dev/null || \
		(echo "Error: Permission denied. Try: sudo make uninstall" && exit 1)
	@echo "Removed $(BINDIR)/funiq$(EXE)"

clean:
	rm -f $(OUT) $(OUT_TEST)

.PHONY: all toolchain build_test test build clean install uninstall
