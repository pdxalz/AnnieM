#ifndef _HEALTH_H_
#define _HEALTH_H_



void publish_health_data();

void init_health(struct mqtt_client *c);


#endif /* _HEALTH_H_ */
