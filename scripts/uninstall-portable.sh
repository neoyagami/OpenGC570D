#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Remove files and services created by the portable GC570D installer.

set -eu

die()
{
	echo "uninstall-portable: $*" >&2
	exit 1
}

[ "$(id -u)" -eq 0 ] || die "run as root (sudo $0)"

KERNEL_RELEASE=$(uname -r)
MODULE_FILE="/var/lib/gc570d/$KERNEL_RELEASE/gc570d.ko"
SYSTEMD_UNIT=/etc/systemd/system/gc570d.service
WIREPLUMBER_POLICY=/etc/wireplumber/wireplumber.conf.d/51-gc570d.conf

if command -v systemctl >/dev/null 2>&1 && [ -d /run/systemd/system ]; then
	systemctl disable gc570d.service 2>/dev/null || true
fi

if grep -q '^gc570d ' /proc/modules; then
	die "autostart is disabled; close capture clients, run 'sudo modprobe -r gc570d', then rerun this uninstaller"
fi

rm -f "$MODULE_FILE"
rmdir "/var/lib/gc570d/$KERNEL_RELEASE" 2>/dev/null || true
rmdir /var/lib/gc570d 2>/dev/null || true
rm -f /usr/local/sbin/gc570d-load
rm -f /usr/local/sbin/gc570d-init-hdmi1
rm -f /usr/local/sbin/gc570d-status
rm -f /usr/local/bin/gc570d-setup-pipewire
rm -f /usr/local/share/gc570d/51-gc570d.conf
rm -f /usr/local/share/gc570d/gc570d.conf
rm -f "$WIREPLUMBER_POLICY"
rm -f "$SYSTEMD_UNIT"
rmdir /etc/gc570d 2>/dev/null || true
rmdir /usr/local/share/gc570d 2>/dev/null || true

if command -v systemctl >/dev/null 2>&1 && [ -d /run/systemd/system ]; then
	systemctl daemon-reload
fi

if [ -f /etc/gc570d/gc570d.conf ]; then
	echo "Preserved local configuration: /etc/gc570d/gc570d.conf"
fi

echo "Removed portable gc570d files for $KERNEL_RELEASE"
echo "The WirePlumber policy is removed; it takes effect at next login or reboot"
