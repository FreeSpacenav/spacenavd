/* SpaceMouse Enterprise idle backlight policy. GPLv3-or-later. */
#include <time.h>
#include "cfgfile.h"
#include "lcd.h"

extern struct cfg cfg;
static struct timespec last_activity;
static int initialized, asleep;

int lcd_is_asleep(void)
{
	return asleep;
}

void lcd_idle_reset(void)
{
	initialized = clock_gettime(CLOCK_MONOTONIC, &last_activity) == 0;
	asleep = 0;
}

void lcd_idle_activity(void)
{
	int wake = asleep && (cfg.lcd_flags & LCD_ENABLED);
	lcd_idle_reset();
	if(wake) lcd_refresh();
}

void lcd_idle_poll(void)
{
	struct timespec now;
	time_t elapsed;
	if(!initialized) lcd_idle_reset();
	if(!initialized || asleep || !(cfg.lcd_flags & LCD_ENABLED) || cfg.lcd_idle_seconds <= 0) return;
	if(clock_gettime(CLOCK_MONOTONIC, &now) < 0) return;
	elapsed = now.tv_sec - last_activity.tv_sec;
	if(now.tv_nsec < last_activity.tv_nsec) elapsed--;
	if(elapsed >= cfg.lcd_idle_seconds) {
		/* Do not retry every poll when the device is absent. Input or an
		 * explicit settings change will try again. Configuration stays on. */
		asleep = 1;
		lcd_refresh();
	}
}
