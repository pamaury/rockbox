/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 * $Id$
 *
 * Copyright (C) 2008 by Dave Chapman
 *
 * LCD driver for the Sansa Fuze - controller unknown
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
#include "lcd.h"

static bool display_on = false; /* is the display turned on? */
static bool display_flipped = false;
static int xoffset = 20; /* needed for flip */

/* TODO: Implement this function */
static void lcd_delay(int x)
{
    /* This is just arbitrary - the OF does something more complex */
    x *= 1024;
    while (x--);
}

static void as3525_dbop_init(void)
{
    CGU_DBOP = (1<<3) | (4-1);

    DBOP_TIMPOL_01 = 0xe167e167;
    DBOP_TIMPOL_23 = 0xe167006e;
    DBOP_CTRL = 0x41008;

    GPIOB_AFSEL = 0xfc;
    GPIOC_AFSEL = 0xff;

    DBOP_TIMPOL_23 = 0x6000e;
    DBOP_CTRL = 0x51008;
    DBOP_TIMPOL_01 = 0x6e167;
    DBOP_TIMPOL_23 = 0xa167e06f;

    /* TODO: The OF calls some other functions here, but maybe not important */
}

static void lcd_write_cmd(int cmd)
{
    int x;

    /* Write register */
    DBOP_CTRL &= ~(1<<14);

    DBOP_TIMPOL_23 = 0xa167006e;

    DBOP_DOUT = cmd;

    /* Wait for fifo to empty */
    while ((DBOP_STAT & (1<<10)) == 0);

    /* This loop is unique to the Fuze */
    x = 0;
    do {
        asm volatile ("nop\n");
    } while (x++ < 4);


    DBOP_TIMPOL_23 = 0xa167e06f;
}

void lcd_write_data(const fb_data* p_bytes, int count)
{
    while (count--)
    {
        DBOP_DOUT = *p_bytes++;

        /* Wait for fifo to empty */
        while ((DBOP_STAT & (1<<10)) == 0);
    }
}

static void lcd_write_reg(int reg, int value)
{
    unsigned short data = value;

    lcd_write_cmd(reg);
    lcd_write_data(&data, 1);
}

/*** hardware configuration ***/

void lcd_set_contrast(int val)
{
    (void)val;
}

void lcd_set_invert_display(bool yesno)
{
    (void)yesno;
}

static void flip_lcd(bool yesno)
{
    (void)yesno;
}


/* turn the display upside down (call lcd_update() afterwards) */
void lcd_set_flip(bool yesno)
{
    display_flipped = yesno;
    xoffset = yesno ? 20 : 0;   /* A guess */

    if (display_on)
        flip_lcd(yesno);
}


static void _display_on(void)
{
    /* Initialise in the same way as the original firmare */

    lcd_write_reg(0x07, 0);
    lcd_write_reg(0x13, 0);

    lcd_delay(10);

    lcd_write_reg(0x11, 0x3704);
    lcd_write_reg(0x14, 0x1a1b);
    lcd_write_reg(0x10, 0x3860);
    lcd_write_reg(0x13, 0x40);

    lcd_delay(10);

    lcd_write_reg(0x13, 0x60);

    lcd_delay(50);

    lcd_write_reg(0x13, 0x70);

    lcd_delay(40);

    lcd_write_reg(0x01, 277);
    lcd_write_reg(0x02, (7<<8));
    lcd_write_reg(0x03, 0x30);
    lcd_write_reg(0x08, 0x01);
    lcd_write_reg(0x0b, (1<<10));
    lcd_write_reg(0x0c, 0);

    lcd_write_reg(0x30, 0x40);
    lcd_write_reg(0x31, 0x0687);
    lcd_write_reg(0x32, 0x0306);
    lcd_write_reg(0x33, 0x104);
    lcd_write_reg(0x34, 0x0585);
    lcd_write_reg(0x35, 255+66);
    lcd_write_reg(0x36, 0x0687+128);
    lcd_write_reg(0x37, 259);
    lcd_write_reg(0x38, 0);
    lcd_write_reg(0x39, 0);

    lcd_write_reg(0x42, (LCD_WIDTH - 1));
    lcd_write_reg(0x43, 0);
    lcd_write_reg(0x44, (LCD_WIDTH - 1));
    lcd_write_reg(0x45, 0);
    lcd_write_reg(0x46, (((LCD_WIDTH - 1) + xoffset) << 8) | xoffset);
    lcd_write_reg(0x47, (LCD_HEIGHT - 1));
    lcd_write_reg(0x48, 0x0);

    lcd_write_reg(0x07, 0x11);

    lcd_delay(40);

    lcd_write_reg(0x07, 0x17);

    display_on = true;  /* must be done before calling lcd_update() */
    lcd_update();
}

/* (e.g. 0, 219) */
static void lcd_window_x(int r0, int r1)
{
    int r2, r3, r4;

    r3 = (LCD_WIDTH - 1);
    r2 = (LCD_WIDTH - 1);

    if (r0 < LCD_WIDTH)
        r2 = r0;

    if (r1 < LCD_WIDTH)
        r3 = r1;

#if 0
    r1 = 0x1db12;

    [r1] = 1; /* byte */
#endif

    r3 += xoffset;

    r4 = r2;

    r4 += xoffset;

    r0 = (r3 << 8);

    r0 |= r4;

    r1 = (r0 << 16) >> 16;

    lcd_write_reg(0x46, r1);

    r1 = (r4 << 16) >> 16;

    lcd_write_reg(0x20, r1);

    lcd_write_reg(0x03, 0x30);

#if 0
    r0 := 0x1e0c4
    r1 := 0x216a8

    r0 := [r0]
    r2 := [r1]

    r0 := (r0 >> 2) << 2;

    if (r0 != r2) {
#endif
        lcd_write_reg(0x00, 0x0001);
        lcd_write_reg(0x11, 0x3704);
        lcd_write_reg(0x14, 0x1a1b);
        lcd_write_reg(0x10, 0x3860);
        lcd_write_reg(0x13, 0x0070);
        lcd_write_reg(0x07, 0x0017);
        lcd_write_reg(0x01, 277);
        lcd_write_reg(0x02, (7<<8));
        lcd_write_reg(0x08, 0x0001);
        lcd_write_reg(0x0b, (1<<10));
        lcd_write_reg(0x0c, 0x0000);
        lcd_write_reg(0x30, 0x0040);
        lcd_write_reg(0x31, 0x0687);
        lcd_write_reg(0x32, 0x0306);
        lcd_write_reg(0x33, 260);
        lcd_write_reg(0x34, 0x0585);
        lcd_write_reg(0x35, 255+66);
        lcd_write_reg(0x36, 0x687+128);
        lcd_write_reg(0x37, 259);
        lcd_write_reg(0x38, 0);
        lcd_write_reg(0x39, 0);
        lcd_write_reg(0x40, 0);
        lcd_write_reg(0x41, 0);
        lcd_write_reg(0x42, (LCD_WIDTH - 1));
        lcd_write_reg(0x43, 0);
        lcd_write_reg(0x44, (LCD_WIDTH - 1));
        lcd_write_reg(0x45, 0);
        lcd_write_reg(0x15, 0);
        lcd_write_reg(0x73, 0);
#if 0
    }
#endif
}

/*  - e.g. 0, 175 */
static void lcd_window_y(int r0, int r1)
{
    int r2, r4;

    r2 = (LCD_HEIGHT - 1);
    r4 = (LCD_HEIGHT - 1);

    if (r0 < LCD_HEIGHT)
        r4 = r0;

    if (r1 < LCD_HEIGHT)
        r2 = r1;

    r1 = (r2 << 16) >> 16;

    lcd_write_reg(0x47, r1);

    r1 = (r4 << 16) >> 16;

    lcd_write_reg(0x48, r1);

    /* ??Start address - (x<<8) | y0 */
    lcd_write_reg(0x21, r1);

    /* Start write to GRAM */    
    lcd_write_cmd(0x22);
}

/* I'm guessing this function is lcd_enable, but it may not be... */
void lcd_enable(int r0)
{
#if 0
    r4 = 0x1db12;
    [r4] = 1;
#endif

    if (r0 != 0) {
        lcd_write_reg(0, 1);

        lcd_delay(10);

        lcd_write_reg(0x10, 0);
        lcd_write_reg(0x11, 0x3704);
        lcd_write_reg(0x14, 0x1a1b);
        lcd_write_reg(0x10, 0x3860);
        lcd_write_reg(0x13, 0x40);

        lcd_delay(10);

        lcd_write_reg(0x13, 0x60);

        lcd_delay(50);

        lcd_write_reg(0x13, 112);

        lcd_delay(40);

        lcd_write_reg(0x07, 0x11);

        lcd_delay(40);

        lcd_write_reg(0x07, 0x17);
    } else {
        lcd_write_reg(0x07, 0x22);

        lcd_delay(40);

        lcd_write_reg(0x07, 0);

        lcd_delay(40);

        lcd_write_reg(0x10, 1);
    }

#if 0
    [r4] = 0;
#endif
}

void lcd_init_device()
{
    as3525_dbop_init();

    GPIOA_DIR |= (1<<5);
    GPIOA_PIN(5) = 0;

    GPIOA_PIN(3) = (1<<3);

    GPIOA_DIR |= (1<<4) | (1<<3);

    GPIOA_PIN(3) = (1<<3);

    GPIOA_PIN(4) = 0;

    GPIOA_DIR |= (1<<7);
    GPIOA_PIN(7) = 0;

    CCU_IO &= ~4;
    CCU_IO &= ~8;

    GPIOD_DIR |= (1<<7);

#if 0
    if (byte[0x21b24] == 0) {
        GPIOD_PIN(7) = (1<<7);
        GPIOD_DIR |= (1<<7);
    }
#endif

    lcd_delay(1);

    GPIOA_PIN(5) = (1<<5);

    lcd_delay(1);

    _display_on();
}

/* Update the display.
   This must be called after all other LCD functions that change the display. */
void lcd_update(void)
{
    if (!display_on)
        return;

    lcd_window_x(0, (LCD_WIDTH - 1));

    lcd_window_y(0, (LCD_HEIGHT - 1));

    lcd_write_data((unsigned short *)lcd_framebuffer, LCD_WIDTH*LCD_HEIGHT);

}

/* Update a fraction of the display. */
void lcd_update_rect(int x, int y, int width, int height)
{
    int xmax, ymax;
    const unsigned short *ptr;

    if (!display_on)
        return;

    xmax = x + width - 1;
    if (xmax >= LCD_WIDTH)
        xmax = LCD_WIDTH - 1; /* Clip right */
    if (x < 0)
        x = 0;                /* Clip left */
    if (x >= xmax)
        return; /* nothing left to do */

    ymax = y + height;
    if (ymax > LCD_HEIGHT)
        ymax = LCD_HEIGHT - 1; /* Clip bottom */
    if (y < 0)
        y = 0; /* Clip top */
    if (y >= ymax)
        return; /* nothing left to do */


    lcd_window_x(x, xmax);
    lcd_window_y(y, ymax);

    ptr = (unsigned short *)&lcd_framebuffer[y][x];

    do
    {
        lcd_write_data(ptr, width);
        ptr += LCD_WIDTH;
    }
    while (++y < ymax);

    lcd_write_data((unsigned short *)lcd_framebuffer, LCD_WIDTH*LCD_HEIGHT);
}
