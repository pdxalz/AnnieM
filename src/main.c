/*
 * Copyright (c) 2019-2022 Actinius
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <modem/lte_lc.h>
#include <zephyr/drivers/gpio.h>

#include <zephyr/net/mqtt.h>
#include <date_time.h>

#include "mqtt_connection.h"
#include "wind_sensor.h"

#include "leds.h"
#include "adc.h"
#include "health.h"

extern int setenv(const char *name, const char *value, int overwrite);

/* The mqtt client struct */
static struct mqtt_client client;
/* File descriptor */
static struct pollfd fds;

static K_SEM_DEFINE(lte_connected, 0, 1);

#define TOPIC_STR "zimbuktu2/testing"


static void mm_callback(struct k_work *timer_id)
{
    int err = data_publish(&client, MQTT_QOS_1_AT_LEAST_ONCE,
                           CONFIG_BUTTON_EVENT_PUBLISH_MSG, sizeof(CONFIG_BUTTON_EVENT_PUBLISH_MSG) - 1,TOPIC_STR, 1);
    if (err)
    {
        turn_leds_on_with_color(RED);
        printk("Failed to send message, %d", err);
        return;
    }
    turn_leds_on_with_color(YELLOW);

}
static K_WORK_DEFINE(mm_work, mm_callback);

void send_mqtt(void)
{
    turn_leds_on_with_color(CYAN);
	k_work_submit(&mm_work);
}

static void lte_handler(const struct lte_lc_evt *const evt)
{
    switch (evt->type)
    {
    case LTE_LC_EVT_NW_REG_STATUS:
        if ((evt->nw_reg_status != LTE_LC_NW_REG_REGISTERED_HOME) &&
            (evt->nw_reg_status != LTE_LC_NW_REG_REGISTERED_ROAMING))
        {
            break;
        }
        printk("Network registration status: %s\n",
               evt->nw_reg_status == LTE_LC_NW_REG_REGISTERED_HOME ? "Connected - home network" : "Connected - roaming");
        k_sem_give(&lte_connected);
        break;
    case LTE_LC_EVT_RRC_UPDATE:
        printk("RRC mode: %s\n", evt->rrc_mode == LTE_LC_RRC_MODE_CONNECTED ? "Connected" : "Idle");
        break;
    default:
        break;
    }
}

static void modem_configure(void)
{
    printk("Connecting to LTE network\n");

    int err = lte_lc_init_and_connect_async(lte_handler);
    if (err)
    {
        printk("Modem could not be configured, error: %d\n", err);
        return;
    }
    k_sem_take(&lte_connected, K_FOREVER);
    turn_leds_on_with_color(GREEN);

    printk("Connected to LTE network\n");
}

void main(void)
{

    init_leds();

    turn_leds_on_with_color(YELLOW);

    int err;
    uint32_t connect_attempt = 0;

    // if (init_power() != 0)
    // {
    // 	printk("Failed to initialize the power control");
    // }
    init_adc();

    // printk("2 ADC testing...\n");
 	// while (1) {
	// 	uint16_t battery_voltage = 0;
	// 	get_adc_voltage(ADC_BATTERY_VOLTAGE_ID, &battery_voltage);
	// 	printk("Battery voltage: %u mV\n", battery_voltage);

	// 	get_adc_voltage(ADC_WIND_DIR_ID, &battery_voltage);
	// 	printk("Direction voltage: %u mV\n", battery_voltage);

	// 	k_sleep(K_MSEC(2000));
	// }





    modem_configure();

    turn_leds_on_with_color(RED);

    err = client_init(&client);
    if (err)
    {
        printk("Failed to initialize MQTT client: %d\n", err);
        return;
    }
    setenv("TZ", "PST8PDT", 1);
    struct _reent r;
    _tzset_r(&r);

    init_wind_sensor(&client);
    init_health(&client);

do_connect:
    if (connect_attempt++ > 0)
    {
        printk("Reconnecting in %d seconds...\n",
               CONFIG_MQTT_RECONNECT_DELAY_S);
        k_sleep(K_SECONDS(CONFIG_MQTT_RECONNECT_DELAY_S));
    }
    err = mqtt_connect(&client);
    if (err)
    {
        printk("Error in mqtt_connect: %d\n", err);
        goto do_connect;
    }

    err = fds_init(&client, &fds);
    if (err)
    {
        printk("Error in fds_init: %d\n", err);
        return;
    }

    while (1)
    {
        err = poll(&fds, 1, mqtt_keepalive_time_left(&client));
        if (err < 0)
        {
            printk("Error in poll(): %d\n", errno);
            break;
        }

        err = mqtt_live(&client);
        if ((err != 0) && (err != -EAGAIN))
        {
            printk("Error in mqtt_live: %d\n", err);
            break;
        }

        if ((fds.revents & POLLIN) == POLLIN)
        {
            err = mqtt_input(&client);
            if (err != 0)
            {
                printk("Error in mqtt_input: %d\n", err);
                break;
            }
        }

        if ((fds.revents & POLLERR) == POLLERR)
        {
            printk("POLLERR\n");
            break;
        }

        if ((fds.revents & POLLNVAL) == POLLNVAL)
        {
            printk("POLLNVAL\n");
            break;
        }
    }

    printk("Disconnecting MQTT client\n");

    err = mqtt_disconnect(&client);
    if (err)
    {
        printk("Could not disconnect MQTT client: %d\n", err);
    }
    goto do_connect;
}