# SpaceMouse Enterprise LCD and profile follow-up

This change follows PR #134 at `acffe75`. It removes Cairo and fixes USB
transfer handling and application-ID/profile arbitration. Profiles remain
experimental; this is not a complete redesign of their configuration model.

## Building

LCD support needs the libusb-1.0 and zlib development packages and pkg-config:

```sh
./configure --enable-spacelcd
make
```

There is no Cairo, librsvg, system-font or image-file dependency. The default
configuration detects LCD dependencies automatically. Explicitly enabling LCD
support fails if dependencies are missing; `--disable-spacelcd` needs neither
library for the daemon build. X11 can independently be disabled.

Zlib is retained. The existing device framing uses a raw DEFLATE payload and a
16-bit compressed-length field. An uncompressed 640 x 150 x 2 framebuffer is
192,000 bytes, so simply omitting compression cannot fit this framing. Whether
the device supports another framing or encoding remains unverified.

## Display and USB behavior

An original 5x7 printable-ASCII font renders directly into little-endian
BGR565 bytes. Unsupported characters use `?`. Labels are clipped to their own
cells; longer labels use the smaller font. The profile title and twelve key
labels retain the original two-row layout. Full configured key sequences are
used when available, subject to the screen's limited space.

The sender inspects the active USB configuration for bulk OUT endpoint 0x01
in a non-HID interface with alternate setting zero. It claims only that
interface, never resets the device, and never enables kernel-driver detachment.
Descriptor, claim and transfer failures terminate the update. Zero-progress
writes fail rather than looping, short successful writes advance correctly,
and claimed interfaces and handles are released on exit. Initialization of
the zlib stream is checked before compression.

Uploads still use synchronous transfers with a one-second timeout per transfer;
this does not provide asynchronous rendering or an overall frame deadline.
The first matching Enterprise device is used, as in the original PR. Multiple
Enterprise devices and recovery of a partially received frame need hardware
validation and further design.

On the connected Enterprise (USB device version 4.42), descriptor inspection
confirmed interface 0 is vendor-specific with bulk OUT endpoint 0x01 and
64-byte packets; interface 1 is HID. The session lacked permission to open
the device, so no screen upload or simultaneous-input test was performed.

## Profile behavior change

`REQ_SET_APP_ID` records identity only. Registration by a background application
must not change global input mappings. Its wire format is unchanged, and the
unused automatic matching function is removed.

Automatic selection uses X11 window focus. `REQ_SET_PROFILE` explicitly selects
a profile; index -1 returns to automatic selection immediately. With no usable
X11 focus information, automatic selection uses the default configuration.
Native Wayland focus switching is not implemented: application identity alone
cannot establish focus, so use explicit profile selection there. A future
focus/activation protocol requires a separate design.

Socket configuration reset and restore now reset profile state and update the
LCD, matching the intent of the existing configuration reload path.

## Tests

Run from the repository root (C compiler, pkg-config, libusb/zlib development
packages, and X11 headers required; no device or running daemon required):

```sh
./tests/run.sh
TEST_CFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' ./tests/run.sh
```

Tests compile isolated copies of the actual sources. They cover simulated USB
selection, cleanup and failures, partial and zero-byte transfers, framebuffer
bounds, label clipping, UTF-8 fallback, wire colors, DEFLATE round trips, profile
selection with and without X11, background application registration, reset and
restore, and configure behavior without Cairo or with missing LCD dependencies.

To produce a PPM preview from the renderer, set `LCD_PREVIEW` to an absolute
output path when running the tests. The preview is simulated, not a photo of
the device. Build validation covers all four combinations of LCD and X11.
The existing daemon has compiler warnings outside these changes.

Before upstream submission, verify on hardware that repeated screen updates
preserve motion and button input, that the display is legible with representative
mappings, and that unplug/replug works. No installed daemon has been replaced.
