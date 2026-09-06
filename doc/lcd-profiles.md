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

Uploads are synchronous, with a one-second budget across bulk transfers for a
frame. Device opening and HID feature ioctls are outside that budget.
The first matching Enterprise device is used, as in the original PR. Multiple
Enterprise devices and recovery of a partially received frame need hardware
validation and further design.

On the connected Enterprise (USB device version 4.42), descriptor inspection
confirmed interface 0 is vendor-specific with bulk OUT endpoint 0x01 and
64-byte packets; interface 1 is HID. Hardware testing confirmed that HID feature report `11 00` turns the backlight
off and `11 64` sets 100% brightness, with the input driver still attached.
A helper using the idle policy and HID sender also turned it off after eight
seconds. No input arrived during that helper test, so hardware wake-on-input
and simultaneous application input remain to be verified.

## Profile behavior change

`REQ_SET_APP_ID` records identity only. Registration by a background application
must not change global input mappings. Its wire format is unchanged, and the
unused automatic matching function is removed.

Automatic selection uses X11 window focus. `REQ_SET_PROFILE` explicitly selects
a profile; index -1 returns to automatic selection immediately. With no usable
X11 focus information, automatic selection uses the default configuration.
GNOME 50 Wayland focus switching is available through the optional compositor
extension and session helper in `contrib/gnome`. This supplies actual compositor
focus separately from application registration. Other Wayland compositors need
their own adapters.

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

## Screen settings and idle sleep

These global options apply to the Enterprise (USB `256f:c633`) on Linux:

```ini
lcd = on
lcd-brightness = 65
lcd-idle = 300
lcd-profile = on
```

`lcd = off` sends a backlight-off command. Brightness is retained separately
(0..100). `lcd-idle` is seconds without Enterprise movement outside the configured
deadzone or a button press; 0 disables sleep (the default), maximum 86400.
The daemon polls the monotonic timer at most every 500 ms while idle sleep is
configured. Motion or a button press wakes automatic sleep at the chosen
brightness. Manual off never wakes from input. Profile changes preserve these
settings and do not wake automatic sleep. Reconnection reapplies the configuration.
These settings are not accepted inside profile blocks.

Brightness uses Linux `HIDIOCSFEATURE(2)` on the hidraw node matching the bulk
USB device's bus and address. The report is `[0x11, brightness]`. The HID driver
stays attached; no USB reset is performed. The daemon needs access to both the
USB device and its hidraw node. A system service normally runs as root; custom
service hardening or udev permissions must also allow these devices.
Reading feature report 0x11 stalled on the tested firmware, so the daemon reports
its configured brightness rather than claiming to read the hardware state.

Protocol reference: https://github.com/TheHoodedFoot/SpaceLCD/blob/master/doc/notes.md
Linux API: https://docs.kernel.org/hid/hidraw.html
This establishes backlight control, not full power removal from LCD electronics.

The matching libspnav and spnavcfg forks provide Screen on/off, brightness,
idle timeout, profile title visibility, and an explicit refresh action. Settings
apply to the running daemon; use Save config to persist them. A setter confirms
configuration acceptance; Refresh reports the device update result. The fork
reserves requests 0x3f00..0x3f09 for this API; upstream allocation is not agreed.
Unsupported daemons return an error and the UI disables unsupported controls.

Tests additionally cover HID report bytes and device matching, idle transitions,
manual-off behavior, settings round trips, profile preservation, and invalid
protocol values. General repository audits remain separate from this work.

## Joystick LED idle timeout

`led-idle = 300` switches each device's LED off after five minutes without that
device's motion outside its deadzone or a button press. `0` means Never (default),
maximum 86400 seconds. This setting is global and preserved across profiles.
Movement or a button press restores the LED's requested On/Auto state; Off stays
off. New application connections do not wake an already sleeping LED. The GUI
exposes this timeout under General, independently of the screen timeout.
The LED timer works even in builds without LCD support.
