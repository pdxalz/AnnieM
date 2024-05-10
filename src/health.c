#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/sensor.h>
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

#define NUM_PWR 12

int n_pwr = NUM_PWR - 1;
uint16_t volts[NUM_PWR];
struct sensor_value temperature[NUM_PWR];
struct sensor_value pressure[NUM_PWR];
struct sensor_value humidity[NUM_PWR];

static uint16_t current_volts;

const struct device *const dev = DEVICE_DT_GET_ONE(bosch_bme680);

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

void convert_to_farhenheit(struct sensor_value *temp)
{
	long tmp = (temp->val1 * 1000 + temp->val2 / 1000) * 9 / 5 + 32000;
	temp->val1 = tmp / 1000;
	temp->val2 = (tmp % 1000 + 50) / 100;
}


static void report_power(uint8_t *buf)
{
	struct sensor_value gas_res;
	current_volts = get_battery_voltage();
	volts[n_pwr] = current_volts;

	sensor_sample_fetch(dev);
	sensor_channel_get(dev, SENSOR_CHAN_AMBIENT_TEMP, &temperature[n_pwr]);
	convert_to_farhenheit(&temperature[n_pwr]);
	sensor_channel_get(dev, SENSOR_CHAN_PRESS, &pressure[n_pwr]);
	pressure[n_pwr].val2 = pressure[n_pwr].val2 / 10000;
	sensor_channel_get(dev, SENSOR_CHAN_HUMIDITY, &humidity[n_pwr]);
	sensor_channel_get(dev, SENSOR_CHAN_GAS_RES, &gas_res);

	buf += sprintf(buf, "{\"pwr\":[");

	for (int i = n_pwr; i < NUM_PWR + n_pwr; ++i)
	{
		buf += sprintf(buf, "[%d, %d.%d],",
					   volts[i % NUM_PWR],
					   temperature[i % NUM_PWR].val1, temperature[i % NUM_PWR].val2);
		// buf += sprintf(buf, "[%d, %d.%d, %d.%02d, %d],",
		// 			   volts[i % NUM_PWR],
		// 			   temperature[i % NUM_PWR].val1, temperature[i % NUM_PWR].val2,
		// 			   pressure[i % NUM_PWR].val1, pressure[i % NUM_PWR].val2, 
		// 			   humidity[i % NUM_PWR].val1 );
	}
	--buf; // remove the last comma
	sprintf(buf, "]}");
	printk("power report: %s\n", buf);
	n_pwr = (n_pwr - 1 + NUM_PWR) % NUM_PWR;
}

void publish_health_data()
{
	int err;

	uint8_t *msgbuf = get_mqtt_message_buf();
	uint8_t *topicbuf = get_mqtt_topic_buf();

	report_power(msgbuf);
	sprintf(topicbuf, "%s/health", CONFIG_MQTT_PRIMARY_TOPIC);

	time_t now;
	now = time(NULL);
	struct tm tm;
	gmtime_r(&now, &tm);

	if (tm.tm_hour == 20)
	{  // send health data at 6 am or 8 pm
		err = data_publish(MQTT_QOS_1_AT_LEAST_ONCE,
						msgbuf, strlen(msgbuf), topicbuf, 1);
		if (err)
		{
			LOG_WRN("Failed to send pwr message, %d\n", err);
			return;
		}
		update_led_mode();  // turn off the led
	}

}

static void publish_volts()
{
	uint8_t msgbuf[256];
	uint8_t * buf;
	char * topic = CONFIG_MQTT_PRIMARY_TOPIC"/volt";

	buf = msgbuf;
	buf += sprintf(buf, "{\"volt\":[");

	for (int i = n_pwr+1; i < NUM_PWR + n_pwr+1; ++i)
	{
		buf += sprintf(buf, "%d,", volts[i % NUM_PWR]);
	}
	--buf; // remove the last comma
	sprintf(buf, "]}");

	data_publish(MQTT_QOS_1_AT_LEAST_ONCE, msgbuf, strlen(msgbuf), topic, 0);
}

static void publish_temperature()
{
	uint8_t msgbuf[256];
	uint8_t * buf;
	char * topic = CONFIG_MQTT_PRIMARY_TOPIC"/temp";

	buf = msgbuf;
	buf += sprintf(buf, "{\"temp\":[");

	for (int i = n_pwr+1; i < NUM_PWR + n_pwr+1; ++i)
	{
		buf += sprintf(buf, "%d.%d,", temperature[i % NUM_PWR].val1, temperature[i % NUM_PWR].val2);
	}
	--buf; // remove the last comma
	sprintf(buf, "]}");

	data_publish(MQTT_QOS_1_AT_LEAST_ONCE, msgbuf, strlen(msgbuf), topic, 0);
}

static void publish_pressure()
{
	uint8_t msgbuf[256];
	uint8_t * buf;
	char * topic = CONFIG_MQTT_PRIMARY_TOPIC"/pres";

	buf = msgbuf;
	buf += sprintf(buf, "{\"pres\":[");

	for (int i = n_pwr+1; i < NUM_PWR + n_pwr+1; ++i)
	{
		buf += sprintf(buf, "%d.%02d,", pressure[i % NUM_PWR].val1, pressure[i % NUM_PWR].val2);
	}
	--buf; // remove the last comma
	sprintf(buf, "]}");

	data_publish(MQTT_QOS_1_AT_LEAST_ONCE, msgbuf, strlen(msgbuf), topic, 0);
}

static void publish_humidity()
{
	uint8_t msgbuf[256];
	uint8_t * buf;
	char * topic = CONFIG_MQTT_PRIMARY_TOPIC"/humd";

	buf = msgbuf;
	buf += sprintf(buf, "{\"humd\":[");

	for (int i = n_pwr+1; i < NUM_PWR + n_pwr+1; ++i)
	{
		buf += sprintf(buf, "%d,", humidity[i % NUM_PWR].val1);
	}
	--buf; // remove the last comma
	sprintf(buf, "]}");

	data_publish(MQTT_QOS_1_AT_LEAST_ONCE, msgbuf, strlen(msgbuf), topic, 0);
}


void report_status_info(int item)
{
	switch (item)
	{
	case 0:
		publish_volts();
		break;
	case 1:
		publish_temperature();
		break;
	case 2:
		publish_pressure();
		break;
	case 3:
		publish_humidity();
		break;
	default:
		break;
	}
}


void init_health()
{
	// struct sensor_value x;

	// testing conversion
	// x.val1 = 0;
	// x.val2 = 0;
	// convert_to_farhenheit(&x);
	// printk("0C = %d.%dF\n", x.val1, x.val2);

	// x.val1 = 27;
	// x.val2 = 230111;
	// convert_to_farhenheit(&x);
	// printk("27.230111C = %d.%dF\n", x.val1, x.val2);

	// x.val1 = 22;
	// x.val2 = 980000;
	// convert_to_farhenheit(&x);
	// printk("22.980000C = %d.%dF\n", x.val1, x.val2);

	if (!device_is_ready(dev))
	{
		printk("BME688 sensor: device not ready.\n");
		return;
	}
}
