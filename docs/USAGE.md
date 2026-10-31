# Capture, audio and RGB usage

The driver exposes video through Video4Linux2 and audio through ALSA. PipeWire
and WirePlumber are desktop integration layers; they are not required for
direct V4L2 or ALSA capture.

```text
HDMI video -> gc570d driver -> V4L2 nodes -> capture application
HDMI audio -> gc570d driver -> ALSA PCM -> direct ALSA application
                                      -> PipeWire -> PulseAudio-compatible source
                                           ^
                                           |
                                     WirePlumber policy
```

## Find the V4L2 nodes

Device numbers are assigned dynamically. Use names instead of assuming
`/dev/video1` or `/dev/video2`:

```sh
for name in /sys/class/video4linux/video*/name; do
    printf '%s: %s\n' "/dev/$(basename "$(dirname "$name")")" "$(cat "$name")"
done
```

Expected names are `gc570d-hdmi2` and `gc570d-hdmi1`.

Both V4L2 nodes publish the recovered discrete YUYV capture sizes (`1920x1080`,
`1280x720`, `1024x768`, `720x576`, `720x480`, and `640x480`). The capture rate
follows the source, so the same size can run at 50 or 60 Hz; do not infer a
frame rate from the output geometry. `VIDIOC_G_PARM` and
`VIDIOC_ENUM_FRAMEINTERVALS` report the source cadence for every advertised
size. HDMI IN 1 normalizes measured receiver timing to the 24/25/30/50/60 Hz
families with a ±3 Hz tolerance and caps higher-rate inputs at 60 fps. HDMI IN 2
decodes the CEA AVI VIC for standard video modes. VIP0/VIP1 perform spatial
scaling only; they do not convert frame rate. The colorspace, YCbCr encoding,
quantization, and transfer function reported by `VIDIOC_G_FMT`/`VIDIOC_TRY_FMT`
describe the packed DMA buffer, not the HDMI source: HDR is tone mapped to SDR
and source YCbCr is converted before capture, so HD sizes report BT.709 limited
range and SD sizes report BT.601 limited range.

* **HDMI IN 1 (`gc570d-hdmi1`)**: Downscales `2160p` and `1440p` inputs to
  `1920x1080` in the `Xilinx` preprocessing stage and captures `1080p`, `1080i`,
  `1024x768`, `720p`, `576p`, and `480p` inputs at their native hardware
  geometry without software scaling. `1080i` uses the recovered hardware field
  handling and is reported with `V4L2_FIELD_INTERLACED`. When capturing
  sub-1080p signals on HDMI IN 1 in applications such as OBS, select the matching
  source resolution in the V4L2 capture properties.
* **HDMI IN 2 (`gc570d-hdmi2`)**: Supports `1080p`, `1080i`, `1024x768`, `720p`,
  `576p`, and `480p` inputs either at their native resolution or hardware-scaled
  to the negotiated V4L2 resolution (such as `1920x1080`) through the `VIP1`
  hardware scaler. `VIP1` reconstructs progressive frames, so `1080i` and the
  scaled outputs are reported as `V4L2_FIELD_NONE`.

`576p50` is implemented but remains unvalidated until a physical 50 Hz source
is tested.
Example query:

```sh
v4l2-ctl -d /dev/video1 --list-formats-ext
v4l2-ctl -d /dev/video2 --list-formats-ext
```

For simultaneous capture, the two nodes may be started in either order. The
physically validated sequence is HDMI IN 2 first, wait for it to stream, then
HDMI IN 1; starting HDMI IN 1 first is supported by preserving the shared VIP
registers while the other channel is active.

## Video4Linux2 capture

Load the module and wait for automatic HDMI IN 1 initialization to report
`auto_ready=yes` in `status.sh`. Query each node before capture to confirm its
advertised format and current input status:

```sh
v4l2-ctl -d /dev/video1 --get-fmt-video --get-input
v4l2-ctl -d /dev/video2 --get-fmt-video --get-input
```

Use the device names reported earlier rather than assuming that these example
node numbers match the local system. A short memory-mapped streaming test can
discard the captured frames while checking that buffers and timestamps advance:

```sh
v4l2-ctl -d /dev/video1 --stream-mmap --stream-count=300 --stream-to=/dev/null
```

Any V4L2 application can consume the nodes directly. When opening both inputs,
start `gc570d-hdmi2` first, wait until it is streaming, and then start
`gc570d-hdmi1`.

The HDMI OUT signal remains at the supported source mode. HDMI IN 1 capture is
a separate SDR path capped at 60 fps and 1920x1080. For example, validated
2160p60 SDR/HDR and 1440p120 HDR passthrough are captured as 1080p60 SDR.

For reliable passthrough mode negotiation, use this physical connection order:

1. Connect and power the monitor or TV on HDMI OUT.
2. Wait for the display to be ready.
3. Connect or power the source on HDMI IN 1.

This order lets the source negotiate its resolution and refresh rate against
the downstream display capabilities exposed through the passthrough path. If
the source was connected first and chose an unsuitable mode, reconnect or
restart it after HDMI OUT is ready.

## Hot-plug and disconnected inputs

When an input has no usable video link at stream start, its V4L2 node remains
available and supplies the built-in generic penguin/cable frame with normal
timestamps at the selected advertised frame rate. The input reports
`V4L2_IN_ST_NO_SIGNAL` while this frame is active. If an active DMA stream
stops completing after a cable or source loss, the same open V4L2 stream falls
back to the built-in frame instead of terminating the capture stream.

Both disconnected inputs have been validated streaming this frame
simultaneously at 1920x1080 YUYV/60 fps. The fallback is produced in software
and does not enable the card's video DMA engines or shared video IRQ.

Both inputs monitor their receiver/link state while the built-in frame is
active. When a link returns, the driver performs the input-specific recovery,
waits for a stable receiver state, and changes that same open V4L2 stream back
to hardware capture. Complete live-to-placeholder-to-live cycles were
physically validated for HDMI IN 1 and HDMI IN 2 without STREAMOFF, closing
the capture client, or selecting the device again. The HDMI IN 1 cycle was also
validated while HDMI IN 2 remained open and live: its passthrough and capture
recovered after HDMI renegotiation while the original V4L2 stream stayed open.
HDMI IN 1 audio follows the same open-stream lifecycle. The driver does not add
or rewrite multichannel EDID data: normal passthrough uses the vendor bridge
EDID, which already advertises 8-channel LPCM and the 5.1/7.1 speaker layout.
The receiver accepts 2-channel stereo as well as multichannel (5.1/7.1, up to
8-channel) 48000 Hz uncompressed LPCM for passthrough to HDMI OUT while routing
the front left/right stereo pair (`SD0`) to the `HDMI 1 (Passthrough)` ALSA
capture device (`S16_LE`, 48000 Hz, 2 channels). Only 5.1 passthrough with
simultaneous stereo capture has been physically validated; 7.1 passthrough and
isolated audio cold start remain unvalidated. Although the bridge EDID
advertises the baseline CTA-861 LPCM sample rates (32/44.1/48 kHz) and no
compressed audio formats, the FPGA capture path and ALSA constraints operate
strictly at 48000 Hz LPCM; 32 kHz and 44.1 kHz LPCM sources (or forced non-LPCM
bitstreams) are rejected by the capture path with a ratelimited kernel warning.
During link loss or dynamic stereo/surround format transitions, the PCM stream
remains alive and supplies silence while the receiver relocks, then
automatically restarts HDMI IN 1 audio DMA so live audio returns without closing
the audio client or reselecting the source. This recovery was physically
validated while HDMI IN 2 continued operating.

Under the default `preserve` policy (`hdmi1_hotplug_policy=preserve`), connecting
or disconnecting an HDMI OUT monitor never pulses upstream HPD or interrupts
HDMI IN 1 capture. The internal 4K60 bridge EDID remains published and active
video/audio capture continues completely uninterrupted. Under `renegotiate` mode,
connecting an HDMI OUT monitor reads and publishes a compatible two-block EDID
with a deliberate upstream HPD pulse to renegotiate with the source, while
disconnecting HDMI OUT keeps the current capture mode without renegotiating.

## System suspend and resume

System suspend is allowed only while the capture hardware is idle. For an
S3/deep suspend, the driver stops its background workers and DMA engines,
clears pending interrupts, and releases PCI bus mastering. On resume it runs
the HDMI receiver, EDID/HPD and RGB initialization again, then restarts the
automatic HDMI IN 1 negotiation. HDMI IN 1 and passthrough can therefore take
about ten seconds to return after wake, just as they do after loading the
module.

If either V4L2 input is streaming, or either ALSA input has been prepared for
capture, the driver returns `-EBUSY` and prevents the system from sleeping.
This protects active capture sessions used by direct V4L2/ALSA clients and by
applications such as OBS or Discord. Stop or close the active capture and
request suspend again. A desktop environment may lock the session before the
kernel rejects the suspend attempt; that lock screen does not mean the machine
entered sleep.

Idle S3/deep resume and active-V4L2 suspend prevention were physically
validated on Bazzite 44. HDMI IN 2 returned at 59.94 fps without reloading the
module, and RGB state was restored. Recovery of a live HDMI IN 1 source and
post-resume ALSA capture still require dedicated physical validation.

## Direct ALSA capture

List the card and PCM device numbers:

```sh
arecord -l
```

On the tested system the stable ALSA card identifier is `DUO`; device 0 is
`HDMI 2 (Capture Only)` and device 1 is `HDMI 1 (Passthrough)`:

```sh
arecord -D hw:DUO,0 -f S16_LE -r 48000 -c 2 -d 5 hdmi2.wav
arecord -D hw:DUO,1 -f S16_LE -r 48000 -c 2 -d 5 hdmi1.wav
```

These commands open the kernel ALSA PCM devices directly and do not pass
through PipeWire, WirePlumber or the PulseAudio compatibility layer. Both
devices provide stereo S16_LE audio at 48 kHz.

If the identifier differs, use the card number printed by `arecord -l`, for
example `hw:4,1`. Each PCM now owns an independent reference to the shared
bridge IRQ and can prepare its matching receiver even when the corresponding
V4L2 stream is not open. Both PCM inputs can be open at the same time and are
independent of whether a video capture client is running.

## PipeWire, WirePlumber and PulseAudio compatibility

The kernel driver exposes the two inputs as ALSA PCM capture devices. PipeWire
can publish those PCMs to desktop applications, WirePlumber selects and
configures the card profile, and `pipewire-pulse` provides the PulseAudio-
compatible source API used by many applications.

`install-portable.sh` installs the supplied WirePlumber policy system-wide, so
it is applied automatically at desktop login. To apply it immediately to an
already-running session, run this as the desktop user:

```sh
./scripts/setup-pipewire.sh
```

The refresh script also installs a per-user copy and restarts the user's
PipeWire/WirePlumber services, so current audio streams are briefly
interrupted. The expected source descriptions are `HDMI 2 (Capture Only)` for
ALSA device 0 and `HDMI 1 (Passthrough)` for ALSA device 1. Inspect the sources
published through the PulseAudio-compatible API with:

```sh
pactl list short sources
pactl list sources
```

The first command prints the internal source names. One of those names can be
passed to a PulseAudio-compatible recorder for an end-to-end test:

```sh
parec --device=INTERNAL_SOURCE_NAME --file-format=wav pipewire-test.wav
```

Stop the recording with `Ctrl+C`.

If `arecord -l` shows the `DUO` card but `pactl` does not show either HDMI
source, inspect the card with `pactl list cards`. An active profile of `off`
means ALSA is healthy but PipeWire has not published its PCM inputs. Run
`./scripts/setup-pipewire.sh` as the desktop user to refresh the WirePlumber
policy and activate `pro-audio`. A source state of `SUSPENDED` is normal while
no application is recording it.

## Optional streaming-application test

OBS was the original target application and is useful as an integration test,
but it is not required to use the driver. For a simultaneous test, add both
V4L2 devices (the physically validated order is HDMI IN 2 then HDMI IN 1),
then select the two PulseAudio-compatible HDMI sources. This verifies that a
streaming client can keep both video nodes and both desktop-audio sources
active together.

## RGB enclosure lighting

The driver registers:

```text
/sys/class/leds/gc570d:rgb:status
```

Read the standard attributes and the GC570D effect selector:

```sh
LED=/sys/class/leds/gc570d:rgb:status
cat "$LED/multi_index"
cat "$LED/multi_intensity"
cat "$LED/brightness"
cat "$LED/effect"
```

The default `colorful` effect breathes through red, yellow, green, cyan, blue,
magenta and white. A standard RGB/brightness write selects static control:

```sh
LED=/sys/class/leds/gc570d:rgb:status
printf '255 0 255\n' | sudo tee "$LED/multi_intensity"
printf '255\n' | sudo tee "$LED/brightness"
```

Restore the full cycle or breathe only the selected color:

```sh
printf 'colorful\n' | sudo tee "$LED/effect"
printf 'breathing\n' | sudo tee "$LED/effect"
printf 'static\n' | sudo tee "$LED/effect"
```

`multi_intensity`, `brightness`, and `trigger` use standard Linux LED-class
interfaces. `effect` is a device-specific extension because Linux has no
generic multicolor breathing-effect ABI matching this controller.

## Diagnostics

```sh
sudo ./scripts/status.sh
sudo cat /sys/kernel/debug/gc570d/it68051_status
sudo cat /sys/kernel/debug/gc570d/splitter_pump_status
sudo dmesg | grep gc570d | tail -n 100
```

When reporting a bug, include the exact kernel version, PCI IDs, source mode,
HDR/HDCP/VRR state, monitor model, module commit, `status.sh` output and the
relevant kernel messages.
