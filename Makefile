SHELL := /bin/bash
.DEFAULT_GOAL := help
PORT ?=
BAUD ?= 460800
ARGS ?= --help
# Treat a trailing /dev/... argument as a port, not a file to build.
PORT_ARGS := $(filter /dev/%,$(MAKECMDGOALS))
ifneq ($(word 2,$(PORT_ARGS)),)
$(error Select one serial port only)
endif
ifneq ($(PORT_ARGS),)
ifneq ($(strip $(PORT)),)
ifneq ($(PORT),$(PORT_ARGS))
$(error PORT and the positional serial port must match)
endif
endif
endif
SERIAL_PORT := $(if $(PORT_ARGS),$(PORT_ARGS),$(PORT))
.PHONY: help install setup build probe flash monitor flash-monitor menuconfig size clean fullclean idf test preview simulator $(PORT_ARGS)
ifneq ($(PORT_ARGS),)
$(PORT_ARGS):
	@:
endif
help:
	@printf '%s\n' 'make install         Install pinned ESP-IDF and tools into .tools/' 'make build           Build ESP32-S3 firmware' 'make probe           Discover serial ports (Linux/macOS)' 'make flash /dev/ttyACM0  Flash, then monitor (Ctrl+] exits)' 'make monitor /dev/ttyACM0  Open serial monitor only' 'make flash PORT=...  Alternative port syntax; flash and monitor' 'make menuconfig      Configure firmware' 'make idf ARGS="..."   Run local idf.py with arbitrary arguments' 'make test            Run native C++ state tests' 'make preview         Render actual LVGL screens into build-host/screens/' 'make simulator       Run the same UI in an SDL2 desktop window' 'make preview-wide    Test and render the 640x172 UI' 'make simulator-wide  Open the 640x172 UI in SDL2'
install setup:
	@./scripts/install-idf.sh
build menuconfig size clean fullclean:
	@./scripts/idf.sh $@
probe:
	@bash scripts/probe.sh
flash flash-monitor:
	@test -n "$(SERIAL_PORT)" || { echo 'Run make probe, then make flash /dev/ttyACM0 (or PORT=...)'; exit 2; }
	@./scripts/idf.sh -p "$(SERIAL_PORT)" -b "$(BAUD)" flash monitor
# `make flash monitor /dev/...` must not open a second monitor, even with -j.
monitor:
ifeq ($(filter flash flash-monitor,$(MAKECMDGOALS)),)
	@test -n "$(SERIAL_PORT)" || { echo 'Run make probe, then make monitor /dev/ttyACM0 (or PORT=...)'; exit 2; }
	@./scripts/idf.sh -p "$(SERIAL_PORT)" -b "$(BAUD)" monitor
else
	@:
endif
idf:
	@./scripts/idf.sh $(ARGS)
test:
	@mkdir -p build-host
	$(CXX) -std=c++20 -Wall -Wextra -Werror -pedantic -g -I main tests/board349_test.cxx -o build-host/board349_tests
	@./build-host/board349_tests
	$(CXX) -std=c++20 -Wall -Wextra -Werror -pedantic -g -I main main/session.cxx main/scale.cxx tests/session_test.cxx -o build-host/session_tests
	@./build-host/session_tests
	$(CXX) -std=c++20 -Wall -Wextra -Werror -pedantic -g -I main main/session.cxx main/scale.cxx tests/scale_test.cxx -o build-host/scale_tests
	@./build-host/scale_tests
	$(CXX) -std=c++20 -Wall -Wextra -Werror -pedantic -g -I main main/session.cxx main/scale.cxx tests/filter_test.cxx -o build-host/filter_tests
	@./build-host/filter_tests
	$(CXX) -std=c++20 -Wall -Wextra -Werror -pedantic -g -I main main/scale.cxx tests/adaptive_test.cxx -o build-host/adaptive_tests
	@./build-host/adaptive_tests
	$(CXX) -std=c++20 -Wall -Wextra -Werror -pedantic -g -I main main/scale.cxx tests/display_test.cxx -o build-host/display_tests
	@./build-host/display_tests
	$(CXX) -std=c++20 -Wall -Wextra -Werror -pedantic -g -I main main/scale.cxx tests/registration_test.cxx -o build-host/registration_tests
	@./build-host/registration_tests
	$(CXX) -std=c++20 -Wall -Wextra -Werror -pedantic -g -I main main/session.cxx main/scale.cxx tests/brew_error_test.cxx -o build-host/brew_error_tests
	@./build-host/brew_error_tests
	$(CXX) -std=c++20 -Wall -Wextra -Werror -pedantic -g -I main main/session.cxx main/scale.cxx tests/auto_brew_test.cxx -o build-host/auto_brew_tests
	@./build-host/auto_brew_tests
	$(CXX) -std=c++20 -Wall -Wextra -Werror -pedantic -g -I main main/session.cxx main/scale.cxx tests/coffee_registration_test.cxx -o build-host/coffee_registration_tests
	@./build-host/coffee_registration_tests
	$(CXX) -std=c++20 -Wall -Wextra -Werror -pedantic -g -I main main/scale.cxx tests/diagnostics_test.cxx -o build-host/diagnostics_tests
	@./build-host/diagnostics_tests
preview:
	@./scripts/host.sh --screenshots
	@./scripts/host.sh --live-screenshots
	@./scripts/host.sh --coffee-screenshots
simulator:
	@./scripts/host.sh --interactive

.PHONY: preview-wide simulator-wide
preview-wide:
	@./scripts/host.sh --wide-screenshots
	@./scripts/host.sh --wide-live-screenshots
simulator-wide:
	@./scripts/host.sh --wide-interactive
