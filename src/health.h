#ifndef _HEALTH_H_
#define _HEALTH_H_


int get_battery_voltage();


void init_health(struct mqtt_client *c);


#endif /* _HEALTH_H_ */
