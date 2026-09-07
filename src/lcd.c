#include "config.h"
#include "lcd.h"
#include "profile.h"
#include "cfgfile.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern struct cfg cfg;

#ifdef HAVE_SPACELCD
#include <stdint.h>
#include "lcd_font.h"
#include <libusb.h>
#include <zlib.h>
#include <time.h>

#define LCD_WIDTH		640
#define LCD_HEIGHT		150
#define LCD_BPP			2
#define LCD_BITMAP_BYTES	(LCD_WIDTH * LCD_HEIGHT * LCD_BPP)
#define LCD_HEADER_SIZE		0x200
#define LCD_DEFLATED_MAX	0xffff
#define LCD_EFFECT_CUT		0x11

#define LCD_USB_VENDOR		0x256f
#define LCD_USB_PRODUCT		0xc633
#define LCD_USB_PACKET_MAX	64
#define LCD_USB_TIMEOUT		1000

/* Draw directly in the device's little-endian BGR565 format. The fixed
 * 5x7 font avoids font discovery, allocation and a graphics dependency.
 * Unsupported UTF-8 sequences are shown as one '?' per character.
 */
static void draw_text(uint8_t *buffer, int x, int y, int right,
        const char *text, unsigned int color, int scale)
{
	const unsigned char *p = (const unsigned char*)text;
	int row, col, dx, dy;

	if(right > LCD_WIDTH) right = LCD_WIDTH;
	while(*p && x + 5 * scale <= right) {
		unsigned int ch = *p++;
		if(ch < 32 || ch > 126) {
			if(ch >= 0xc0) {
				while((*p & 0xc0) == 0x80) p++;
			}
			ch = '?';
		}
		for(row = 0; row < 7; row++) {
			for(col = 0; col < 5; col++) {
				if(!(lcd_font[ch - 32][row] & (1 << (4 - col)))) continue;
				for(dy = 0; dy < scale; dy++) {
					for(dx = 0; dx < scale; dx++) {
						int px = x + col * scale + dx;
						int py = y + row * scale + dy;
						if(px >= 0 && px < right && py >= 0 && py < LCD_HEIGHT) {
							int offset = (py * LCD_WIDTH + px) * LCD_BPP;
							buffer[offset] = color & 0xff;
							buffer[offset + 1] = color >> 8;
						}
					}
				}
			}
		}
		x += 6 * scale;
	}
}

static void render_bitmap(uint8_t *buffer)
{
	int button;

	memset(buffer, 0, LCD_BITMAP_BYTES);
	if(!(cfg.lcd_flags & LCD_ENABLED)) return;
	if(cfg.lcd_flags & LCD_PROFILE) draw_text(buffer, 10, 6, LCD_WIDTH - 10, profile_get_name(), 0xffff, 2);
	for(button = 0; button < 12; button++) {
		const char *label = profile_get_button_label(button);
		int x = 10 + (button % 6) * (LCD_WIDTH / 6);
		int y = 36 + (button / 6) * 45;
		char number[8];

		/* Separate the button number from its label to leave more room. */
		snprintf(number, sizeof number, "%d", button + 1);
		draw_text(buffer, x, y, x + 96, number, 0xffff, 1);
		draw_text(buffer, x, y + 11, x + 96, *label ? label : "None",
				*label ? 0xffe0 : 0x52aa, strlen(label) > 8 ? 1 : 2);
	}
}

static int lcd_compress(const uint8_t *src, uint8_t *dst, int srclen)
{
	z_stream stream;
	int result, outsize;

	memset(&stream, 0, sizeof stream);
	stream.avail_in = srclen;
	stream.next_in = (Bytef *)src;
	stream.avail_out = LCD_DEFLATED_MAX;
	stream.next_out = dst;

	if(deflateInit2(&stream, -1, Z_DEFLATED, -15, 9, Z_FIXED) != Z_OK) {
		return -1;
	}
	result = deflate(&stream, Z_FINISH);
	outsize = LCD_DEFLATED_MAX - stream.avail_out;
	deflateEnd(&stream);

	return (result == Z_STREAM_END) ? outsize : -1;
}

/* Find the non-HID interface containing the LCD bulk OUT endpoint.
 * Only alternate setting zero is supported; never reconfigure the device
 * or detach its input driver just to update the display.
 */
static int lcd_usb_interface(libusb_device_handle *handle)
{
	struct libusb_config_descriptor *config;
	int i, j, k, interface = -1;
	int rc = libusb_get_active_config_descriptor(libusb_get_device(handle), &config);

	if(rc < 0) {
		logmsg(LOG_WARNING, "lcd: cannot read USB configuration: %s\n", libusb_strerror(rc));
		return -1;
	}
	for(i = 0; i < config->bNumInterfaces && interface < 0; i++) {
		const struct libusb_interface *iface = config->interface + i;
		for(j = 0; j < iface->num_altsetting && interface < 0; j++) {
			const struct libusb_interface_descriptor *alt = iface->altsetting + j;
			if(alt->bAlternateSetting || alt->bInterfaceClass == LIBUSB_CLASS_HID) continue;
			for(k = 0; k < alt->bNumEndpoints; k++) {
				const struct libusb_endpoint_descriptor *ep = alt->endpoint + k;
				if(ep->bEndpointAddress == 0x01 &&
						(ep->bmAttributes & LIBUSB_TRANSFER_TYPE_MASK) == LIBUSB_TRANSFER_TYPE_BULK) {
					interface = alt->bInterfaceNumber;
					break;
				}
			}
		}
	}
	libusb_free_config_descriptor(config);
	return interface;
}

static int lcd_usb_send(uint8_t *data, int size)
{
	libusb_context *context;
	libusb_device_handle *handle;
	int transferred, interface, rc, result = -1;
	struct timespec started, now;

	rc = libusb_init(&context);
	if(rc < 0) {
		logmsg(LOG_WARNING, "lcd: libusb_init failed: %s\n", libusb_strerror(rc));
		return -1;
	}
	handle = libusb_open_device_with_vid_pid(context, LCD_USB_VENDOR, LCD_USB_PRODUCT);
	if(!handle) {
		libusb_exit(context);
		return -1;	/* Device absent or inaccessible. */
	}
	if(lcd_hid_set_brightness(libusb_get_bus_number(libusb_get_device(handle)),
			libusb_get_device_address(libusb_get_device(handle)),
			(cfg.lcd_flags & LCD_ENABLED) && !lcd_is_asleep() ? cfg.lcd_brightness : 0) < 0) goto close;
	if(!(cfg.lcd_flags & LCD_ENABLED) || lcd_is_asleep()) {
		result = 0;
		goto close; /* Backlight off: no framebuffer upload needed. */
	}
	interface = lcd_usb_interface(handle);
	if(interface < 0) {
		logmsg(LOG_WARNING, "lcd: no supported LCD interface found\n");
		goto close;
	}
	rc = libusb_claim_interface(handle, interface);
	if(rc < 0) {
		logmsg(LOG_WARNING, "lcd: cannot claim interface %d: %s\n", interface, libusb_strerror(rc));
		goto close;
	}

	if(clock_gettime(CLOCK_MONOTONIC, &started) < 0) goto release;
	while(size > 0) {
		long elapsed;
		int timeout;
		int chunk = size > LCD_USB_PACKET_MAX ? LCD_USB_PACKET_MAX : size;
		if(clock_gettime(CLOCK_MONOTONIC, &now) < 0) goto release;
		elapsed = (now.tv_sec - started.tv_sec) * 1000 + (now.tv_nsec - started.tv_nsec) / 1000000;
		if(elapsed >= LCD_USB_TIMEOUT) {
			logmsg(LOG_WARNING, "lcd: frame upload timed out\n");
			goto release;
		}
		timeout = LCD_USB_TIMEOUT - elapsed;
		transferred = 0;
		rc = libusb_bulk_transfer(handle, 0x01, data, chunk, &transferred, timeout);
		if(rc < 0 || transferred <= 0 || transferred > chunk) {
			logmsg(LOG_WARNING, "lcd: USB transfer failed (%s, %d/%d bytes)\n",
					rc < 0 ? libusb_strerror(rc) : "invalid transfer length", transferred, chunk);
			goto release;
		}
		data += transferred;
		size -= transferred;
	}
	result = 0;
release:
	rc = libusb_release_interface(handle, interface);
	if(rc < 0) {
		logmsg(LOG_WARNING, "lcd: cannot release interface %d: %s\n", interface, libusb_strerror(rc));
		result = -1;
	}
close:
	libusb_close(handle);
	libusb_exit(context);
	return result;
}
#endif	/* HAVE_SPACELCD */

extern struct cfg cfg;

int lcd_refresh(void)
{
#ifdef HAVE_SPACELCD
	uint8_t *bitmap, *usbdata;
	int compressed_size, result;

	if(!(cfg.lcd_flags & LCD_ENABLED) || lcd_is_asleep()) return lcd_usb_send(0, 0);
	bitmap = malloc(LCD_BITMAP_BYTES);
	usbdata = calloc(1, LCD_DEFLATED_MAX + LCD_HEADER_SIZE);
	if(!bitmap || !usbdata) {
		free(bitmap);
		free(usbdata);
		return -1;
	}

	render_bitmap(bitmap);

	compressed_size = lcd_compress(bitmap, usbdata + LCD_HEADER_SIZE, LCD_BITMAP_BYTES);
	free(bitmap);
	if(compressed_size < 0 || compressed_size > 65535) {
		logmsg(LOG_WARNING, "lcd: compression failed\n");
		free(usbdata);
		return -1;
	}

	/* header: effect byte, flags, compressed length (16-bit LE) */
	usbdata[0] = LCD_EFFECT_CUT;
	usbdata[1] = 0x0f;
	usbdata[2] = compressed_size & 0xff;
	usbdata[3] = (compressed_size >> 8) & 0xff;

	result = lcd_usb_send(usbdata, compressed_size + LCD_HEADER_SIZE);
	free(usbdata);
	return result;
#else
	return -1;
#endif
}

void lcd_update_mappings(void)
{
	lcd_refresh();
}

int lcd_supported(void)
{
#if defined(HAVE_SPACELCD) && defined(__linux__)
	return 1;
#else
	return 0;
#endif
}
