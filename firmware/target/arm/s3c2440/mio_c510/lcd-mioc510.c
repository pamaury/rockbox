/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 * $Id$
 *
 * Copyright (C) 2011 by Amaury Pouly
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
#include "cpu.h"
#include "string.h"
#include "lcd.h"
#include "kernel.h"
#include "lcd-target.h"

/** Mio C510 screen is rotated by 90° */

/* Copies a rectangle from one framebuffer to another. Can be used in
   single transfer mode with width = num pixels, and height = 1 which
   allows a full-width rectangle to be copied more efficiently. */
void lcd_copy_buffer_rect(fb_data *dst, const fb_data *src,
                                 int width, int height)
{
    #if 1
    /* dst is wrong since the screen is rotated, so recompute real
     * location */
    unsigned dst_off = ((unsigned char *)dst - (unsigned char *)FRAME) / sizeof(fb_data);
    unsigned dst_x = dst_off % LCD_WIDTH;
    unsigned dst_y = dst_off / LCD_WIDTH;

    for(int y = 0; y < height; y++)
    {
        for(int x = 0; x < width; x++)
        {
            unsigned pos = (dst_y + y) * LCD_WIDTH + dst_x + x;
            unsigned virtual_x = pos % LCD_WIDTH;
            unsigned virtual_y = pos / LCD_WIDTH;
            unsigned rot_x = virtual_y;
            unsigned rot_y = virtual_x;
            ((fb_data *)FRAME)[(rot_y + 1) * LCD_HEIGHT - rot_x] = src[y * LCD_WIDTH + x];
        }
    }
    #else
    for(int x = 0; x < LCD_WIDTH; x++)
        for(int y = 0; y < LCD_HEIGHT; y++)
            ((fb_data *)FRAME)[(x + 1) * LCD_HEIGHT - y] = lcd_framebuffer[y][x];
    #endif
}

/* Line write helper function for lcd_yuv_blit. Write two lines of yuv420. */
extern void lcd_write_yuv420_lines(fb_data *dst,
                                   unsigned char const * const src[3],
                                   int width,
                                   int stride)
{
}

extern void lcd_write_yuv420_lines_odither(fb_data *dst,
                                           unsigned char const * const src[3],
                                           int width,
                                           int stride,
                                           int x_screen, /* To align dither pattern */
                                           int y_screen)
{
}
