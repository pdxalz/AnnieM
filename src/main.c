/*
 * Copyright (c) 2018 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/net/socket.h>
#include <dk_buttons_and_leds.h>
#include <modem/lte_lc.h>
#include <zephyr/drivers/gpio.h>
#include <date_time.h>
#include <soc.h>
#include <zephyr/drivers/kscan.h>

#define LOG_LEVEL LOG_LEVEL_DBG
#include <zephyr/logging/log.h>

/* STEP 2.3 - Include the header file for the MQTT Library*/
#include <zephyr/net/mqtt.h>

#include "mqtt_connection.h"
#include "wind_sensor.h"
#include "power.h"
#include "adc.h"

extern int setenv(const char *name, const char *value, int overwrite);

/* The mqtt client struct */
static struct mqtt_client client;
/* File descriptor */
static struct pollfd fds;

static K_SEM_DEFINE(lte_connected, 0, 1);

LOG_MODULE_REGISTER(AnnieM, LOG_LEVEL_INF);

uint8_t msg[] = "0 hello test";
uint8_t cnt = 0;
uint8_t buf[100];

// static K_TIMER_DEFINE(wind_check_timer, wind_check, NULL);
static struct k_timer wind_check_timer;

static void adc_check(struct k_timer *work)
{
	uint16_t dir = get_wind_direction();
	LOG_INF("dir: %d ", dir);
}

K_TIMER_DEFINE(ktimeradc, adc_check, NULL);

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
		LOG_INF("Network registration status: %s",
				evt->nw_reg_status == LTE_LC_NW_REG_REGISTERED_HOME ? "Connected - home network" : "Connected - roaming");
		k_sem_give(&lte_connected);
		break;
	case LTE_LC_EVT_RRC_UPDATE:
		LOG_INF("RRC mode: %s", evt->rrc_mode == LTE_LC_RRC_MODE_CONNECTED ? "Connected" : "Idle");
		break;
	default:
		break;
	}
}

static void modem_configure(void)
{
	LOG_INF("Connecting to LTE network");

	int err = lte_lc_init_and_connect_async(lte_handler);
	if (err)
	{
		LOG_INF("Modem could not be configured, error: %d", err);
		return;
	}
	k_sem_take(&lte_connected, K_FOREVER);
	LOG_INF("Connected to LTE network");
	dk_set_led_on(DK_LED2);
}

static void button_handler(uint32_t button_state, uint32_t has_changed)
{
	switch (has_changed)
	{
	}
}

void main(void)
{
	int err;
	uint32_t connect_attempt = 0;

	if (dk_leds_init() != 0)
	{
		LOG_ERR("Failed to initialize the LED library");
	}
	if (init_power() != 0)
	{
		LOG_ERR("Failed to initialize the power control");
	}
	init_adc();
	modem_configure();

	if (dk_buttons_init(button_handler) != 0)
	{
		LOG_ERR("Failed to initialize the buttons library");
	}

	err = client_init(&client);
	if (err)
	{
		LOG_ERR("Failed to initialize MQTT client: %d", err);
		return;
	}
	setenv("TZ", "PST8PDT", 1);
	struct _reent r;
	_tzset_r(&r);

	init_wind_sensor(&client);
	//	k_timer_start(&wind_check_timer, K_SECONDS(get_sample_time()), K_SECONDS(get_sample_time()));
	k_timer_init(&wind_check_timer, wind_check_callback, NULL);
	k_timer_start(&wind_check_timer, K_SECONDS(15), K_SECONDS(60 * 5));
	k_timer_start(&ktimeradc, K_SECONDS(15), K_MSEC(100));

do_connect:
	if (connect_attempt++ > 0)
	{
		LOG_INF("Reconnecting in %d seconds...",
				CONFIG_MQTT_RECONNECT_DELAY_S);
		k_sleep(K_SECONDS(CONFIG_MQTT_RECONNECT_DELAY_S));
	}
	err = mqtt_connect(&client);
	if (err)
	{
		LOG_ERR("Error in mqtt_connect: %d", err);
		goto do_connect;
	}

	err = fds_init(&client, &fds);
	if (err)
	{
		LOG_ERR("Error in fds_init: %d", err);
		return;
	}

	while (1)
	{
		err = poll(&fds, 1, mqtt_keepalive_time_left(&client));
		if (err < 0)
		{
			LOG_ERR("Error in poll(): %d", errno);
			break;
		}

		err = mqtt_live(&client);
		if ((err != 0) && (err != -EAGAIN))
		{
			LOG_ERR("Error in mqtt_live: %d", err);
			break;
		}

		if ((fds.revents & POLLIN) == POLLIN)
		{
			err = mqtt_input(&client);
			if (err != 0)
			{
				LOG_ERR("Error in mqtt_input: %d", err);
				break;
			}
		}

		if ((fds.revents & POLLERR) == POLLERR)
		{
			LOG_ERR("POLLERR");
			break;
		}

		if ((fds.revents & POLLNVAL) == POLLNVAL)
		{
			LOG_ERR("POLLNVAL");
			break;
		}
	}

	LOG_INF("Disconnecting MQTT client");

	err = mqtt_disconnect(&client);
	if (err)
	{
		LOG_ERR("Could not disconnect MQTT client: %d", err);
	}
	goto do_connect;
}