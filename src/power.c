#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/logging/log.h>
#include <dk_buttons_and_leds.h>
#include <modem/lte_lc.h>
#include <zephyr/drivers/gpio.h>
#include <adp536x.h>
#include <zephyr/net/mqtt.h>
#include "mqtt_connection.h"
#include "temperature.h"
#include "power.h"

LOG_MODULE_DECLARE(AnnieM);

#define FAN_ENABLE_NODE DT_ALIAS(fanenable)
#define BOOST_ENABLE_NODE DT_ALIAS(boostenable)

static const struct gpio_dt_spec fan = GPIO_DT_SPEC_GET(FAN_ENABLE_NODE, gpios);
static const struct gpio_dt_spec boost = GPIO_DT_SPEC_GET(BOOST_ENABLE_NODE, gpios);

uint8_t pwmsg[100];
uint8_t ptopic[80];

#define NUM_PWR 12
int n_pwr = NUM_PWR - 1;
uint8_t soc[NUM_PWR];
uint16_t volts[NUM_PWR];
uint16_t temperature[NUM_PWR];
uint16_t current_volts;

int init_power()
{
	int err;
	if (!device_is_ready(fan.port))
	{
		return -1;
	}

	if (!device_is_ready(boost.port))
	{
		return -1;
	}

	err = gpio_pin_configure_dt(&fan, GPIO_OUTPUT_INACTIVE);
	if (err < 0)
	{
		return err;
	}

	err = gpio_pin_configure_dt(&boost, GPIO_OUTPUT_INACTIVE);
	if (err < 0)
	{
		return err;
	}

	return 0;
}

void toggle_boost()
{
	gpio_pin_toggle_dt(&boost);
}

void set_boost(bool enable)
{
	// 		gpio_pin_set_dt(&boost, 1);
	gpio_pin_set_dt(&boost, enable ? 1 : 0);
}

uint16_t get_volts()
{
	return current_volts;
}

void report_power(uint8_t *buf)
{
	double temp;

	if (adp536x_fg_soc(&soc[n_pwr]))
	{
		LOG_ERR("SOC failed");
		return;
	}
	if (adp536x_fg_volts(&volts[n_pwr]))
	{
		LOG_ERR("volts failed");
		return;
	}
	current_volts = volts[n_pwr];
	if (get_temperature(&temp))
	{

		LOG_ERR("temperature failed");
		return;
	}
	temperature[n_pwr] = temp * 9.0 / 5.0 + 32.0;
	buf += sprintf(buf, "{\"pwr\":[");

	for (int i = n_pwr; i < NUM_PWR + n_pwr; ++i)
	{
		buf += sprintf(buf, "[%d, %d, %d],", soc[i % NUM_PWR], volts[i % NUM_PWR], temperature[i % NUM_PWR]);
	}
	--buf; // remove the last comma
	sprintf(buf, "]}");

	n_pwr = (n_pwr - 1 + NUM_PWR) % NUM_PWR;
}
int az_set_led_on(uint8_t led_idx)
{
	dk_set_led_on(led_idx);
//	dk_set_led_off(led_idx);
	
}