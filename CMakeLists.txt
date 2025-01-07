cmake_minimum_required(VERSION 3.20.0)
set(BOARD teensy41)

find_package(Zephyr)
project(my_zephyr_app)

target_sources(app PRIVATE 
src/main.c
src/matrix/matrix.c
src/network_defs/activation_layer.c
src/network_defs/batchnorm_layer.c
src/network_defs/classifier.c
src/network_defs/connected_layer.c
src/network_defs/convolutional_layer.c
src/network_defs/net.c
src/utils/image.c
src/utils/list.c
)