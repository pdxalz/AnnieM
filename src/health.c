#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <date_time.h>
#include <zephyr/net/mqtt.h>
#include <stdio.h>

#include "leds.h"
#include "health.h"
#include "mqtt_connection.h"
#include "adc.h"

extern uint8_t wmsg[200];
extern uint8_t topic[80];
extern uint8_t buf[100];
static struct mqtt_client *_pclient;

static void health_timer_callback(struct k_timer *work);
static void health_worker_callback(struct k_work *timer_id);

static K_TIMER_DEFINE(health_timer, health_timer_callback, NULL);
static K_WORK_DEFINE(health_worker, health_worker_callback);

#define NUM_PWR 12

int n_pwr = NUM_PWR - 1;
uint16_t volts[NUM_PWR];
uint16_t temperature[NUM_PWR];
static uint16_t current_volts;

static void health_timer_callback(struct k_timer *work)
{
	int rc;
	time_t temp;
	struct tm *timeptr;

	temp = time(NULL);
	timeptr = localtime(&temp);
	rc = strftime(buf, sizeof(buf), "Today is %A, %b %d.\nTime:  %r", timeptr);
	//	printk("health timer: %s  chars: %d\n", buf, rc);

	turn_leds_on_with_color(CYAN);
	k_work_submit(&health_worker);
}

void report_power(uint8_t *buf)
{
//	get_adc_voltage(ADC_BATTERY_VOLTAGE_ID, &volts[n_pwr]);
	current_volts = get_battery_voltage();
	volts[n_pwr] = current_volts;

	// if (get_temperature(&temp))
	// {

	// 	LOG_ERR("temperature failed");
	// 	return;
	// }
	temperature[n_pwr] = get_temperature();
	buf += sprintf(buf, "{\"pwr\":[");

	for (int i = n_pwr; i < NUM_PWR + n_pwr; ++i)
	{
		buf += sprintf(buf, "[%d, %d],", volts[i % NUM_PWR], temperature[i % NUM_PWR]);
	}
	--buf; // remove the last comma
	sprintf(buf, "]}");

	n_pwr = (n_pwr - 1 + NUM_PWR) % NUM_PWR;
}

static void health_worker_callback(struct k_work *timer_id)
{
	int err;
	time_t now;
	now = time(NULL);
	struct tm tm;
	localtime_r(&now, &tm);

	int hour = tm.tm_hour;
	int minute = tm.tm_min;

	//	printk("health worker\n");

	report_power(wmsg);
	sprintf(topic, "%s/health", CONFIG_MQTT_PRIMARY_TOPIC);

	err = data_publish(_pclient, MQTT_QOS_1_AT_LEAST_ONCE,
					   wmsg, strlen(wmsg), topic, 1);
	if (err)
	{
		printk("Failed to send pwr message, %d\n", err);
		return;
	}
}

// function to convert F to C
int FahrenheitToCelsius(float FahrenTemp)
{
	return (FahrenTemp - 32) * 5 / 9;
}


#define BATVOLT_R1 4.7f
#define BATVOLT_R2 10.0f

int get_battery_voltage()
{
	uint16_t volts;
	int corrected;

	get_adc_voltage(ADC_BATTERY_VOLTAGE_ID, &volts);
	corrected = (volts * ((BATVOLT_R1 + BATVOLT_R2) / BATVOLT_R2));
	printk("battery %d  %d\n",  volts, corrected);
	return corrected;
}

int get_temperature()
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
	printk("temperature=%d volts=%d\n", temperature, volts);
	return temperature;
}

void init_health(struct mqtt_client *c)
{
	//	printk("health init\n");
	_pclient = c;
	k_timer_start(&health_timer, K_SECONDS(60), K_MINUTES(60));
}
