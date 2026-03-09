CXX=g++
CXXFLAGS=-Wall -g -std=c++11
OPT_FLAG=-O2
OUT=bin/funiq
OUT_TEST=bin/funiq_test

# Use DESTDIR and PREFIX for flexible install paths.
# If PREFIX is not set, try ~/.local/bin first (no sudo needed), then
# fall back to /usr/local/bin, and finally /usr/bin as a last resort.
DESTDIR=
ifndef PREFIX
  ifneq ($(wildcard $(HOME)/.local/bin),)
    PREFIX=$(HOME)/.local
  else ifneq ($(wildcard /usr/local/bin),)
    PREFIX=/usr/local
  else
    PREFIX=/usr
  endif
endif
BINDIR=$(DESTDIR)$(PREFIX)/bin

all: build test

# Ensure bin directory exists before building
$(OUT) $(OUT_TEST): | bin

bin:
	mkdir -p bin

build_test: $(OUT_TEST)

$(OUT_TEST):
	$(CXX) $(CXXFLAGS) -Ilib test/test.cpp -o $(OUT_TEST)

# Build the binary first, then run tests independently so a test failure
# does not prevent the binary from being produced.
test: build_test
	$(OUT_TEST)

build: $(OUT)

$(OUT):
	$(CXX) $(CXXFLAGS) $(OPT_FLAG) -Ilib src/funiq.cpp -o $(OUT)

# Install the compiled binary to the system path.
# Hint the user to use sudo if the install fails due to permissions.
install: build
	@install -d $(BINDIR) 2>/dev/null || \
		(echo "Error: Permission denied writing to $(BINDIR). Try: sudo make install" && exit 1)
	@install -m 755 $(OUT) $(BINDIR)/funiq 2>/dev/null || \
		(echo "Error: Permission denied writing to $(BINDIR). Try: sudo make install" && exit 1)
	@echo "Installed funiq to $(BINDIR)/funiq"

# Remove the installed binary
uninstall:
	@rm -f $(BINDIR)/funiq 2>/dev/null || \
		(echo "Error: Permission denied. Try: sudo make uninstall" && exit 1)
	@echo "Removed $(BINDIR)/funiq"

clean:
	rm -f $(OUT) $(OUT_TEST)

.PHONY: all build_test test build clean install uninstall
