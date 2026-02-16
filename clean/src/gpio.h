#ifndef __GPIO_H__
#define __GPIO_H__

#include "types.h"
#include "registers.h"

/*
 * ASM2464PD GPIO Driver
 *
 * Control register at 0xC620 + gpio_num is a mux selector (bits 4:0 only).
 * No pull-down or hi-Z mode. Valid GPIOs: 0-27
 * Input state: 0xC650 + (gpio_num / 8), bit (gpio_num % 8)
 */

#define GPIO_INPUT      0x00
#define GPIO_LOW        0x02
#define GPIO_HIGH       0x03

#define GPIO_NUM_MAX    27

static void gpio_set(uint8_t gpio_num, uint8_t mode) {
    REG_GPIO_CTRL(gpio_num) = mode;
}

static uint8_t gpio_read(uint8_t gpio_num) {
    return (REG_GPIO_INPUT(gpio_num) >> (gpio_num & 7)) & 1;
}

#endif /* __GPIO_H__ */
