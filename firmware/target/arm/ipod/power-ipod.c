/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 * $Id$
 *
 * Copyright (C) 2002 by Linus Nielsen Feltzing
 *
 * All files in this archive are subject to the GNU General Public License.
 * See the file COPYING in the source tree root for full license agreement.
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY
 * KIND, either express or implied.
 *
 ****************************************************************************/
#include "config.h"
#include "cpu.h"
#include <stdbool.h>
#include "adc.h"
#include "kernel.h"
#include "system.h"
#include "power.h"
#include "logf.h"
#include "pcf50605.h"
#include "usb.h"
#include "lcd.h"

void power_init(void)
{
#ifdef IPOD_1G2G  /* probably also 3rd gen */
    GPIOC_ENABLE |= 0x40;      /* GPIO C6 is HDD power (low active) */
    GPIOC_OUTPUT_VAL &= ~0x40; /* on by default */
    GPIOC_OUTPUT_EN |= 0x40;   /* enable output */
#else
    pcf50605_init();
#endif
}

bool charger_inserted(void)
{
#ifdef IPOD_VIDEO
    return (GPIOL_INPUT_VAL & 0x08)?false:true;
#else
    /* This needs filling in for other ipods. */
    return false;
#endif
}

/* Returns true if the unit is charging the batteries. */
bool charging_state(void) {
    return (GPIOB_INPUT_VAL & 0x01)?false:true;
}


void ide_power_enable(bool on)
{
#ifdef IPOD_1G2G /* probably also 3rd gen */
    if (on)
        GPIOC_OUTPUT_VAL &= ~0x40;
    else
        GPIOC_OUTPUT_VAL |= 0x40;
#else
    /* We do nothing on other iPods yet */
    (void)on;
#endif
}

bool ide_powered(void)
{
#ifdef IPOD_1G2G  /* probably also 3rd gen */
    return !(GPIOC_OUTPUT_VAL & 0x40);
#else
    /* pretend we are always powered - we don't turn it off on the ipod */
    return true;
#endif
}

void power_off(void)
{
#if defined(HAVE_LCD_COLOR)
    /* Clear the screen and backdrop to
    remove ghosting effect on shutdown */
    lcd_set_backdrop(NULL);
    lcd_set_background(LCD_WHITE);
    lcd_clear_display();
    lcd_update();
    sleep(HZ/16);
#endif

#ifndef BOOTLOADER
#ifdef IPOD_1G2G
    /* we cannot turn off the 1st gen/ 2nd gen yet. Need to figure out sleep mode. */
    system_reboot();
#else
    /* We don't turn off the ipod, we put it in a deep sleep */
    pcf50605_standby_mode();
#endif
#endif
}
