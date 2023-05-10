#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/logging/log.h>
#include <dk_buttons_and_leds.h>
#include <modem/lte_lc.h>
#include <zephyr/drivers/gpio.h>

LOG_MODULE_DECLARE(Lesson4_Exercise1);

#define WIND_SPEED_NODE DT_ALIAS(windspeed0)
static const struct gpio_dt_spec windspeed = GPIO_DT_SPEC_GET(WIND_SPEED_NODE, gpios);

volatile int frequency = 0;
volatile int64_t lasttime = 0;
volatile bool toggle = false;
#define WIND_SCALE (102.0 / 60.0)

void sample_function(struct k_work *work)
{
	float f = frequency / 6.0 * WIND_SCALE;
	int speed = (int)f;
	LOG_INF("Windspeed %d ...", speed);
	frequency = 0;
}

K_TIMER_DEFINE(sample_timer, sample_function, NULL);

void ws_calc()
{

}

void windspeed_handler(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	int64_t time = k_uptime_get();
	if ((time - lasttime) > 10)
	{
		frequency++;
		toggle = !toggle;
	}
	lasttime = time;

	dk_set_led_on(DK_LED1);
	dk_set_led_off(DK_LED2);
}

static struct gpio_callback windspeed_cb_data;


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
	/* STEP 3 - Configure the interrupt on the button's pin */
	err = gpio_pin_interrupt_configure_dt(&windspeed, GPIO_INT_EDGE_TO_ACTIVE);

	/* STEP 6 - Initialize the static struct gpio_callback variable   */
	gpio_init_callback(&windspeed_cb_data, windspeed_handler, BIT(windspeed.pin));

	/* STEP 7 - Add the callback function by calling gpio_add_callback()   */
	gpio_add_callback(windspeed.port, &windspeed_cb_data);


	k_timer_start(&sample_timer, K_SECONDS(6), K_SECONDS(6));
	return 0;
}
