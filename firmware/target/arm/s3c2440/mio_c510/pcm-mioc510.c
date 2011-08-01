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
#include <stdlib.h>
#include "config.h"
#include "system.h"
#include "kernel.h"
#include "logf.h"
#include "audio.h"
#include "sound.h"
#include "file.h"
#include "pcm-internal.h"


void fiq_handler(void) __attribute__((interrupt ("FIQ")));

void pcm_play_lock(void)
{
}

void pcm_play_unlock(void)
{
}

void pcm_play_dma_init(void)
{
}

void pcm_postinit(void)
{
    audiohw_postinit();
}

void pcm_dma_apply_settings(void)
{
}

void pcm_play_dma_start(const void *addr, size_t size)
{
    (void) addr;
    (void) size;
}

void pcm_play_dma_stop(void)
{
}

void pcm_play_dma_pause(bool pause)
{
    (void) pause;
}

void fiq_handler(void)
{
}

size_t pcm_get_bytes_waiting(void)
{
    return 0;
}

const void *pcm_play_dma_get_peak_buffer(int *count)
{
    (void) count;
    return NULL;
}
