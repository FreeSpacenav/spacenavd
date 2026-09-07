#ifndef PROFILE_EDIT_H_
#define PROFILE_EDIT_H_
#include "spnav_profiles.h"
int profile_edit_get(struct spnav_profile_set *s);
int profile_edit_apply(const struct spnav_profile_set *s);
void profile_edit_touch(void);
int profile_edit_capture(const void *owner, int enable);
int profile_edit_capturing(void);
#endif
