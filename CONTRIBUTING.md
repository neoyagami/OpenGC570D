# Contributing

## Branch and validation workflow

`main` is the stable branch. It contains changes that have passed repository
checks and the relevant physical-hardware validation. Releases are made from
`main` only.

`dev` is the integration and hardware-test branch. Feature and driver-fix pull
requests should normally target `dev`, especially when they change HDMI
negotiation, EDID/HPD handling, DMA, interrupts, audio recovery, suspend/resume
or another timing-sensitive hardware path. A change may be merged into `dev`
with clearly documented test gaps or known limitations so that it can receive
broader physical testing before it reaches `main`.

The normal flow is:

1. Create a focused feature or fix branch from the current `dev` branch.
2. Open a pull request targeting `dev` and document static evidence, physical
   test results, known limitations and hardware still needing validation.
3. Run the automated checks and test the affected paths on physical hardware.
4. Promote a tested group of changes with a pull request from `dev` to `main`.
5. Create a release only after the promotion has reached `main`.

Urgent fixes for a released version may target `main` directly when their scope
and validation justify it. Such fixes must also be merged or applied back to
`dev` so that the branches do not regress. Do not force-push or routinely delete
either `main` or `dev`; preserve their shared review and validation history.

Merging into `dev` means that a change is accepted for integration testing. It
does not by itself mean that the feature is stable, fully validated or included
in a release.

## Driver source layout

The driver is a composite kernel module built from independent objects under
`src/`.  Shared state, register definitions and internal interfaces live in
`src/gc570d.h`.

- `gc570d-main.c`: bridge setup, PCI probe/removal and module registration
- `gc570d-hw.c`: interrupts, register diagnostics and Xilinx access
- `gc570d-i2c.c`: common I2C transactions
- `gc570d-led.c`: enclosure RGB LED support
- `gc570d-receiver.c`: IT6802/IT68051 receiver support
- `gc570d-splitter.c`: splitter initialization and link/event handling
- `gc570d-dma.c`: VIP and raw video DMA handling
- `gc570d-audio.c`: raw audio DMA and ALSA PCM support
- `gc570d-video.c`: V4L2 streaming for both HDMI inputs

Keep new code in the narrowest matching source file.  Add cross-component
interfaces to `gc570d.h` only when they are genuinely shared.

Contributions should be independently written code, tests or documentation
based on lawful interoperability research and scoped to the public Linux
implementation described in `docs/REVERSE_ENGINEERING.md`.

## Distribution testing

Help testing the driver beyond its current Bazzite 44 environment is wanted.
Reports from Fedora, Debian, Ubuntu, Arch, immutable distributions and other
kernel versions are welcome, including reports where the driver does not build
or load.

A useful test report includes:

1. Distribution and version, `uname -r`, and compiler version.
2. The output of `lspci -nnk -d 1461:0054`.
3. Whether Secure Boot is enabled and how the module was installed and signed.
4. HDMI source, display, resolution, refresh rate, SDR/HDR and passthrough use.
5. Which V4L2 and ALSA inputs were tested, including concurrent-stream order.
6. Relevant build output, `modinfo ./gc570d.ko`, and bounded `dmesg` excerpts.
7. Whether the result is direct physical observation or static analysis only.

Do not publish proprietary vendor drivers, firmware, captures containing
private material, or other restricted artifacts with a report.

For code changes:

1. Describe the exact PCI subsystem, kernel and source/display mode.
2. Keep hardware writes bounded and explain their evidence.
3. Preserve the validated HDMI IN 2 path unless the change explicitly targets
   and tests it.
4. For concurrent video, test HDMI IN 2 STREAMON before HDMI IN 1.
5. Run the compilation, repository and style checks described below.
6. Separate a static-analysis conclusion from a physical hardware result.

### Code review checks

Contributors and reviewers must use the same baseline before a driver change is
merged into `dev`. Run these commands from a clean checkout with the target
kernel build files installed:

```sh
make W=1
make check
git diff --check
```

Kernel source changes must also pass the target kernel's `checkpatch.pl` in
strict mode. For a feature branch targeting `dev`, run:

```sh
CHECKPATCH=/lib/modules/$(uname -r)/build/scripts/checkpatch.pl
git diff --no-ext-diff --unified=80 origin/dev...HEAD -- \
  'src/*.c' 'src/*.h' | "$CHECKPATCH" --no-tree --strict -
```

Use the actual target branch instead of `origin/dev` for an urgent fix aimed at
`main`. New errors, warnings or strict checks must be corrected or explicitly
explained in the pull request when hardware timing requires a deliberate
exception. A check that is a verified false positive should still be avoided
when a clearer equivalent comment or layout removes it without changing
behavior.

`make check` validates POSIX shell syntax, packaging and release contents. When
a contribution changes the systemd unit, also run:

```sh
systemd-analyze verify config/systemd/gc570d.service
```

Keep shell commands readable and wrap newly added lines at approximately 100
columns. The review must record which checks were run, distinguish environmental
tool warnings from source warnings, and leave the worktree clean after generated
build files are removed with `make clean`.

AI-assisted contributions are welcome when disclosed. The contributor remains
responsible for reviewing and testing the submitted work.

Distribution packaging changes should follow
[`docs/PACKAGING.md`](docs/PACKAGING.md) and must not claim Secure Boot,
attestation or anti-cheat compatibility solely because a module has a local
signature. Review [`docs/DATA_PROVENANCE.md`](docs/DATA_PROVENANCE.md) before
redistributing the embedded EDID or HDR lookup data.

Security-sensitive reports should follow [`SECURITY.md`](SECURITY.md) instead
of disclosing exploit details in a public issue.
