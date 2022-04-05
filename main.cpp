/* mbed Microcontroller Library
 * Copyright (c) 2022 ARM Limited
 * SPDX-License-Identifier: Apache-2.0
 */

#include "mbed.h"

BufferedSerial serial_out(USBTX, USBRX);
FileHandle *mbed::mbed_override_console(int fd)
{
    return &serial_out;
}

int main()
{
    printf("Hello World\n");
    return 0;
}