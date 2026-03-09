CXX=g++
CXXFLAGS=-Wall -g -std=c++11
OPT_FLAG=-O2
OUT=bin/funiq
OUT_TEST=bin/funiq_test

# Use DESTDIR and PREFIX for flexible install paths
DESTDIR=
PREFIX=/usr/local
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

# Install the compiled binary to the system path
install: build
	install -d $(BINDIR)
	install -m 755 $(OUT) $(BINDIR)/funiq

# Remove the installed binary
uninstall:
	rm -f $(BINDIR)/funiq

clean:
	rm -f $(OUT) $(OUT_TEST)

.PHONY: all build_test test build clean install uninstall
