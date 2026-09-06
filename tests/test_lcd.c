/* Exercise the real LCD code against a simulated USB device. */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "../src/lcd.c"

struct cfg cfg;
int lcd_is_asleep(void) { return 0; }
static int backlight;
int lcd_hid_set_brightness(int bus, int address, int level)
{ assert(bus == 5 && address == 10); backlight = level; return 0; }
uint8_t LIBUSB_CALL libusb_get_bus_number(libusb_device *d) { (void)d; return 5; }
uint8_t LIBUSB_CALL libusb_get_device_address(libusb_device *d) { (void)d; return 10; }
static int mode, calls, claims, releases, closes, exits, resets, detached;
static int init_error, absent, descriptor_error, claim_error;
static unsigned char received[200000];
static int received_size;
static struct libusb_endpoint_descriptor endpoint;
static struct libusb_interface_descriptor alt[2];
static struct libusb_interface interfaces[2];
static struct libusb_config_descriptor descriptor;
static int dummy;
static const char *label = "Escape";

void logmsg(int priority, const char *fmt, ...) { (void)priority; (void)fmt; }
const char *profile_get_name(void) { return "Blender"; }
const char *profile_get_button_label(int button) { return button ? "" : label; }
int LIBUSB_CALL libusb_init(libusb_context **ctx)
{
	if(ctx) *ctx = (libusb_context*)&dummy;
	return init_error;
}
void LIBUSB_CALL libusb_exit(libusb_context *ctx) { (void)ctx; exits++; }
libusb_device_handle * LIBUSB_CALL libusb_open_device_with_vid_pid(libusb_context *ctx, uint16_t v, uint16_t p)
{
	(void)ctx; assert(v == 0x256f && p == 0xc633);
	return absent ? NULL : (libusb_device_handle*)&dummy;
}
libusb_device * LIBUSB_CALL libusb_get_device(libusb_device_handle *h)
{ (void)h; return (libusb_device*)&dummy; }
int LIBUSB_CALL libusb_get_active_config_descriptor(libusb_device *d, struct libusb_config_descriptor **c)
{ (void)d; *c = &descriptor; return descriptor_error; }
void LIBUSB_CALL libusb_free_config_descriptor(struct libusb_config_descriptor *c)
{ assert(c == &descriptor); }
int LIBUSB_CALL libusb_set_auto_detach_kernel_driver(libusb_device_handle *h, int enable)
{ (void)h; (void)enable; detached++; return 0; }
int LIBUSB_CALL libusb_reset_device(libusb_device_handle *h)
{ (void)h; resets++; return 0; }
int LIBUSB_CALL libusb_claim_interface(libusb_device_handle *h, int n)
{ (void)h; claims++; assert(n == 3); return claim_error; }
int LIBUSB_CALL libusb_release_interface(libusb_device_handle *h, int n)
{ (void)h; releases++; assert(n == 3); return 0; }
void LIBUSB_CALL libusb_close(libusb_device_handle *h) { (void)h; closes++; }
int LIBUSB_CALL libusb_bulk_transfer(libusb_device_handle *h, unsigned char ep, unsigned char *data,
	int length, int *transferred, unsigned int timeout)
{
	(void)h; assert(ep == 1); assert(timeout > 0); assert(++calls < 10000);
	if(mode == 1) { *transferred = 0; return 0; }
	if(mode == 2) { *transferred = 0; return LIBUSB_ERROR_TIMEOUT; }
	if(mode == 3) { *transferred = 2; return LIBUSB_ERROR_NO_DEVICE; }
	*transferred = mode == 4 && length > 7 ? 7 : length;
	memcpy(received + received_size, data, *transferred);
	received_size += *transferred;
	return 0;
}
static void reset(void)
{
	mode = calls = claims = releases = closes = exits = resets = detached = 0;
	init_error = absent = descriptor_error = claim_error = received_size = 0;
	memset(&endpoint, 0, sizeof endpoint);
	memset(alt, 0, sizeof alt);
	memset(interfaces, 0, sizeof interfaces);
	memset(&descriptor, 0, sizeof descriptor);
	endpoint.bEndpointAddress = 1;
	endpoint.bmAttributes = LIBUSB_TRANSFER_TYPE_BULK;
	alt[0].bInterfaceNumber = 0;
	alt[0].bInterfaceClass = LIBUSB_CLASS_HID;
	alt[1].bInterfaceNumber = 3;
	alt[1].bInterfaceClass = LIBUSB_CLASS_VENDOR_SPEC;
	alt[1].bNumEndpoints = 1;
	alt[1].endpoint = &endpoint;
	interfaces[0].altsetting = &alt[0]; interfaces[0].num_altsetting = 1;
	interfaces[1].altsetting = &alt[1]; interfaces[1].num_altsetting = 1;
	descriptor.bNumInterfaces = 2; descriptor.interface = interfaces;
}
int main(int argc, char **argv)
{
	unsigned char data[130], guarded[LCD_BITMAP_BYTES + 2], decoded[LCD_BITMAP_BYTES];
	int i;
	z_stream stream;
	(void)argc; (void)argv;
	cfg.lcd_flags = 3; cfg.lcd_brightness = 65;
	for(i = 0; i < (int)sizeof data; i++) data[i] = i;
	reset(); mode = 4;
	assert(lcd_usb_send(data, sizeof data) == 0);
	assert(received_size == sizeof data && !memcmp(data, received, sizeof data));
	assert(claims == 1 && releases == 1 && closes == 1 && exits == 1);
	assert(!resets && !detached);
	for(i = 1; i <= 3; i++) {
		reset(); mode = i;
		assert(lcd_usb_send(data, sizeof data) == -1);
		assert(calls == 1 && releases == 1 && closes == 1 && exits == 1);
	}
	reset(); claim_error = LIBUSB_ERROR_BUSY;
	assert(lcd_usb_send(data, sizeof data) == -1);
	assert(!calls && !releases && closes == 1 && exits == 1);
	reset(); descriptor_error = LIBUSB_ERROR_IO;
	assert(lcd_usb_send(data, sizeof data) == -1);
	assert(!claims && closes == 1 && exits == 1);
	reset(); endpoint.bEndpointAddress = 0x81;
	assert(lcd_usb_send(data, sizeof data) == -1 && !claims);
	reset(); alt[1].bInterfaceClass = LIBUSB_CLASS_HID;
	assert(lcd_usb_send(data, sizeof data) == -1 && !claims);
	reset(); absent = 1;
	assert(lcd_usb_send(data, sizeof data) == -1 && !closes && exits == 1);
	reset(); init_error = LIBUSB_ERROR_OTHER;
	assert(lcd_usb_send(data, sizeof data) == -1 && !exits);

	cfg.lcd_flags = 3;
	memset(guarded, 0xa5, sizeof guarded);
	label = "Control_L+Escape";
	render_bitmap(guarded + 1);
	assert(guarded[0] == 0xa5 && guarded[sizeof guarded - 1] == 0xa5);
	/* No label may paint into the gutter before the next button cell. */
	for(i = 35; i < 70; i++) {
		int x;
		for(x = 106; x < 116; x++) {
			int offset = 1 + (i * LCD_WIDTH + x) * 2;
			assert(!guarded[offset] && !guarded[offset + 1]);
		}
	}
	/* Very long and non-ASCII text must not overrun the framebuffer. */
	label = "Control_L+Shift_L+VeryLongButtonName";
	render_bitmap(guarded + 1);
	assert(guarded[0] == 0xa5 && guarded[sizeof guarded - 1] == 0xa5);
	memset(decoded, 0, sizeof decoded);
	draw_text(decoded, 0, 0, 6, "\xc3\xa9", 0xffe0, 1);
	memset(guarded + 1, 0, LCD_BITMAP_BYTES);
	draw_text(guarded + 1, 0, 0, 6, "?", 0xffe0, 1);
	assert(!memcmp(decoded, guarded + 1, sizeof decoded));
	for(i = 0; i < LCD_BITMAP_BYTES; i += 2) {
		assert((decoded[i] == 0 && decoded[i + 1] == 0) ||
				(decoded[i] == 0xe0 && decoded[i + 1] == 0xff));
	}
	label = "Control_L+Escape";
	render_bitmap(guarded + 1);
	reset(); lcd_update_mappings();
	assert(received_size > LCD_HEADER_SIZE && received[0] == 0x11 && received[1] == 0x0f);
	assert((received[2] | received[3] << 8) == received_size - LCD_HEADER_SIZE);
	for(i = 4; i < LCD_HEADER_SIZE; i++) assert(received[i] == 0);
	memset(&stream, 0, sizeof stream);
	stream.next_in = received + LCD_HEADER_SIZE;
	stream.avail_in = received_size - LCD_HEADER_SIZE;
	stream.next_out = decoded; stream.avail_out = sizeof decoded;
	assert(inflateInit2(&stream, -15) == Z_OK);
	assert(inflate(&stream, Z_FINISH) == Z_STREAM_END);
	assert(stream.total_out == LCD_BITMAP_BYTES);
	assert(!memcmp(decoded, guarded + 1, sizeof decoded));
	inflateEnd(&stream);
	if(argc == 2) {
		FILE *fp = fopen(argv[1], "wb"); assert(fp);
		fprintf(fp, "P6\n640 150\n255\n");
		for(i = 0; i < LCD_BITMAP_BYTES; i += 2) {
			/* Device wire format: little-endian BGR565. */
			unsigned char rgb[3];
			rgb[0] = (decoded[i] & 31) * 255 / 31;
			rgb[1] = (((decoded[i + 1] & 7) << 3) | (decoded[i] >> 5)) * 255 / 63;
			rgb[2] = (decoded[i + 1] >> 3) * 255 / 31;
			fwrite(rgb, 1, 3, fp);
		}
		fclose(fp);
	}
	cfg.lcd_flags = 0;
	reset(); assert(lcd_refresh() == 0 && backlight == 0 && !calls && !claims);
	render_bitmap(decoded);
	for(i = 0; i < LCD_BITMAP_BYTES; i++) assert(decoded[i] == 0);
	cfg.lcd_flags = 1;
	render_bitmap(decoded);
	for(i = 0; i < 30 * LCD_WIDTH * 2; i++) assert(decoded[i] == 0);
	puts("LCD tests passed");
	return 0;
}
