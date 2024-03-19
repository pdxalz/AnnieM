#include <zephyr/kernel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/net/mqtt.h>
#include "ArducamCamera.h"
#include "mqtt_connection.h"
#include "cameraThread.h"
#include "ArducamCamera.h"

#define PIC_BUFFER_SIZE 1024 // CONFIG_MQTT_MESSAGE_BUFFER_SIZE
#define WORK_DELAY 400		 // image dim if quick startup
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
const char *singlecharcmds = "bcdefghijlmnopqrsuwx";

struct image_mode_t
{
	uint8_t mode;
	uint32_t min;
	uint32_t max;
};

const struct image_mode_t image_modes[] = {
	{CAM_IMAGE_MODE_96X96, 200, 8000},
	{CAM_IMAGE_MODE_128X128, 200, 12000},
	{CAM_IMAGE_MODE_QVGA, 1000, 30000},
	{CAM_IMAGE_MODE_320X320, 1000, 35000},
	{CAM_IMAGE_MODE_VGA, 1000, 100000},
	{CAM_IMAGE_MODE_HD, 2000, 250000},
	{CAM_IMAGE_MODE_UXGA, 200, 400000},
	{CAM_IMAGE_MODE_FHD, 200, 500000},
	{CAM_IMAGE_MODE_WQXGA2, 200, 800000}};

static uint8_t pic_buffer[PIC_BUFFER_SIZE];

bool sending_photo()
{
	return sending;
}
#if 1
void app_take_pict(uint8_t mode_index)
{
	uint8_t mode = image_modes[mode_index].mode;
	int err;
	int length = PIC_BUFFER_SIZE;

	sending = true;
	takePicture(&camera, mode, CAM_IMAGE_PIX_FMT_JPG);

	k_msleep(PICT_DELAY);

	if (camera.receivedLength < image_modes[mode_index].min ||
		camera.receivedLength > image_modes[mode_index].max)
	{
		sprintf(pic_buffer, "%d < %d %d", image_modes[mode_index].min, camera.receivedLength, image_modes[mode_index].min);
		err = data_publish(MQTT_QOS_1_AT_LEAST_ONCE, pic_buffer, strlen(pic_buffer), "zimbuktu/jpgError", 0);
		sending = false;
		return;
	}
	err = data_publish(MQTT_QOS_1_AT_LEAST_ONCE, "S", 1, "zimbuktu/jpgStart", 0);

	k_msleep(START_DELAY);

	while (camera.receivedLength > 0)
	{
		if (camera.receivedLength <= PIC_BUFFER_SIZE)
		{
			length = camera.receivedLength;
		}
		readBuff(&camera, pic_buffer, length);
		// for (int i = 0; i < length; ++i)
		// {
		// 	pic_buffer[i] = readByte(&camera);
		// }
		err = data_publish(MQTT_QOS_1_AT_LEAST_ONCE, pic_buffer, length, "zimbuktu/jpgData", 0);
		k_msleep(DATA_DELAY);
	}
	err = data_publish(MQTT_QOS_1_AT_LEAST_ONCE, "E", 1, "zimbuktu/jpgEnd", 0);
	k_msleep(END_DELAY);

	sending = false;
}
#endif

#if 1
void app_take_pict_fake_data(uint8_t mode_index)
{
	int err;
	int length = PIC_BUFFER_SIZE;
	int receivedLength = image_modes[mode_index].max / 2;
	sending = true;

	k_msleep(PICT_DELAY);

	err = data_publish(MQTT_QOS_1_AT_LEAST_ONCE, "S", 1, "zimbuktu/jpgStart", 0);

	k_msleep(START_DELAY);

	while (receivedLength > 0)
	{
		if (receivedLength <= PIC_BUFFER_SIZE)
		{
			length = receivedLength;
		}
		for (int i = 0; i < length; ++i)
		{
			pic_buffer[i] = i;
		}
		err = data_publish(MQTT_QOS_1_AT_LEAST_ONCE, pic_buffer, length, "zimbuktu/jpgData", 0);
		receivedLength -= length;
		k_msleep(DATA_DELAY);
	}
	err = data_publish(MQTT_QOS_1_AT_LEAST_ONCE, "E", 1, "zimbuktu/jpgEnd", 0);
	k_msleep(END_DELAY);

	sending = false;
}
#endif

#if 1
void app_take_pict_send_serial(uint8_t mode_index)
{
	uint8_t mode = image_modes[mode_index].mode;
	sending = true;

	takePicture(&camera, mode, CAM_IMAGE_PIX_FMT_JPG);
	printk("START\n");
	printk("length= %d\n", camera.totalLength);

	if (camera.receivedLength < image_modes[mode_index].min ||
		camera.receivedLength > image_modes[mode_index].max)
	{
		sprintf(pic_buffer, "%d < %d %d", image_modes[mode_index].min, camera.receivedLength, image_modes[mode_index].min);
		printk("Length error\n");

		sending = false;
		return;
	}

	int count = 0;
	while (camera.receivedLength > 0)
	{
		++count;
		uint8_t ch = readByte(&camera);
		printk("%02x", ch);
		if (count >= 1024)
		{
			count = 0;
			printk("\n");
			k_msleep(DATA_DELAY);
		}
	}
	printk("\nEND\n");
	sending = false;
}
#endif

#if 1
// #define PBSIZE 40
#define PBSIZE PIC_BUFFER_SIZE
static char uartbuf[PBSIZE + 2];

void app_take_pict_serial_buffer(uint8_t mode_index)
{
	uint8_t mode = image_modes[mode_index].mode;
	sending = true;

	takePicture(&camera, mode, CAM_IMAGE_PIX_FMT_JPG);
	printk("START\n");
	printk("length= %d\n", camera.totalLength);

	if (camera.receivedLength < image_modes[mode_index].min ||
		camera.receivedLength > image_modes[mode_index].max)
	{
		sprintf(pic_buffer, "%d < %d %d", image_modes[mode_index].min, camera.receivedLength, image_modes[mode_index].min);
		printk("Length error\n");

		sending = false;
		return;
	}

	int len = 0;
	while (camera.receivedLength > 0)
	{
		if (PBSIZE < camera.receivedLength)
			len = (PBSIZE < camera.receivedLength) ? PBSIZE : camera.receivedLength;
		if (len == 0)
			break;
		readBuff(&camera, pic_buffer, len);

		char *p = uartbuf;
		for (int i = 0; i < len; ++i)
		{
			p += snprintk(p, 3, "%02x", pic_buffer[i]);
		}
		printk("%s\n", uartbuf);
		k_msleep(DATA_DELAY);
	}
	printk("\nEND\n");
	sending = false;
}
#endif

void camera_work_handler(struct k_work *work)
{
	struct work_info *pinfo = CONTAINER_OF(work, struct work_info, work);
	printk("camera_work_handler start\n");

	if (CAM_ERR_SUCCESS == begin(&camera))
	{
		printk("init ok\n");
	}
	else
	{
		printk("init failed\n");
	}
	// printk("%s\n", camera.myCameraInfo.cameraId);
	// printk("res %d id %d\n", camera.myCameraInfo.supportResolution, camera.cameraId);

	printk("command: %c %d\n", pinfo->cmd, pinfo->param);
	k_msleep(WORK_DELAY);

	switch (pinfo->cmd)
	{
	case 'b':
		setBrightness(&camera, pinfo->param);
		printk("setBrightness %d\n", pinfo->param);
		break;

	case 'c':
		setContrast(&camera, pinfo->param);
		printk("setContrast %d\n", pinfo->param);
		break;

	case 'd':
		setColorEffect(&camera, pinfo->param);
		printk("setColorEffect %d\n", pinfo->param);
		break;

	case 'e':
		setEV(&camera, pinfo->param);
		printk("setEV %d\n", pinfo->param);
		break;

	case 'f':
		setAutoFocus(&camera, pinfo->param);
		printk("setAutoFocus %d\n", pinfo->param);
		break;

	case 'g':
		setAutoExposure(&camera, pinfo->param);
		printk("setAutoExposure %d\n", pinfo->param);
		break;

	case 'h':
		setAbsoluteExposure(&camera, pinfo->param);
		printk("setAbsoluteExposure %d\n", pinfo->param);
		break;

	case 'i':
		setISOSensitivity(&camera, pinfo->param);
		printk("setISOSensitivity %d\n", pinfo->param);
		break;

	case 'j':
		setAutoISOSensitive(&camera, pinfo->param);
		printk("setAutoISOSensitive %d\n", pinfo->param);
		break;

	case 'l':
		if (pinfo->param)
		{
			lowPowerOn(&camera);
			printk("lowPowerOn\n");
		}
		else
		{
			lowPowerOff(&camera);
			printk("lowPowerOff\n");
		}
		break;

	case 'm':
		app_take_pict_send_serial(pinfo->param % sizeof(image_modes));
		break;

	case 'n':
		app_take_pict_fake_data(pinfo->param % sizeof(image_modes));
		break;

	case 'o':
		app_take_pict_serial_buffer(pinfo->param % sizeof(image_modes));
		break;

	case 'p':
		app_take_pict(pinfo->param % sizeof(image_modes));
		break;

	case 'q':
		setImageQuality(&camera, pinfo->param);
		printk("setImageQuality %d\n", pinfo->param);
		break;

	case 'r':
		reset(&camera);
		printk("reset\n");
		break;

	case 's':
		setSharpness(&camera, pinfo->param);
		printk("setSharpness %d\n", pinfo->param);
		break;

	case 'u':
		setSaturation(&camera, pinfo->param);
		printk("setSaturation %d\n", pinfo->param);
		break;

	case 'w':
		setAutoWhiteBalance(&camera, pinfo->param);
		printk("setAutoWhiteBalance %d\n", pinfo->param);
		break;

	case 'x':
		setAutoWhiteBalanceMode(&camera, pinfo->param);
		printk("setAutoWhiteBalanceMode %d\n", pinfo->param);
		break;

	default:
	}
	cameraComplete(&camera);
}

void cameraCommand(char *cmd)
{
	int err;

	if ((strlen(cmd) >= 2) && (strchr(singlecharcmds, cmd[0])))
	{
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
	printk("init start\n");
	camera = createArducamCamera(1);

	k_work_queue_start(&camera_work_q, camera_stack_area,
					   K_THREAD_STACK_SIZEOF(camera_stack_area), WORKQ_PRIORITY,
					   NULL);

	k_work_init(&camera_work.work, camera_work_handler);
	// printk("init complete\n");

	// if (IS_ENABLED(CONFIG_WATCHDOG))
	// {
	// 	watchdog_init_and_start();
	// }
}
