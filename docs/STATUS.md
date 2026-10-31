# Validation status

Hardware validation was performed on one AVerMedia Live Gamer DUO GC570D,
PCI `1461:0054`, subsystem `1461:5700`, on Bazzite 44 and primarily with a PS5
source. Testing on other Linux distributions and kernel versions is wanted;
see [`CONTRIBUTING.md`](../CONTRIBUTING.md) for the requested report details.

| Function | Result |
| --- | --- |
| HDMI IN 2 1080p60 V4L2 | Physically validated |
| HDMI IN 2 open-stream live/no-signal/live hotplug | Physically validated without STREAMOFF: 884 frames/15 s before physical disconnect, built-in frame with healthy I2C/RGB while disconnected, and 841 frames/15 s after physical reconnect; zero unknown IRQs |
| HDMI IN 2 stereo S16_LE/48 kHz ALSA | Physically validated |
| HDMI IN 1 open-stream live/no-signal/live hotplug | Physically validated without STREAMOFF: live 1440p60 HDR input/downscaled capture and audio, built-in frame plus silent PCM after physical disconnect, then automatic return of video and real audio after reconnect; HDMI IN 2 remained active |
| HDMI IN 1 2160p60 SDR/HDR passthrough | Physically validated |
| HDMI IN 1 2160p60 SDR/HDR to 1080p60 SDR capture | Physically validated, including SDR-to-HDR and HDR-to-SDR recovery on the same open V4L2 stream |
| HDMI IN 1 1440p60 SDR/HDR passthrough | Physically validated |
| HDMI IN 1 1440p120 HDR passthrough | Physically validated |
| HDMI IN 1 1440p60 SDR/HDR to 1080p60 SDR capture | Physically validated |
| HDMI IN 1 measured 1440p120 HDR to 1080p60 SDR capture | Physically validated |
| HDMI IN 1 native 1080i, 1024x768, 720p60, and 480p60 SDR capture | Physically validated at native V4L2 capture geometry (`1920x1080` for 1080i using the recovered hardware field handling and `V4L2_FIELD_INTERLACED`, `1024x768`, `1280x720`, and `720x480`/`640x480`) |
| HDMI IN 2 1080i, 720p, 480p, and 1024x768 capture and VIP1 hardware scaling | Physically validated both at native geometry and hardware-scaled to `1920x1080`, including automatic VIP1 reinitialization across live source resolution changes; 1080i is reconstructed as progressive `V4L2_FIELD_NONE` |
| HDMI IN 1 and HDMI IN 2 576p50 capture | Implemented in the format tables and VIP0/VIP1 scaler paths; 576p50 remains unvalidated until a physical 50 Hz source is tested |
| HDMI IN 1 stereo S16_LE/48 kHz ALSA | Physically validated |
| HDMI IN 1 5.1 LPCM passthrough with stereo ALSA capture | Physically validated on Nintendo Switch 2: 5.1 LPCM passthrough to HDMI OUT with simultaneous stereo S16_LE/48 kHz capture (front left/right via I2S SD0) and automatic open-stream recovery across 5.1 <-> stereo transitions |
| HDMI IN 1 7.1 LPCM passthrough | Unvalidated; the vendor bridge EDID advertises 8-channel LPCM, but only 5.1 was physically tested |
| HDMI IN 1 isolated audio cold start | Unvalidated; the settled-format handshake was exercised across live stereo/surround transitions, not as a standalone cold start |
| Both V4L2 streams, HDMI 2 then HDMI 1 | Physically validated |
| Both V4L2 streams, HDMI 1 then HDMI 2 | Implemented by preserving shared VIP colour/bypass state; reverse order is accepted |
| Source-rate reporting in `VIDIOC_G_PARM`/`VIDIOC_ENUM_FRAMEINTERVALS` | Cadence is reported per source, not per output size: HDMI IN 1 normalizes measured receiver timing to the 24/25/30/50/60 Hz families with a ±3 Hz tolerance (a nominal 60 Hz source measures 57–59 Hz) before publishing it or selecting the capture mode, and HDMI IN 2 maps the AVI VIC to the CEA 24/25/30/50/60/100/120/200/240 Hz families. 50 Hz and 60 Hz capture paths are implemented; 60 Hz is physically validated and 50 Hz reporting (including 576p50) awaits a physical 50 Hz source test |
| Capture colorspace reporting in `VIDIOC_G_FMT`/`VIDIOC_TRY_FMT` | Describes the packed DMA output rather than the HDMI source: HD sizes report BT.709 limited range and SD sizes BT.601 limited range. HDR is tone mapped to SDR and source YCbCr is converted before the DMA buffer, so source colorimetry and quantization are not advertised |
| Both ALSA/PipeWire sources in OBS | Physically validated |
| Saved OBS scene restoring both videos and both audios | Physically validated without manually reordering or recreating sources; both PCM workers running, both video DMAs advancing, four shared-IRQ owners and zero unknown IRQs |
| Standard RGB writes and colorful breathing cycle | Physically validated |
| Generic no-signal V4L2 frame on both HDMI inputs | Physically validated concurrently at 1920x1080 YUYV/60 fps; no video DMA or IRQ users |
| Idle S3/deep suspend and resume | Physically validated twice on Bazzite 44 with kernel `7.2.9-ogc3.1.fc44.x86_64`: the driver quiesced the card, resumed through the full HDMI2 base initialization, restored RGB, kept both V4L2 nodes registered, and captured HDMI IN 2 again at 59.94 fps without a module reload |
| Suspend while V4L2 capture is active | Physically validated with a continuously streaming HDMI IN 2 V4L2 client: the driver returned `-EBUSY` for both the deep attempt and the subsequent s2idle fallback, preventing sleep without disrupting the capture; a later idle S3 cycle still succeeded |
| HDMI IN 1 and ALSA recovery across S3 | Implemented but not yet physically validated with a live HDMI IN 1 source or active post-resume ALSA capture |
| VRR, HDCP content, every monitor/source combination | Unsupported or unvalidated |

“Physically validated” means I observed the real output or capture and, where
applicable, also checked frame counts/hashes, timing, audio levels, or register
state. It does not mean certification across all Linux kernels or hardware
revisions.
