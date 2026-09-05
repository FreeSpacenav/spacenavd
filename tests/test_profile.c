#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/profile.c"
struct cfg cfg;
struct profile profiles[MAX_PROFILES];
int num_profiles;
const char *(*kbemu_keyname)(unsigned int);
static const char *focused;
void logmsg(int priority, const char *fmt, ...) { (void)priority; (void)fmt; }
#ifdef USE_X11
int x11_get_focused_wm_class(char *buf, int size)
{
	if(!focused) return -1;
	snprintf(buf, size, "%s", focused);
	return 0;
}
#endif
int main(void)
{
	cfg.sensitivity = 1;
	profiles[0].name = "Blender"; profiles[0].match_class = "blender";
	profiles[0].pcfg.sensitivity = 2;
	profiles[1].name = "CAD"; profiles[1].match_class = "cad";
	profiles[1].pcfg.sensitivity = 3;
	num_profiles = 2;
	profile_on_cfg_reload(&cfg);
	assert(profile_set_manual(0) == 1 && cfg.sensitivity == 2);
	focused = "CAD";
	assert(profile_refresh_active() == 0 && profile_active_index() == 0);
	assert(profile_set_manual(50) == -1 && profile_active_index() == 0);
	/* Returning to auto must refresh immediately, even without X11. */
	assert(profile_set_manual(-1) == 1);
#ifdef USE_X11
	assert(profile_active_index() == 1 && cfg.sensitivity == 3);
	focused = "BLENDER";
	assert(profile_refresh_active() == 1 && profile_active_index() == 0);
	focused = NULL;
	assert(profile_refresh_active() == 1);
#else
	assert(profile_active_index() == -1);
#endif
	assert(cfg.sensitivity == 1);
	cfg.kbmap_count[0] = 2;
	cfg.kbmap_str[0] = "Control_L+Escape";
	assert(!strcmp(profile_get_button_label(0), "Control_L+Escape"));
	assert(!strcmp(profile_get_button_label(-1), ""));
	assert(!strcmp(profile_get_button_label(MAX_BUTTONS), ""));
	puts("Profile tests passed");
	return 0;
}
