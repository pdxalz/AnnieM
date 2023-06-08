#ifndef _ADC_H_
#define _ADC_H_

/**
 * @brief Take a temperature sample.
 *
 * Value will either be a real sensor value taken from a sensor with device tree alias temp_sensor,
 * (if CONFIG_TEMP_DATA_USE_SENSOR is set) or will otherwise be a pseudorandom dummy reading.
 *
 * @param[out] temp - Pointer to the double to be filled with the taken temperature sample.
 * @return int - 0 on success, otherwise, negative error code.
 */
int get_battery_voltage(uint16_t *battery_voltage);
bool init_adc();

#endif /* _ADC_H_ */
