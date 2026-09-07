/* Per-device LED idle policy. SPDX-License-Identifier: GPL-3.0-or-later */
#include <time.h>
#include "dev.h"
#include "cfgfile.h"
#include "led_idle.h"
extern struct cfg cfg;

void led_idle_activity(struct device *dev)
{
	int asleep = dev->led_asleep;
	dev->led_timer_valid = clock_gettime(CLOCK_MONOTONIC, &dev->led_activity) == 0;
	dev->led_asleep = 0;
	if(asleep && dev->set_led) dev->set_led(dev, dev->led_requested);
}

void led_idle_reset(void)
{
	struct device *dev;
	for(dev = get_devices(); dev; dev = dev->next) {
		led_idle_activity(dev);
		if(dev->set_led) dev->set_led(dev, dev->led_requested);
	}
}

void led_idle_poll(void)
{
	struct device *dev;
	struct timespec now;
	if(cfg.led_idle_seconds <= 0 || clock_gettime(CLOCK_MONOTONIC, &now) < 0) return;
	for(dev = get_devices(); dev; dev = dev->next) {
		time_t elapsed;
		if(!dev->led_timer_valid) led_idle_activity(dev);
		if(!dev->led_timer_valid || dev->led_asleep || !dev->led_requested || !dev->set_led) continue;
		elapsed = now.tv_sec - dev->led_activity.tv_sec;
		if(now.tv_nsec < dev->led_activity.tv_nsec) elapsed--;
		if(elapsed >= cfg.led_idle_seconds) {
			dev->led_asleep = 1;
			dev->set_led(dev, 0);
		}
	}
}
