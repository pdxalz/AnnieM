#include <zephyr/drivers/adc.h>
#include "adc.h"
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(adc, LOG_LEVEL_INF);

#define BATVOLT_R1 4.7f			 // MOhm
#define BATVOLT_R2 10.0f		 // MOhm
#define INPUT_VOLT_RANGE 3.67f	 // Volts
#define VALUE_RANGE_10_BIT 1.023 // (2^10 - 1) / 1000

#define ADC_NODE DT_NODELABEL(adc)

#define ADC_RESOLUTION 10
#define ADC_GAIN ADC_GAIN_1_6
#define ADC_REFERENCE ADC_REF_INTERNAL
#define ADC_ACQUISITION_TIME ADC_ACQ_TIME(ADC_ACQ_TIME_MICROSECONDS, 10)
#define ADC_1ST_CHANNEL_INPUT SAADC_CH_PSELP_PSELP_AnalogInput0
#define ADC_2ND_CHANNEL_INPUT SAADC_CH_PSELP_PSELP_AnalogInput1
#define ADC_3RD_CHANNEL_INPUT SAADC_CH_PSELP_PSELP_AnalogInput2

#define BUFFER_SIZE 1
static int16_t m_sample_buffer[BUFFER_SIZE];

static const struct device *adc_dev;

// Battery voltage ADC
static const struct adc_channel_cfg m_1st_channel_cfg = {
	.gain = ADC_GAIN,
	.reference = ADC_REFERENCE,
	.acquisition_time = ADC_ACQUISITION_TIME,
	.channel_id = ADC_BATTERY_VOLTAGE_ID,
	.input_positive = ADC_1ST_CHANNEL_INPUT,
};

// Wind direction ADC
static const struct adc_channel_cfg m_2nd_channel_cfg = {
	.gain = ADC_GAIN,
	.reference = ADC_REFERENCE,
	.acquisition_time = ADC_ACQUISITION_TIME,
	.channel_id = ADC_WIND_DIR_ID,
	.input_positive = ADC_2ND_CHANNEL_INPUT,
};

// Temperature Sensor ADC
static const struct adc_channel_cfg m_3rd_channel_cfg = {
	.gain = ADC_GAIN,
	.reference = ADC_REFERENCE,
	.acquisition_time = ADC_ACQUISITION_TIME,
	.channel_id = ADC_TEMPERATURE_ID,
	.input_positive = ADC_3RD_CHANNEL_INPUT,
};

// Get battery voltage in millivolts, return 0 if successful
int get_adc_voltage(uint8_t channel, uint16_t *battery_voltage)
{
	int err;

	const struct adc_sequence sequence = {
		.channels = BIT(channel),
		.buffer = m_sample_buffer,
		.buffer_size = sizeof(m_sample_buffer), // in bytes!
		.resolution = ADC_RESOLUTION,
	};

	if (!adc_dev)
	{
		return -1;
	}

	err = adc_read(adc_dev, &sequence);
	if (err)
	{
		LOG_WRN("ADC read err: %d\n", err);

		return err;
	}

	float sample_value = 0;
	for (int i = 0; i < BUFFER_SIZE; i++)
	{
		sample_value += (float)m_sample_buffer[i];
	}
	//	printk("   buf: %d\n", m_sample_buffer[0] );
	sample_value /= BUFFER_SIZE;
	*battery_voltage = (uint16_t)(sample_value * (INPUT_VOLT_RANGE / VALUE_RANGE_10_BIT));
	//	*battery_voltage = (uint16_t)(sample_value * (INPUT_VOLT_RANGE / VALUE_RANGE_10_BIT) * ((BATVOLT_R1 + BATVOLT_R2) / BATVOLT_R2));

	return 0;
}

// initalize all ADC channels
bool init_adc()
{
	int err;

	adc_dev = DEVICE_DT_GET(ADC_NODE);
	if (!adc_dev)
	{
		LOG_WRN("Error getting adc failed\n");

		return false;
	}

	err = adc_channel_setup(adc_dev, &m_1st_channel_cfg);
	if (err)
	{
		LOG_WRN("Error in adc setup: %d\n", err);

		return false;
	}

	err = adc_channel_setup(adc_dev, &m_2nd_channel_cfg);
	if (err)
	{
		LOG_WRN("Error in adc setup: %d\n", err);

		return false;
	}

	err = adc_channel_setup(adc_dev, &m_3rd_channel_cfg);
	if (err)
	{
		LOG_WRN("Error in adc setup: %d\n", err);

		return false;
	}

	return true;
}
