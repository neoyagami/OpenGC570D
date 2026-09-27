# SPDX-License-Identifier: GPL-2.0-only
# Kbuild definition and convenience targets for building and installing gc570d.ko.

obj-m += gc570d.o
gc570d-y := \
	src/gc570d-main.o \
	src/gc570d-hw.o \
	src/gc570d-i2c.o \
	src/gc570d-led.o \
	src/gc570d-receiver.o \
	src/gc570d-splitter.o \
	src/gc570d-dma.o \
	src/gc570d-audio.o \
	src/gc570d-video.o

KERNEL_RELEASE ?= $(shell uname -r)
KDIR ?= /lib/modules/$(KERNEL_RELEASE)/build

# Match the kernel's toolchain. A kernel built with clang (CONFIG_CC_IS_CLANG=y,
# e.g. CachyOS) passes clang-only flags to the compiler, so a module built with
# the default GCC fails on options such as -mstack-alignment=8. Auto-select the
# LLVM toolchain unless the caller already chose one (make LLVM=1 / CC=...).
KERNEL_AUTOCONF := $(KDIR)/include/config/auto.conf
ifeq ($(shell grep -qs '^CONFIG_CC_IS_CLANG=y' $(KERNEL_AUTOCONF) && echo 1),1)
LLVM ?= 1
endif
export LLVM

INSTALL_MOD_DIR ?= extra/gc570d
NO_SIGNAL_SOURCE := assets/no-signal.png
NO_SIGNAL_DATA := data/gc570d_no_signal_640x360.inc

.PHONY: all clean install uninstall check no-signal prepare-no-signal

all: prepare-no-signal
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean

install: all
	install -D -m 0644 gc570d.ko \
		/lib/modules/$(KERNEL_RELEASE)/$(INSTALL_MOD_DIR)/gc570d.ko
	depmod -a $(KERNEL_RELEASE)

uninstall:
	rm -f /lib/modules/$(KERNEL_RELEASE)/$(INSTALL_MOD_DIR)/gc570d.ko
	depmod -a $(KERNEL_RELEASE)

prepare-no-signal: $(NO_SIGNAL_DATA)

$(NO_SIGNAL_DATA): $(NO_SIGNAL_SOURCE)
	./scripts/generate-no-signal-asset.sh

no-signal:
	./scripts/generate-no-signal-asset.sh

check:
	sh -n scripts/*.sh
	./scripts/check-release.sh
