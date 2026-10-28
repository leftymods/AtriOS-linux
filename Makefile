# AtriOS Linux Master Makefile
# Target: Yandex Station Max (S905X3 Mod)

.PHONY: all build rebuild clean verify help

all: build

# Full build: rootfs + system partition + image (version from ./VERSION)
build:
	@./build_atrios.sh

# Rebuild only the system partition and image on top of the existing rootfs
rebuild:
	@./build_atrios.sh --no-buildroot

# Verify the last built image
verify:
	@img="$$(ls -t tools/fw_builder/yandex_s905x3_custom_v*.img | head -1)"; \
	 tools/bin/ampack verify "$$img" && sha256sum "$$img"

clean:
	@rm -rf tools/fw_builder/staging tools/fw_builder/overlay/system
	@rm -f tools/fw_builder/.fw_builder_cache.json

help:
	@echo "AtriOS Linux build targets:"
	@echo "  make build    - Full build (rootfs + image), version from ./VERSION"
	@echo "  make rebuild  - Repack the image using the existing rootfs.tar"
	@echo "  make verify   - Verify and hash the newest image"
	@echo "  make clean    - Remove staging and overlay output directories"
