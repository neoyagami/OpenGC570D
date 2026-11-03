#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Build and install the module, boot service, helper scripts, and audio policy.

set -eu

die()
{
	echo "install-portable: $*" >&2
	exit 1
}

[ "$(id -u)" -eq 0 ] || die "run as root (sudo $0)"

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
PROJECT_DIR=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
KERNEL_RELEASE=$(uname -r)
MODULE_DIR="/var/lib/gc570d/$KERNEL_RELEASE"
MODULE_SELINUX_PATH='/var/lib/gc570d(/.*)?'
SYSTEMD_UNIT=/etc/systemd/system/gc570d.service
WIREPLUMBER_POLICY=/etc/wireplumber/wireplumber.conf.d/51-gc570d.conf
CONFIG_FILE=/etc/gc570d/gc570d.conf

make -C "$PROJECT_DIR" GC570D_DEVELOPER_DEBUGFS=0 || die "module build failed"
install -D -m 0644 "$PROJECT_DIR/gc570d.ko" "$MODULE_DIR/gc570d.ko"

# Fedora's SELinux policy permits module_load from modules_object_t, not the
# generic var_lib_t inherited by this portable location.  Record a persistent
# file-context rule so boot-time insmod works and later reinstalls/relabels keep
# the correct type.  chcon is a bounded fallback for systems without semanage.
if command -v selinuxenabled >/dev/null 2>&1 && selinuxenabled; then
	if command -v semanage >/dev/null 2>&1; then
		semanage fcontext -a -t modules_object_t "$MODULE_SELINUX_PATH" 2>/dev/null ||
			semanage fcontext -m -t modules_object_t "$MODULE_SELINUX_PATH"
		restorecon -RF /var/lib/gc570d
	elif command -v chcon >/dev/null 2>&1; then
		chcon -R -t modules_object_t /var/lib/gc570d
		echo "WARNING: installed a non-persistent SELinux label because semanage is unavailable" >&2
	else
		die "SELinux is enforcing but neither semanage nor chcon is available"
	fi
fi

install -D -m 0755 "$SCRIPT_DIR/gc570d-load.sh" /usr/local/sbin/gc570d-load
install -D -m 0755 "$SCRIPT_DIR/gc570d-init-hdmi1.sh" /usr/local/sbin/gc570d-init-hdmi1
install -D -m 0755 "$SCRIPT_DIR/status.sh" /usr/local/sbin/gc570d-status
install -D -m 0755 "$SCRIPT_DIR/setup-pipewire.sh" /usr/local/bin/gc570d-setup-pipewire
install -D -m 0644 "$PROJECT_DIR/config/wireplumber/51-gc570d.conf" \
	/usr/local/share/gc570d/51-gc570d.conf
install -D -m 0644 "$PROJECT_DIR/config/wireplumber/51-gc570d.conf" \
	"$WIREPLUMBER_POLICY"
install -D -m 0644 "$PROJECT_DIR/config/gc570d.conf" \
	/usr/local/share/gc570d/gc570d.conf
if [ ! -f "$CONFIG_FILE" ]; then
	install -D -m 0644 "$PROJECT_DIR/config/gc570d.conf" \
		"$CONFIG_FILE"
	echo "Installed default configuration: $CONFIG_FILE"
fi

if command -v systemctl >/dev/null 2>&1 && [ -d /run/systemd/system ]; then
	install -D -m 0644 "$PROJECT_DIR/config/systemd/gc570d.service" \
		"$SYSTEMD_UNIT"
	systemctl daemon-reload
	systemctl enable gc570d.service
	echo "Enabled gc570d.service for automatic loading at boot"
else
	echo "WARNING: systemd is not active; automatic module loading was not enabled" >&2
fi

echo "Installed gc570d for $KERNEL_RELEASE in $MODULE_DIR"
echo "Installed system-wide WirePlumber policy: $WIREPLUMBER_POLICY"
echo "Reboot to load the driver and apply desktop audio automatically"
echo "Load without reboot: sudo gc570d-load"
echo "Refresh the current desktop audio session: gc570d-setup-pipewire"
echo "Rebuild and reinstall after each kernel update"
