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
#include "cpu.h"
#include "kernel.h"
#include "sound.h"
#include "i2c-s3c2440.h"
#include "system-target.h"
#include "timer.h"
#include "wmcodec.h"

void audiohw_init(void)
{

}

void audiohw_enable_headphone_jack(bool enable)
{
}

void wmcodec_write(int reg, int data)
{
    unsigned char d[2];
    /* data is 9-bit wide so one bit of the data is part of the register address */
    d[0] = (reg << 1) | ((data & 0x100) >> 8);
    d[1] = data;
    i2c_write(0x34, d, sizeof(d));
}
