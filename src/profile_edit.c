/* Transactional profile editing: validate and allocate everything before replacing live state. */
#include "profile_edit.h"
#include "kbemu.h"
#include "profile.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#ifdef __linux__
unsigned int keysym_to_linux_keycode(unsigned int key);
#endif
static unsigned int edit_revision;
void profile_edit_touch(void)
{
    if (!edit_revision) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        edit_revision =
            (unsigned int)ts.tv_nsec ^ (unsigned int)ts.tv_sec ^ ((unsigned int)getpid() << 16);
    }
    if (++edit_revision == 0)
        ++edit_revision;
}
static void export_cfg(struct spnav_profile *p, const struct cfg *c)
{
    int i;
    p->sensitivity = (int)(c->sensitivity * 1000);
    p->swapyz = c->swapyz;
    for (i = 0; i < 64; i++) {
        p->axes[i].sensitivity = i < 3   ? (int)(c->sens_trans[i] * 1000)
                                 : i < 6 ? (int)(c->sens_rot[i - 3] * 1000)
                                         : 1000;
        p->axes[i].deadzone = c->dead_threshold[i];
        p->axes[i].invert = c->invert[i];
        p->axes[i].map = c->map_axis[i];
        p->buttons[i].map = c->map_button[i];
        p->buttons[i].action = c->bnact[i];
        p->buttons[i].count = c->kbmap_count[i];
        memcpy(p->buttons[i].keys, c->kbmap[i], sizeof p->buttons[i].keys);
        memcpy(p->buttons[i].label, c->button_label[i], 64);
    }
}
int profile_edit_get(struct spnav_profile_set *s)
{
    int i;
    struct cfg *base = profile_base_config();
    if (!edit_revision)
        profile_edit_touch();
    memset(s, 0, sizeof *s);
    s->version = 1;
    s->revision = edit_revision;
    s->count = num_profiles + 1;
    strcpy(s->profiles[0].name, "Default");
    export_cfg(&s->profiles[0], base);
    for (i = 0; i < num_profiles; i++) {
        struct spnav_profile *p = &s->profiles[i + 1];
        p->source_index = i + 1;
        snprintf(p->name, sizeof p->name, "%s", profiles[i].name);
        snprintf(p->match, sizeof p->match, "%s", profiles[i].match_class);
        p->overrides = profiles[i].edit_overrides;
        memcpy(p->axis_override, profiles[i].edit_axes, sizeof p->axis_override);
        memcpy(p->button_override, profiles[i].edit_buttons, sizeof p->button_override);
        export_cfg(p, &profiles[i].pcfg);
    }
    return 0;
}
static int valid_text(const char *s, int size, int quotes)
{
    int i;
    for (i = 0; i < size; i++) {
        unsigned char c = s[i];
        if (!c)
            return 1;
        if (c < 32 || c == 127 || (quotes && c == '"'))
            return 0;
    }
    return 0;
}
static void release_cfg(struct cfg *c)
{
    int i;
    for (i = 0; i < 64; i++)
        free(c->kbmap_str[i]);
    for (i = 0; i < MAX_CUSTOM; i++)
        free(c->devname[i]);
}
static int copy_cfg(struct cfg *dst, const struct cfg *src)
{
    int i;
    *dst = *src;
    memset(dst->kbmap_str, 0, sizeof dst->kbmap_str);
    memset(dst->devname, 0, sizeof dst->devname);
    for (i = 0; i < 64; i++)
        if (src->kbmap_str[i] && !(dst->kbmap_str[i] = strdup(src->kbmap_str[i])))
            goto fail;
    for (i = 0; i < MAX_CUSTOM; i++)
        if (src->devname[i] && !(dst->devname[i] = strdup(src->devname[i])))
            goto fail;
    return 0;
fail:
    release_cfg(dst);
    memset(dst, 0, sizeof *dst);
    return -1;
}
static void import_cfg(struct cfg *c, const struct spnav_profile *p, int base)
{
    int i;
    if (base || (p->overrides & 1))
        c->sensitivity = p->sensitivity / 1000.0f;
    if (base || (p->overrides & 2))
        c->swapyz = p->swapyz;
    for (i = 0; i < 64; i++) {
        if (base || p->axis_override[i]) {
            if (i < 3)
                c->sens_trans[i] = p->axes[i].sensitivity / 1000.0f;
            else if (i < 6)
                c->sens_rot[i - 3] = p->axes[i].sensitivity / 1000.0f;
            c->dead_threshold[i] = p->axes[i].deadzone;
            c->invert[i] = p->axes[i].invert;
            c->map_axis[i] = p->axes[i].map;
        }
        if (base || p->button_override[i]) {
            c->map_button[i] = p->buttons[i].map;
            c->bnact[i] = p->buttons[i].action;
            c->kbmap_count[i] = p->buttons[i].count;
            memcpy(c->kbmap[i], p->buttons[i].keys, sizeof c->kbmap[i]);
            memcpy(c->button_label[i], p->buttons[i].label, 64);
            free(c->kbmap_str[i]);
            c->kbmap_str[i] = 0; /* numeric persistence preserves all keysyms */
        }
    }
}
int profile_edit_apply(const struct spnav_profile_set *s)
{
    int i, j, k, n = 0;
    struct cfg newbase;
    struct profile *next;
    struct cfg *old;
    if (s->version != 1 || s->count < 1 || s->count > 17 ||
        strncmp(s->profiles[0].name, "Default", 64) || s->profiles[0].match[0])
        return -1;
    if (s->revision != edit_revision)
        return -2;
    for (i = 0; i < s->count; i++) {
        const struct spnav_profile *p = &s->profiles[i];
        if (p->source_index < 0 || p->source_index > num_profiles || !valid_text(p->name, 64, 1) ||
            !p->name[0] || !valid_text(p->match, 256, 0) || (i && !p->match[0]) ||
            p->sensitivity < 0 || p->sensitivity > 100000 || p->swapyz < 0 || p->swapyz > 1 ||
            p->overrides < 0 || p->overrides > 3)
            return -1;
        for (j = 0; j < i; j++)
            if (!strcmp(p->name, s->profiles[j].name))
                return -1;
        for (j = 0; j < 64; j++) {
            const struct spnav_profile_button *b = &p->buttons[j];
            const struct spnav_profile_axis *a = &p->axes[j];
            if (p->axis_override[j] < 0 || p->axis_override[j] > 1 || p->button_override[j] < 0 ||
                p->button_override[j] > 1 || a->sensitivity < 0 || a->sensitivity > 100000 ||
                a->deadzone < 0 || a->deadzone > 32767 || a->invert < 0 || a->invert > 1 ||
                a->map < 0 || a->map >= 64 || b->map < 0 || b->map >= 64 || b->action < 0 ||
                b->action >= MAX_BNACT || b->count < 0 || b->count > 8 ||
                !valid_text(b->label, 64, 0))
                return -1;
            for (k = 0; k < b->count; k++) {
#ifdef __linux__
                if(!keysym_to_linux_keycode(b->keys[k])) return -3;
#else
                if (!b->keys[k] || !kbemu_keyname || !kbemu_keyname(b->keys[k])) return -3;
#endif
            }
        }
    }
    next = calloc(MAX_PROFILES, sizeof *next);
    if (!next)
        return -1;
    old = profile_base_config();
    if (copy_cfg(&newbase, old) < 0) {
        free(next);
        return -1;
    }
    import_cfg(&newbase, &s->profiles[0], 1);
    for (i = 1; i < s->count; i++) {
        struct profile *p = &next[i - 1];
        const struct spnav_profile *q = &s->profiles[i];
        n = i;
        p->name = strdup(q->name);
        p->match_class = strdup(q->match);
        if (copy_cfg(&p->pcfg, q->source_index ? &profiles[q->source_index - 1].pcfg : &newbase) <
                0 ||
            !p->name || !p->match_class)
            goto fail;
        import_cfg(&p->pcfg, &s->profiles[0], 1);
        p->edit_overrides = q->overrides;
        memcpy(p->edit_axes, q->axis_override, sizeof p->edit_axes);
        memcpy(p->edit_buttons, q->button_override, sizeof p->edit_buttons);
        import_cfg(&p->pcfg, q, 0);
    }
    release_cfg(old);
    for (i = 0; i < num_profiles; i++) {
        free(profiles[i].name);
        free(profiles[i].match_class);
        release_cfg(&profiles[i].pcfg);
    }
    memcpy(profiles, next, MAX_PROFILES * sizeof *next);
    num_profiles = s->count - 1;
    free(next);
    profile_replace_base(&newbase);
    profile_edit_touch();
    return 0;
fail:
    for (i = 0; i < n; i++) {
        free(next[i].name);
        free(next[i].match_class);
        release_cfg(&next[i].pcfg);
    }
    release_cfg(&newbase);
    free(next);
    return -1;
}

static const void *capture_owner;
static struct timespec capture_until;
int profile_edit_capturing(void)
{
    struct timespec now;
    if (!capture_owner || clock_gettime(CLOCK_MONOTONIC, &now) < 0)
        return 0;
    if (now.tv_sec > capture_until.tv_sec ||
        (now.tv_sec == capture_until.tv_sec && now.tv_nsec >= capture_until.tv_nsec))
        capture_owner = 0;
    return capture_owner != 0;
}
int profile_edit_capture(const void *owner, int enable)
{
    if (!owner || (profile_edit_capturing() && capture_owner != owner))
        return -1;
    if (!enable) {
        capture_owner = 0;
        return 0;
    }
    if (clock_gettime(CLOCK_MONOTONIC, &capture_until) < 0)
        return -1;
    capture_until.tv_sec += 10;
    capture_owner = owner;
    return 0;
}
