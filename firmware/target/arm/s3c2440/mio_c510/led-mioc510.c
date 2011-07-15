/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 * $Id$
 *
 * Copyright (c) 2011 by Amaury Pouly
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
#include "cpu.h"
#include "kernel.h"
#include "led-mioc510.h"

/* LED functions for debug */

void led_init(void)
{
    S3C2440_GPIO_CONFIG(GPGCON, 11, GPIO_OUTPUT);
}

/* Turn on one or more LEDS */
void set_leds(int led_mask)
{
    GPGDAT |= led_mask;
}

/* Turn off one or more LEDS */
void clear_leds(int led_mask)
{
    GPGDAT &= ~led_mask;
}

/* Alternate flash pattern1 and pattern2 */ 
/* Never returns */
void led_flash(int led_pattern1, int led_pattern2)
{   
    while (1)
    {
        set_leds(led_pattern1);
        sleep(HZ/2);
        clear_leds(led_pattern1);
        
        set_leds(led_pattern2);
        sleep(HZ/2);
        clear_leds(led_pattern2);
    }
}
