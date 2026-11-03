#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Report installation, kernel-module, capture-device, audio, and service status.

set -u

list_pulse_sources()
{
	if [ "$(id -u)" -eq 0 ] && [ -n "${SUDO_USER:-}" ] &&
	   command -v runuser >/dev/null 2>&1; then
		desktop_uid=$(id -u "$SUDO_USER")
		runuser -u "$SUDO_USER" -- env \
			XDG_RUNTIME_DIR="/run/user/$desktop_uid" \
			pactl list sources short
	else
		pactl list sources short
	fi
}

echo "== Kernel =="
uname -r

echo
echo "== Boot service =="
if command -v systemctl >/dev/null 2>&1 && [ -d /run/systemd/system ]; then
	printf 'enabled: '
	enabled=$(systemctl is-enabled gc570d.service 2>/dev/null || true)
	echo "${enabled:-no}"
	printf 'active: '
	active=$(systemctl is-active gc570d.service 2>/dev/null || true)
	echo "${active:-no}"
else
	echo "systemd is not active"
fi

echo
echo "== PCI =="
if command -v lspci >/dev/null 2>&1; then
	lspci -nnk -d 1461:0054 || true
else
	echo "lspci is not installed"
fi

echo
echo "== Module =="
if grep -q '^gc570d ' /proc/modules; then
	grep '^gc570d ' /proc/modules
	if [ -r /sys/module/gc570d/parameters/hdmi1_hotplug_policy ]; then
		printf 'hdmi1_hotplug_policy: '
		cat /sys/module/gc570d/parameters/hdmi1_hotplug_policy
	fi
else
	echo "gc570d is not loaded"
fi

echo
echo "== V4L2 =="
FOUND_VIDEO=false
for name_path in /sys/class/video4linux/video*/name; do
	[ -e "$name_path" ] || continue
	name=$(cat "$name_path")
	case "$name" in
		gc570d-*)
			node=/dev/$(basename "$(dirname "$name_path")")
			echo "$node: $name"
			FOUND_VIDEO=true
			;;
	esac
done
[ "$FOUND_VIDEO" = true ] || echo "no gc570d V4L2 nodes"

echo
echo "== ALSA =="
if command -v arecord >/dev/null 2>&1; then
	arecord -l 2>/dev/null | grep -A 2 -B 1 -E 'AVerMedia|HDMI IN' || echo "no gc570d ALSA capture card"
else
	echo "arecord is not installed"
fi

echo
echo "== PipeWire-Pulse =="
if command -v pactl >/dev/null 2>&1; then
	list_pulse_sources 2>/dev/null | grep -E 'gc570d|AVerMedia|pro-input' || echo "no published gc570d sources"
else
	echo "pactl is not installed"
fi

echo
echo "== RGB =="
LED=/sys/class/leds/gc570d:rgb:status
if [ -d "$LED" ]; then
	printf 'brightness: '
	cat "$LED/brightness"
	printf 'multi_index: '
	cat "$LED/multi_index"
	printf 'multi_intensity: '
	cat "$LED/multi_intensity"
	if [ -r "$LED/effect" ]; then
		printf 'effect: '
		cat "$LED/effect"
	fi
else
	echo "gc570d RGB device is absent"
fi

echo
echo "== HDMI IN 1 state pump =="
PUMP=/sys/kernel/debug/gc570d/splitter_pump_status
if [ -r "$PUMP" ]; then
	cat "$PUMP"
else
	echo "status unavailable; run this script with sudo after mounting debugfs"
fi

echo
echo "== Recent kernel messages =="
if dmesg >/dev/null 2>&1; then
	dmesg | grep gc570d | tail -n 40 || true
else
	echo "dmesg is restricted; rerun with sudo"
fi
