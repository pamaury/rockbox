/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 * $Id$
 *
 * adopted for HD300 by Marcin Bukat
 * Copyright (C) 2009 by Bertrik Sikken
 * Copyright (C) 2008 by Robert Kukla 
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
#include "rtc.h" 
#include "i2c-coldfire.h"

/*  Driver for the Seiko S35380A real-time clock chip with i2c interface

    This driver was derived from rtc_s3539a.c and adapted for the MPIO HD300
 */

#define RTC_ADDR   0x60

#define STATUS_REG1     0
#define STATUS_REG2     1
#define REALTIME_DATA1  2
#define REALTIME_DATA2  3
#define INT1_REG        4
#define INT2_REG        5
#define CLOCK_CORR_REG  6
#define FREE_REG        7

/* STATUS_REG1 flags */
#define STATUS_REG1_POC   0x80
#define STATUS_REG1_BLD   0x40
#define STATUS_REG1_INT2  0x20
#define STATUS_REG1_INT1  0x10
#define STATUS_REG1_SC1   0x08
#define STATUS_REG1_SC0   0x04
#define STATUS_REG1_H1224 0x02
#define STATUS_REG1_RESET 0x01


static void reverse_bits(unsigned char* v, int size)
{
    static const unsigned char flipnibble[] =
        {0x00, 0x08, 0x04, 0x0C, 0x02, 0x0A, 0x06, 0x0E,
         0x01, 0x09, 0x05, 0x0D, 0x03, 0x0B, 0x07, 0x0F};
    int i;
                                            
    for (i = 0; i < size; i++) {
        v[i] = (flipnibble[v[i] & 0x0F] << 4) |
                flipnibble[(v[i] >> 4) & 0x0F];
    }
}    

void rtc_init(void)
{
    unsigned char status_reg;
    i2c_read(I2C_IFACE_1, RTC_ADDR | (STATUS_REG1<<1), &status_reg, 1);

    if ( (status_reg & STATUS_REG1_POC) ||
         (status_reg & STATUS_REG1_BLD) )
    {
        /* perform rtc reset*/
        status_reg |= STATUS_REG1_RESET;
        i2c_write(I2C_IFACE_1, RTC_ADDR | (STATUS_REG1<<1), &status_reg, 1);
    }
}

int rtc_read_datetime(struct tm *tm)
{
    unsigned char buf[7];
    unsigned int i;
    int ret;

    ret = i2c_read(I2C_IFACE_1, RTC_ADDR | (REALTIME_DATA1<<1), buf, sizeof(buf));
    reverse_bits(buf, sizeof(buf));

    buf[4] &= 0x3f; /* mask out p.m. flag */

    for (i = 0; i < sizeof(buf); i++)
        buf[i] = BCD2DEC(buf[i]);

    tm->tm_sec = buf[6];
    tm->tm_min = buf[5];
    tm->tm_hour = buf[4];
    tm->tm_wday = buf[3];
    tm->tm_mday = buf[2];
    tm->tm_mon = buf[1] - 1;
    tm->tm_year = buf[0] + 100;
    
    return ret;
}

int rtc_write_datetime(const struct tm *tm)
{
    unsigned char buf[7];
    unsigned int i;
    int ret;

    buf[6] = tm->tm_sec;
    buf[5] = tm->tm_min;
    buf[4] = tm->tm_hour;
    buf[3] = tm->tm_wday;
    buf[2] = tm->tm_mday;
    buf[1] = tm->tm_mon + 1;
    buf[0] = tm->tm_year - 100;

    for (i = 0; i < sizeof(buf); i++)
        buf[i] = DEC2BCD(buf[i]);

    reverse_bits(buf, sizeof(buf));
    ret = i2c_write(I2C_IFACE_1, RTC_ADDR | (REALTIME_DATA1<<1), buf, sizeof(buf));

    return ret;
}

