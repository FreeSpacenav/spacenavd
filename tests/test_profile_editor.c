#include "../src/cfgfile.c"
#include "../src/profile.c"
#include "../src/profile_edit.c"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef USE_X11
int x11_get_focused_wm_class(char *b, int n)
{
    if (n)
        b[0] = 0;
    return -1;
}
#endif
void logmsg(int p, const char *fmt, ...)
{
    (void)p;
    (void)fmt;
}
static unsigned int sym(const char *s)
{
    return !strcmp(s, "Control_L") ? 0xffe3 : !strcmp(s, "s") ? 0x73 : 0;
}
static const char *name(unsigned int k) { return k == 0xffe3 ? "Control_L" : k == 0x73 ? "s" : 0; }
unsigned int (*kbemu_keysym)(const char *) = sym;
const char *(*kbemu_keyname)(unsigned int) = name;
int main(int argc, char **argv)
{
    struct spnav_profile_set *s = calloc(1, sizeof *s), *check = calloc(1, sizeof *s);
    unsigned int rev;
    assert(argc == 2 && s && check);
    default_cfg(&cfg);
    profile_on_cfg_reload(&cfg);
    assert(profile_edit_get(s) == 0 && s->count == 1);
    rev = s->revision;
    s->count = 2;
    s->profiles[1] = s->profiles[0];
    strcpy(s->profiles[1].name, "Blender");
    strcpy(s->profiles[1].match, "blender.desktop");
    s->profiles[1].overrides = 0;
    memset(s->profiles[1].axis_override, 0, sizeof s->profiles[1].axis_override);
    memset(s->profiles[1].button_override, 0, sizeof s->profiles[1].button_override);
    s->profiles[1].button_override[0] = 1;
    s->profiles[1].buttons[0].count = 2;
    s->profiles[1].buttons[0].keys[0] = 0xffe3;
    s->profiles[1].buttons[0].keys[1] = 0x73;
    strcpy(s->profiles[1].buttons[0].label, "Save project");
    assert(profile_edit_apply(s) == 0);
    assert(profile_edit_apply(s) == -2); /* stale snapshot */
    assert(profile_set_focus(s, "blender.desktop") >= 0);
    assert(!strcmp(profile_get_button_label(0), "Save project"));
    assert(profile_edit_get(s) == 0);
    s->profiles[0].sensitivity = 2500;
    assert(profile_edit_apply(s) == 0 && cfg.sensitivity == 2.5f);
    assert(profile_edit_get(s) == 0);
    rev = s->revision;
    s->profiles[1].buttons[0].count = 9;
    assert(profile_edit_apply(s) == -1);
    assert(profile_edit_get(check) == 0 && check->revision == rev);
    assert(write_cfg(argv[1], profile_base_config()) == 0);
    profile_clear_focus(s);
    assert(read_cfg(argv[1], &cfg) == 0);
    profile_on_cfg_reload(&cfg);
    assert(profile_edit_get(check) == 0 && check->count == 2);
    assert(!check->profiles[1].axis_override[0]);
    assert(check->profiles[1].buttons[0].count == 2);
    assert(!strcmp(check->profiles[1].buttons[0].label, "Save project"));
    assert(profile_edit_capture(s,1)==0 && profile_edit_capturing());
    assert(profile_edit_capture(check,1)==-1);
    assert(profile_edit_capture(check,0)==-1);
    capture_until.tv_sec=0;
    assert(!profile_edit_capturing());
    assert(profile_edit_capture(check,1)==0);
    assert(profile_edit_capture(check,0)==0 && !profile_edit_capturing());
    free(s);
    free(check);
    puts("Profile editor validation, conflict, inheritance and persistence passed");
    return 0;
}
