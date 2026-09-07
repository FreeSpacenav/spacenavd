#include <assert.h>
#include <stdio.h>
#include <time.h>
#include "dev.h"
#include "cfgfile.h"
static time_t now;
static int state, writes;
struct cfg cfg;
static struct device dev;
struct device *get_devices(void) {return &dev;}
static void set_led(struct device *d, int value) {(void)d;state=value;writes++;}
static int fake_clock(clockid_t id, struct timespec *ts) {assert(id==CLOCK_MONOTONIC);ts->tv_sec=now;ts->tv_nsec=0;return 0;}
#define clock_gettime fake_clock
#include "../src/led_idle.c"
int main(void) {
 dev.set_led=set_led;dev.led_requested=1;cfg.led_idle_seconds=10;
 led_idle_reset();now=9;led_idle_poll();assert(!dev.led_asleep);
 now=10;led_idle_poll();assert(dev.led_asleep && state==0);
 led_idle_activity(&dev);assert(!dev.led_asleep && state==1);
 now=20;led_idle_poll();assert(dev.led_asleep);
 dev.led_requested=0;led_idle_activity(&dev);assert(!dev.led_asleep && state==0);
 cfg.led_idle_seconds=0;dev.led_requested=1;led_idle_reset();
 now=1000;led_idle_poll();assert(!dev.led_asleep && state==1);
 puts("LED idle tests passed");return 0;
}
