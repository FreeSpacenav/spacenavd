#ifndef BUTTON_KEYS_H_
#define BUTTON_KEYS_H_
struct device;
struct cfg;
int button_keys_event(struct device *dev, int button, int press, const struct cfg *cfg);
void button_keys_release(struct device *dev);
#endif
