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
#include "dircache.h"

#if !defined(HAVE_DIRCACHE)
#error USB-MTP requires dircache to be enabled
#endif

/* TODO: find a better place for this function. */
static void fat2tm(struct tm *tm, unsigned short date, unsigned short time)
{
    tm->tm_year = ((date >> 9 ) & 0x7F) + 80;
    tm->tm_mon  =  (date >> 5 ) & 0x0F;
    tm->tm_mday =  (date      ) & 0x1F;
    tm->tm_hour =  (time >> 11) & 0x1F;
    tm->tm_min  =  (time >> 5 ) & 0x3F;
    tm->tm_sec  =  (time & 0x1F) << 1;
}

/*
 * A function to check whether a dircache entry is valid or not
 */
static bool is_dircache_entry_valid(const struct dircache_entry *entry)
{
    return entry != NULL && entry->name_len != 0;
}

/*
 * Handle conversions
 */
static bool check_dircache(void)
{
    if(!dircache_is_enabled() || dircache_is_initializing())
    {
        logf("mtp: oops, dircache is not enabled, trying something...");
        return false;
    }
    else
        return true;
}

static const struct dircache_entry *mtp_handle_to_dircache_entry(uint32_t handle, bool accept_root)
{
    /* NOTE invalid entry for 0xffffffff but works with it */
    struct dircache_entry *entry = (struct dircache_entry *)handle;
    if(entry == (struct dircache_entry *)0xffffffff)
        return accept_root ? entry : NULL;
    
    if(!dircache_is_valid_ptr(entry))
        return NULL;

    return entry;
}

static uint32_t dircache_entry_to_mtp_handle(const struct dircache_entry *entry)
{
    if(entry==0)
        return 0x00000000;
    else
    {
        if(entry->d_name[0] == '<')
            return 0x00000000;
        else
            return (uint32_t)entry;
    }
}

/* accept stor_id=0x00000000 iff direntry is well specified (ie not root) */
/* depth first search */
/* returns mtp error code */
uint16_t generic_list_files2(uint32_t stor_id, const struct dircache_entry *direntry, list_file_func_t lff, void *arg)
{
    const struct dircache_entry *entry;
    uint16_t ret;
    unsigned status;
    
    logf("mtp: list_files stor_id=0x%lx entry=0x%lx", stor_id, (uint32_t)direntry);
    
    if((uint32_t)direntry == 0xffffffff)
    {
        /* check stor_id is valid */
        if(!is_valid_storage_id(stor_id))
            return ERROR_INVALID_STORAGE_ID;
        
        direntry = dircache_get_entry_ptr_ex(get_storage_id_mount_point(stor_id), true);
        logf("Mount point dircache entry for '%s': 0x%lx", get_storage_id_mount_point(stor_id), (uint32_t)direntry);
        if(!dircache_is_valid_ptr(direntry))
            return ERROR_INVALID_OBJ_HANDLE;
    }
    else
    {
        if(!(direntry->attribute&ATTR_DIRECTORY))
            return ERROR_INVALID_PARENT_OBJ;
        direntry = direntry->down;
    }
    
    for(entry = direntry; entry != NULL; entry = entry->next)
    {
        if(!is_dircache_entry_valid(entry))
            continue;
        /* FIXME this code depends on how mount points are named ! */
        /* skip "." and ".." and files that begin with "<"*/
        if(entry->d_name[0] == '.' && entry->d_name[1] == '\0')
            continue;
        if(entry->d_name[0] == '.' && entry->d_name[1] == '.'  && entry->d_name[2] == '\0')
            continue;
        if(entry->d_name[0] == '<')
            continue;
        
        status = lff(stor_id, dircache_entry_to_mtp_handle(entry), arg);
        if(status & LF_STOP)
            return ERROR_OK;
        
        /* handle recursive listing */
        if((entry->attribute & ATTR_DIRECTORY) && (status & LF_ENTER))
        {
            ret = generic_list_files2(stor_id, entry, lff, arg);
            if(ret != ERROR_OK)
                return ret;
        }
    }

    return ERROR_OK;
}

uint16_t generic_list_files(uint32_t stor_id, uint32_t obj_handle, list_file_func_t lff, void *arg)
{
    const struct dircache_entry *direntry = mtp_handle_to_dircache_entry(obj_handle, true);
    uint16_t ret;
    
    /* handle all storages if necessary */
    if(stor_id == 0xffffffff)
    {
        /* if all storages are requested, only root is a valid parent object */
        if((uint32_t)direntry != 0xffffffff)
            return ERROR_INVALID_OBJ_HANDLE;
        
        stor_id = get_first_storage_id();
        
        do
        {
            ret = generic_list_files2(stor_id, direntry, lff, arg);
            if(ret != ERROR_OK)
                return ret;
        }while((stor_id = get_next_storage_id(stor_id)));
        
        return ERROR_OK;
    }
    else
        return generic_list_files2(stor_id, direntry, lff, arg);
}

uint32_t get_object_storage_id(uint32_t handle)
{
    const struct dircache_entry *entry = mtp_handle_to_dircache_entry(handle, false);
    /* FIXME ugly but efficient */
    while(entry->up)
    {
        entry = entry->up;
        if(entry->d_name[0] == '<')
            return volume_to_storage_id(entry->d_name[VOL_ENUM_POS] - '0');
    }
    
    return volume_to_storage_id(0);
}

bool is_valid_object_handle(uint32_t handle, bool accept_root)
{
    return mtp_handle_to_dircache_entry(handle, accept_root) != NULL;
}

void copy_object_path(uint32_t obj_handle, char *buffer, int size)
{
    dircache_copy_path(mtp_handle_to_dircache_entry(obj_handle, false), buffer, size);
}

const char *get_object_filename(uint32_t handle)
{
    return mtp_handle_to_dircache_entry(handle, false)->d_name;
}

uint32_t get_object_handle_by_name(const char *ptr)
{
    /* don't accept root */
    const struct dircache_entry *entry = dircache_get_entry_ptr(ptr);
    return dircache_entry_to_mtp_handle(entry);
}

bool is_directory_object(uint32_t handle)
{
    return mtp_handle_to_dircache_entry(handle, false)->attribute & ATTR_DIRECTORY;
}

bool is_hidden_object(uint32_t handle)
{
    return mtp_handle_to_dircache_entry(handle, false)->attribute & ATTR_HIDDEN;
}

bool is_system_object(uint32_t handle)
{
    return mtp_handle_to_dircache_entry(handle, false)->attribute & ATTR_SYSTEM;
}

uint32_t get_object_size(uint32_t handle)
{
    return mtp_handle_to_dircache_entry(handle, false)->size;
}

uint32_t get_parent_object(uint32_t handle)
{
    const struct dircache_entry *entry = mtp_handle_to_dircache_entry(handle, true);
    if(entry->up)
        return dircache_entry_to_mtp_handle(entry->up);
    else
        return 0x00000000;
}

void copy_object_date_created(uint32_t handle, struct tm *filetm)
{
    const struct dircache_entry *entry = mtp_handle_to_dircache_entry(handle, false);
    fat2tm(filetm, entry->wrtdate, entry->wrttime);
}

void copy_object_date_modified(uint32_t handle, struct tm *filetm)
{
    const struct dircache_entry *entry = mtp_handle_to_dircache_entry(handle, false);
    fat2tm(filetm, entry->wrtdate, entry->wrttime);
}

persistent_unique_id_t get_object_persistent_unique_id(uint32_t handle)
{
    /*const struct dircache_entry *entry = mtp_handle_to_dircache_entry(handle, false);*/
    persistent_unique_id_t pui;
    
    /* Persistent Unique Object Identifier: md5sum of the complete filename ? */
    /* For now, it's only the handle repeated 4 times */
    pui.u32[0]=pui.u32[1]=pui.u32[2]=pui.u32[3]=handle;
    
    return pui;
}
/*
 * Init
 */
void init_object_mgr(void)
{
    check_dircache();
}

void deinit_object_mgr(void)
{
}
