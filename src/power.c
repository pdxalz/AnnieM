#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/logging/log.h>
#include <dk_buttons_and_leds.h>
#include <modem/lte_lc.h>
#include <zephyr/drivers/gpio.h>

LOG_MODULE_DECLARE(Lesson4_Exercise1);

#define FAN_ENABLE_NODE DT_ALIAS(fanenable)
#define BOOST_ENABLE_NODE DT_ALIAS(boostenable)


static const struct gpio_dt_spec fan = GPIO_DT_SPEC_GET(FAN_ENABLE_NODE, gpios);
static const struct gpio_dt_spec boost = GPIO_DT_SPEC_GET(BOOST_ENABLE_NODE, gpios);



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


	err = gpio_pin_configure_dt(&fan, GPIO_OUTPUT_ACTIVE);
	if (err < 0)
	{
		return err;
	}
	
	err = gpio_pin_configure_dt(&boost, GPIO_OUTPUT_ACTIVE);
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
	gpio_pin_set_dt(&boost, enable ?  1 : 0);
}
