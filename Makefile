# AtriOS Linux Master Makefile
# Target: Yandex Station Max (S905X3 Mod)

.PHONY: all build clean help

all: build

build:
	@./build_atrios.sh

clean:
	@rm -rf tools/fw_builder/staging tools/fw_builder/overlay/system

help:
	@echo "AtriOS Linux build targets:"
	@echo "  make build (or make) - Build complete firmware image"
	@echo "  make clean           - Clean staging and overlay output directories"
