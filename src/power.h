#ifndef _POWER_H_
#define _POWER_H_


int init_power();
void toggle_boost();
void set_boost(bool enable);
uint16_t get_volts();
void report_power(uint8_t *buf);
int az_set_led_on(uint8_t led_idx);
#endif /* _POWER_H_ */

