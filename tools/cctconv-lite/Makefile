# Standalone picoloop <-> ChooChooTracker converter: one file, no dependency.
#   make            build pico2cct
#   make install    copy it to $(PREFIX)/bin (default /usr/local)

CXX ?= g++
CXXFLAGS ?= -O2 -Wall -Wextra
PREFIX ?= /usr/local

TOOL = pico2cct

all: $(TOOL)

$(TOOL): pico2cct.cpp
	$(CXX) -std=c++17 $(CXXFLAGS) $(LDFLAGS) -o $@ $<

install: $(TOOL)
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 $(TOOL) $(DESTDIR)$(PREFIX)/bin/$(TOOL)

clean:
	rm -f $(TOOL)

.PHONY: all install clean
