#include "../src/button_keys.c"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned int out[32];
static int presses[32], used;
static void send(unsigned int k, int p)
{
    out[used] = k;
    presses[used++] = p;
}
void (*kbemu_send_key)(unsigned int, int) = send;
int main(void)
{
    struct device a = {0}, b = {0};
    struct cfg c = {0};
    c.kbmap_count[0] = 2;
    c.kbmap[0][0] = 0xffe3;
    c.kbmap[0][1] = 's';
    assert(button_keys_event(&a, 0, 1, &c) == 1 && used == 2);
    c.kbmap[0][1] = 'z'; /* focus switches while the old shortcut is held */
    assert(button_keys_event(&a, 0, 0, &c) == 1 && used == 4 && out[2] == 's' && !presses[2] &&
           out[3] == 0xffe3);
    used = 0;
    c.kbmap_count[0] = 1;
    button_keys_event(&a, 0, 1, &c);
    button_keys_event(&b, 0, 1, &c);
    assert(used == 1);
    button_keys_release(&a);
    assert(used == 1);
    button_keys_release(&b);
    assert(used == 2 && !presses[1]);
    puts("Held shortcuts survive focus changes and release on disconnect without dropping shared "
         "modifiers");
}
