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
#include "button-target.h"
#include "system.h"
#include "system-target.h"
#include "cpu.h"

void button_init_device(void)
{
}

int button_read_device(void)
{
    int res = 0;
    if(!(GPGDAT & (1 << 5))) res |= BUTTON_VOL_DOWN;
    if(!(GPGDAT & (1 << 4))) res |= BUTTON_VOL_UP;
    if(!(GPGDAT & (1 << 3))) res |= BUTTON_MENU;
    if(!(GPFDAT & (1 << 0))) res |= BUTTON_POWER;
    if(!(GPFDAT & (1 << 2))) res |= BUTTON_RESET;
    return res;
}
