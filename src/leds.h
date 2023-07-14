#ifndef _LEDS_H_
#define _LED_H_

typedef enum
{
    RED,
    GREEN,
    BLUE,
    MAGENTA,
    CYAN,
    YELLOW
} led_color_t;


void init_leds(void);
void turn_leds_off(void);
void turn_leds_on_with_color(led_color_t color);


#endif /* _LED_H_ */
