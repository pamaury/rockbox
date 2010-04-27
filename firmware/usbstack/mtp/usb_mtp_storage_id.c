/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Copyright (C) 2009 by Amaury Pouly
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
#include "usb_mtp_internal.h"
#define LOGF_ENABLE
#include "logf.h"
#include "storage.h"
#include "fat.h"

#ifdef HAVE_MULTIVOLUME
/* For now, if there are several volumes, there are coded 0x0001000x with x=1,2,3,... */
static bool valid_volume[NUM_VOLUMES];

int storage_id_to_volume(uint32_t stor_id)
{
    /* assume valid storage id */
    return (stor_id & 0x0000ffff) - 1;
}

bool is_valid_storage_id(uint32_t stor_id)
{
    if((stor_id & 0xffff0000) != 0x00010000)
        return false;
    if((stor_id & 0x0000ffff) == 0x0000 || (stor_id & 0x0000ffff) > NUM_VOLUMES)
        return false;

    return valid_volume[storage_id_to_volume(stor_id)];
}

void probe_storages(void)
{
    int i;
    char buffer[32];
    
    valid_volume[0] = true; /* main volume */
    
    for(i = 1; i < NUM_VOLUMES; i++)
    {
        snprintf(buffer, sizeof buffer, "/" VOL_NAMES, i);
        DIR *d = opendir(buffer);
        if(d != NULL)
        {
            closedir(d);
            valid_volume[i] = true;
        }
        else
            valid_volume[i] = false;
    }
}

uint32_t volume_to_storage_id(int volume)
{
    /* assume valid volume */
    return (0x00010000 | (uint16_t)(volume+1));
}

uint32_t get_first_storage_id(void)
{
    return volume_to_storage_id(0);
}

/* returns 0 if it's the last one */
uint32_t get_next_storage_id(uint32_t stor_id)
{
    /* assume valid id */
    int vol = storage_id_to_volume(stor_id);
    
    while((++vol)<NUM_VOLUMES && !valid_volume[vol])
        ;
    
    return vol==NUM_VOLUMES ? 0 : volume_to_storage_id(vol);
}

const char *get_storage_description(uint32_t stor_id)
{
    static char buffer[64];
    /* assume valid volume */
    int volume = storage_id_to_volume(stor_id);
    
    if(volume == 0)
        snprintf(buffer, sizeof buffer, "Rockbox Internal Storage");
    else
        snprintf(buffer, sizeof buffer, "Rockbox Volume %d", volume);
    
    return &buffer[0];
}

const char *get_volume_identifier(uint32_t stor_id)
{
    (void) stor_id;
    return "";
}

const char *get_storage_id_mount_point(uint32_t stor_id)
{
    static char buffer[32];
    /* assume valid volume */
    int volume = storage_id_to_volume(stor_id);
    
    if(volume == 0)
        snprintf(buffer, sizeof buffer, "/");
    else
        snprintf(buffer, sizeof buffer, "/" VOL_NAMES "/", volume);
    
    return &buffer[0];
}

#else
bool is_valid_storage_id(uint32_t stor_id)
{
    return stor_id == 0x00010001;
}

void probe_storages(void)
{
}

int storage_id_to_volume(uint32_t stor_id)
{
    /* assume valid storage id */
    (void) stor_id;
    return 0;
}

uint32_t volume_to_storage_id(int volume)
{
    /* assume valid volume */
    (void) volume;
    return 0x00010001;
}

uint32_t get_first_storage_id(void)
{
    return volume_to_storage_id(0);
}

/* returns 0 if it's the last one */
uint32_t get_next_storage_id(uint32_t stor_id)
{
    (void) stor_id;
    return 0;
}

const char *get_storage_description(uint32_t stor_id)
{
    (void) stor_id;
    return "Rockbox Internal Storage";
}

const char *get_volume_identifier(uint32_t stor_id)
{
    (void) stor_id;
    return "";
}

const char *get_storage_id_mount_point(uint32_t stor_id)
{
    (void) stor_id;
    return "/";
}
#endif

/*
 * Common
 */

uint64_t get_storage_size(uint32_t stor_id)
{
    unsigned long size;
    
    fat_size(IF_MV2(storage_id_to_volume(stor_id),) &size, NULL);
    return (uint64_t)size * 1024; /* size is reported in Kib */
}

uint64_t get_storage_free_space(uint32_t stor_id)
{
    unsigned long free;
    
    fat_size(IF_MV2(storage_id_to_volume(stor_id),) NULL, &free);
    return (uint64_t)free * 1024; /* size is reported in Kib */
}

