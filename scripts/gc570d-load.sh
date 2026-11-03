#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Load the GC570D kernel module and its dependencies from an installed or local build.

set -eu

die()
{
	echo "gc570d-load: $*" >&2
	exit 1
}

[ "$(id -u)" -eq 0 ] || die "run as root (sudo $0)"

CONFIG_FILE="/etc/gc570d/gc570d.conf"
DEFAULT_CONFIG_FILE="/etc/default/gc570d"
HDMI1_HOTPLUG_POLICY="preserve"

if [ -f "$CONFIG_FILE" ]; then
	# shellcheck source=/dev/null
	. "$CONFIG_FILE"
elif [ -f "$DEFAULT_CONFIG_FILE" ]; then
	# shellcheck source=/dev/null
	. "$DEFAULT_CONFIG_FILE"
fi

AUTO_HDMI1=1
RELOAD=0
while [ $# -gt 0 ]; do
	case "$1" in
		--reload)
			RELOAD=1
			shift
			;;
		--hdmi2-only)
			AUTO_HDMI1=0
			shift
			;;
		--policy=*)
			HDMI1_HOTPLUG_POLICY="${1#--policy=}"
			shift
			;;
		preserve|renegotiate)
			HDMI1_HOTPLUG_POLICY="$1"
			shift
			;;
		*)
			die "usage: $0 [--reload] [--hdmi2-only] [--policy=preserve|renegotiate]"
			;;
	esac
done

if grep -q '^gc570d ' /proc/modules; then
	if [ "$RELOAD" -eq 1 ]; then
		rmmod gc570d ||
			die "could not unload existing gc570d module; it remains bound (close capture clients first)"
	else
		echo "gc570d is already loaded; module defaults were not changed (use --reload to reload)"
		exit 0
	fi
fi

KERNEL_RELEASE=$(uname -r)
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
CHECKOUT_MODULE="$SCRIPT_DIR/../gc570d.ko"
PORTABLE_MODULE="/var/lib/gc570d/$KERNEL_RELEASE/gc570d.ko"

for dependency in \
	snd-pcm \
	videodev \
	videobuf2-v4l2 \
	videobuf2-dma-contig \
	led-class-multicolor
do
	modprobe "$dependency" || die "could not load dependency $dependency"
done

if [ -f "$CHECKOUT_MODULE" ]; then
	insmod "$CHECKOUT_MODULE" \
		auto_hdmi1="$AUTO_HDMI1" \
		hdmi1_hotplug_policy="$HDMI1_HOTPLUG_POLICY" ||
		die "insmod failed; run make and check dmesg"
elif modinfo -k "$KERNEL_RELEASE" gc570d >/dev/null 2>&1; then
	modprobe gc570d \
		auto_hdmi1="$AUTO_HDMI1" \
		hdmi1_hotplug_policy="$HDMI1_HOTPLUG_POLICY" ||
		die "modprobe failed; check Secure Boot and dmesg"
elif [ -f "$PORTABLE_MODULE" ]; then
	insmod "$PORTABLE_MODULE" \
		auto_hdmi1="$AUTO_HDMI1" \
		hdmi1_hotplug_policy="$HDMI1_HOTPLUG_POLICY" ||
		die "insmod failed; check vermagic, Secure Boot and dmesg"
else
	die "no module for $KERNEL_RELEASE; run make or install-portable.sh"
fi

sleep 1
grep -q '^gc570d ' /proc/modules || die "module did not remain loaded"
if [ "$AUTO_HDMI1" -eq 1 ]; then
	printf '%s%s\n' \
		'gc570d loaded; HDMI IN 2 is ready and HDMI IN 1/passthrough ' \
		"will initialize automatically (policy: $HDMI1_HOTPLUG_POLICY)"
else
	echo "gc570d loaded in HDMI IN 2-only mode"
fi
