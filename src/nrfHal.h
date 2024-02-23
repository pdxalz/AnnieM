#ifndef __NRFHAL_H
#define __NRFHAL_H
#include <zephyr/kernel.h>

#define arducamSpiBegin()        spiBegin()
#define arducamSpiTransfer(val)  spiTransfer(val) //  SPI communication sends a byte
#define arducamSpiCsPinHigh(pin) spiCsHigh(pin)        // Set the CS pin of SPI to high level
#define arducamSpiCsPinLow(pin)  spiCsLow(pin)         // Set the CS pin of SPI to low level
#define arducamCsOutputMode(pin) spiCsOutputMode(pin)
#define arducamDelayMs(val)      delayMs(val) //  Delay Ms
#define arducamDelayUs(val)      delayUs(val) // Delay Us


void spiBegin();
uint8_t spiTransfer(uint8_t val);
void spiCsHigh(int pin);
void spiCsLow(int pin);
void spiCsOutputMode(int pin);
void delayMs(uint16_t val);
void delayUs(uint16_t val);

#endif /*__NRFHAL_H*/
