SHELL := /bin/bash
.DEFAULT_GOAL := help
PORT ?=
BAUD ?= 460800
ARGS ?= --help
.PHONY: help install setup build flash monitor flash-monitor menuconfig size clean fullclean idf test preview simulator
help:
	@printf '%s\n' 'make install         Install pinned ESP-IDF and tools into .tools/' 'make build           Build ESP32-S3 firmware' 'make flash PORT=...  Flash device' 'make monitor PORT=...  Serial monitor (Ctrl+] exits)' 'make flash-monitor PORT=...  Flash and monitor' 'make menuconfig      Configure firmware' 'make idf ARGS="..."   Run local idf.py with arbitrary arguments' 'make test            Run native C++ state tests' 'make preview         Render actual LVGL screens into build-host/screens/' 'make simulator       Run the same UI in an SDL2 desktop window'
install setup:
	@./scripts/install-idf.sh
build menuconfig size clean fullclean:
	@./scripts/idf.sh $@
flash monitor flash-monitor:
	@test -n "$(PORT)" || { echo 'Set PORT, e.g. PORT=/dev/ttyACM0'; exit 2; }
	@./scripts/idf.sh -p "$(PORT)" -b "$(BAUD)" $(subst -, ,$@)
idf:
	@./scripts/idf.sh $(ARGS)
test:
	@mkdir -p build-host
	$(CXX) -std=c++20 -Wall -Wextra -Werror -pedantic -g -I main main/session.cxx tests/session_test.cxx -o build-host/session_tests
	@./build-host/session_tests
preview:
	@./scripts/host.sh --screenshots
simulator:
	@./scripts/host.sh --interactive
