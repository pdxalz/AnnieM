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

#define SECONDS_PER_SAMPLE (60)    // 60 is the minimum, lower requires code fix
#define SAMPLE_DURATION 6
#define REPORTS_PER_HOUR 6     // 6 is every 10 minutes
#define MINUTES_PER_REPORT (60 / REPORTS_PER_HOUR)

#define WIND_SPEED_NODE DT_ALIAS(windspeed0)
static const struct gpio_dt_spec windspeed = GPIO_DT_SPEC_GET(WIND_SPEED_NODE, gpios);

#define WIND_SCALE (102.0 / 60.0)
#define MAX_DIRECTION_VOLTAGE 1630
#define NORTH_OFFSET 90 // Aim to the east so discontinuity is not at north

static volatile int frequency = 0;
static volatile int64_t lasttime = 0;

static int sample_count;
static int speed;
static int gust;
static int lull;

static bool broker_cleared = false;
static uint16_t wind_direction;

// circular buffer holding direction voltages
uint16_t _dir_buf[8]; // fixed size due to averaging calculation
uint16_t _dir_idx = 0;

struct w_sensor
{
	uint8_t speed;
	uint8_t gust;
	uint8_t lull;
	uint16_t direction;
} wind_sensor[REPORTS_PER_HOUR];

static struct gpio_callback windspeed_cb_data;

static void sensor_sample_timer_cb(struct k_timer *work);
static void wind_direction_timer_cb(struct k_timer *work);
static void wind_speed_sample_timer_cb(struct k_timer *work);
static void publish_reports_work_cb(struct k_work *timer_id);

static void restart_samples();
static void build_array_string(uint8_t *buf, struct tm *t);
static void clear_broker_history();
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
	k_timer_start(&wind_speed_sample_timer, K_SECONDS(SAMPLE_DURATION), K_FOREVER);

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
	_dir_buf[_dir_idx] = (((uint32_t)voltage * 360) / MAX_DIRECTION_VOLTAGE + NORTH_OFFSET) % 360;

	_dir_idx = (_dir_idx + 1) % 8;
	wind_direction = circ_avg(
		circ_avg(circ_avg(_dir_buf[0], _dir_buf[1]), circ_avg(_dir_buf[2], _dir_buf[3])),
		circ_avg(circ_avg(_dir_buf[4], _dir_buf[5]), circ_avg(_dir_buf[6], _dir_buf[7])));

	//	LOG_INF("dir volts %d  dir %d\n", voltage, wind_direction);
}

// The timer sets the period wind speed pulses are counted. At the end of the timer
// all sensor data gathering (speed and direction) is complete and then
// the wind speed is calculated from the number of pulses counted.
// A job is submitted to send the MQTT data
static void wind_speed_sample_timer_cb(struct k_timer *work)
{
	float f = frequency / 6.0 * WIND_SCALE;
	int current_speed = (int)f;
	++sample_count;
	speed += current_speed;
	gust = (gust > current_speed) ? gust : current_speed;
	lull = (lull < current_speed) ? lull : current_speed;

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

// background task that sends the MQTT sensor data
// data is sent less often if night time or little wind
static void publish_reports_work_cb(struct k_work *timer_id)
{
	time_t now;
	now = time(NULL);
	struct tm tm;
	gmtime_r(&now, &tm);
	int avg_speed;

	clear_broker_history();

	int hour = tm.tm_hour;
	int minute = tm.tm_min;

	// only report if it's the first sample period of the report period
	if (minute % MINUTES_PER_REPORT != 0)
	{
		turn_leds_on_with_color(BLUE);
		return;
	}

	turn_leds_on_with_color(MAGENTA);

	// zero out hourly data for the first report of the hour
	if (minute < MINUTES_PER_REPORT)
	{
		for (int i = 0; i < REPORTS_PER_HOUR; ++i)
		{
			wind_sensor[i].speed = 0;
			wind_sensor[i].gust = 0;
			wind_sensor[i].lull = 0;
			wind_sensor[i].direction = 0;
		}
	}

	avg_speed = speed / sample_count;
	// 0,0 indicates unset item.
	if (avg_speed == 0 && wind_direction == 0)
	{
		wind_direction = 1;
	}

	wind_sensor[minute / MINUTES_PER_REPORT].speed = avg_speed;
	wind_sensor[minute / MINUTES_PER_REPORT].gust = gust;
	wind_sensor[minute / MINUTES_PER_REPORT].lull = lull;
	k_timer_stop(&wind_direction_timer);
	wind_sensor[minute / MINUTES_PER_REPORT].direction = wind_direction;
	restart_samples();

	uint8_t *msgbuf = get_mqtt_message_buf();
	uint8_t *topicbuf = get_mqtt_topic_buf();

	build_array_string(msgbuf, &tm);
	sprintf(topicbuf, "%s/wind/%02d", CONFIG_MQTT_PRIMARY_TOPIC, hour);

	bool end_of_hour = minute / 5 == 11;

	// always publish data just before the next hour, or
	// publish only if the time is between 10AM and 9PM and
	// the speed is higher than 5
	if (end_of_hour ||
		( // hour > 9 && hour < 21 &&
			(avg_speed >= 0)))
	{

		int err;
		err = data_publish(MQTT_QOS_1_AT_LEAST_ONCE,
						   msgbuf, strlen(msgbuf), topicbuf, 1);
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
// Static functions
//************************

static void restart_samples()
{
	sample_count = 0;
	speed = 0;
	gust = 0;
	lull = 100;
}

// creates JSON string containing time and wind data
static void build_array_string(uint8_t *buf, struct tm *t)
{

	buf += sprintf(buf, "{\"time\":\"%04d-%02d-%02dT%02d:%02dZ\", ", (t->tm_year + 1900), t->tm_mon + 1, t->tm_mday, t->tm_hour, t->tm_min);
	buf += sprintf(buf, "\"wind\":[");

	for (int i = 0; i < REPORTS_PER_HOUR; ++i)
	{
		buf += sprintf(buf, "[%d, %d, %d, %d],", wind_sensor[i].speed, wind_sensor[i].direction, wind_sensor[i].gust, wind_sensor[i].lull);
	}
	--buf; // remove the last comma
	sprintf(buf, "]}");
}

// erases the persistant MQTT data, occurs once at boot time
static void clear_broker_history()
{
	uint8_t *topicbuf = get_mqtt_topic_buf();

	// clear the broker data first time after power up
	if (!broker_cleared)
	{
		LOG_WRN("clearing broker history\n");
		broker_cleared = true;
		for (int i = 0; i < 24; ++i)
		{
			sprintf(topicbuf, "%s/wind/%02d", CONFIG_MQTT_PRIMARY_TOPIC, i);
			int err = data_publish(MQTT_QOS_1_AT_LEAST_ONCE,
								   "", 0, topicbuf, 1);
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

	restart_samples();


	k_timer_start(&sensor_sample_timer, K_SECONDS(5), K_SECONDS(SECONDS_PER_SAMPLE));

	return 0;
}
