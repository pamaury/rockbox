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

#include <stdio.h>
#include <system.h>
#include <inttypes.h>
#include "config.h"
#include "gcc_extensions.h"
#include "lcd.h"
#include "backlight.h"
#include "button-target.h"
#include "common.h"
#include "storage.h"
#include "disk.h"
#include "panic.h"
#include "power.h"
#include "system-target.h"
#include "i2c.h"
#include "i2c-s3c2440.h"

int show_logo(void);

uint8_t bypass_hex[] =
{
    0xFC,0xF3,0x3B,0x1E,0x3E,0x01,0x00,0xFC,0xF3,0x3B,0x1E,0x45,0x43,0xFC,0xFC,
    0xF3,0x3B,0x1E,0x3D,0x02,0x50,0xFC,0xF3,0x3B,0x1E,0x34,0x00,0x66,0xFC,0xF3,
    0x3B,0x1E,0x36,0x00,0x07,0xFC,0xF3,0x3B,0x1E,0x35,0x00,0x06,0xFC,0xF3,0x3B,
    0x1E,0x3B,0x00,0x10,0xFC,0xF3,0x3B,0x1D,0x00,0x00,0x01,0xFC,0xF3,0x3B,0x1E,
    0x46,0x01,0x11,0xFC,0xF3,0x3B,0x1E,0x30,0x03,0x39,0xFC,0xF3,0x3B,0x1E,0x4D,
    0x04,0x00,0xFC,0xF3,0x3B,0x1E,0x41,0x00,0x01,0xFC,0xF3,0x3B,0x1E,0x44,0x00,
    0x01,0xFC,0xF3,0x3B,0x1E,0x52,0x00,0x0F,0xFC,0xF3,0x3B,0x1D,0x11,0x00,0x03,
    0xFC,0xF3,0x3B,0x1D,0x12,0x00,0x03,0xFC,0xF3,0x3B,0x1D,0x03,0x25,0x00,0xFC,
    0xF3,0x3B,0x1D,0x04,0x28,0x00,0xFC,0xF3,0x3B,0x1D,0x42,0x00,0x1F,0xFC,0xF3,
    0x3B,0x1E,0x6A,0x00,0x02,0xFC,0xF3,0x3B,0x1E,0x63,0x00,0x03,0xFC,0xF3,0x3B,
    0x1E,0xC3,0x30,0x00,0xFC,0xF3,0x3B,0x1E,0x9A,0x08,0x00,0xFC,0xF3,0x3B,0x1E,
    0x3A,0x00,0x00
};

static void i2c_scan(void)
{
    printf("Scanning i2c...");
    for(int addr = 2; addr <= 254; addr += 2)
    {
        int ret = i2c_write(addr, NULL, 0);
        if(ret == 0)
            printf("Device: %x", addr);
    }
}

void main(void) NORETURN_ATTR;
void main(void)
{
    unsigned char* loadbuffer;
    int buffer_size;
    void(*kernel_entry)(void);
    int ret;

    system_init();
    kernel_init();

    button_init_device();

    enable_irq();

    lcd_init();
    show_logo();

    backlight_init();

    i2c_init();

    /* */
    GPBUP |= 0x400;
    GPBCON &= ~0x300000;
    GPBCON |= 0x100000;
    GPBDAT |= 0x400;
    /* for i2s */
    GPEUP |= 0xC01F;
    GPECON &= ~0xF00003FF;
    GPECON |= 0xA00002AA;
    /* */
    GPFCON &= ~0x300;
    GPFCON |= 0x100;
    GPFDAT &= ~0x10;
    /* */
    GPJUP |= 0x40;
    GPJCON &= ~0x3000;
    GPJCON |= 0x1000;
    GPJDAT &= ~0x40;
    sleep(HZ / 10);
    GPJDAT |= 0x40;
    GPCUP |= 0x80;
    GPCCON &= ~0xC000;
    GPCCON |= 0x4000;
    GPCDAT &= ~0x80;
    sleep(HZ / 10);
    /* */
    printf("Send bypass.hex...");
    ret = i2c_write(0xC0, bypass_hex, sizeof(bypass_hex));
    printf("Done: %d", ret);
    /* */
    GPJUP |= 0x800;
    GPJDAT |= 0x800;
    
    i2c_scan();
    printf("Die.");
    system_exception_wait();

    ret = storage_init();
    if(ret < 0)
        error(EATA, ret, true);

    while(!disk_init(IF_MV(0)))
        panicf("disk_init failed!");

    while((ret = disk_mount_all()) <= 0)
    {
        error(EDISK, ret, true);
    }

    printf("Loading firmware");

    /* Flush out anything pending first */
    cpucache_invalidate();
    loadbuffer = (unsigned char*) 0x31000000;
    buffer_size = (unsigned char*)0x31400000 - loadbuffer;

    while((ret = load_firmware(loadbuffer, BOOTFILE, buffer_size)) < 0)
    {
        error(EBOOTFILE, ret, true);
    }

    system_prepare_fw_start();
    kernel_entry = (void*) loadbuffer;
    cpucache_invalidate();
    printf("Executing");
    kernel_entry();
    printf("ERR: Failed to boot");

    /* never returns */
    while(1) ;
}
