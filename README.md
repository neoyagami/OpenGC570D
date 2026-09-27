# OpenGC570D — experimental GC570D Linux driver

Unofficial Linux support for the AVerMedia Live Gamer DUO GC570D
(`1461:0054`, subsystem `1461:5700`). The project exposes both HDMI inputs to
Video4Linux2 and ALSA, initializes HDMI IN 1 passthrough, performs the tested
HDR-to-SDR/downscale capture path, and exposes the enclosure lighting through
the standard Linux multicolor LED class. A supplied WirePlumber policy
publishes both ALSA inputs through PipeWire and its PulseAudio-compatible API
for desktop applications.

This is experimental software for one exact PCI subsystem. It is not an
official AVerMedia project and is not endorsed by AVerMedia. Read
[`NOTICE.md`](NOTICE.md) and [`docs/REVERSE_ENGINEERING.md`](docs/REVERSE_ENGINEERING.md)
before redistributing it.

> **Fork note:** this is a fork maintained by
> [@eliseohh](https://github.com/eliseohh), forked from
> [neoyagami/OpenGC570D](https://github.com/neoyagami/OpenGC570D). The driver
> source is upstream's and unchanged; the only addition is the verified
> CachyOS/Arch build note under
> [Verified configurations](#verified-configurations). Upstream remains the
> canonical project.

## Why this project exists

The GC570D had no Linux driver that provided the functions I needed. This
capture card was one of the last pieces of hardware keeping me tied to Windows:
passthrough, two capture inputs, their audio and the enclosure lighting were
all part of my normal use. I did not want to replace the card; I wanted to
understand the hardware I own and make it useful on Linux.

Ghidra and AI agents were used as engineering tools to study the behavior of
the official Windows driver and reimplement the required hardware operations
through Linux PCI, V4L2, ALSA and LED interfaces. Research priorities,
hypotheses, hardware trials and publication decisions were determined through
direct human review rather than delegated to the tools. The goal is learning
and interoperability, and the personal result is simple: this card no longer
requires Windows, so I can finally leave Windows behind.

A personal note: this repository is an experiment born partly out of
desperation. I began without prior expertise in kernel drivers, HDMI state
machines or capture DMA, and I could not have brought the project this far
without AI assistance. The development was collaborative, but its direction,
physical hardware work, evaluation of results and publication remained under
my control. AI agents were powerful engineering tools in that process,
alongside Ghidra and careful one-test-at-a-time hardware work.

## Current hardware status

Validated on the project hardware:

| Function | Working behavior |
| --- | --- |
| HDMI IN 1 passthrough | 3840x2160p60 SDR and HDR; 2560x1440p60 SDR and HDR; 2560x1440p120 HDR |
| HDMI IN 1 high-resolution capture | 1440p60 SDR/HDR and measured 1440p120 HDR input converted to 1920x1080 YUYV SDR at 60 fps |
| HDMI IN 1 native capture | 1280x720p60 SDR input captured as 1280x720 YUYV at 60 fps; 720x480p59.94/60 SDR input from a RetroScaler2x captured as 720x480 YUYV and displayed at its intended 4:3 aspect ratio |
| HDMI IN 2 capture | 1920x1080 YUYV at 60 fps |
| HDMI IN 1 audio | `HDMI 1 (Passthrough)`, ALSA capture device 1, stereo S16_LE at 48 kHz; direct ALSA and PipeWire validated, including automatic recovery on the same open audio stream after disconnect/reconnect |
| HDMI IN 2 audio | `HDMI 2 (Capture Only)`, ALSA capture device 0, stereo S16_LE at 48 kHz; direct ALSA and PipeWire validated |
| Concurrent operation | Both V4L2 streams and both audio inputs active together through the tested ALSA/PipeWire path |
| Hot-plug and disconnected inputs | Both V4L2 nodes concurrently supply the built-in generic no-signal frame at 1920x1080 YUYV/60 fps without video DMA; HDMI IN 1 and HDMI IN 2 each physically passed a complete live-to-placeholder-to-live cycle on the same open V4L2 stream, including HDMI IN 1 video and audio recovery while HDMI IN 2 remained active |
| RGB enclosure | Standard `gc570d:rgb:status` multicolor LED, fixed RGB, single-color breathing and red/yellow/green/cyan/blue/magenta/white breathing cycle |

The 720x480 mode stores a 3:2 raster but represents 4:3 video through its
non-square pixel aspect ratio. Testing also confirmed that only one client
should negotiate the format of a V4L2 input at a time: multiple capture or
PipeWire sources opened on the same input can request competing resolutions.

Recovered but not yet fully validated: the remaining 1080p-or-lower input
matrix, including 720x576, every source/monitor combination, VRR, and all
advertised refresh rates. HDCP-protected content is unsupported. Capture
output is SDR, capped at 60 fps, and no larger than 1920x1080.

## Verified configurations

Independent reproductions of the driver on other distributions:

| Distribution | Kernel | Compiler | Secure Boot | Result |
| --- | --- | --- | --- | --- |
| CachyOS (Arch-based) | `7.2.2-1-cachyos` (clang-built) | clang 22.1.8 | disabled | `1461:0054`/`1461:5700` binds; HDMI IN 2 capture, both ALSA capture devices and the RGB LED verified |

Build note for clang-built kernels: these kernels pass clang-only flags
(`-mstack-alignment=8`, `-mretpoline-external-thunk`, ...) that GCC rejects.
The Makefile reads the target kernel's `include/config/auto.conf` and selects
the LLVM toolchain automatically when `CONFIG_CC_IS_CLANG=y`, so plain `make`
works; `make LLVM=1` still forces it explicitly:

```sh
make                 # auto-selects LLVM on clang-built kernels
sudo ./scripts/gc570d-load.sh
```

## Testing help wanted

The current development and hardware validation environment is Bazzite 44.
Testing help is especially welcome on other Linux distributions, kernel
versions, desktop audio stacks, HDMI sources and displays. Successful results
are as useful as failures because they help distinguish driver problems from
distribution-specific installation or integration issues.

Please include the distribution and version, `uname -r`, compiler, PCI
subsystem ID, installation method, Secure Boot state, source/display mode and
the V4L2 or ALSA behavior observed. See [`CONTRIBUTING.md`](CONTRIBUTING.md)
for the complete test-report checklist.

## Source layout

Each source, script, and integration file starts with an SPDX
`GPL-2.0-only` identifier and a short description of its responsibility.

| Path | Responsibility |
| --- | --- |
| `src/gc570d-main.c` | PCI probe/removal, module parameters, lifetime, and subsystem coordination |
| `src/gc570d-hw.c` | Interrupts, bridge/Xilinx access, and common hardware diagnostics |
| `src/gc570d-i2c.c` | Transactions through the card's internal I2C controllers |
| `src/gc570d-receiver.c` | HDMI receiver setup, signal detection, EDID, and color conversion |
| `src/gc570d-splitter.c` | HDMI passthrough, HPD, EDID, TMDS/SCDC, and output state machine |
| `src/gc570d-dma.c` | Video DMA descriptors, VIP/scaler setup, and diagnostic capture |
| `src/gc570d-video.c` | V4L2 devices, videobuf2 queues, formats, and streaming |
| `src/gc570d-audio.c` | ALSA PCM devices and audio DMA |
| `src/gc570d-led.c` | Multicolor LED-class device and enclosure effects |
| `src/gc570d.h` | Shared registers, constants, state, and subsystem declarations |
| `assets/` | Editable project artwork, including the replaceable 640x360 no-signal frame |
| `data/` | Generated/static data embedded by the driver: HDR LUT and YUYV no-signal frame |
| `scripts/` | Build-tree loading, installation, audio setup, status, and release checks |
| `config/` | systemd autoload and system-wide WirePlumber policy |

## Requirements

- A Linux kernel with headers/build files for the running kernel.
- GCC, make, binutils and the normal external-module toolchain.
- V4L2, videobuf2, ALSA and multicolor LED kernel support.
- `v4l2-ctl` for command-line video tests and `alsa-utils` for `arecord`.
- PipeWire, WirePlumber and `pactl` for the tested desktop audio path.
- Secure Boot disabled, or a locally signed module enrolled by the owner.

Do not bind the card to a virtual machine or `vfio-pci` while loading this
driver.

## Quick start

Build for the running kernel:

```sh
make
```

From the checkout, load the module. HDMI IN 2 becomes available immediately;
HDMI IN 1 and passthrough initialize automatically in the background when the
source and HDMI OUT display are connected and powered:

```sh
sudo ./scripts/gc570d-load.sh
```

The complete automatic negotiation normally takes about ten seconds. The
module keeps retrying while it waits for the HDMI equipment.

Publish both ALSA inputs through WirePlumber, PipeWire and the
PulseAudio-compatible API. Run this as the desktop user, not with `sudo`:

```sh
./scripts/setup-pipewire.sh
```

Inspect the result:

```sh
sudo ./scripts/status.sh
```

Detailed installation, capture, audio and RGB instructions are in
[`docs/INSTALL.md`](docs/INSTALL.md) and [`docs/USAGE.md`](docs/USAGE.md).
The no-signal image can be replaced before compilation; see
[`docs/INSTALL.md`](docs/INSTALL.md#customize-the-no-signal-image).

## Installation choices

On a conventional mutable distribution, `sudo make install` installs the
module under `/lib/modules/$(uname -r)/extra/gc570d/` and runs `depmod`.

On Bazzite/Fedora Atomic or another immutable system, use the portable
installer. It keeps the module under `/var/lib/gc570d/<kernel>/` and installs
small launchers under `/usr/local/sbin`. It also enables `gc570d.service` and
installs the WirePlumber policy system-wide, so the driver and both desktop
audio sources are available automatically after reboot:

```sh
sudo ./scripts/install-portable.sh
sudo reboot
```

The module must be rebuilt after every kernel update. An unsigned out-of-tree
module may be rejected by Secure Boot.

For immediate use without reboot, run `sudo gc570d-load`; the current desktop
session may then need `gc570d-setup-pipewire` once to reload WirePlumber.

The normal default enables every implemented part. To load only HDMI IN 2 for
diagnosis, then enable HDMI IN 1 manually later:

```sh
sudo ./scripts/gc570d-load.sh --hdmi2-only
sudo ./scripts/gc570d-init-hdmi1.sh
```

## Simultaneous capture ordering

Any application opening both V4L2 nodes must start HDMI IN 2 first and HDMI IN
1 second. Direct ALSA clients can open `HDMI 2 (Capture Only)` and `HDMI 1
(Passthrough)` independently. For desktop applications, the portable
installation publishes both sources automatically through PipeWire; when
running from a checkout, run `setup-pipewire.sh`. OBS was used to validate this
four-source arrangement as the original streaming target, but it is not
required to access any driver interface.

## Research and authorship disclosure

This is not presented as a clean-room implementation. A lawfully obtained
official Windows driver was analyzed with Ghidra to understand device
initialization and interoperability behavior. AI agents assisted with
static-analysis navigation, comparison of decompiled routines, Linux driver
and script implementation, documentation, and the preparation of bounded
hardware tests. The project goals and technical approach were established
under my direction; hardware tests were performed and physically observed by
me, and the resulting code and documentation were reviewed before publication.
Full details are in
[`docs/REVERSE_ENGINEERING.md`](docs/REVERSE_ENGINEERING.md).

## License

The independently written Linux source and scripts are licensed under
GPL-2.0-only. See [`LICENSE`](LICENSE). This license grant does not grant any
rights to third-party trademarks or proprietary vendor software.
