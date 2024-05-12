#include <zephyr/kernel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/net/mqtt.h>
#include "ArducamCamera.h"
#include "mqtt_connection.h"
#include "cameraThread.h"
#include "ArducamCamera.h"

#include <modem/modem_info.h>
#include "health.h"
#include "leds.h"

#define PIC_BUFFER_SIZE 1024 // CONFIG_MQTT_MESSAGE_BUFFER_SIZE
#define PIC_SEND_LENGTH 256	 // z should be PIC_BUFFER_SIZE, but there's corruption
// #define PIC_SEND_LENGTH 512	 // z should be PIC_BUFFER_SIZE, but there's corruption
#define WORK_DELAY 400 // image dim if quick startup
#define PICT_DELAY 1
#define START_DELAY 1
#define DATA_DELAY 200 // image fails if too quick
#define END_DELAY 1

#define CAMERA_THREAD_STACK_SIZE 4096
#define WORKQ_PRIORITY 4
#define MAX_CMD 10

static K_THREAD_STACK_DEFINE(camera_stack_area, CAMERA_THREAD_STACK_SIZE);
static struct k_work_q camera_work_q = {0};
struct work_info
{
	struct k_work work;
	char cmd;
	uint32_t param;
} camera_work;

ArducamCamera camera;
static bool sending = false;

/*
b brightness
c contrast
d color effect
e ev level
f autofocus
g autoexposure
h absoluteexposure
i isosensitivity
j autoiso
l lowpower
m send camera data out serial port
n create fake camera data
o send serial buffer
p picture
q imagequality
r reset
s sharpness
u saturation
w white balance
x white balance mode
*/
const char *singlecharcmds = "efhlmnopqsz";
// const char *singlecharcmds = "bcdefghijlmnopqrsuwx";

struct image_mode_t
{
	uint8_t mode;
	uint32_t min;
	uint32_t max;
};

const struct image_mode_t image_modes[] = {
	{CAM_IMAGE_MODE_96X96, 200, 8000},
	{CAM_IMAGE_MODE_128X128, 200, 12000},
	{CAM_IMAGE_MODE_QVGA, 1000, 45000},
	{CAM_IMAGE_MODE_320X320, 1000, 55000},
	{CAM_IMAGE_MODE_VGA, 1000, 150000},
	{CAM_IMAGE_MODE_HD, 2000, 500000},
	{CAM_IMAGE_MODE_UXGA, 200, 700000},
	{CAM_IMAGE_MODE_FHD, 200, 800000},
	{CAM_IMAGE_MODE_WQXGA2, 200, 1500000}};

static uint8_t pic_buffer[PIC_BUFFER_SIZE];

int network_info_log(int param)
{
	char sbuf[40];
	char msgbuf[100];
	switch (param)
	{
	case 0:
		modem_info_string_get(MODEM_INFO_RSRP, sbuf, sizeof(sbuf));
		snprintf(msgbuf, 100, "Signal strength: %s\n", sbuf);
		break;
	case 1:
		modem_info_string_get(MODEM_INFO_CUR_BAND, sbuf, sizeof(sbuf));
		snprintf(msgbuf, 100, "Current LTE band: %s\n", sbuf);
		break;
	}
	printk("%s", msgbuf);
	data_publish(MQTT_QOS_1_AT_LEAST_ONCE, msgbuf, strlen(msgbuf), "zimbuktu/modem", 0);

	// modem_info_string_get(MODEM_INFO_SUP_BAND, sbuf, sizeof(sbuf));
	// printk("Supported LTE bands: %s\n", sbuf);
	// modem_info_string_get(MODEM_INFO_AREA_CODE, sbuf, sizeof(sbuf));
	// printk("Tracking area code: %s\n", sbuf);
	// modem_info_string_get(MODEM_INFO_UE_MODE, sbuf, sizeof(sbuf));
	// printk("Current mode: %s\n", sbuf);
	// modem_info_string_get(MODEM_INFO_OPERATOR, sbuf, sizeof(sbuf));
	// printk("Current operator name: %s\n", sbuf);
	// modem_info_string_get(MODEM_INFO_CELLID, sbuf, sizeof(sbuf));
	// printk("Cell ID of the device: %s\n", sbuf);
	// modem_info_string_get(MODEM_INFO_IP_ADDRESS, sbuf, sizeof(sbuf));
	// printk("IP address of the device: %s\n", sbuf);
	// modem_info_string_get(MODEM_INFO_FW_VERSION, sbuf, sizeof(sbuf));
	// printk("Modem firmware version: %s\n", sbuf);
	// modem_info_string_get(MODEM_INFO_LTE_MODE, sbuf, sizeof(sbuf));
	// printk("LTE-M support mode: %s\n", sbuf);
	// modem_info_string_get(MODEM_INFO_NBIOT_MODE, sbuf, sizeof(sbuf));
	// printk("NB-IoT support mode: %s\n", sbuf);
	// modem_info_string_get(MODEM_INFO_GPS_MODE, sbuf, sizeof(sbuf));
	// printk("GPS support mode: %s\n", sbuf);
	// modem_info_string_get(MODEM_INFO_DATE_TIME, sbuf, sizeof(sbuf));
	// printk("Mobile network time and date: %s\n", sbuf);
	// printk("===============================");
	return 0;
}

bool sending_photo()
{
	return sending;
}
static uint8_t exposure = 0;
static uint8_t sharpness = 0;
static uint8_t focus = 0;
static uint8_t quality = 0;

int app_take_pict(uint8_t mode_index)
{
	uint8_t mode = image_modes[mode_index].mode;
	int err;
	int length = PIC_SEND_LENGTH;

	sending = true;
	if (takePicture(&camera,
					mode,
					CAM_IMAGE_PIX_FMT_JPG,
					exposure,
					sharpness,
					focus,
					quality) == CAM_ERR_TIMEOUT)
		return CAM_ERR_TIMEOUT;

	k_msleep(PICT_DELAY);
	printk("image size= %d\n", camera.receivedLength);
	if (camera.receivedLength < image_modes[mode_index].min ||
		camera.receivedLength > image_modes[mode_index].max)
	{
		sprintf(pic_buffer, "%d < %d %d", image_modes[mode_index].min, camera.receivedLength, image_modes[mode_index].max);
		err = data_publish(MQTT_QOS_1_AT_LEAST_ONCE, pic_buffer, strlen(pic_buffer), "zimbuktu/jpgError", 0);
		sending = false;
		printk("Length error\n");
		return CAM_ERR_LENGTH;
	}
	sprintf(pic_buffer, "%d", camera.receivedLength);
	err = data_publish(MQTT_QOS_1_AT_LEAST_ONCE, pic_buffer, strlen(pic_buffer), "zimbuktu/jpgStart", 0);

	k_msleep(START_DELAY);

	while (camera.receivedLength > 0)
	{
		printk(".");
		if (camera.receivedLength <= PIC_SEND_LENGTH)
		{
			printk("&");

			length = camera.receivedLength;
		}
		printk("-");

		readBuff(&camera, pic_buffer, length);
		printk("+");

		// for (int i = 0; i < length; ++i)
		// {
		// 	pic_buffer[i] = readByte(&camera);
		// }
		err = data_publish(MQTT_QOS_1_AT_LEAST_ONCE, pic_buffer, length, "zimbuktu/jpgData", 0);
		printk("!");

		k_msleep(DATA_DELAY);
	}
	printk("*");

	err = data_publish(MQTT_QOS_1_AT_LEAST_ONCE, "E", 1, "zimbuktu/jpgEnd", 0);
	k_msleep(END_DELAY);

	sending = false;
	return CAM_ERR_SUCCESS;
}

void camera_work_handler(struct k_work *work)
{
	struct work_info *pinfo = CONTAINER_OF(work, struct work_info, work);

	switch (pinfo->cmd)
	{
	case 'e':
		exposure = pinfo->param;
		return;
	case 's':
		sharpness = pinfo->param;
		return;
	case 'f':
		focus = pinfo->param;
		return;
	case 'q':
		quality = pinfo->param;
		return;

	case 'h':
		report_status_info(pinfo->param);
		return;

	case 'l':
		set_led_mode(pinfo->param);
		return;

	case 'p':
	{
		int err = begin(&camera);
		if (CAM_ERR_SUCCESS == err)
		{
			k_msleep(WORK_DELAY);
			app_take_pict(pinfo->param % sizeof(image_modes));
		}
		else
		{
			printk("init failed\n");
		}
		cameraComplete(&camera);
		return;
	}
	case 'z':
		network_info_log(pinfo->param);
		return;
	}
}

void cameraCommand(char *cmd)
{
	int err;

	if ((strlen(cmd) >= 2) && (strchr(singlecharcmds, cmd[0])))
	{
		if (cmd[0] == 'z' && cmd[1] == '!')
		{
			// reset the system immediately
			NVIC_SystemReset();
			while (1)
				;
		}

		camera_work.cmd = cmd[0];
		camera_work.param = atoi(&cmd[1]);
		err = k_work_submit_to_queue(&camera_work_q, &camera_work.work);
		printk("qsubmit: %d\n", err);
	}
	else
	{
		printk("invalid command: %s\n", cmd);
	}
}

void cameraThreadInit()
{
	printk("camera init start\n");
	camera = createArducamCamera(1);

	k_work_queue_start(&camera_work_q, camera_stack_area,
					   K_THREAD_STACK_SIZEOF(camera_stack_area), WORKQ_PRIORITY,
					   NULL);

	k_work_init(&camera_work.work, camera_work_handler);
	printk("camera init complete\n");
}
