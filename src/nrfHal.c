#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/spi.h>

#include "nrfHal.h"

#define MY_SPI_MASTER DT_NODELABEL(my_spi_master)
#define LED0_NODE DT_ALIAS(led0)
#define CS0_NODE DT_ALIAS(cs0)
#define EXT_PWR_NODE DT_ALIAS(extpwr)
#define TESTPIN_NODE DT_ALIAS(testpin)

// SPI master functionality
const struct device *spi_dev;
static struct k_poll_signal spi_done_sig = K_POLL_SIGNAL_INITIALIZER(spi_done_sig);

struct spi_cs_control spim_cs = {
	.gpio = SPI_CS_GPIOS_DT_SPEC_GET(DT_NODELABEL(reg_my_spi_master)),
	.delay = 0,
};

static const struct gpio_dt_spec spi_cs = GPIO_DT_SPEC_GET(CS0_NODE, gpios);
static const struct gpio_dt_spec ext_pwr = GPIO_DT_SPEC_GET(EXT_PWR_NODE, gpios);
static const struct gpio_dt_spec testpin = GPIO_DT_SPEC_GET(TESTPIN_NODE, gpios);

void spiBegin()
{
	spi_dev = DEVICE_DT_GET(MY_SPI_MASTER);
	if(!device_is_ready(spi_dev)) {
		printk("SPI master device not ready!\n");
	}
	if(!device_is_ready(spim_cs.gpio.port)){
		printk("SPI master chip select device not ready!\n");
	}

	printk("spi initialized\n");    
}

static const struct spi_config spi_cfg = {
	.operation = SPI_WORD_SET(8) | SPI_TRANSFER_MSB |
				 SPI_MODE_CPOL | SPI_MODE_CPHA,
	.frequency = 4000000,
	.slave = 0,
	.cs = &spim_cs,
};

uint8_t spiTransfer(uint8_t val) //  SPI communication sends a byte
{
//	static uint8_t counter = 0;
	static uint8_t tx_buffer[2];
	static uint8_t rx_buffer[2];

	const struct spi_buf tx_buf = {
		.buf = tx_buffer,
		.len = 1
	};
	const struct spi_buf_set tx = {
		.buffers = &tx_buf,
		.count = 1
	};

	struct spi_buf rx_buf = {
		.buf = rx_buffer,
		.len = 1,
	};
	const struct spi_buf_set rx = {
		.buffers = &rx_buf,
		.count = 1
	};

	// Update the TX buffer with a rolling counter
	// tx_buffer[0] = counter++;
	// tx_buffer[1] = counter+42;
//	printk("SPI TX: 0x%.2x, 0x%.2x\n", tx_buffer[0], tx_buffer[1]);
	tx_buffer[0] = val;

	// Reset signal
	k_poll_signal_reset(&spi_done_sig);
	
	// Start transaction
	int error = spi_transceive_signal(spi_dev, &spi_cfg, &tx, &rx, &spi_done_sig);
	if(error != 0){
		printk("SPI transceive error: %i\n", error);
		return error;
	}

	// Wait for the done signal to be raised and log the rx buffer
	int spi_signaled, spi_result;
	do{
		k_poll_signal_check(&spi_done_sig, &spi_signaled, &spi_result);
	} while(spi_signaled == 0);
//	printk("SPI RX: 0x%.2x, 0x%.2x\n", rx_buffer[0], rx_buffer[1]);
	return rx_buffer[0];
	#if 0
	static uint8_t counter = 0;
	static uint8_t tx_buffer[2];
	static uint8_t rx_buffer[2];

	const struct spi_buf tx_buf = {
		.buf = tx_buffer,
		.len = sizeof(tx_buffer)
	};
	const struct spi_buf_set tx = {
		.buffers = &tx_buf,
		.count = 1
	};

	struct spi_buf rx_buf = {
		.buf = rx_buffer,
		.len = sizeof(rx_buffer),
	};
	const struct spi_buf_set rx = {
		.buffers = &rx_buf,
		.count = 1
	};

	// Update the TX buffer with a rolling counter
	tx_buffer[0] = counter++;
	tx_buffer[1] = counter+42;
	printk("SPI TX: 0x%.2x, 0x%.2x\n", tx_buffer[0], tx_buffer[1]);

	// Reset signal
	k_poll_signal_reset(&spi_done_sig);
	
	// Start transaction
	int error = spi_transceive_signal(spi_dev, &spi_cfg, &tx, &rx, &spi_done_sig);
	if(error != 0){
		printk("SPI transceive error: %i\n", error);
		return error;
	}

	// Wait for the done signal to be raised and log the rx buffer
	int spi_signaled, spi_result;
	do{
		k_poll_signal_check(&spi_done_sig, &spi_signaled, &spi_result);
	} while(spi_signaled == 0);
	printk("SPI RX: 0x%.2x, 0x%.2x\n", rx_buffer[0], rx_buffer[1]);
	return 0;
	#endif
}

void spiCsHigh(int pin)        // Set the CS pin of SPI to high level
{
	gpio_pin_set_dt(&spi_cs,0);
}

void spiCsLow(int pin)         // Set the CS pin of SPI to low level
{
	gpio_pin_set_dt(&spi_cs,1);

}

void spiCsOutputMode(int pin)
{
	int ret;

	if (!device_is_ready(spi_cs.port)) {
		return;
	}
	ret = gpio_pin_configure_dt(&spi_cs, GPIO_OUTPUT_ACTIVE);
	if (ret < 0) {
		return;
	}

	if (!device_is_ready(ext_pwr.port)) {
		return;
	}
	ret = gpio_pin_configure_dt(&ext_pwr, GPIO_OUTPUT_ACTIVE);
	if (ret < 0) {
		return;
	}
	
	if (!device_is_ready(testpin.port)) {
		return;
	}
	ret = gpio_pin_configure_dt(&testpin, GPIO_OUTPUT_ACTIVE);
	if (ret < 0) {
		return;
	}	

	printk("gpios initialized\n");
}


void extPwrOn()        // Set the EXT_PWR pin  to high level
{
	gpio_pin_set_dt(&ext_pwr,1);
}

void extPwrOff()         // Set the EXT_PWR pin of SPI to low level
{
	gpio_pin_set_dt(&ext_pwr,0);
}

void testPinOn()        // Set the EXT_PWR pin  to high level
{
	gpio_pin_set_dt(&testpin,1);
}

void testPinOff()         // Set the EXT_PWR pin of SPI to low level
{
	gpio_pin_set_dt(&testpin,0);
}

void delayMs(uint16_t val) //  Delay Ms
{
    k_msleep(val);
}

void delayUs(uint16_t val) // Delay Us
{

}
