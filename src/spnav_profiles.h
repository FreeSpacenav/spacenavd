/* Version 1 local profile snapshot. Integer-only layout; values in thousandths. */
#ifndef SPNAV_PROFILES_H_
#define SPNAV_PROFILES_H_
#define SPNAV_PROFILE_VERSION 1
#define SPNAV_PROFILE_MAX 17
#define SPNAV_PROFILE_AXES 64
#define SPNAV_PROFILE_BUTTONS 64
#define SPNAV_PROFILE_KEYS 8
struct spnav_profile_button {
 int map, action, count;
 unsigned int keys[8];
 char label[64];
};
struct spnav_profile_axis { int sensitivity, deadzone, invert, map; };
struct spnav_profile {
 char name[64], match[256];
 int source_index; /* original snapshot index; preserves legacy options on rename/duplicate */
 int sensitivity, swapyz, overrides; /* bits: sensitivity=1, swap=2 */
 int axis_override[64], button_override[64];
 struct spnav_profile_axis axes[64];
 struct spnav_profile_button buttons[64];
};
struct spnav_profile_set {
 unsigned int version, revision;
 int count; /* includes Default at index zero */
 struct spnav_profile profiles[17];
};
#ifdef __cplusplus
extern "C" {
#endif
/* 0 success; -1 invalid/unavailable; -2 stale revision; -3 unsupported key. Read before editing. */
int spnav_profiles_read(struct spnav_profile_set *profiles);
int spnav_profiles_apply(struct spnav_profile_set *profiles);
int spnav_profile_capture(int enable); /* suppress mappings for 10 seconds while identifying buttons */
int spnav_profile_active(void); /* snapshot index; zero is Default */
int spnav_profile_focus(char *buf, int size);
#ifdef __cplusplus
}
#endif
#endif
