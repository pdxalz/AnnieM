#include <zephyr/net/socket.h>

#ifndef _MQTTCONNECTION_H_
#define _MQTTCONNECTION_H_
#define LED_CONTROL_OVER_MQTT DK_LED1 /*The LED to control over MQTT*/
#define IMEI_LEN 15
#define CGSN_RESPONSE_LENGTH (IMEI_LEN + 6 + 1) /* Add 6 for \r\nOK\r\n and 1 for \0 */
#define CLIENT_ID_LEN sizeof("nrf-") + IMEI_LEN

uint8_t * get_mqtt_message_buf();
uint8_t * get_mqtt_topic_buf();


/**@brief Initialize the MQTT client structure
 */
int client_init();

/**@brief Initialize the file descriptor structure used by poll.
 */
int fds_init(struct mqtt_client *c, struct pollfd *fds);

void mqtt_idleloop();

/**@brief Function to publish data on the configured topic
 */
int data_publish(enum mqtt_qos qos,
				 uint8_t *data, size_t len, uint8_t *topic, uint8_t retain);

int get_sample_time();
bool sleepy_mode();

#endif /* _CONNECTION_H_ */
