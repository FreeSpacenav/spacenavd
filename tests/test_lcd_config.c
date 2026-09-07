#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/cfgfile.c"
#include "../src/profile.c"
const char *(*kbemu_keyname)(unsigned int);
void logmsg(int p, const char *fmt, ...) { (void)p; (void)fmt; }
int main(int argc, char **argv)
{
	FILE *fp;
	char text[16384];
	size_t count;
	assert(argc == 2);
	fp = fopen(argv[1], "w"); assert(fp);
	fputs("led-idle = 60\nlcd-idle = 120\nlcd-brightness = 65\nlcd = off\nlcd-profile = on\nprofile \"Blender\" class=blender\n sensitivity = 2\nend\n", fp); fclose(fp);
	assert(read_cfg(argv[1], &cfg) == 0);
	assert(cfg.lcd_flags == 2 && num_profiles == 1);
	assert(cfg.lcd_idle_seconds == 120 && cfg.lcd_brightness == 65);
	assert(write_cfg(argv[1], &cfg) == 0);
	assert(num_profiles == 1 && !strcmp(profiles[0].name, "Blender"));
	fp = fopen(argv[1], "r"); assert(fp);
	count = fread(text, 1, sizeof text - 1, fp); text[count] = 0; fclose(fp);
	assert(strstr(text, "editor-controls = 2000,0,1\n")); /* canonical profile override */
	assert(read_cfg(argv[1], &cfg) == 0 && cfg.lcd_flags == 2);
	cfg.led_idle_seconds = 180; cfg.repeat_msec = 250; cfg.lcd_flags = 1; cfg.lcd_idle_seconds = 300; cfg.lcd_brightness = 40;
	assert(write_cfg(argv[1], &cfg) == 0);
	assert(read_cfg(argv[1], &cfg) == 0 && cfg.lcd_flags == 1);
	assert(num_profiles == 1 && cfg.repeat_msec == 250 && cfg.led_idle_seconds == 180);
	assert(cfg.lcd_idle_seconds == 300 && cfg.lcd_brightness == 40);
	profile_on_cfg_reload(&cfg);
	assert(profile_set_focus(&cfg, "blender.desktop") == 1);
	assert(!strcmp(profile_get_name(), "Blender"));
	assert(cfg.sensitivity == 2 && cfg.lcd_idle_seconds == 300 && cfg.led_idle_seconds == 180);
	assert(profile_set_focus(&cfg, "chatgpt.desktop") == 1);
	assert(!strcmp(profile_get_name(), "Default"));
	profile_clear_focus(&cfg);
	default_cfg(&cfg); assert(cfg.lcd_idle_seconds == 0); assert(cfg.lcd_flags == 3);
	puts("LCD configuration persistence tests passed");
	return 0;
}
