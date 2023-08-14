#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include "leds.h"

#define GPIO_NODE DT_NODELABEL(gpio0)
#define BUTTON_NODE DT_ALIAS(sw0)

#define RED_LED_NODE DT_ALIAS(led0)
#define GREEN_LED_NODE DT_ALIAS(led1)
#define BLUE_LED_NODE DT_ALIAS(led2)

#define LED_OFF 0
#define LED_ON !LED_OFF

static const struct device *gpio_dev;
static struct gpio_callback gpio_cb;

static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(BUTTON_NODE, gpios);

static struct gpio_dt_spec red_led = GPIO_DT_SPEC_GET(RED_LED_NODE, gpios);
static struct gpio_dt_spec green_led = GPIO_DT_SPEC_GET(GREEN_LED_NODE, gpios);
static struct gpio_dt_spec blue_led = GPIO_DT_SPEC_GET(BLUE_LED_NODE, gpios);

static bool button_pressed = false;



void button_pressed_callback(const struct device *gpiob, struct gpio_callback *cb, gpio_port_pins_t pins)
{
    button_pressed = true;
    


}

bool init_button(void)
{
    int ret = gpio_pin_configure_dt(&button, GPIO_INPUT);
    if (ret != 0)
    {
        printk("Error %d: failed to configure %s pin %d\n",
               ret, button.port->name, button.pin);

        return false;
    }

    ret = gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_TO_ACTIVE);
    if (ret != 0)
    {
        printk("Error %d: failed to configure interrupt on %s pin %d\n",
               ret, button.port->name, button.pin);

        return false;
    }

    gpio_init_callback(&gpio_cb, button_pressed_callback, BIT(button.pin));
    gpio_add_callback(button.port, &gpio_cb);

    return true;
}

void init_leds(void)
{
    gpio_dev = DEVICE_DT_GET(GPIO_NODE);

    if (!gpio_dev)
    {
        printk("Error getting GPIO device binding\r\n");

        return;
    }

    if (!init_button())
    {
        return;
    }

    gpio_pin_configure_dt(&red_led, GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&green_led, GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&blue_led, GPIO_OUTPUT_INACTIVE);
}

void turn_leds_off(void)
{
    gpio_pin_set_dt(&red_led, LED_OFF);
    gpio_pin_set_dt(&green_led, LED_OFF);
    gpio_pin_set_dt(&blue_led, LED_OFF);
}

void turn_leds_on_with_color(led_color_t color)
{ 
    switch (color)
    {
    case RED:
        gpio_pin_set_dt(&red_led, LED_ON);
        gpio_pin_set_dt(&green_led, LED_OFF);
        gpio_pin_set_dt(&blue_led, LED_OFF);
        break;
    case GREEN:
        gpio_pin_set_dt(&red_led, LED_OFF);
        gpio_pin_set_dt(&green_led, LED_ON);
        gpio_pin_set_dt(&blue_led, LED_OFF);
        break;
    case BLUE:
        gpio_pin_set_dt(&red_led, LED_OFF);
        gpio_pin_set_dt(&green_led, LED_OFF);
        gpio_pin_set_dt(&blue_led, LED_ON);
        break;
    case MAGENTA:
        gpio_pin_set_dt(&red_led, LED_ON);
        gpio_pin_set_dt(&green_led, LED_OFF);
        gpio_pin_set_dt(&blue_led, LED_ON);
        break;
    case CYAN:
        gpio_pin_set_dt(&red_led, LED_OFF);
        gpio_pin_set_dt(&green_led, LED_ON);
        gpio_pin_set_dt(&blue_led, LED_ON);
        break;
    case YELLOW:
        gpio_pin_set_dt(&red_led, LED_ON);
        gpio_pin_set_dt(&green_led, LED_ON);
        gpio_pin_set_dt(&blue_led, LED_OFF);
        break;
    }
}
