#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
build=$(mktemp -d)
trap 'rm -rf "$build"' EXIT HUP INT TERM
# Copy sources so quoted config.h includes cannot pick up the daemon's
# generated configuration. This tests both X11 variants after any build.
mkdir "$build/src" "$build/tests"
cp src/*.[ch] "$build/src/"
cp tests/*.c tests/test_configure.sh "$build/tests/"
cp configure Makefile.in "$build/"
cd "$build"
printf '#define HAVE_SPACELCD\n#define HAVE_VSNPRINTF\n' > "$build/src/config.h"
cc=${CC:-cc}
flags="-g -Wall -Wextra -Wno-unused-parameter ${TEST_CFLAGS:-}"
$cc $flags -I"$build" -Isrc $(pkg-config --cflags libusb-1.0) tests/test_lcd.c \
	-o "$build/test_lcd" $(pkg-config --libs libusb-1.0 zlib)
"$build/test_lcd" ${LCD_PREVIEW:+"$LCD_PREVIEW"}
$cc $flags -I"$build" -Isrc tests/test_profile.c -o "$build/test_profile"
"$build/test_profile"
$cc $flags -DUSE_X11 -I"$build" -Isrc tests/test_profile.c -o "$build/test_profile_x11"
"$build/test_profile_x11"
$cc $flags -ffunction-sections -fdata-sections -I"$build" -Isrc \
	tests/test_app_id.c src/profile.c src/profile_edit.c src/keymap.c src/proto.c -Wl,--gc-sections -o "$build/test_app_id"
"$build/test_app_id"
sh tests/test_configure.sh
$cc $flags -I"$build" -Isrc tests/test_lcd_config.c -o "$build/test_lcd_config"
"$build/test_lcd_config" "$build/lcd.conf"
$cc $flags -I"$build" -Isrc tests/test_lcd_hid.c -o "$build/test_lcd_hid"
"$build/test_lcd_hid"

$cc $flags -I"$build" -Isrc tests/test_lcd_idle.c -o "$build/test_lcd_idle"
"$build/test_lcd_idle"

$cc $flags -I"$build" -Isrc tests/test_led_idle.c -o "$build/test_led_idle"
"$build/test_led_idle"

$cc $flags -I"$build" -Isrc tests/test_profile_editor.c src/keymap.c -o "$build/test_profile_editor"
"$build/test_profile_editor" "$build/profiles.conf"
$cc $flags -I"$build" -Isrc tests/test_button_keys.c -o "$build/test_button_keys"
"$build/test_button_keys"

$cc $flags -ffunction-sections -fdata-sections -I"$build" -Isrc tests/test_profiles_protocol.c src/profile.c src/profile_edit.c src/keymap.c src/proto.c -Wl,--gc-sections -o "$build/test_profiles_protocol"
"$build/test_profiles_protocol"
