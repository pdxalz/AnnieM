#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <dk_buttons_and_leds.h>
#include <modem/lte_lc.h>
#include <zephyr/drivers/gpio.h>
#include <date_time.h>

#include <zephyr/net/mqtt.h>

#include "mqtt_connection.h"
#include "wind_sensor.h"
#include "adc.h"
#include "health.h"
#include "leds.h"

#define WIND_SPEED_NODE DT_ALIAS(windspeed0)
static const struct gpio_dt_spec windspeed = GPIO_DT_SPEC_GET(WIND_SPEED_NODE, gpios);

#define WIND_SCALE (102.0 / 60.0)
#define MAX_DIRECTION_VOLTAGE 1630
#define NORTH_OFFSET 90 // Aim to the east so discontinuity is not at north

static volatile int frequency = 0;
static volatile int64_t lasttime = 0;
static int speed;
static int oldspeed = 0;
static bool broker_cleared = false;
static uint16_t wind_direction;

uint8_t wmsg[200];
uint8_t tmsg[80];
uint8_t topic[80];
uint8_t buf[100];

uint16_t vbuf[8];
uint16_t v_index = 0;

#define SAMPLES_PER_HOUR 12
struct w_sensor
{
	uint8_t speed;
	uint16_t direction;
} wind_sensor[12];

/* The mqtt client struct */
static struct mqtt_client *_pclient;
static struct gpio_callback windspeed_cb_data;

static void wind_check_callback(struct k_timer *work);
static void check_wind_direction(struct k_timer *work);
void frequency_counter(struct k_timer *work);
static void speed_calc_callback(struct k_work *timer_id);

static K_TIMER_DEFINE(wind_check_timer, wind_check_callback, NULL);
static K_TIMER_DEFINE(ktimeradc, check_wind_direction, NULL);
static K_TIMER_DEFINE(frequency_timer, frequency_counter, NULL);
static K_WORK_DEFINE(repeating_timer_work, speed_calc_callback);

// initiates the measuring of wind data
static void wind_check_callback(struct k_timer *work)
{
	int rc;
	time_t temp;
	struct tm *timeptr;

	temp = time(NULL);
	timeptr = localtime(&temp);
	rc = strftime(buf, sizeof(buf), "Today is %A, %b %d.\nTime:  %r", timeptr);
	//	printk("time: %s  chars: %d\n", buf, rc);

	if (sleepy_mode())
	{
		return;
	}
	turn_leds_on_with_color(GREEN);

	// set_boost(true);
	begin_wind_sample();
}

static void build_array_string(uint8_t *buf, struct tm *t)
{

	buf += sprintf(buf, "{\"time\":\"%04d-%02d-%02dT%02d:%02d:%02d.000Z\", ", (t->tm_year + 1900), t->tm_mon, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec);
	buf += sprintf(buf, "\"wind\":[");

	for (int i = 0; i < SAMPLES_PER_HOUR; ++i)
	{
		buf += sprintf(buf, "[%d, %d],", wind_sensor[i].speed, wind_sensor[i].direction);
	}
	--buf; // remove the last comma
	sprintf(buf, "]}");
}

// erases the persistant MQTT data, occurs once
static void clear_broker_history()
{
	// clear the broker data first time after power up
	if (!broker_cleared)
	{
		printk("clearing broker history\n");
		broker_cleared = true;
		for (int i = 0; i < 24; ++i)
		{
			sprintf(topic, "%s/wind/%02d", CONFIG_MQTT_PRIMARY_TOPIC, i);
			int err = data_publish(_pclient, MQTT_QOS_1_AT_LEAST_ONCE,
								   "", 0, topic, 1);
			if (err)
			{
				printk("Failed to send broker clear message, %d\n", err);
				return;
			}
		}
	}
}

// averages two angles, result is between 0 and 359
static uint16_t circ_avg(uint16_t a, uint16_t b)
{
	int16_t diff = ((a - b + 180 + 360) % 360) - 180;
	int16_t angle = (360 + b + (diff / 2)) % 360;
	//	printk("avg=      %d, %d, %d, %d", a, b, diff, angle);
	return angle;
}

static void check_wind_direction(struct k_timer *work)
{
	uint16_t voltage;

	if (get_adc_voltage(ADC_WIND_DIR_ID, &voltage) != 0)
	{
		printk("Failed to get direction voltage\n");
		return;
	};

	//	printk("direction voltage, %d\n", voltage);
	vbuf[v_index] = (((uint32_t)voltage * 360) / MAX_DIRECTION_VOLTAGE + NORTH_OFFSET) % 360;

	v_index = (v_index + 1) % 8;
	wind_direction = circ_avg(
		circ_avg(circ_avg(vbuf[0], vbuf[1]), circ_avg(vbuf[2], vbuf[3])),
		circ_avg(circ_avg(vbuf[4], vbuf[5]), circ_avg(vbuf[6], vbuf[7])));

	printk("dir volts %d  dir %d\n", voltage, wind_direction);
}

#define NUM_PWR 12

// background task that sends the MQTT sensor data
static void speed_calc_callback(struct k_work *timer_id)
{
	time_t now;
	now = time(NULL);
	struct tm tm;
	localtime_r(&now, &tm);

	clear_broker_history();

	int hour = tm.tm_hour;
	int minute = tm.tm_min;

	if (minute < 60 / SAMPLES_PER_HOUR)
	{
		// zero out hourly data
		for (int i = 0; i < SAMPLES_PER_HOUR; ++i)
		{
			wind_sensor[i].speed = 0;
			wind_sensor[i].direction = 0;
		}
	}

	// 0,0 indicates unset item.
	if (speed == 0 && wind_direction == 0)
	{
		wind_direction = 1;
	}

	wind_sensor[minute / 5].speed = speed;
	k_timer_stop(&ktimeradc);
	wind_sensor[minute / 5].direction = wind_direction;

	build_array_string(wmsg, &tm);
	sprintf(topic, "%s/wind/%02d", CONFIG_MQTT_PRIMARY_TOPIC, hour);
	// sprintf(topic, "%s/wind/00", CONFIG_MQTT_PRIMARY_TOPIC);

	// always publish data just before the next hour, or
	// publish only if the time is between 10AM and 9PM and
	// the speed changed or is higher than 2
	if ((minute / 5 == 11) ||
		(hour > 9 && hour < 21 && (speed > 2 || speed != oldspeed)))
	{

		int err;
		oldspeed = speed;
		err = data_publish(_pclient, MQTT_QOS_1_AT_LEAST_ONCE,
						   wmsg, strlen(wmsg), topic, 1);
		if (err)
		{
			printk("Failed to send message, %d\n", err);
			return;
		}

		uint16_t current_volts;

		//	get_adc_voltage(ADC_BATTERY_VOLTAGE_ID, &current_volts);
		current_volts = get_battery_voltage();

		sprintf(topic, "%s/recent", CONFIG_MQTT_PRIMARY_TOPIC);
		sprintf(wmsg, "{\"time\":\"%2d/%2d %2d:%02d\",\"sp\":\"%d\",\"dir\":\"%d\",\"mv\":\"%d\"}",
				tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, speed, wind_direction, current_volts);
		err = data_publish(_pclient, MQTT_QOS_1_AT_LEAST_ONCE,
						   wmsg, strlen(wmsg), topic, 1);
		if (err)
		{
			printk("Failed to send pwr message, %d\n", err);
			return;
		}
	}
}

// Calculates the wind speed from the number of pulses,
// then submit a job to send the MQTT data
void frequency_counter(struct k_timer *work)
{
	float f = frequency / 6.0 * WIND_SCALE;
	speed = (int)f;
	printk("Windspeed %d ...\n", speed);
	frequency = 0;
	turn_leds_off();
	// set_boost(false);

	k_work_submit(&repeating_timer_work);
}

// ISR called on each pulse from the speed sensor, counts the number of pulses in
// the timer period
void windspeed_handler(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	int64_t time = k_uptime_get();
	if ((time - lasttime) > 10)
	{
		frequency++;
	}
	lasttime = time;
}

// Starts timers for doing the speed and direction sense
void begin_wind_sample()
{
	frequency = 0;
	k_timer_start(&frequency_timer, K_SECONDS(6), K_FOREVER);

	k_timer_start(&ktimeradc, K_SECONDS(1), K_MSEC(400));
}

int init_wind_sensor(struct mqtt_client *c)
{
	_pclient = c;
	int err;
	if (!device_is_ready(windspeed.port))
	{
		return -1;
	}

	err = gpio_pin_configure_dt(&windspeed, GPIO_INPUT | GPIO_PULL_UP);
	if (err < 0)
	{
		return err;
	}
	err = gpio_pin_interrupt_configure_dt(&windspeed, GPIO_INT_EDGE_TO_ACTIVE);

	gpio_init_callback(&windspeed_cb_data, windspeed_handler, BIT(windspeed.pin));

	gpio_add_callback(windspeed.port, &windspeed_cb_data);

	// k_timer_start(&wind_check_timer, K_SECONDS(15), K_SECONDS(20));
	k_timer_start(&wind_check_timer, K_SECONDS(15), K_SECONDS(60 * 5));

	return 0;
}

int get_windspeed()
{
	return speed;
}
