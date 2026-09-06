#ifndef LED_IDLE_H_
#define LED_IDLE_H_
struct device;
void led_idle_reset(void);
void led_idle_activity(struct device *dev);
void led_idle_poll(void);
#endif
