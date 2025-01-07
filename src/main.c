#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/usb/usb_device.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/drivers/uart.h>
#include <arm_math.h>
#include "matrix/matrix.h"

static q7_t a = 0;
static q7_t b = 1;
static q7_t c = 0;

// Create USB Device
static const struct device *const dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

int main (void) {
	//Local variables
	uint32_t dtr = 0;

	//If usb enable failed, return
	if (usb_enable(NULL)) {
		return 0;
	}

	//Wait for console to start up
	/* Poll if the DTR flag was set */
	while (!dtr) {
		uart_line_ctrl_get(dev, UART_LINE_CTRL_DTR, &dtr);
		/* Give CPU resources to low priority threads. */
		k_sleep(K_MSEC(100));
	}

	printk("Hello World! Finished Initializing!\n");
	while (1)
	{
		arm_add_q7(&a,&b,&c,1);
		arm_add_q7(&a,&b,&a,1);
		printk("Sample val: %d\n",c);
		k_msleep(1000);
	}

    return 0;
}