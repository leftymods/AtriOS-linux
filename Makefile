# Mainline is the default; legacy 4.9 / Amlogic packaging is explicit.
.PHONY: all build kernel packages rootfs uboot verify test legacy-build legacy-rebuild clean help
all: build
build:
	@bash ./compile.sh TARGET=image
kernel:
	@bash ./compile.sh TARGET=kernel
packages:
	@bash ./compile.sh TARGET=packages
rootfs:
	@bash ./compile.sh TARGET=rootfs
uboot:
	@bash ./compile.sh TARGET=uboot
verify:
	@bash ./scripts/verify_all.sh
test:
	@python3 -m pytest -q tests/test_build_regressions.py
legacy-build:
	@bash ./build_atrios.sh
legacy-rebuild:
	@bash ./build_atrios.sh --no-buildroot
clean:
	@bash ./compile.sh clean
help:
	@echo "Mainline: build kernel packages rootfs uboot verify test clean"
	@echo "Legacy 4.9: legacy-build legacy-rebuild"
