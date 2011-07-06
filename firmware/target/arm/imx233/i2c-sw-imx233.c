/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 * $Id$
 *
 * Copyright (C) 2011 by amaury Pouly
 *
 * Based on Rockbox iriver bootloader by Linus Nielsen Feltzing
 * and the ipodlinux bootloader by Daniel Palffy and Bernard Leach
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY
 * KIND, either express or implied.
 *
 ****************************************************************************/
#include "config.h"
#include "system.h"
#include "kernel.h"
#include "dma-imx233.h"
#include "i2c-imx233.h"
#include "pinctrl-imx233.h"

static void i2c_delay(void)
{
    udelay(30);
}

/* release SCL and read it */
static bool i2c_read_scl(void)
{
    imx233_enable_gpio_output(0, 30, false);
    return !!imx233_get_gpio_input_mask(0, 1 << 30);
}

/* release SDA and read it */
static bool i2c_read_sda(void)
{
    imx233_enable_gpio_output(0, 31, false);
    return !!imx233_get_gpio_input_mask(0, 1 << 31);
}

/* drive SCL to low */
static void i2c_clear_scl(void)
{
    imx233_enable_gpio_output(0, 30, true);
}

/* drive SDA to low */
static void i2c_clear_sda(void)
{
    imx233_enable_gpio_output(0, 31, true);
}

void imx233_i2c_init(void)
{
    /* setup pins (assume external pullups) */
    imx233_enable_gpio_output(0, 30, false);
    imx233_enable_gpio_output(0, 31, false);
    imx233_set_gpio_output(0, 30, false);
    imx233_set_gpio_output(0, 31, false);
}

/********************************************************/

static void i2c_do_start(bool restart)
{
    if(restart)
    {
        /* release sda (which should be high) */
        i2c_read_sda();
        /* wait */
        i2c_delay();
        /* clock stretch */
        while(!i2c_read_scl());
    }
    /* drive sda (START) */
    i2c_clear_sda();
    /* wait */
    i2c_delay();
    /* drive scl */
    i2c_clear_scl();
}

static void i2c_do_stop(void)
{
    /* drive sda */
    i2c_clear_sda();
    /* wait */
    i2c_delay();
    /* clock stretch */
    while(!i2c_read_scl());
    /* clear sda */
    i2c_read_sda();
    /* wait (bus free) */
    i2c_delay();
}

static void i2c_write_bit(bool b)
{
    /* drive/clear sda */
    if(b)
        i2c_read_sda();
    else
        i2c_clear_sda();
    /* wait */
    i2c_delay();
    /* clock stretch */
    while(!i2c_read_scl());
    /* wait */
    i2c_delay();
    /* drive scl */
    i2c_clear_scl();
}

static bool i2c_read_bit(void)
{
    /* release sda to let the device drive it */
    i2c_read_sda();
    /* wait */
    i2c_delay();
    /* clock stretch */
    while(!i2c_read_scl());
    /* read */
    bool b = i2c_read_sda();
    /* wait */
    i2c_delay();
    /* drive scl */
    i2c_clear_scl();
    return b;
}

/* return ack */
static bool i2c_write_byte(uint8_t b)
{
    /* MSB first */
    for(int bit = 0; bit < 8; bit++)
    {
        i2c_write_bit(!!(b & 0x80));
        b <<= 1;
    }
    return i2c_read_bit();
}

static uint8_t i2c_read_byte(bool nak)
{
    uint8_t v = 0;
    /* MSB first */
    for(int bit = 0; bit < 8; bit++)
        v = v << 1 | i2c_read_bit();
    i2c_write_bit(nak);
    return v;
}

int i2c_readmem(int device, int address, unsigned char* buf, int count)
{
    i2c_do_start(false);
    if(i2c_write_byte(device)) return -1;
    if(i2c_write_byte(address)) return -2;
    i2c_do_start(true);
    if(i2c_write_byte(device | 1)) return -3;
    while(count-- > 0)
        *buf++ = i2c_read_byte(count == 0);
    i2c_do_stop();
    return 0;
}

int i2c_writemem(int device, int address, const unsigned char* buf, int count)
{
    i2c_do_start(false);
    if(i2c_write_byte(device)) return -1;
    if(i2c_write_byte(address)) return -2;
    while(count-- > 0)
        if(i2c_write_byte(*buf++))
            return -4;
    i2c_do_stop();
    return 0;
}
