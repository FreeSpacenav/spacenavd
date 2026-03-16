#include "config.h"
#include "lcd.h"
#include "profile.h"
#include "cfgfile.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_SPACELCD
#include <stdint.h>
#include <cairo/cairo.h>
#include <libusb.h>
#include <zlib.h>

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

static void rgb_to_bgr(uint8_t *dst, const uint8_t *src, int size)
{
	int i;
	for(i = 0; i < size; i += 2) {
		dst[i]     = (src[i + 1] >> 3) | (src[i] & 0xe0);
		dst[i + 1] = (src[i] << 3) | (src[i + 1] & 0x07);
	}
}

static void render_bitmap(uint8_t *buffer)
{
	const int cols = 6;
	const int rows = 2;
	int btn = 0;
	int r, c;
	cairo_surface_t *surface;
	cairo_t *cr;

	surface = cairo_image_surface_create(CAIRO_FORMAT_RGB16_565, LCD_WIDTH, LCD_HEIGHT);
	cr = cairo_create(surface);

	/* black background */
	cairo_set_source_rgb(cr, 0, 0, 0);
	cairo_paint(cr);

	/* profile name */
	cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
	cairo_set_font_size(cr, 18);
	cairo_set_source_rgb(cr, 1, 1, 1);
	cairo_move_to(cr, 10, 20);
	cairo_show_text(cr, profile_get_name());

	/* button labels */
	cairo_set_font_size(cr, 16);
	for(r = 0; r < rows; r++) {
		for(c = 0; c < cols; c++, btn++) {
			const char *lbl = profile_get_button_label(btn);
			int x = 10 + c * (LCD_WIDTH / cols);
			int y = 50 + r * 45;
			char text[64];

			if(lbl[0]) {
				cairo_set_source_rgb(cr, 0, 1, 1);	/* cyan */
				snprintf(text, sizeof text, "%d: %s", btn + 1, lbl);
			} else {
				cairo_set_source_rgb(cr, 0.33, 0.33, 0.33);	/* #555 */
				snprintf(text, sizeof text, "%d: None", btn + 1);
			}
			cairo_move_to(cr, x, y);
			cairo_show_text(cr, text);
		}
	}

	cairo_surface_flush(surface);
	rgb_to_bgr(buffer, cairo_image_surface_get_data(surface), LCD_BITMAP_BYTES);

	cairo_destroy(cr);
	cairo_surface_destroy(surface);
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

	deflateInit2(&stream, -1, Z_DEFLATED, -15, 9, Z_FIXED);
	result = deflate(&stream, Z_FINISH);
	outsize = LCD_DEFLATED_MAX - stream.avail_out;
	deflateEnd(&stream);

	return (result == Z_STREAM_END) ? outsize : -1;
}

static int lcd_usb_send(uint8_t *data, int size)
{
	libusb_device_handle *handle;
	int transferred, i, rc;

	rc = libusb_init(NULL);
	if(rc < 0) {
		logmsg(LOG_WARNING, "lcd: libusb_init failed: %s\n", libusb_strerror(rc));
		return -1;
	}

	handle = libusb_open_device_with_vid_pid(NULL, LCD_USB_VENDOR, LCD_USB_PRODUCT);
	if(!handle) {
		libusb_exit(NULL);
		return -1;	/* device not present, not an error worth logging */
	}

	libusb_set_auto_detach_kernel_driver(handle, 1);
	for(i = 0; i < 2; i++) {
		libusb_claim_interface(handle, i);
		libusb_reset_device(handle);
	}

	while(size > 0) {
		int chunk = size > LCD_USB_PACKET_MAX ? LCD_USB_PACKET_MAX : size;
		libusb_bulk_transfer(handle, 0x01, data, chunk, &transferred, LCD_USB_TIMEOUT);
		data += transferred;
		size -= transferred;
	}

	for(i = 0; i < 2; i++) {
		libusb_release_interface(handle, i);
	}
	libusb_close(handle);
	libusb_exit(NULL);
	return 0;
}
#endif	/* HAVE_SPACELCD */

extern struct cfg cfg;

void lcd_update_mappings(void)
{
#ifdef HAVE_SPACELCD
	uint8_t *bitmap, *usbdata;
	int compressed_size;

	bitmap = malloc(LCD_BITMAP_BYTES);
	usbdata = calloc(1, LCD_DEFLATED_MAX + LCD_HEADER_SIZE);
	if(!bitmap || !usbdata) {
		free(bitmap);
		free(usbdata);
		return;
	}

	render_bitmap(bitmap);

	compressed_size = lcd_compress(bitmap, usbdata + LCD_HEADER_SIZE, LCD_BITMAP_BYTES);
	free(bitmap);
	if(compressed_size < 0 || compressed_size > 65535) {
		logmsg(LOG_WARNING, "lcd: compression failed\n");
		free(usbdata);
		return;
	}

	/* header: effect byte, flags, compressed length (16-bit LE) */
	usbdata[0] = LCD_EFFECT_CUT;
	usbdata[1] = 0x0f;
	usbdata[2] = compressed_size & 0xff;
	usbdata[3] = (compressed_size >> 8) & 0xff;

	lcd_usb_send(usbdata, compressed_size + LCD_HEADER_SIZE);
	free(usbdata);
#endif
}
