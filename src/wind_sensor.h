#ifndef _WIND_SENSOR_H_
#define _WIND_SENSOR_H_

void begin_wind_sample();
int init_wind_sensor(struct mqtt_client * c);
int get_windspeed();

#endif /* _WIND_SENSOR_H_ */

