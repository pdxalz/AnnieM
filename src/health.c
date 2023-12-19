#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <date_time.h>
#include <zephyr/net/mqtt.h>
#include <stdio.h>

#include "leds.h"
#include "health.h"
#include "adc.h"
#include "mqtt_connection.h"
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(health, LOG_LEVEL_INF);

extern uint8_t wmsg[200];
extern uint8_t topic[80];
extern uint8_t buf[100];
static struct mqtt_client *_pclient;


#define NUM_PWR 12

int n_pwr = NUM_PWR - 1;
uint16_t volts[NUM_PWR];
uint16_t temperature[NUM_PWR];
static uint16_t current_volts;


#define BATVOLT_R1 4.7f
#define BATVOLT_R2 10.0f

static int get_battery_voltage()
{
	uint16_t volts;
	int corrected;

	get_adc_voltage(ADC_BATTERY_VOLTAGE_ID, &volts);
	corrected = (volts * ((BATVOLT_R1 + BATVOLT_R2) / BATVOLT_R2));
	LOG_DBG("battery %d  %d\n", volts, corrected);
	return corrected;
}

static int get_annie_temperature()
{
	const int T1 = 0;	 // low C temp
	const int T2 = 50;	 // high C temp
	const int V1 = 2100; // low voltage
	const int V2 = 1558; // high voltage
	int temperature;

	uint16_t volts;

	get_adc_voltage(ADC_TEMPERATURE_ID, &volts);

	temperature = T1 + (volts - V1) * (T2 - T1) / (V2 - V1); // conversion
	temperature = (temperature * 9.0 / 5.0) + 32;			 // F conversion
	return temperature;
}

static void report_power(uint8_t *buf)
{
	current_volts = get_battery_voltage();
	volts[n_pwr] = current_volts;

	temperature[n_pwr] = get_annie_temperature();
	buf += sprintf(buf, "{\"pwr\":[");

	for (int i = n_pwr; i < NUM_PWR + n_pwr; ++i)
	{
		buf += sprintf(buf, "[%d, %d],", volts[i % NUM_PWR], temperature[i % NUM_PWR]);
	}
	--buf; // remove the last comma
	sprintf(buf, "]}");

	n_pwr = (n_pwr - 1 + NUM_PWR) % NUM_PWR;
}



void publish_health_data()
{
	int err;

	report_power(wmsg);
	sprintf(topic, "%s/health", CONFIG_MQTT_PRIMARY_TOPIC);

	err = data_publish(_pclient, MQTT_QOS_1_AT_LEAST_ONCE,
					   wmsg, strlen(wmsg), topic, 1);
	if (err)
	{
		LOG_WRN("Failed to send pwr message, %d\n", err);
		return;
	}
}

void init_health(struct mqtt_client *c)
{
	_pclient = c;
}
