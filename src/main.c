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

#define REPEAT_NUM 3

#define AVGPOOLING_2_OUT_CH		5
#define AVGPOOLING_2_IN_CH		5
#define AVGPOOLING_2_INPUT_W		12
#define AVGPOOLING_2_INPUT_H		1
#define AVGPOOLING_2_DST_SIZE		60
#define AVGPOOLING_2_INPUT_SIZE		60
#define AVGPOOLING_2_OUT_ACTIVATION_MIN -128
#define AVGPOOLING_2_OUT_ACTIVATION_MAX 127
#define AVGPOOLING_2_INPUT_BATCHES	1
#define AVGPOOLING_2_FILTER_X		3
#define AVGPOOLING_2_FILTER_Y		1
#define AVGPOOLING_2_STRIDE_X		1
#define AVGPOOLING_2_STRIDE_Y		2
#define AVGPOOLING_2_PAD_X		1
#define AVGPOOLING_2_PAD_Y		0
#define AVGPOOLING_2_OUTPUT_W		12
#define AVGPOOLING_2_OUTPUT_H		1

const int8_t avgpooling_2_input[60] = {
	-82, -104, 10,	-28, -52, -51, -66, 52,	 124, -74, -21,	 4,  37,   -7,	-33,
	102, 110,  24,	52,  121, 13,  -55, -79, -92, -35, -103, 86, 95,   46,	32,
	-24, -123, 120, 29,  -77, -97, -69, -68, 58,  38,  3,	 3,  79,   -47, 112,
	-52, -113, -46, 107, 68,  83,  -70, 91,	 14,  113, 74,	 73, -103, -98, 25};

const int8_t avgpooling_2_output_ref[60] = {
	-67, -85, 31, 48,  -63, -51, -55, 33,  30, -53, 10,  16,  38,  56,  5,
	31,  20,  -6, -16, 18,	4,   47,  13,  2,  39,	-38, -31, 45,  -6,  -27,
	-75, -35, 49, 44,  -2,	-39, -63, 44,  13, 24,	-49, -60, -12, 39,  73,
	11,  -60, 41, 25,  98,	35,  -37, -19, 8,  69,	79,  2,	  -6,  -42, 69};

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

	int8_t output[AVGPOOLING_2_DST_SIZE] = {0};

	cmsis_nn_context ctx;
	cmsis_nn_pool_params pool_params;
	cmsis_nn_dims input_dims;
	cmsis_nn_dims filter_dims;
	cmsis_nn_dims output_dims;

	input_dims.n = AVGPOOLING_2_INPUT_BATCHES;
	input_dims.w = AVGPOOLING_2_INPUT_W;
	input_dims.h = AVGPOOLING_2_INPUT_H;
	input_dims.c = AVGPOOLING_2_IN_CH;
	filter_dims.w = AVGPOOLING_2_FILTER_X;
	filter_dims.h = AVGPOOLING_2_FILTER_Y;
	output_dims.w = AVGPOOLING_2_OUTPUT_W;
	output_dims.h = AVGPOOLING_2_OUTPUT_H;
	output_dims.c = AVGPOOLING_2_OUT_CH;

	pool_params.padding.w = AVGPOOLING_2_PAD_X;
	pool_params.padding.h = AVGPOOLING_2_PAD_Y;
	pool_params.stride.w = AVGPOOLING_2_STRIDE_X;
	pool_params.stride.h = AVGPOOLING_2_STRIDE_Y;

	pool_params.activation.min = AVGPOOLING_2_OUT_ACTIVATION_MIN;
	pool_params.activation.max = AVGPOOLING_2_OUT_ACTIVATION_MAX;

	ctx.size = arm_avgpool_s8_get_buffer_size(AVGPOOLING_2_OUTPUT_W, AVGPOOLING_2_IN_CH);
	ctx.buf = malloc(ctx.size);

	arm_cmsis_nn_status result = arm_avgpool_s8(&ctx,
						   &pool_params,
						   &input_dims,
						   avgpooling_2_input,
						   &filter_dims,
						   &output_dims,
						   output);

	free(ctx.buf);

	bool first_run = true;

	while (1)
	{
		if (first_run) {
			for (int i = 0; i < 60; i++){
				printk("Element: %d \n",i);
				printk("Expected: %d, Result %d: \n", avgpooling_2_output_ref[i], output[i]);
				k_msleep(500);
			}
			first_run = false;
		}
	}
	

    return 0;
}
