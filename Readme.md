# iPod Nano 3G Emulator

This project is an attempt at rehosting the entirety of the 3rd Generation iPod Nano. It is currently a work in progress, and is not yet functional. The goal is to be able to run the iPod Nano 3G's firmware in a QEMU emulator.

This work is based on devos50's work emulating the iPhone 1G and the iTouch 2G. See the repo this is forked from for more information.

Before I started this, I started a more bespoke emulator based on Unicorn. [Check that out here](https://github.com/lemonjesus/iPod-n3g-emulator), but it's by no means as far along as this project.

## What works

* SPI NOR flash
* I2C communications probably work, but the PMU is currently a dummy device
* The LCD screen
* Hardware JPEG decoding (it's good enough for now, but it isn't perfect)

Currently, the iPod Nano 3G emulator is able to boot partially into the EFI. At some point, it decides it can't actually boot, so it displays the Red X image.

Below this, the README is a work in progress.

## Building

Configure it with:

```
mkdir build
cd build
../configure --enable-sdl --disable-cocoa --target-list=arm-softmmu --disable-capstone --disable-pie --disable-slirp --disable-werror --extra-cflags="-g" --extra-ldflags='-lcrypto'
make -j12
```

## How to get the files required to run the emulator:

### BOOTROM
Simply dump this with wInd3x from a real iPod Nano 5G. The emulator will make the required patches to get around the bootrom's security checks. The SHA256 of the bootrom is `e1d75912517026f492c7ad25d6d7671ce59e4caac365c049cc6ffddf3e47467e`.

### NOR and EFI Bootloader

The iPod Nano 5G doesn't have a NOR chip, but it can still boot from NOR, and we do that because we don't yet have full NAND support.

Download a firmware IPSW, look for `N33.bootloader.release.rb3.dec`, decrypt it using wInd3x. That's the bootloader we'll place into NOR. Mine has a SHA256 sum of `d6c3bd668f54f6718cbb2ad2916bc93d007cba53adea4935c680fbbd618ab5b2` but yours might differ (depending on the version and the decryption method used).


### NAND

lol, lmao even

## Other Notes

Run it with:

```
build/arm-softmmu/qemu-system-arm \
    -M iPod-Nano3G,bootrom=bootrom.bin,bootloader=bootloader.bin \
    -cpu arm1176 -d unimp
```
