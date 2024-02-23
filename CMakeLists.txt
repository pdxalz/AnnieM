#
# Copyright (c) 2019-2022 Actinius
#
# SPDX-License-Identifier: BSD-3-Clause
#

cmake_minimum_required(VERSION 3.8.2)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(iss_position)

target_sources(app PRIVATE src/main.c)
target_sources(app PRIVATE src/leds.c)
target_sources(app PRIVATE src/adc.c)
target_sources(app PRIVATE src/health.c)
target_sources(app PRIVATE src/wind_sensor.c)
target_sources(app PRIVATE src/mqtt_connection.c)
target_sources(app PRIVATE src/cameraThread.c)
target_sources(app PRIVATE src/ArducamCamera.c)
target_sources(app PRIVATE src/nrfHal.c)
