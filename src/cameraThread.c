#include <zephyr/kernel.h>
#include <stdlib.h>
#include <string.h>
#include "ArducamCamera.h"
#include "cameraThread.h"
#include "ArducamCamera.h"
#include <zephyr/console/console.h>

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
p picture
r reset
q imagequality
r reset
s sharpness
u saturation
w white balance
x white balance mode
*/
const char *singlecharcmds = "bcdefghijlpqrsuwx";
const uint8_t image_modes[] = {
	CAM_IMAGE_MODE_96X96,
	CAM_IMAGE_MODE_128X128,
	CAM_IMAGE_MODE_QVGA,
	CAM_IMAGE_MODE_320X320,
	CAM_IMAGE_MODE_VGA,
	CAM_IMAGE_MODE_HD,
	CAM_IMAGE_MODE_UXGA,
	CAM_IMAGE_MODE_FHD,
	CAM_IMAGE_MODE_WQXGA2};

void app_take_pict(uint8_t image_mode)
{
	takePicture(&camera, image_mode, CAM_IMAGE_PIX_FMT_JPG);
	printk("START\n");
	printk("length= %d\n", camera.totalLength);
	int count = 0;
	while (camera.receivedLength > 0)
	{
		++count;
		uint8_t ch = readByte(&camera);
		printk("%02x", ch);
		if (count >= 40)
		{
			count = 0;
			printk("\n");
		}
	}
	printk("\nEND\n");
}

void camera_work_handler(struct k_work *work)
{
	struct work_info *pinfo = CONTAINER_OF(work, struct work_info, work);

	printk("command: %c %d\n", pinfo->cmd, pinfo->param);
k_msleep(4000);

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

	case 'p':
		uint8_t mode = image_modes[pinfo->param % sizeof(image_modes)];
		app_take_pict(mode);
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
