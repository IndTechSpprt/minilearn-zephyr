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

//Custom libraries

#include "matrix/matrix.h"
#include "network_defs/uwnet.h"
#include "utils/cifar_train_data.h"
#include "utils/cifar_test_data.h"
#include "network_defs/parameters.h"
#include "network_defs/weights.h"

// Create USB Device
static const struct device *const dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

#define CLASSES 3

// Number of filter to keep (the rest are prunned)
#define numberOFilters 16

// Buffers using during inference
static q7_t conv3_w_2[numberOFilters * CONV3_IM_CH * CONV3_KER_DIM * CONV3_KER_DIM] = {0};
static q7_t conv3_b_2[numberOFilters] = {0};
static q7_t conv3_out[numberOFilters * CONV3_OUT_DIM * CONV3_OUT_DIM];
static q7_t pool3_out[numberOFilters * POOL3_OUT_DIM * POOL3_OUT_DIM];
static q7_t interface_out[INTERFACE_OUT];
static q7_t linear_out[LINEAR_OUT];
static q7_t y_out[INTERFACE_OUT];

// declare the inference functions (CMSIS-NN)
uint32_t network(q7_t *input);
uint32_t network2(q7_t *input);

// Buffers to store intermittend results
static q7_t conv1_out[CONV1_OUT_CH * CONV1_OUT_DIM * CONV1_OUT_DIM];
static q7_t pool1_out[CONV1_OUT_CH * POOL1_OUT_DIM * POOL1_OUT_DIM];
static q7_t conv2_out[CONV2_OUT_CH * CONV2_OUT_DIM * CONV2_OUT_DIM];
static q7_t pool2_out[CONV2_OUT_CH * POOL2_OUT_DIM * POOL2_OUT_DIM];
static q15_t conv_buffer[MAX_CONV_BUFFER_SIZE];
static q15_t fc_buffer[MAX_FC_BUFFER];

// Buffer to store old and learning weights
static q7_t conv1_w[CONV1_WT_SHAPE] = CONV1_WT;
static q7_t conv1_b[CONV1_BIAS_SHAPE] = CONV1_BIAS;
static q7_t conv2_w[CONV2_WT_SHAPE] = CONV2_WT;
static q7_t conv2_b[CONV2_BIAS_SHAPE] = CONV2_BIAS;
static q7_t conv3_w[CONV3_WT_SHAPE] = CONV3_WT;
static q7_t conv3_b[CONV3_BIAS_SHAPE] = CONV3_BIAS;
static q7_t interface_w[INTERFACE_WT_SHAPE] = INTERFACE_WT;
static q7_t interface_b[INTERFACE_BIAS_SHAPE] = INTERFACE_BIAS;
static q7_t linear_w[LINEAR_WT_SHAPE] = LINEAR_WT;
static q7_t linear_b[LINEAR_BIAS_SHAPE] = LINEAR_BIAS;

// batch size for learning
static int batch = 8;

// helper functions for measureing l1-2 norm of Ws
void insert(int item, float itprio);
void del();
void display();

struct node
{
    float priority;
    int info;
    struct node *next;
} *start = NULL, *q, *temp, *new;

typedef struct node N;

uint32_t network(q7_t *input)
{

    arm_convolve_HWC_q7_RGB(input, CONV1_IM_DIM, CONV1_IM_CH, conv1_w,
                            CONV1_OUT_CH, CONV1_KER_DIM, CONV1_PADDING, CONV1_STRIDE,
                            conv1_b, CONV1_BIAS_LSHIFT,
                            CONV1_OUT_RSHIFT, conv1_out, CONV1_OUT_DIM,
                            conv_buffer, NULL);

    arm_maxpool_q7_HWC(conv1_out, POOL1_IM_DIM, POOL1_IM_CH, POOL1_KER_DIM, POOL1_PADDING,
                       POOL1_STRIDE, POOL1_OUT_DIM, (q7_t *)conv_buffer, pool1_out);
    arm_relu_q7(pool1_out, POOL1_OUT_DIM * POOL1_OUT_DIM * CONV1_OUT_CH);

    arm_convolve_HWC_q7_fast(pool1_out, CONV2_IM_DIM, CONV2_IM_CH, conv2_w, CONV2_OUT_CH, CONV2_KER_DIM,
                             CONV2_PADDING, CONV2_STRIDE, conv2_b, CONV2_BIAS_LSHIFT, CONV2_OUT_RSHIFT, conv2_out,
                             CONV2_OUT_DIM, conv_buffer, NULL);

    arm_maxpool_q7_HWC(conv2_out, POOL2_IM_DIM, POOL2_IM_CH, POOL2_KER_DIM, POOL2_PADDING, POOL2_STRIDE, POOL2_OUT_DIM, (q7_t *)conv_buffer, pool2_out);
    arm_relu_q7(pool2_out, POOL2_OUT_DIM * POOL2_OUT_DIM * CONV2_OUT_CH);

    arm_convolve_HWC_q7_fast(pool2_out, CONV3_IM_DIM, CONV3_IM_CH, conv3_w, CONV3_OUT_CH, CONV3_KER_DIM,
                             CONV3_PADDING, CONV3_STRIDE, conv3_b, CONV3_BIAS_LSHIFT, CONV3_OUT_RSHIFT, conv3_out,
                             CONV3_OUT_DIM, conv_buffer, NULL);
    arm_maxpool_q7_HWC(conv3_out, POOL3_IM_DIM, POOL3_IM_CH, POOL3_KER_DIM, POOL3_PADDING, POOL3_STRIDE, POOL3_OUT_DIM, (q7_t *)conv_buffer, pool3_out);
    arm_relu_q7(pool3_out, POOL3_OUT_DIM * POOL3_OUT_DIM * CONV3_OUT_CH);
    arm_fully_connected_q7_opt(pool3_out, interface_w, INTERFACE_DIM, INTERFACE_OUT, INTERFACE_BIAS_LSHIFT, INTERFACE_OUT_RSHIFT, interface_b,
                               interface_out, fc_buffer);

    arm_relu_q7(interface_out, INTERFACE_OUT);

    arm_fully_connected_q7_opt(interface_out, linear_w, LINEAR_DIM, LINEAR_OUT, LINEAR_BIAS_LSHIFT, LINEAR_OUT_RSHIFT, linear_b,
                               linear_out, fc_buffer);

    arm_softmax_q7(linear_out, LINEAR_OUT, y_out);
    uint32_t class = 0;
    uint32_t blockSize = sizeof(y_out);
    for (int i = 0; i < 10; i++)
    {
        if (y_out[i] > y_out[class])
            class = i;
    }
    return class;
}

uint32_t network2(q7_t *input)
{

    arm_convolve_HWC_q7_RGB(input, CONV1_IM_DIM, CONV1_IM_CH, conv1_w,
                            CONV1_OUT_CH, CONV1_KER_DIM, CONV1_PADDING, CONV1_STRIDE,
                            conv1_b, CONV1_BIAS_LSHIFT,
                            CONV1_OUT_RSHIFT, conv1_out, CONV1_OUT_DIM,
                            conv_buffer, NULL);

    arm_maxpool_q7_HWC(conv1_out, POOL1_IM_DIM, POOL1_IM_CH, POOL1_KER_DIM, POOL1_PADDING,
                       POOL1_STRIDE, POOL1_OUT_DIM, (q7_t *)conv_buffer, pool1_out);
    arm_relu_q7(pool1_out, POOL1_OUT_DIM * POOL1_OUT_DIM * CONV1_OUT_CH);

    arm_convolve_HWC_q7_fast(pool1_out, CONV2_IM_DIM, CONV2_IM_CH, conv2_w, CONV2_OUT_CH, CONV2_KER_DIM,
                             CONV2_PADDING, CONV2_STRIDE, conv2_b, CONV2_BIAS_LSHIFT, CONV2_OUT_RSHIFT, conv2_out,
                             CONV2_OUT_DIM, conv_buffer, NULL);

    arm_maxpool_q7_HWC(conv2_out, POOL2_IM_DIM, POOL2_IM_CH, POOL2_KER_DIM, POOL2_PADDING, POOL2_STRIDE, POOL2_OUT_DIM, (q7_t *)conv_buffer, pool2_out);
    arm_relu_q7(pool2_out, POOL2_OUT_DIM * POOL2_OUT_DIM * CONV2_OUT_CH);

    // convolution layer 3
    arm_convolve_HWC_q7_fast(pool2_out, CONV3_IM_DIM, CONV3_IM_CH, conv3_w_2, numberOFilters, CONV3_KER_DIM,
                             CONV3_PADDING, CONV3_STRIDE, conv3_b_2, CONV3_BIAS_LSHIFT, CONV3_OUT_RSHIFT, conv3_out,
                             CONV3_OUT_DIM, conv_buffer, NULL);

    return 0;
}


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

    int filterSize = CONV3_IM_CH * CONV3_KER_DIM * CONV3_KER_DIM;

    for (int j = 0; j < CONV3_OUT_CH; j++)
    {
        float l1norm = 0, l2norm = 0;
        float maxNorm = 0;
        for (int index = j * filterSize; index < filterSize * (j + 1); index++)
        {
            float val = (float)(conv3_w[index]) / powf(2, CONV3_WEIGHT_Q);
            l1norm += fabs(val);
            l2norm += val * val;
            if (fabs(val) > maxNorm)
                maxNorm = fabs(val);
        }
        l2norm = sqrtf(l2norm);
        printf("filter: %d , l1 norm : %f, l2 norm : %f , max norm : %f \n", j, l1norm, l2norm, maxNorm);
        insert(j, l2norm);
    }
    
	    struct timeval t1;
    gettimeofday(&t1, NULL);
    srand(t1.tv_usec * t1.tv_sec);

    uint32_t index = 0;
    int PoolOut = POOL2_OUT_DIM * POOL2_OUT_DIM * CONV2_OUT_CH;

    float rate = .0001;
    float momentum = .9;
    float decay = .00001;
    int correct = 0;

    data training_data;
    matrix q_X0 = make_matrix(NODE_0_TOTAL_TRAIN_IMAGES, PoolOut);
    matrix q_Y0 = make_matrix(NODE_0_TOTAL_TRAIN_IMAGES, CLASSES);

    // get the results of the last layers (and dequantized int -> float)
    //in practice you dequantize only during training 
    
    float scale = 1 << CONV2_OUT_Q;

	
    for (int img_row = 0; img_row < NODE_0_TOTAL_TRAIN_IMAGES; img_row++)
    {
        index = network(NODE_0_TRAIN_IMAGES[img_row]);

        for (int i = 0; i < PoolOut; i++)
        {
            q_X0.data[img_row * q_X0.cols + i] = (float)pool2_out[i] / scale;
        }
        q_Y0.data[(img_row * q_Y0.cols) + NODE_0_TRAIN_LABELS[img_row]] = 1;
    }

    training_data.x = q_X0;
    training_data.y = q_Y0;

    matrix X_test = make_matrix(TOTAL_TEST_IMAGES, PoolOut);
    matrix Y_test = make_matrix(TOTAL_TEST_IMAGES, CLASSES);

    for (int img_i = 0; img_i < TOTAL_TEST_IMAGES; img_i++)
    {
        index = network(TEST_IMAGES[img_i]);
        if (TEST_LABELS[img_i] == (q7_t)index)
        {
            correct++;
        }
        for (int i = 0; i < PoolOut; i++)
        {
            X_test.data[img_i * X_test.cols + i] = (float)pool2_out[i] / powf(2, CONV2_OUT_Q);
        }
        Y_test.data[(img_i * Y_test.cols) + TEST_LABELS[img_i]] = 1;
    }

    data test;
    test.x = X_test;
    test.y = Y_test;

    int iterations = NODE_0_TOTAL_TRAIN_IMAGES / batch;
    static net active_learning = {0};
    active_learning.n = 7;
    active_learning.layers = calloc(7, sizeof(layer));
    active_learning.layers[0] = make_convolutional_layer(CONV3_IM_DIM, CONV3_IM_DIM, CONV3_IM_CH, numberOFilters, CONV3_KER_DIM, CONV3_STRIDE);

    // calcualte the l2 norm of filters
    temp = start;
    for (int filter = 0; filter < numberOFilters; filter++)
    {
        if (temp != NULL)
            printf("%d norm =%f\n", temp->info, temp->priority);
        temp = temp->next;
    }

    // we pruned base on l2 nrom
    temp = start;
    for (int filter = 0; filter < numberOFilters; filter++)
    {
        int startIndex = temp->info;
        temp = temp->next;
        int i = 0;
        for (int index = startIndex * filterSize; index < filterSize * (startIndex + 1); index++)
        {
            active_learning.layers[0].w.data[filter * filterSize + i] = (float)(conv3_w[index]) / powf(2, CONV3_WEIGHT_Q);
            i++;
        }
    }
    //select the filters and dequantize
    temp = start;
    for (int index = 0; index < numberOFilters; index++)
    {
        int filter_bias = temp->info;
        temp = temp->next;
        active_learning.layers[0].b.data[index] = (float)conv3_b[filter_bias] / powf(2, CONV3_BIAS_Q);
    }

    // free the filter indexes
    while (start != NULL)
    {
        temp = start;
        start = start->next;
        free(temp);
    }

    // define the network for learning
    active_learning.layers[1] = make_batchnorm_layer(numberOFilters);
    active_learning.layers[2] = make_activation_layer(RELU);
    int convout = numberOFilters * CONV3_OUT_DIM * CONV3_OUT_DIM;
    active_learning.layers[3] = make_connected_layer(convout, INTERFACE_OUT);
    active_learning.layers[3].freeze = 1;

    // copy the values
    for (int index = 0; index < convout * INTERFACE_OUT; index++)
    {
        active_learning.layers[3].w.data[index] = (float)interface_w[index] / powf(2, INTERFACE_WEIGHT_Q);
    }

    for (int index = 0; index < INTERFACE_BIAS_SHAPE; index++)
    {
        active_learning.layers[3].b.data[index] = (float)interface_b[index] / powf(2, INTERFACE_BIAS_Q);
    }

    active_learning.layers[4] = make_activation_layer(RELU);
    active_learning.layers[5] = make_connected_layer(INTERFACE_OUT, CLASSES);
    active_learning.layers[5].freeze = 1;

    for (int index = 0; index < LINEAR_WT_SHAPE; index++)
    {    
        active_learning.layers[5].w.data[index] = (float)linear_w[index] / powf(2, LINEAR_WEIGHT_Q);
    }

    for (int index = 0; index < LINEAR_BIAS_SHAPE; index++)
    {
        active_learning.layers[5].b.data[index] = (float)linear_b[index] / powf(2, LINEAR_BIAS_Q);
    }
    active_learning.layers[6] = make_activation_layer(SOFTMAX);

    printf("-----------------------------------------------------------------------------\n");

    for (int index = 0; index < filterSize * numberOFilters; index++)
    {
        conv3_w_2[index] = (int)ceilf(active_learning.layers[0].w.data[index] * powf(2, CONV3_WEIGHT_Q));
    }

    for (int index = 0; index < numberOFilters; index++)
    {
        conv3_b_2[index] = (int)ceilf(active_learning.layers[0].b.data[index] * powf(2, CONV3_BIAS_Q));
    }

    PoolOut = numberOFilters * CONV3_OUT_DIM * CONV3_OUT_DIM;
    active_learning.n = 4;
    active_learning.layers[0] = make_connected_layer(PoolOut, INTERFACE_OUT);
    active_learning.layers[0].freeze = 0;
    active_learning.layers[1] = make_activation_layer(RELU);
    active_learning.layers[2] = make_connected_layer(INTERFACE_OUT, CLASSES);
    active_learning.layers[2].freeze = 0;
    active_learning.layers[3] = make_activation_layer(SOFTMAX);

    data training_data2;

    matrix q_X = make_matrix(NODE_0_TOTAL_TRAIN_IMAGES, PoolOut);
    matrix q_Y = make_matrix(NODE_0_TOTAL_TRAIN_IMAGES, CLASSES);


    for (int img_row = 0; img_row < NODE_0_TOTAL_TRAIN_IMAGES; img_row++)
    {
        index = network2(NODE_0_TRAIN_IMAGES[img_row]);
        for (int i = 0; i < PoolOut; i++)
        {
            q_X.data[img_row * q_X.cols + i] = (float)conv3_out[i] / powf(2, CONV3_OUT_Q);
        }
        q_Y.data[(img_row * q_Y.cols) + NODE_0_TRAIN_LABELS[img_row]] = 1;
    }

    training_data2.x = q_X;
    training_data2.y = q_Y;

    matrix X_test2 = make_matrix(TOTAL_TEST_IMAGES, PoolOut);
    matrix Y_test2 = make_matrix(TOTAL_TEST_IMAGES, CLASSES);

    for (int img_i = 0; img_i < TOTAL_TEST_IMAGES; img_i++)
    {
        index = network2(TEST_IMAGES[img_i]);
        for (int i = 0; i < PoolOut; i++)
        {
            X_test2.data[img_i * X_test2.cols + i] = (float)conv3_out[i] / powf(2, CONV3_OUT_Q);
        }
        Y_test2.data[(img_i * Y_test2.cols) + TEST_LABELS[img_i]] = 1;
    }

    data test2;
    test2.x = X_test2;
    test2.y = Y_test2;

    float test_acc = accuracy_net(active_learning, test2);
    printf("test acc = %f\n", test_acc);

    for (int epoch = 1; epoch < 25; epoch++)
    {
        train_image_classifier(active_learning, training_data2, batch, iterations, rate, momentum, decay);
        float train_acc = accuracy_net(active_learning, training_data2);
        printf("%d :: train acc = %f & ", epoch, train_acc);
        float test_acc = accuracy_net(active_learning, test2);
        printf("test acc = %f\n", test_acc);
    }

    return 0;
}


void insert(int item, float itprio)
{
    // int item, itprio;
    new = (N *)malloc(sizeof(N));

    new->info = item;
    new->priority = itprio;
    new->next = NULL;
    if (start == NULL)
    {
        start = new;
    }
    else if (start != NULL && itprio < start->priority)
    {
        new->next = start;
        start = new;
    }
    else
    {
        q = start;
        while (q->next != NULL && q->next->priority < itprio)
        {
            q = q->next;
        }
        new->next = q->next;
        q->next = new;
    }
}