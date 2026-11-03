<!-- SPDX-License-Identifier: GPL-2.0-only -->

# Universal Blue inclusion request draft

This is a review draft, not a claim that Universal Blue or Easy Anti-Cheat has
approved OpenGC570D.

This draft targets the `v1.0.6` source tag. Submit it only after that release
has been published and the final tag has passed the RPM/AKMOD validation.

## Package request: ublue-os/packages

### Describe the package

Please add `gc570d` and `gc570d-kmod` to the `ublue-os/akmods` COPR. They
package OpenGC570D, an independently written GPL-2.0-only Linux driver for the
AVerMedia Live Gamer DUO GC570D PCIe capture card. The supported hardware is
strictly limited to PCI `1461:0054`, subsystem `1461:5700`.

The driver provides both HDMI inputs through V4L2 and ALSA, HDMI IN 1
passthrough, HDR-to-SDR/downscaled capture and standard multicolor LED-class
control. The motivation for Universal Blue packaging is hardware enablement on
Bazzite and a kernel-specific module built and signed through the distribution
pipeline rather than a locally installed unsigned module.

Upstream source and release:

- <https://github.com/neoyagami/OpenGC570D>
- <https://github.com/neoyagami/OpenGC570D/releases/tag/v1.0.6>

The repository includes source/provenance, reverse-engineering, security,
maintainer and packaging documentation. It contains no vendor driver or
firmware and does not require firmware at runtime.

### Information on the package

- License: `GPL-2.0-only`; the module reports `MODULE_LICENSE("GPL v2")`.
- Architectures: `x86_64` only until another architecture is physically
  validated.
- Common RPM: `gc570d-1.0.6-1.fc44.noarch`.
- AKMOD RPM: `akmod-gc570d-1.0.6-1.fc44.x86_64`.
- PCI alias: `pci:v00001461d00000054sv00001461sd00005700bc*sc*i*`.
- Fedora 44 release gate: GitHub Actions builds both RPMs and SRPMs, runs
  `rpmlint`, rebuilds the module through AKMOD and confirms that the
  development `debugfs` interface is disabled. Publish `v1.0.6` only after
  both jobs pass for the final tag.
- Native driver validation: Bazzite 44 with OGC kernel
  `7.2.9-ogc3.1.fc44.x86_64`; detailed hardware results are in
  `docs/STATUS.md`.

The proposed specifications are under `packaging/fedora/` in the upstream
repository.

## Kmod inclusion request: ublue-os/akmods

Please consider `gc570d` for the x86_64 `extra` group on the OGC and OGC-LTS
kernel builds used by Bazzite. It fills a Linux hardware-support gap for a PCIe
capture card and is comparable in scope to the existing `sc0710` capture-card
kmod in that group.

After the RPM specifications are accepted into the `ublue-os/akmods` COPR, the
proposed integration is:

1. Add `build_files/extra/build-kmod-gc570d.sh` using the reviewed script in
   this directory.
2. Invoke it in the `EXTRA` section of `Containerfile.in`.
3. Add OpenGC570D to the `extra` package table in the akmods README.
4. Regenerate the workflows if required by the current akmods contribution
   process.
5. Add `*gc570d*.rpm` from `{extra,kmods-extra}` to Bazzite's
   `build_files/install-kernel-akmods` selection.

The requested outcome is a module built for the exact OGC kernel and signed by
Universal Blue's normal kernel-module signing pipeline. This request does not
self-declare Secure Boot, attestation or EAC Linux v2 compatibility; those
properties require distribution acceptance, signing and full-system testing.
