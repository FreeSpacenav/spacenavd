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
	return strstr(buf1, buf2) != NULL;
}

static int activate_profile(int new_index)
{
	if(new_index != active_profile) {
		if(new_index >= 0) {
			logmsg(LOG_INFO, "Profile switch: %s\n", profiles[new_index].name ? profiles[new_index].name : "(unnamed)");
			cfg = profiles[new_index].pcfg;
		} else {
			logmsg(LOG_INFO, "Profile switch: Default\n");
			cfg = base_cfg;
		}
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

#ifdef USE_X11
	{
		char cls[256] = {0};
		x11_get_focused_wm_class(cls, sizeof cls);
		return activate_profile(find_profile(cls));
	}
#else
	return 0;
#endif
}

int profile_match_app_id(const char *app_id)
{
	if(manual_override) return 0;
	return activate_profile(find_profile(app_id));
}

int profile_set_manual(int index)
{
	if(index < -1 || index >= num_profiles) return -1;

	if(index == -1) {
		/* return to auto mode */
		manual_override = 0;
		logmsg(LOG_INFO, "Profile mode: auto\n");
		return 0;
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
	if(cfg.kbmap_count[button] <= 0) return "";
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
