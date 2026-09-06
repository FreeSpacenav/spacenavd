#include <assert.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <linux/hidraw.h>
#include <linux/input.h>
#define LCD_SYS_HIDRAW_DIR "fake-sys"
#define LCD_DEV_DIR "fake-dev"
static int feature_count, feature_value, fail_ioctl;
static int test_ioctl(int fd, unsigned long request, ...)
{
	va_list ap; void *arg;
	(void)fd; va_start(ap, request); arg = va_arg(ap, void*); va_end(ap);
	if(request == HIDIOCGRAWINFO) {
		struct hidraw_devinfo *info = arg;
		info->bustype = BUS_USB; info->vendor = 0x256f; info->product = 0xc633;
		return 0;
	}
	assert(request == HIDIOCSFEATURE(2));
	assert(((unsigned char*)arg)[0] == 0x11);
	feature_value = ((unsigned char*)arg)[1]; feature_count++;
	return fail_ioctl ? -1 : 2;
}
#define ioctl test_ioctl
#include "../src/lcd_hid.c"
#undef ioctl
void logmsg(int p, const char *fmt, ...) { (void)p; (void)fmt; }
static void file(const char *path, const char *s)
{ FILE *f=fopen(path,"w"); assert(f); fputs(s,f); fclose(f); }
int main(void)
{
	mkdir("fake-sys",0700); mkdir("fake-sys/hidraw0",0700);
	mkdir("fake-usb",0700); mkdir("fake-usb/interface",0700); mkdir("fake-usb/interface/hid",0700);
	mkdir("fake-dev",0700);
	assert(symlink("../../fake-usb/interface/hid", "fake-sys/hidraw0/device") == 0);
	file("fake-usb/busnum","5\n"); file("fake-usb/devnum","10\n"); file("fake-dev/hidraw0","");
	assert(lcd_hid_set_brightness(5,10,0)==0 && feature_value==0 && feature_count==1);
	assert(lcd_hid_set_brightness(5,10,100)==0 && feature_value==100);
	assert(lcd_hid_set_brightness(5,10,35)==0 && feature_value==35);
	assert(lcd_hid_set_brightness(5,11,50)==-1 && feature_count==3);
	assert(lcd_hid_set_brightness(5,10,101)==-1 && feature_count==3);
	fail_ioctl=1; assert(lcd_hid_set_brightness(5,10,0)==-1);
	puts("HID backlight command tests passed");
	return 0;
}
