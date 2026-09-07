#ifndef LCD_H_
#define LCD_H_

/* Update SpaceMouse Enterprise LCD with current profile name and mapped keys.
 * No-op if device is not present or libraries are unavailable.
 */
void lcd_update_mappings(void);
int lcd_refresh(void);
int lcd_supported(void);
int lcd_is_asleep(void);
void lcd_idle_reset(void);
void lcd_idle_activity(void);
void lcd_idle_poll(void);
int lcd_hid_set_brightness(int bus, int address, int level);

#endif
