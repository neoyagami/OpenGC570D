#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Validate that a release contains the required files and no proprietary artifacts.

set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
PROJECT_DIR=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
FAILED=false

echo "Checking public release artifacts..."
find "$PROJECT_DIR" \
	-path "$PROJECT_DIR/.git" -prune -o \
	-path "$PROJECT_DIR/assets/no-signal.png" -prune -o \
	-type f \( \
		-iname '*.sys' -o -iname '*.dll' -o -iname '*.exe' -o \
		-iname '*.msi' -o -iname '*.zip' -o -iname '*.7z' -o \
		-iname '*.rar' -o -iname '*.bin' -o -iname '*.fw' -o \
		-iname '*.gpr' -o -iname '*.gbf' -o -iname '*.rep' -o \
		-iname '*.wav' -o -iname '*.raw' -o -iname '*.yuv' -o \
		-iname '*.yuyv' -o -iname '*.p010' -o -iname '*.pcap' -o \
		-iname '*.png' -o -iname '*.jpg' -o -iname '*.jpeg' \
	\) -print | while IFS= read -r prohibited; do
		echo "PROHIBITED: $prohibited"
	done

PROHIBITED_COUNT=$(find "$PROJECT_DIR" \
	-path "$PROJECT_DIR/.git" -prune -o \
	-path "$PROJECT_DIR/assets/no-signal.png" -prune -o \
	-type f \( \
		-iname '*.sys' -o -iname '*.dll' -o -iname '*.exe' -o \
		-iname '*.msi' -o -iname '*.zip' -o -iname '*.7z' -o \
		-iname '*.rar' -o -iname '*.bin' -o -iname '*.fw' -o \
		-iname '*.gpr' -o -iname '*.gbf' -o -iname '*.rep' -o \
		-iname '*.wav' -o -iname '*.raw' -o -iname '*.yuv' -o \
		-iname '*.yuyv' -o -iname '*.p010' -o -iname '*.pcap' -o \
		-iname '*.png' -o -iname '*.jpg' -o -iname '*.jpeg' \
	\) -print | wc -l)

if [ "$PROHIBITED_COUNT" -ne 0 ]; then
	FAILED=true
fi

for required in \
	.github/CODEOWNERS \
	.github/PULL_REQUEST_TEMPLATE.md \
	.github/workflows/build.yml \
	README.md \
	LICENSE \
	NOTICE.md \
	MAINTAINERS.md \
	SECURITY.md \
	docs/DATA_PROVENANCE.md \
	docs/PACKAGING.md \
	docs/REVERSE_ENGINEERING.md \
	packaging/fedora/README.md \
	packaging/fedora/gc570d-kmod.spec \
	packaging/fedora/gc570d.spec \
	packaging/fedora/gc570d.rpmlintrc \
	packaging/ublue/REQUEST.md \
	packaging/ublue/build-kmod-gc570d.sh \
	src/gc570d.h \
	src/gc570d-main.c \
	src/gc570d-hw.c \
	src/gc570d-i2c.c \
	src/gc570d-led.c \
	src/gc570d-receiver.c \
	src/gc570d-splitter.c \
	src/gc570d-dma.c \
	src/gc570d-audio.c \
	src/gc570d-video.c \
	config/systemd/gc570d.service \
	config/wireplumber/51-gc570d.conf \
	config/gc570d.conf \
	assets/README.md \
	assets/no-signal.png \
	data/gc570d_hdr_lut_3000.inc \
	data/gc570d_no_signal_640x360.inc \
	scripts/check-toolchain-selection.sh \
	scripts/check-fedora-packaging.sh \
	scripts/generate-no-signal-asset.sh
do
	if [ ! -f "$PROJECT_DIR/$required" ]; then
		echo "MISSING: $required"
		FAILED=true
	fi
done

for licensed in "$PROJECT_DIR/Makefile" \
	"$PROJECT_DIR"/.github/CODEOWNERS \
	"$PROJECT_DIR"/.github/PULL_REQUEST_TEMPLATE.md \
	"$PROJECT_DIR"/.github/workflows/*.yml \
	"$PROJECT_DIR"/src/*.c \
	"$PROJECT_DIR"/src/*.h \
	"$PROJECT_DIR"/data/*.inc \
	"$PROJECT_DIR"/assets/*.md \
	"$PROJECT_DIR"/packaging/fedora/*.spec \
	"$PROJECT_DIR"/packaging/fedora/*.rpmlintrc \
	"$PROJECT_DIR"/packaging/ublue/*.sh \
	"$PROJECT_DIR"/scripts/*.sh \
	"$PROJECT_DIR"/config/*.conf \
	"$PROJECT_DIR"/config/systemd/*.service \
	"$PROJECT_DIR"/config/wireplumber/*.conf
do
	if [ -f "$licensed" ] &&
	   ! head -n 6 "$licensed" | grep -q 'SPDX-License-Identifier: GPL-2.0-only'; then
		echo "MISSING GPL-2.0-only SPDX HEADER: $licensed"
		FAILED=true
	fi
done

if [ "$FAILED" = true ]; then
	echo "Release check failed" >&2
	exit 1
fi

echo "Release artifact check passed"
echo "Manual review is still required: inspect git status and git diff --cached"
