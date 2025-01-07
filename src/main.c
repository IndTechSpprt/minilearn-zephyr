#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/time.h>
#include <zephyr/kernel.h>

//Zephyr includes for LED & USB
#include <zephyr/drivers/gpio.h>
#include <zephyr/usb/usb_device.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/drivers/uart.h>

//CMSIS-NN for inference
#include <arm_math.h>
#include <arm_nnfunctions.h>

#include "matrix/matrix.h"

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
    return 0;
}
