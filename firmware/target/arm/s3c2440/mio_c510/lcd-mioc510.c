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
#include <string.h>
#include "cpu.h"
#include "system.h"
#include "backlight-target.h"
#include "lcd.h"

static void wait(unsigned int t) __attribute__((naked));

static void wait(unsigned int t)
{
    asm volatile(
        "    movs    r0, r0 \n\
        0: \n\
            bxeq    lr \n\
            ldr     r2, =0x4D2 \n\
        1: \n\
            subs    r2, #1 \n\
            bne     1b \n\
            subs    r0, #1 \n\
            b       0b");
}

void lcd_init_device(void)
{
    /* vsync, hsync, vclk, vd[0:15], vden, lend, lcd_pwren */
    
    GPGCON &= ~0xc00000;// GPG11 as input
    wait(0x32);
    GPCUP = 0x0000ff03;// all led pins
    GPCCON &= 0x3FFC03;
    GPCCON |= 0xAA8002A8;
    GPDUP = 0x00000000ff;
    GPDCON &= 0x3F000F;
    GPDCON |= 0xAA80AAA0;
    GPBUP = 0xffffffff;
    GPBCON = (GPBCON & ~3) | 1;
    GPBDAT &= ~1;
    GPJDAT &= ~8;
    GPJDAT |= 4;
    GPJUP = 0xffff;
    GPJCON &= ~0xCF0;
    GPJCON |= 0x50;
    GPJDAT &= ~4;
    wait(1);
    GPJDAT |= 8;
    wait(1);
    /*
    INTMSK |= 0x400;
    SRCPND = 0x400;
    INTPND = 0x400;
    */
    TCFG0 &= ~0xff;
    TCFG0 |= 0x20;
    TCFG1 &= ~0xf;
    TCNTB0 = 0x2FF;
    TCMPB0 = 0xE6;
    TCON &= ~0xF;
    TCON |= 2;
    TCON &= ~0xF;
    TCON |= 9;
    wait(1);

    LCDCON1 = 0x978;
    LCDCON2 = 0x24FC080;
    LCDCON3 = 0x80EF0A;
    LCDCON4 = 0xD04;
    LCDCON5 = 0xB01;
    LCDSADDR1 = LCD_FRAME_ADDR / 2; /* assume properly aligned */
    LCDSADDR2 = ((LCD_FRAME_ADDR + LCD_WIDTH * LCD_HEIGHT * 2) & 0x3FFFFF) / 2;
    LCDSADDR3 = 240;
    TPAL = 0;
    LCDCON1 |= 1;
    wait(0x64);

    GPBCON = (GPBCON & ~3) | 2;
    GPCCON &= ~0xC00;
    GPGCON &= ~0xC00000;
    GPGCON |= 0x400000;
}

void lcd_enable(bool enable)
{

}

void lcd_update(void)
{
    lcd_copy_buffer_rect((fb_data *)FRAME, &lcd_framebuffer[0][0],
                         LCD_WIDTH*LCD_HEIGHT, 1);
}

void lcd_update_rect(int x, int y, int width, int height)
{
    (void) x;
    (void) y;
    (void) width;
    (void) height;
    lcd_update();
}
