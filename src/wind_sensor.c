#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/logging/log.h>
#include <dk_buttons_and_leds.h>
#include <modem/lte_lc.h>
#include <zephyr/drivers/gpio.h>
#include <date_time.h>

#include <zephyr/net/mqtt.h>

#include "mqtt_connection.h"
#include "wind_sensor.h"
#include "power.h"

LOG_MODULE_DECLARE(AnnieM);

#define WIND_SPEED_NODE DT_ALIAS(windspeed0)
static const struct gpio_dt_spec windspeed = GPIO_DT_SPEC_GET(WIND_SPEED_NODE, gpios);

#define WIND_SCALE (102.0 / 60.0)

static volatile int frequency = 0;
static volatile int64_t lasttime = 0;
static volatile bool toggle = false;
static int speed;
static bool broker_cleared = false;

uint8_t wmsg[200];
uint8_t tmsg[80];
uint8_t topic[80];

#define SAMPLES_PER_HOUR 12
struct w_sensor
{
	uint8_t speed;
	uint8_t direction;
} wind_sensor[12];

/* The mqtt client struct */
static struct mqtt_client *_pclient;

static void build_array_string(uint8_t *buf, struct tm *t)
{

	buf += sprintf(buf, "{\"time\":\"%04d-%02d-%02dT%02d:%02d:%02d.000Z\", ", t->tm_year, t->tm_mon, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec);
	buf += sprintf(buf, "{\"wind\":[");

	for (int i = 0; i < SAMPLES_PER_HOUR; ++i)
	{
		buf += sprintf(buf, "[%d, %d],", wind_sensor[i].speed, wind_sensor[i].direction);
	}
	--buf; // remove the last comma
	sprintf(buf, "]}");
}

static void clear_broker_history()
{
	// clear the broker data first time after power up
	if (!broker_cleared)
	{
		broker_cleared = true;
		for (int i = 0; i < 24; ++i)
		{
			sprintf(topic, "%s/wind/%02d", CONFIG_MQTT_PRIMARY_TOPIC, i);
			int err = data_publish(_pclient, MQTT_QOS_1_AT_LEAST_ONCE,
								   "", 0, topic, 1);
			if (err)
			{
				LOG_INF("Failed to send broker clear message, %d", err);
				return;
			}
		}
	}
}

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
		for (int i = 0; i < SAMPLES_PER_HOUR; ++i)
		{
			wind_sensor[i].speed = 0;
			wind_sensor[i].direction = 0;
		}
	}

	wind_sensor[minute / 5].speed = speed;
	wind_sensor[minute / 5].direction = 1;

	build_array_string(wmsg, &tm);
//	LOG_INF("h %d m:%d   %s", hour, minute, wmsg); 
//undo	sprintf(topic, "%s/wind/%02d", CONFIG_MQTT_PRIMARY_TOPIC, hour);
	sprintf(topic, "%s/wind/00", CONFIG_MQTT_PRIMARY_TOPIC);

	int err = data_publish(_pclient, MQTT_QOS_1_AT_LEAST_ONCE,
						   wmsg, strlen(wmsg) - 1, topic, 1);
	if (err)
	{
		LOG_INF("Failed to send message, %d", err);
		return;
	}
	report_power(wmsg);
	sprintf(topic, "%s/health", CONFIG_MQTT_PRIMARY_TOPIC);
	
	err = data_publish(_pclient, MQTT_QOS_1_AT_LEAST_ONCE,
						   wmsg, strlen(wmsg) - 1, topic, 1);
	if (err)
	{
		LOG_INF("Failed to send pwr message, %d", err);
		return;
	}

}

K_WORK_DEFINE(repeating_timer_work, speed_calc_callback);

void frequency_counter(struct k_timer *work)
{
	float f = frequency / 6.0 * WIND_SCALE;
	speed = (int)f;
	LOG_INF("Windspeed %d ...", speed);
	frequency = 0;
	dk_set_led_on(DK_LED2);
	dk_set_led_off(DK_LED1);
	set_boost(false);

	k_work_submit(&repeating_timer_work);
}

K_TIMER_DEFINE(frequency_timer, frequency_counter, NULL);

void windspeed_handler(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	int64_t time = k_uptime_get();
	if ((time - lasttime) > 10)
	{
		frequency++;
		toggle = !toggle;
	}
	lasttime = time;
}

static struct gpio_callback windspeed_cb_data;

void begin_wind_sample()
{
	frequency = 0;
	k_timer_start(&frequency_timer, K_SECONDS(6), K_FOREVER);
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
	/* STEP 3 - Configure the interrupt on the button's pin */
	err = gpio_pin_interrupt_configure_dt(&windspeed, GPIO_INT_EDGE_TO_ACTIVE);

	/* STEP 6 - Initialize the static struct gpio_callback variable   */
	gpio_init_callback(&windspeed_cb_data, windspeed_handler, BIT(windspeed.pin));

	/* STEP 7 - Add the callback function by calling gpio_add_callback()   */
	gpio_add_callback(windspeed.port, &windspeed_cb_data);

	return 0;
}

int get_windspeed()
{
	return speed;
}
