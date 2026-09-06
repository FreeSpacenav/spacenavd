#include <assert.h>
#include <stdio.h>
#include <time.h>
#include "cfgfile.h"
#include "lcd.h"
static time_t now;
static int updates;
struct cfg cfg;
static int fake_clock(clockid_t id, struct timespec *ts)
{ assert(id == CLOCK_MONOTONIC); ts->tv_sec = now; ts->tv_nsec = 0; return 0; }
int lcd_refresh(void) { updates++; return 0; }
#define clock_gettime fake_clock
#include "../src/lcd_idle.c"
int main(void)
{
 cfg.lcd_flags = LCD_ENABLED | LCD_PROFILE; cfg.lcd_idle_seconds = 60;
 lcd_idle_reset();
 now = 59; lcd_idle_poll(); assert(!lcd_is_asleep() && updates == 0);
 now = 60; lcd_idle_poll(); assert(lcd_is_asleep() && updates == 1);
 now = 90; lcd_idle_poll(); assert(updates == 1);
 lcd_idle_activity(); assert(!lcd_is_asleep() && updates == 2);
 now = 149; lcd_idle_poll(); assert(!lcd_is_asleep());
 now = 150; lcd_idle_poll(); assert(lcd_is_asleep() && updates == 3);
 cfg.lcd_flags = LCD_PROFILE; lcd_idle_reset();
 lcd_idle_activity(); now = 1000; lcd_idle_poll(); assert(!lcd_is_asleep() && updates == 3);
 cfg.lcd_flags |= LCD_ENABLED; cfg.lcd_idle_seconds = 0; lcd_idle_reset();
 now = 2000; lcd_idle_poll(); assert(!lcd_is_asleep() && updates == 3);
 cfg.lcd_idle_seconds = 30; lcd_idle_reset();
 now = 2020; lcd_idle_activity(); now = 2030; lcd_idle_poll(); assert(!lcd_is_asleep());
 now = 2050; lcd_idle_poll(); assert(lcd_is_asleep());
 puts("LCD idle timer tests passed");
 return 0;
}
