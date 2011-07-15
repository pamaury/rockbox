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
#ifndef _LED_MIOC510_H_
#define _LED_MIOC510_H_

/* LED functions for debug etc */

/* Charging led has two states: orange and green;
 * we use the convention off=orange, on=green */
#define LED_CHARGING    (1 << 11)      /* GPG11 */

#define LED_NONE    0x0000
#define LED_ALL     LED_CHARGING

void led_init(void);

/* Turn on */
void set_leds(int led_mask);

/* Turn off */
void clear_leds(int led_mask);

/* Alternate flash of pattern1 and pattern2 - never returns */
void led_flash(int led_pattern1, int led_pattern2);

#endif /* _LED_MIOC510_H_ */
