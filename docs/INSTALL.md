# Build and installation

## Confirm the exact device

This driver binds only to PCI vendor/device `1461:0054` with subsystem
`1461:5700`.

```sh
lspci -nn -d 1461:0054
lspci -nnk -d 1461:0054
```

Stop virtual machines using the card and detach it from `vfio-pci` before
continuing.

## Install build dependencies

Package names vary by distribution. You need the compiler toolchain and the
build tree matching `uname -r`. Typical examples are:

```sh
# Fedora-family
sudo dnf install gcc make kernel-devel-$(uname -r) v4l-utils alsa-utils

# Debian/Ubuntu-family
sudo apt install build-essential linux-headers-$(uname -r) v4l-utils alsa-utils
```

Bazzite and other image-based systems normally require entering a suitable
development environment or layering the matching kernel-devel package. A
successful build requires this directory:

```sh
ls -ld /lib/modules/$(uname -r)/build
```

The project currently needs installation and runtime testing beyond Bazzite
44. If these package instructions work—or need adjustment—on another
distribution, please report the distribution, release, kernel version and the
exact packages or development environment used. The full checklist is in
[`CONTRIBUTING.md`](../CONTRIBUTING.md).

## Build

```sh
make
modinfo ./gc570d.ko
```

Never force-load a module built for another kernel release. If BTF generation
is skipped because `vmlinux` is unavailable, the module can still be usable;
compiler or modpost errors are different and must be fixed.

### Customize the no-signal image

The no-signal image is embedded in `gc570d.ko`, so customize it before
building or installing the module. The source must be a 640x360 PNG stored as
`assets/no-signal.png`.

To prepare another image without stretching it, install `ffmpeg` (which also
provides `ffprobe`) and `xxd`, then run:

```sh
ffmpeg -v error -y -i /path/to/my-image.png \
  -vf "scale=640:360:force_original_aspect_ratio=decrease:flags=lanczos,pad=640:360:(ow-iw)/2:(oh-ih)/2:color=black" \
  assets/no-signal.png
make no-signal
```

`make no-signal` immediately validates the exact dimensions and regenerates
`data/gc570d_no_signal_640x360.inc` in the YUYV422 format used by the driver.
It is optional: a normal build or installation also regenerates the embedded
data automatically whenever `assets/no-signal.png` is newer. Then install
normally:

```sh
sudo make install
# Or, on Bazzite/Fedora Atomic:
sudo ./scripts/install-portable.sh
```

Running `make no-signal` is unnecessary when using the supplied default.
Only use artwork that you have the right to embed; if you redistribute the
resulting module, the image must be distributable under compatible terms.

## Load from the checkout

```sh
sudo ./scripts/gc570d-load.sh
```

The loader resolves kernel dependencies and then uses an installed module or
the `gc570d.ko` beside the repository scripts. It does not unload a running
driver.

## Conventional system installation

On a mutable distribution:

```sh
sudo make install
sudo modprobe gc570d
```

This installs only for the running kernel. Rebuild and reinstall after a
kernel update.

## Portable installation for Bazzite/Fedora Atomic

The root image is immutable, so installing into `/usr/lib/modules` may be
inappropriate. The portable installer copies the built module to
`/var/lib/gc570d/<kernel-release>/`, installs a system-wide WirePlumber policy
and enables a systemd unit that loads the driver on every boot:

```sh
sudo ./scripts/install-portable.sh
sudo reboot
```

HDMI IN 2 is ready when the module loads. HDMI IN 1 and passthrough use the
driver's background state machine, so disconnected or late-powered HDMI
equipment does not block boot. At desktop login, WirePlumber reads the global
policy and publishes both named capture sources automatically.

For immediate use without reboot:

```sh
sudo gc570d-load
gc570d-setup-pipewire
```

The second command only refreshes the already-running desktop audio session;
it is not needed on subsequent logins or boots.

To remove only the files installed by the portable installer, first close OBS
and every V4L2/ALSA client, then run:

```sh
sudo ./scripts/uninstall-portable.sh
```

The uninstaller deliberately preserves `/etc/gc570d/gc570d.conf` so local
hotplug-policy choices are not deleted. Remove that file manually if it is no
longer needed.

The installed module is specific to the running kernel. After every kernel
update, boot the new kernel and run `install-portable.sh` again before relying
on automatic loading.

## Secure Boot

An unsigned out-of-tree module is commonly rejected when Secure Boot/module
signature enforcement is active. Sign `gc570d.ko` using the procedure for
your distribution and enroll your own key, or disable Secure Boot. Do not use
`--force-vermagic` or similar force-loading options.

## Automatic HDMI IN 1 initialization

HDMI IN 2 is initialized during module probe. By default, the driver then runs
the recovered HDMI IN 1/HDMI OUT sequence automatically in a background state
machine. It waits and retries while the source or display is absent, and
normally completes about ten seconds after both are available.

## HDMI IN 1 hotplug policy

The driver supports two startup hotplug policies for HDMI IN 1 and passthrough:

- `preserve` (default): Prioritizes uninterrupted capture. Publishes the internal
  4K60 bridge EDID at startup so HDMI IN 1 works standalone without an external
  HDMI OUT monitor. When a monitor is connected or disconnected later, upstream
  HPD is never pulsed and capture continues uninterrupted. If the monitor
  supports the active source mode, passthrough displays immediately.
- `renegotiate`: Prioritizes downstream monitor compatibility. If a monitor is
  connected at startup, its EDID is read and published before upstream HPD is
  raised for the first time, avoiding an unnecessary second negotiation. When a
  monitor is connected later, the driver reads its EDID, publishes a compatible
  two-block EDID, and deliberately pulses upstream HPD to renegotiate. Disconnecting
  HDMI OUT does not pulse HPD or interrupt ongoing capture.

The policy is selected at startup and remains read-only while the module is loaded.
It can be configured in `/etc/gc570d/gc570d.conf` (installed by `install-portable.sh`):

```sh
HDMI1_HOTPLUG_POLICY="preserve"
```

Or via modprobe options (e.g. `/etc/modprobe.d/gc570d.conf`):

```text
options gc570d hdmi1_hotplug_policy=preserve
```

Or when running the loader manually:

```sh
sudo gc570d-load --policy=renegotiate
# Or force-reload:
sudo gc570d-load --reload --policy=preserve
```

For the most reliable resolution negotiation, connect and power the HDMI OUT
display before connecting or powering the HDMI IN 1 source. This gives the
source the downstream display capabilities during its initial HDMI/EDID
exchange. If they were connected in the opposite order and negotiation fails,
reconnect or restart the source after the display is ready.

Check progress with:

```sh
sudo ./scripts/status.sh
```

For diagnosis, load only HDMI IN 2 by changing the normal default, then start
HDMI IN 1 explicitly later:

```sh
sudo ./scripts/gc570d-load.sh --hdmi2-only
sudo ./scripts/gc570d-init-hdmi1.sh
```

Do not unload the module while passthrough or capture is in use.
