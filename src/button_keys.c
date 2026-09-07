/* Capture the binding on press, so a profile switch cannot leave keys held. */
#include "button_keys.h"
#include "cfgfile.h"
#include "dev.h"
#include "kbemu.h"
#include <string.h>
static struct {
    unsigned int key;
    int refs;
} held[4096];
static void key_event(unsigned int key, int press)
{
    int i, empty = -1;
    for (i = 0; i < 4096; i++) {
        if (held[i].refs && held[i].key == key) {
            if (press)
                held[i].refs++;
            else if (!--held[i].refs)
                kbemu_send_key(key, 0);
            return;
        }
        if (!held[i].refs && empty < 0)
            empty = i;
    }
    if (press && empty >= 0) {
        held[empty].key = key;
        held[empty].refs = 1;
        kbemu_send_key(key, 1);
    }
}
int button_keys_event(struct device *dev, int button, int press, const struct cfg *c)
{
    int i, n;
    if (button < 0 || button >= 64)
        return 0;
    n = dev->held_key_count[button];
    if (!press && n) {
        for (i = n - 1; i >= 0; i--)
            key_event(dev->held_keys[button][i], 0);
        dev->held_key_count[button] = 0;
        return 1;
    }
    if (n)
        return 1;
    if (!press || c->bnact[button] || (n = c->kbmap_count[button]) <= 0)
        return 0;
    if (n > 8)
        return 0;
    memcpy(dev->held_keys[button], c->kbmap[button], n * sizeof(unsigned int));
    dev->held_key_count[button] = n;
    for (i = 0; i < n; i++)
        key_event(dev->held_keys[button][i], 1);
    return 1;
}
void button_keys_release(struct device *dev)
{
    int i, j;
    for (i = 0; i < 64; i++) {
        for (j = dev->held_key_count[i] - 1; j >= 0; j--)
            key_event(dev->held_keys[i][j], 0);
        dev->held_key_count[i] = 0;
    }
}
