#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <modem/lte_lc.h>
#include <zephyr/drivers/gpio.h>
#include <date_time.h>

#include <zephyr/net/mqtt.h>

#include "mqtt_connection.h"
#include "wind_sensor.h"
#include "adc.h"
#include "health.h"
#include "leds.h"
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(sensor, LOG_LEVEL_INF);

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

// todo replace message building buffers
uint8_t wmsg[200];
uint8_t topic[80];
uint8_t buf[100];

uint16_t vbuf[8];
uint16_t v_index = 0;

// todo: should be calculated
#define SAMPLES_PER_HOUR 12
struct w_sensor
{
	uint8_t speed;
	uint16_t direction;
} wind_sensor[12];

static struct gpio_callback windspeed_cb_data;

static void sensor_sample_timer_cb(struct k_timer *work);
static void wind_direction_timer_cb(struct k_timer *work);
static void wind_speed_sample_timer_cb(struct k_timer *work);
static void publish_reports_work_cb(struct k_work *timer_id);
static uint16_t circ_avg(uint16_t a, uint16_t b);

//************************
// Timers and Work threads
//************************
static K_TIMER_DEFINE(sensor_sample_timer, sensor_sample_timer_cb, NULL);
static K_TIMER_DEFINE(wind_direction_timer, wind_direction_timer_cb, NULL);
static K_TIMER_DEFINE(wind_speed_sample_timer, wind_speed_sample_timer_cb, NULL);
static K_WORK_DEFINE(publish_reports_work, publish_reports_work_cb);

// initiates the measuring of wind data
static void sensor_sample_timer_cb(struct k_timer *work)
{
	turn_leds_on_with_color(GREEN);

	// start counting windspeed pulses
	frequency = 0;
	k_timer_start(&wind_speed_sample_timer, K_SECONDS(6), K_FOREVER);

	// start sampling direction sensor
	k_timer_start(&wind_direction_timer, K_SECONDS(1), K_MSEC(400));
}

// updates the wind_direction variable by reading sensor voltage and applying a running average.
// runs multiple times while windspeed is being calculated
static void wind_direction_timer_cb(struct k_timer *work)
{
	uint16_t voltage;

	if (get_adc_voltage(ADC_WIND_DIR_ID, &voltage) != 0)
	{
		LOG_WRN("Failed to get direction voltage\n");
		return;
	};

	//	printk("direction voltage, %d\n", voltage);
	vbuf[v_index] = (((uint32_t)voltage * 360) / MAX_DIRECTION_VOLTAGE + NORTH_OFFSET) % 360;

	v_index = (v_index + 1) % 8;
	wind_direction = circ_avg(
		circ_avg(circ_avg(vbuf[0], vbuf[1]), circ_avg(vbuf[2], vbuf[3])),
		circ_avg(circ_avg(vbuf[4], vbuf[5]), circ_avg(vbuf[6], vbuf[7])));

	//	LOG_INF("dir volts %d  dir %d\n", voltage, wind_direction);
}

// The timer sets the period wind speed pulses are counted. At the end of the timer
// all sensor data gathering (speed and direction) is complete and then
// the wind speed is calculated from the number of pulses counted.
// A job is submitted to send the MQTT data
static void wind_speed_sample_timer_cb(struct k_timer *work)
{
	float f = frequency / 6.0 * WIND_SCALE;
	speed = (int)f;
	LOG_DBG("Windspeed %d ...\n", speed);
	frequency = 0;

	k_work_submit(&publish_reports_work);
}

// ISR called on each pulse from the speed sensor, counts the number of pulses in
// the timer period
void windspeed_handler(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	int64_t time = k_uptime_get();
	// filter out sensor glitches
	if ((time - lasttime) > 10)
	{
		frequency++;
	}
	lasttime = time;
}

//************************
// Static functions
//************************

// creates JSON string containing time and wind data
static void build_array_string(uint8_t *buf, struct tm *t)
{

	buf += sprintf(buf, "{\"time\":\"%04d-%02d-%02dT%02d:%02dZ\", ", (t->tm_year + 1900), t->tm_mon + 1, t->tm_mday, t->tm_hour, t->tm_min);
	buf += sprintf(buf, "\"wind\":[");

	for (int i = 0; i < SAMPLES_PER_HOUR; ++i)
	{
		buf += sprintf(buf, "[%d, %d],", wind_sensor[i].speed, wind_sensor[i].direction);
	}
	--buf; // remove the last comma
	sprintf(buf, "]}");
}

// erases the persistant MQTT data, occurs once at boot time
static void clear_broker_history()
{
	// clear the broker data first time after power up
	if (!broker_cleared)
	{
		LOG_WRN("clearing broker history\n");
		broker_cleared = true;
		for (int i = 0; i < 24; ++i)
		{
			sprintf(topic, "%s/wind/%02d", CONFIG_MQTT_PRIMARY_TOPIC, i);
			int err = data_publish(MQTT_QOS_1_AT_LEAST_ONCE,
								   "", 0, topic, 1);
			if (err)
			{
				LOG_WRN("Failed to send broker clear message, %d\n", err);
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

// background task that sends the MQTT sensor data
// data is sent less often if night time or little wind
static void publish_reports_work_cb(struct k_work *timer_id)
{
	time_t now;
	now = time(NULL);
	struct tm tm;
	gmtime_r(&now, &tm);

	turn_leds_on_with_color(BLUE);

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
	k_timer_stop(&wind_direction_timer);
	wind_sensor[minute / 5].direction = wind_direction;

	build_array_string(wmsg, &tm);
	sprintf(topic, "%s/wind/%02d", CONFIG_MQTT_PRIMARY_TOPIC, hour);

	bool end_of_hour = minute / 5 == 11;

	// always publish data just before the next hour, or
	// publish only if the time is between 10AM and 9PM and
	// the speed is higher than 5
	if (end_of_hour ||
		( // hour > 9 && hour < 21 &&
			(speed >= 0)))
	{

		int err;
		oldspeed = speed;
		err = data_publish(MQTT_QOS_1_AT_LEAST_ONCE,
						   wmsg, strlen(wmsg), topic, 1);
		if (err)
		{
			LOG_WRN("Failed to send message, %d\n", err);
			return;
		}
	}
	if (end_of_hour)
	{
		k_msleep(1000);
		turn_leds_on_with_color(RED);
		publish_health_data();
	}
}

//************************
// Public functions
//************************

int init_wind_sensor()
{
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

	// k_timer_start(&sensor_sample_timer, K_SECONDS(15), K_SECONDS(20));
	k_timer_start(&sensor_sample_timer, K_SECONDS(15), K_SECONDS(60 * 5));

	return 0;
}
