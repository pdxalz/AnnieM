#include <zephyr/kernel.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/device.h>
#include "watchdog.h"
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(watchdog, LOG_LEVEL_INF);

#define CONFIG_WATCHDOG_TIMEOUT_MSEC (5*60*1000)

#define WDT_NODE DT_ALIAS(watchdog0)

int wdt_channel_id;
struct wdt_timeout_cfg wdt_config;
static const struct device *const wdt = DEVICE_DT_GET(WDT_NODE);

static void wdt_callback(const struct device *wdt_dev, int channel_id)
{
    static bool handled_event;

    if (handled_event)
    {
        return;
    }

    wdt_feed(wdt_dev, channel_id);

    LOG_INF("MQTT connnection is broken, reseting system\n");
    handled_event = true;
}

void watchdog_still_running()
{
    if (IS_ENABLED(CONFIG_WATCHDOG))
    {
        wdt_feed(wdt, wdt_channel_id);
    }
}

void watchdog_init_and_start(void)
{
    if (!device_is_ready(wdt))
    {
        LOG_ERR("Watchdog device not ready\n");
        return;
    }

    wdt_config.flags = WDT_FLAG_RESET_SOC;
    wdt_config.window.min = 0U;
    wdt_config.window.max = CONFIG_WATCHDOG_TIMEOUT_MSEC;
    wdt_config.callback = wdt_callback;

    wdt_channel_id = wdt_install_timeout(wdt, &wdt_config);
    if (wdt_channel_id < 0)
    {
        LOG_ERR("Cannot install watchdog timeout\n");
        return;
    }

    wdt_setup(wdt, WDT_OPT_PAUSE_HALTED_BY_DBG);
}
