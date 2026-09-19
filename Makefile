# SPDX-License-Identifier: GPL-2.0
#
# Root Makefile for Thunderobot Linux Platform Driver and CLI Tool
#

.PHONY: all kernel cli clean install check-version sync-version version-patch version-minor version-major patch minor major help

all: kernel cli

kernel:
	$(MAKE) -C kernel

cli:
	cargo build --release --manifest-path cli/Cargo.toml

clean:
	$(MAKE) -C kernel clean
	cargo clean --manifest-path cli/Cargo.toml

install:
	$(MAKE) -C kernel install
	install -Dm755 cli/target/release/thunderobot /usr/local/bin/thunderobot

check-version:
	./script/sync-version.sh --check

sync-version:
	./script/sync-version.sh

version-patch:
	cargo release version patch --manifest-path cli/Cargo.toml --execute --no-confirm
	./script/sync-version.sh

version-minor:
	cargo release version minor --manifest-path cli/Cargo.toml --execute --no-confirm
	./script/sync-version.sh

version-major:
	cargo release version major --manifest-path cli/Cargo.toml --execute --no-confirm
	./script/sync-version.sh

# Shortcuts
patch: version-patch
minor: version-minor
major: version-major

help:
	@echo "Thunderobot Build & Release Management"
	@echo ""
	@echo "Build targets:"
	@echo "  make all             - Build both kernel module and CLI tool"
	@echo "  make kernel          - Build kernel module"
	@echo "  make cli             - Build release CLI binary"
	@echo "  make clean           - Clean kernel and CLI build artifacts"
	@echo ""
	@echo "Version & Release orchestration:"
	@echo "  make version-patch   - cargo release patch + sync kernel/dkms/README"
	@echo "  make version-minor   - cargo release minor + sync kernel/dkms/README"
	@echo "  make version-major   - cargo release major + sync kernel/dkms/README"
	@echo "  make sync-version    - Synchronize Cargo.toml version to all files"
	@echo "  make check-version   - Verify all version numbers are in sync"
