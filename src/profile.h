/* Profile management for per-application mappings */
#ifndef PROFILE_H_
#define PROFILE_H_

#include "cfgfile.h"

/* initialize/reset internal state after config reload */
void profile_on_cfg_reload(struct cfg *c);

/* Refresh active profile based on current focused window (X11 polling).
 * Returns 1 if profile changed.
 */
int profile_refresh_active(void);

/* Try to match a profile by app_id string (client-reported).
 * Returns 1 if profile changed.
 */
int profile_match_app_id(const char *app_id);

/* Manually set active profile by index. -1 returns to auto mode.
 * Returns 1 if profile changed, 0 if unchanged, -1 on invalid index.
 */
int profile_set_manual(int index);

/* Get the active profile index, or -1 if none */
int profile_active_index(void);

/* Get a user-presentable label for a given button mapping under active profile. */
const char *profile_get_button_label(int button);

/* Get the current profile name (or "Default" if none) */
const char *profile_get_name(void);

#endif
