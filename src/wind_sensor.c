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
#include "adc.h"

LOG_MODULE_DECLARE(AnnieM);

#define WIND_SPEED_NODE DT_ALIAS(windspeed0)
static const struct gpio_dt_spec windspeed = GPIO_DT_SPEC_GET(WIND_SPEED_NODE, gpios);

#define WIND_SCALE (102.0 / 60.0)
#define MAX_DIRECTION_VOLTAGE 2900
#define NORTH_OFFSET 84

static volatile int frequency = 0;
static volatile int64_t lasttime = 0;
static int speed;
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


static void wind_check_callback(struct k_timer *work)
{
	int rc;
	time_t temp;
	struct tm *timeptr;

	temp = time(NULL);
	timeptr = localtime(&temp);
	rc = strftime(buf, sizeof(buf), "Today is %A, %b %d.\nTime:  %r", timeptr);
	//		LOG_INF("time: %s  chars: %d", buf, rc);

	if (sleepy_mode())
	{
		dk_set_led_on(DK_LED3);
		dk_set_led_off(DK_LED2);
		dk_set_led_off(DK_LED1);
		return;
	}
	dk_set_led_on(DK_LED1);
	dk_set_led_off(DK_LED2);

	set_boost(true);
	begin_wind_sample();
}

static void build_array_string(uint8_t *buf, struct tm *t)
{

	buf += sprintf(buf, "{\"time\":\"%04d-%02d-%02dT%02d:%02d:%02d.000Z\", ", (t->tm_year+1900), t->tm_mon, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec);
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

static uint16_t circ_avg(uint16_t a, uint16_t b)
{
	int16_t diff = ((a - b + 180 + 360) % 360) - 180;
	int16_t angle = (360 + b + (diff / 2)) % 360;
	//	LOG_INF("avg=      %d, %d, %d, %d", a, b, diff, angle);
	return angle;
}

static void check_wind_direction(struct k_timer *work)
{
	uint16_t voltage;

	if (get_battery_voltage(&voltage) != 0)
	{
		LOG_INF("Failed to get direction voltage");
		return;
	};

	//	LOG_INF("direction voltage, %d", voltage);
	vbuf[v_index] = (((uint32_t)voltage * 360) / MAX_DIRECTION_VOLTAGE + NORTH_OFFSET) % 360;

	v_index = (v_index + 1) % 8;
	wind_direction = circ_avg(
		circ_avg(circ_avg(vbuf[0], vbuf[1]), circ_avg(vbuf[2], vbuf[3])),
		circ_avg(circ_avg(vbuf[4], vbuf[5]), circ_avg(vbuf[6], vbuf[7])));
	//	uint16_t avg = circ_avg(vbuf[0], vbuf[1]);
	LOG_INF("vv= %d, %d, %d", vbuf[0], vbuf[1], wind_direction);
	// LOG_INF("volts, v=, %d, %d", voltage, v);
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
	//	LOG_INF("h %d m:%d   %s", hour, minute, wmsg);
	sprintf(topic, "%s/wind/%02d", CONFIG_MQTT_PRIMARY_TOPIC, hour);
	// sprintf(topic, "%s/wind/00", CONFIG_MQTT_PRIMARY_TOPIC);

	int err;

	err = data_publish(_pclient, MQTT_QOS_1_AT_LEAST_ONCE,
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


void windspeed_handler(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	int64_t time = k_uptime_get();
	if ((time - lasttime) > 10)
	{
		frequency++;
	}
	lasttime = time;
}


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
	/* STEP 3 - Configure the interrupt on the button's pin */
	err = gpio_pin_interrupt_configure_dt(&windspeed, GPIO_INT_EDGE_TO_ACTIVE);

	/* STEP 6 - Initialize the static struct gpio_callback variable   */
	gpio_init_callback(&windspeed_cb_data, windspeed_handler, BIT(windspeed.pin));

	/* STEP 7 - Add the callback function by calling gpio_add_callback()   */
	gpio_add_callback(windspeed.port, &windspeed_cb_data);

	k_timer_start(&wind_check_timer, K_SECONDS(15), K_SECONDS(60 * 5));

	return 0;
}

int get_windspeed()
{
	return speed;
}
