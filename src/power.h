#ifndef _POWER_H_
#define _POWER_H_


int init_power();
void toggle_boost();
void set_boost(bool enable);
void report_power(uint8_t *buf);

#endif /* _POWER_H_ */

