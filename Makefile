CC ?= cc
PYTHON ?= python3
NODE ?= node
PKG_CONFIG ?= pkg-config
CFLAGS ?= -O2
CPPFLAGS += -Isrc
WARNINGS = -std=c11 -Wall -Wextra -Wpedantic -Werror
BUNDLE ?= build/16-pads.lv2
LV2DIR ?= $(HOME)/.lv2
ENABLE_MOD_GUI ?= 0

.PHONY: all test test-core test-ui bench install clean FORCE
all: $(BUNDLE)/sampler.so $(BUNDLE)/manifest.ttl

$(BUNDLE):
	mkdir -p $@

$(BUNDLE)/sampler.so: src/plugin.c src/sampler.c src/sampler.h | $(BUNDLE)
	@set -e; LV2_FLAGS="$$($(PKG_CONFIG) --cflags lv2)"; \
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) -fPIC -shared \
		$$LV2_FLAGS src/plugin.c src/sampler.c -lm -o $@

$(BUNDLE)/manifest.ttl: tools/metadata.py modgui/icon.html modgui/style.css modgui/script.js modgui/screenshot.png modgui/thumbnail.png FORCE | $(BUNDLE)
	$(PYTHON) tools/metadata.py $(BUNDLE) --mod-gui $(ENABLE_MOD_GUI)

build/test_sampler: tests/test_sampler.c src/sampler.c src/sampler.h
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) tests/test_sampler.c src/sampler.c -lm -o $@

build/test_plugin: tests/test_plugin.c src/plugin.c src/sampler.c src/sampler.h
	mkdir -p build
	@set -e; LV2_FLAGS="$$($(PKG_CONFIG) --cflags lv2)"; \
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) $$LV2_FLAGS \
		tests/test_plugin.c src/sampler.c -lm -o $@

test-core: build/test_sampler
	./build/test_sampler
	$(PYTHON) -m unittest discover -s tests -p 'test_*.py'

test: test-core build/test_plugin
	./build/test_plugin

build/bench_render: tools/bench_render.c src/sampler.c src/sampler.h
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) tools/bench_render.c src/sampler.c -lm -o $@

bench: build/bench_render
	./build/bench_render

test-ui:
	$(NODE) tests/test_modgui.js

install: all
	mkdir -p "$(DESTDIR)$(LV2DIR)"
	cp -R "$(BUNDLE)" "$(DESTDIR)$(LV2DIR)/"

clean:
	rm -f build/test_sampler build/test_plugin "$(BUNDLE)/sampler.so" \
		"$(BUNDLE)/manifest.ttl" "$(BUNDLE)/sampler.ttl" "$(BUNDLE)/modgui.ttl"
