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
#include "cameraThread.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

extern int setenv(const char *name, const char *value, int overwrite);

static K_SEM_DEFINE(lte_connected, 0, 1);

#define TOPIC_STR "zimbuktu2/testing"

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
        LOG_DBG("Network registration status: %s\n",
                evt->nw_reg_status == LTE_LC_NW_REG_REGISTERED_HOME ? "Connected - home network" : "Connected - roaming");
        k_sem_give(&lte_connected);
        break;
    case LTE_LC_EVT_RRC_UPDATE:
        LOG_DBG("RRC mode: %s\n", evt->rrc_mode == LTE_LC_RRC_MODE_CONNECTED ? "Connected" : "Idle");
        break;
    default:
        break;
    }
}

static void modem_configure(void)
{
    LOG_INF("Connecting to LTE network\n");

    int err = lte_lc_init_and_connect_async(lte_handler);
    if (err)
    {
        LOG_ERR("Modem could not be configured, error: %d\n", err);
        return;
    }
    k_sem_take(&lte_connected, K_FOREVER);
    turn_leds_on_with_color(CYAN);

    LOG_INF("Connected to LTE network\n");
}

void main(void)
{
    init_leds();
    turn_leds_on_with_color(WHITE);

    int err;

    init_adc();

    modem_configure();

    turn_leds_on_with_color(YELLOW);
    k_msleep(2000);
    cameraThreadInit();

    err = client_init();
    if (err)
    {
        LOG_ERR("Failed to initialize MQTT client: %d\n", err);
        return;
    }
    setenv("TZ", "PST8PDT", 1);
    struct _reent r;
    _tzset_r(&r);

    init_wind_sensor();
    init_health();

    mqtt_idleloop(); // does not return
}