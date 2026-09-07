#include "config.h"
#include "profile.h"
#include "logger.h"
#include "kbemu.h"
#ifdef USE_X11
#include "proto_x11.h"
#endif
#include <string.h>
#include <ctype.h>

extern struct cfg cfg;

static struct cfg base_cfg;
static int active_profile = -1;
static const void *focus_owner;
static char focus_id[256];
static int manual_override;	/* when set, auto-detection is suppressed */

void profile_on_cfg_reload(struct cfg *c)
{
	base_cfg = *c;
	active_profile = -1;
	manual_override = 0;
}

static int match_class(const char *str, const char *match)
{
	char buf1[256], buf2[256];
	size_t i;

	if(!str || !match || !*str || !*match) return 0;

	for(i = 0; i < sizeof(buf1) - 1 && str[i]; i++) buf1[i] = (char)tolower((unsigned char)str[i]);
	buf1[i] = 0;
	for(i = 0; i < sizeof(buf2) - 1 && match[i]; i++) buf2[i] = (char)tolower((unsigned char)match[i]);
	buf2[i] = 0;
	return buf2[0] == '=' ? strcmp(buf1, buf2 + 1) == 0 : strstr(buf1, buf2) != NULL;
}

static int activate_profile(int new_index)
{
	if(new_index != active_profile) {
		int lcd_flags = cfg.lcd_flags, lcd_brightness = cfg.lcd_brightness;
		int lcd_idle_seconds = cfg.lcd_idle_seconds, led_idle_seconds = cfg.led_idle_seconds;
		if(new_index >= 0) {
			logmsg(LOG_INFO, "Profile switch: %s\n", profiles[new_index].name ? profiles[new_index].name : "(unnamed)");
			cfg = profiles[new_index].pcfg;
		} else {
			logmsg(LOG_INFO, "Profile switch: Default\n");
			cfg = base_cfg;
		}
		cfg.lcd_flags = lcd_flags;
		cfg.lcd_brightness = lcd_brightness;
		cfg.lcd_idle_seconds = lcd_idle_seconds;
		cfg.led_idle_seconds = led_idle_seconds;
		active_profile = new_index;
		return 1;
	}
	return 0;
}

static int find_profile(const char *id)
{
	int i;

	if(id && *id) {
		for(i = 0; i < num_profiles; i++) {
			if(profiles[i].match_class && match_class(id, profiles[i].match_class)) {
				return i;
			}
		}
	}
	return -1;
}

int profile_refresh_active(void)
{
	if(manual_override) return 0;
	if(focus_owner) return activate_profile(find_profile(focus_id));

#ifdef USE_X11
	{
		char cls[256] = {0};
		x11_get_focused_wm_class(cls, sizeof cls);
		return activate_profile(find_profile(cls));
	}
#else
	return activate_profile(-1);
#endif
}

int profile_set_manual(int index)
{
	if(index < -1 || index >= num_profiles) return -1;

	if(index == -1) {
		/* return to auto mode */
		manual_override = 0;
		logmsg(LOG_INFO, "Profile mode: auto\n");
		return profile_refresh_active();
	}

	manual_override = 1;
	return activate_profile(index);
}

int profile_active_index(void)
{
	return active_profile;
}

const char *profile_get_button_label(int button)
{
	if(button < 0 || button >= MAX_BUTTONS) return "";
	if(cfg.button_label[button][0]) return cfg.button_label[button];
	if(cfg.kbmap_count[button] <= 0) return "";
	if(cfg.kbmap_str[button]) return cfg.kbmap_str[button];
	if(kbemu_keyname) {
		const char *nm = kbemu_keyname(cfg.kbmap[button][0]);
		return nm ? nm : "";
	}
	return "";
}

const char *profile_get_name(void)
{
	if(active_profile >= 0 && active_profile < num_profiles) {
		if(profiles[active_profile].name) return profiles[active_profile].name;
	}
	return "Default";
}

int profile_set_focus(const void *owner, const char *app_id)
{
	const unsigned char *p = (const unsigned char*)app_id;
	if(!owner || !app_id || strlen(app_id) >= sizeof focus_id ||
			(focus_owner && focus_owner != owner)) return -1;
	for(; *p; p++) if(*p < 32 || *p == 127) return -1;
	focus_owner = owner;
	strcpy(focus_id, app_id);
	return profile_refresh_active();
}

int profile_clear_focus(const void *owner)
{
	if(!focus_owner || focus_owner != owner) return 0;
	focus_owner = 0;
	focus_id[0] = 0;
	return profile_refresh_active();
}

struct cfg *profile_base_config(void)
{
 if(active_profile < 0) base_cfg = cfg;
 else profiles[active_profile].pcfg = cfg;
 base_cfg.lcd_flags=cfg.lcd_flags; base_cfg.lcd_brightness=cfg.lcd_brightness;
 base_cfg.lcd_idle_seconds=cfg.lcd_idle_seconds; base_cfg.led_idle_seconds=cfg.led_idle_seconds;
 base_cfg.led=cfg.led; base_cfg.grab_device=cfg.grab_device;
 base_cfg.repeat_msec=cfg.repeat_msec;
 return &base_cfg;
}
const char *profile_focus_id(void) {
#ifdef USE_X11
 static char xclass[256];
 if(!focus_owner){xclass[0]=0;x11_get_focused_wm_class(xclass,sizeof xclass);return xclass;}
#endif
 return focus_id;
}
void profile_replace_base(struct cfg *c)
{
 cfg=*c; profile_on_cfg_reload(&cfg); profile_refresh_active();
}
