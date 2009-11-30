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

/*
 *
 * Misc
 *
 */

static const struct mtp_string mtp_ext =
{
    0,
    {} /* empty strings don't have null at the end */
};

static const struct mtp_array_uint16_t mtp_op_supported =
{
    21,
    {MTP_OP_GET_DEV_INFO,
     MTP_OP_OPEN_SESSION,
     MTP_OP_CLOSE_SESSION,
     MTP_OP_GET_STORAGE_IDS,
     MTP_OP_GET_STORAGE_INFO,
     MTP_OP_GET_NUM_OBJECTS,
     MTP_OP_GET_OBJECT_HANDLES,
     MTP_OP_GET_OBJECT_INFO,
     MTP_OP_GET_OBJECT,
     MTP_OP_DELETE_OBJECT,
     MTP_OP_SEND_OBJECT_INFO,
     MTP_OP_SEND_OBJECT,
     MTP_OP_GET_DEV_PROP_DESC,
     MTP_OP_GET_DEV_PROP_VALUE,
     MTP_OP_MOVE_OBJECT,
     MTP_OP_COPY_OBJECT,
     MTP_OP_GET_PARTIAL_OBJECT,
     MTP_OP_GET_OBJ_PROPS_SUPPORTED,
     MTP_OP_GET_OBJ_PROP_DESC,
     MTP_OP_GET_OBJ_PROP_VALUE,
     MTP_OP_GET_OBJ_REFERENCES
     }
};

static const struct mtp_array_uint16_t mtp_evt_supported =
{
    0,
    {}
};

static const struct mtp_array_uint16_t mtp_dev_prop_supported =
{
    3,
    {DEV_PROP_BATTERY_LEVEL,
     DEV_PROP_DATE_TIME,
     DEV_PROP_FRIENDLY_NAME}
};

static const struct mtp_array_uint16_t mtp_capture_fmt =
{
    0,
    {}
};

static const struct mtp_array_uint16_t mtp_playback_fmt =
{
    2,
    {OBJ_FMT_UNDEFINED,
     OBJ_FMT_ASSOCIATION}
};

static const struct mtp_string mtp_manufacturer =
{
    12,
    {'R','o','c','k','b','o','x','.','o','r','g','\0'} /* null-terminated */
};


static const struct mtp_string mtp_model =
{
    21,
    {'R','o','c','k','b','o','x',' ',
     'm','e','d','i','a',' ',
     'p','l','a','y','e','r','\0'} /* null-terminated */
};

static const struct mtp_string device_friendly_name =
{
    21,
    {'R','o','c','k','b','o','x',' ',
     'm','e','d','i','a',' ',
     'p','l','a','y','e','r','\0'} /* null-terminated */
};

static const struct mtp_string mtp_dev_version =
{
    4,
    {'s','v','n','\0'} /* null-terminated */
};

static const struct mtp_string mtp_serial =
{
    42,
    {'0','0','0','0','0','0','0','0','0','0','0','0','0','0','0','0',
     '0','0','0','0','0','0','0','0','0','0','0','0','0','0','0','0',
     '0','0','0','0','0','0','0','0','0','\0'} /* null-terminated */
};

static const struct device_info dev_info=
{
    100, /* Standard Version */
    0x00000006, /* Vendor Extension ID: seems to be 0x6 for Microsoft but not written in the spec */
    100, /* MTP Version */
    0x0000 /* Functional Mode */
};

/*
 *
 * Code
 *
 */

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
    return entry!=NULL && entry->name_len != 0;
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

/*
 *
 * Operation handling
 *
 */
void open_session(uint32_t session_id)
{
    logf("mtp: open session %lu", session_id);
    
    if(mtp_state.session_id != 0x00000000)
        return fail_op_with(ERROR_SESSION_ALREADY_OPEN, NO_DATA_PHASE);
    
    mtp_state.session_id = session_id;
    mtp_cur_resp.code = ERROR_OK;
    mtp_cur_resp.nb_parameters = 0;
    send_response();
}

void close_session(bool send_resp)
{
    if(mtp_state.session_id == 0x00000000)
        return fail_op_with(ERROR_SESSION_NOT_OPEN, NO_DATA_PHASE);
    logf("mtp: close session %lu", mtp_state.session_id);
    
    mtp_state.session_id = 0x00000000;
    /* destroy pending object (if any) */
    if(mtp_state.has_pending_oi)
    {
        const struct dircache_entry *entry = mtp_handle_to_dircache_entry(mtp_state.pending_oi.handle, false);
        static char path[MAX_PATH];
        if(entry != NULL)
        {
            dircache_copy_path(entry, path, sizeof(path));
            logf("mtp: remove unfinished pending object: \"%s\"", path);
            remove(path);
        }
        mtp_state.has_pending_oi = false;
    }
    
    if(send_resp)
    {
        mtp_cur_resp.code = ERROR_OK;
        mtp_cur_resp.nb_parameters = 0;
        send_response();
    }
}

void get_device_info(void)
{
    logf("mtp: get device info");
    
    start_pack_data_block();
    pack_data_block_uint16_t(dev_info.std_version);
    pack_data_block_uint32_t(dev_info.vendor_ext);
    pack_data_block_uint16_t(dev_info.mtp_version);
    pack_data_block_string(&mtp_ext);
    pack_data_block_uint16_t(dev_info.mode);
    pack_data_block_array_uint16_t(&mtp_op_supported);
    pack_data_block_array_uint16_t(&mtp_evt_supported);
    pack_data_block_array_uint16_t(&mtp_dev_prop_supported);
    pack_data_block_array_uint16_t(&mtp_capture_fmt);
    pack_data_block_array_uint16_t(&mtp_playback_fmt);
    pack_data_block_string(&mtp_manufacturer);
    pack_data_block_string(&mtp_model);
    pack_data_block_string(&mtp_dev_version);
    pack_data_block_string(&mtp_serial);
    finish_pack_data_block();
    
    mtp_cur_resp.code = ERROR_OK;
    mtp_cur_resp.nb_parameters = 0;
    
    send_data_block();
}

void get_storage_ids(void)
{
    uint32_t stor_id = get_first_storage_id();
    logf("mtp: get storage ids");
    
    start_pack_data_block();
    start_pack_data_block_array();
    
    do
    {
        logf("mtp: storage %lx -> %s", stor_id, get_storage_id_mount_point(stor_id));
        pack_data_block_array_elem_uint32_t(stor_id);
    }while((stor_id = get_next_storage_id(stor_id)));
    
    finish_pack_data_block_array();
    finish_pack_data_block();
    
    mtp_cur_resp.code = ERROR_OK;
    mtp_cur_resp.nb_parameters = 0;
    
    send_data_block();
}

void get_storage_info(uint32_t stor_id)
{
    logf("mtp: get storage info: stor_id=0x%lx", stor_id);
    if(!is_valid_storage_id(stor_id))
        return fail_op_with(ERROR_INVALID_STORAGE_ID, SEND_DATA_PHASE);
    
    unsigned long size, free;
    fat_size(IF_MV2(storage_id_to_volume(stor_id),) &size, &free);
    size *= SECTOR_SIZE;
    free *= SECTOR_SIZE;
    
    start_pack_data_block();
    pack_data_block_uint16_t(STOR_TYPE_FIXED_RAM); /* Storage Type */
    pack_data_block_uint16_t(FS_TYPE_GENERIC_HIERARCHICAL); /* Filesystem Type */
    pack_data_block_uint16_t(ACCESS_CAP_RW); /* Access Capability */
    pack_data_block_uint64_t(size); /* Max Capacity (optional for read only) */
    pack_data_block_uint64_t(free); /* Free Space in bytes (optional for read only) */
    pack_data_block_uint32_t(0); /* Free Space in objects (optional for read only) */
    pack_data_block_string_charz(get_storage_description(stor_id)); /* Storage Description */
    pack_data_block_string_charz(get_volume_identifier(stor_id)); /* Volume Identifier */
    finish_pack_data_block();
    
    mtp_cur_resp.code = ERROR_OK;
    mtp_cur_resp.nb_parameters = 0;
    
    send_data_block();
}

/*
 * Objet operations
 */

/* accept stor_id=0x0 if and only if direntry is well specified (ie not root) */
static bool list_files2(uint32_t stor_id, const struct dircache_entry *direntry, bool recursive)
{
    const struct dircache_entry *entry;
    uint32_t *ptr;
    uint32_t nb_elems=0;
    
    logf("mtp: list_files stor_id=0x%lx entry=0x%lx rec=%s",stor_id, (uint32_t)direntry, recursive ? "yes" : "no");
    
    if((uint32_t)direntry == 0xffffffff)
    {
        /* check stor_id is valid */
        if(!is_valid_storage_id(stor_id))
        {
            fail_op_with(ERROR_INVALID_STORAGE_ID, SEND_DATA_PHASE);
            return false;
        }
        direntry = dircache_get_entry_ptr(get_storage_id_mount_point(stor_id));
    }
    else
    {
        if(!(direntry->attribute & ATTR_DIRECTORY))
        {
            fail_op_with(ERROR_INVALID_PARENT_OBJ, SEND_DATA_PHASE);
            return false;
        }
        direntry = direntry->down;
    }
    
    ptr = (uint32_t *)get_data_block_ptr();
    
    for(entry = direntry; entry != NULL; entry = entry->next)
    {
        if(!is_dircache_entry_valid(entry))
            continue;
        /* FIXME this code depends on how mount points are named ! */
        /* skip "." and ".." and files that begin with "<"*/
        /*logf("mtp: add entry \"%s\"(len=%ld, 0x%lx)", entry->d_name, entry->name_len, (uint32_t)entry);*/
        if(entry->d_name[0] == '.' && entry->d_name[1] == '\0')
            continue;
        if(entry->d_name[0] == '.' && entry->d_name[1] == '.'  && entry->d_name[2] == '\0')
            continue;
        if(entry->d_name[0] == '<')
            continue;
        
        pack_data_block_array_elem_uint32_t(dircache_entry_to_mtp_handle(entry));
        nb_elems++;
        
        /* handle recursive listing */
        if(recursive && (entry->attribute & ATTR_DIRECTORY))
            if (!list_files2(stor_id, entry, recursive))
                return false;
    }

    return true;
}

/* accept stor_id=0x0 if and only if direntry is well specified (ie not root) */
static void list_files(uint32_t stor_id, const struct dircache_entry *direntry, bool recursive)
{
    start_pack_data_block();
    start_pack_data_block_array();

    /* handle all storages if necessary */
    if(stor_id == 0xffffffff)
    {
        /* if all storages are requested, only root is a valid parent object */
        if((uint32_t)direntry != 0xffffffff)
            return fail_op_with(ERROR_INVALID_OBJ_HANDLE, SEND_DATA_PHASE);
        
        stor_id = get_first_storage_id();
        
        do
        {
            if(!list_files2(stor_id, (const struct dircache_entry *)0xffffffff, recursive))
                return;
        }while((stor_id = get_next_storage_id(stor_id)));
    }
    else if (!list_files2(stor_id, direntry, recursive))
        return; /* the error response is sent by list_files2 */

    finish_pack_data_block_array();
    finish_pack_data_block();

    mtp_cur_resp.code = ERROR_OK;
    mtp_cur_resp.nb_parameters = 0;

    send_data_block();
}

void get_num_objects(int nb_params, uint32_t stor_id, uint32_t obj_fmt, uint32_t obj_handle_parent)
{
    (void) nb_params;
    (void) stor_id;
    (void) obj_fmt;
    (void) obj_handle_parent;
    return fail_op_with(ERROR_OP_NOT_SUPPORTED, SEND_DATA_PHASE);
}

void get_object_handles(int nb_params, uint32_t stor_id, uint32_t obj_fmt, uint32_t obj_handle_parent)
{
    if(!check_dircache())
        return fail_op_with(ERROR_DEV_BUSY, SEND_DATA_PHASE);
    
    logf("mtp: get object handles: nb_params=%d stor_id=0x%lx obj_fmt=0x%lx obj_handle_parent=0x%lx",
        nb_params, stor_id, obj_fmt, obj_handle_parent);
    
    /* if then third parameter is not present, set to the default value */
    if(nb_params < 3)
        obj_handle_parent = 0x00000000;
    /* if there are two parameters, make sure the second one does not filter anything */
    if(nb_params >= 2)
    {
        if(obj_fmt != 0x00000000)
            return fail_op_with(ERROR_SPEC_BY_FMT_UNSUPPORTED, SEND_DATA_PHASE);
    }
    
    if(stor_id != 0xffffffff && !is_valid_storage_id(stor_id))
        return fail_op_with(ERROR_INVALID_STORAGE_ID, SEND_DATA_PHASE);
    
    /* parent_handle=0x00000000 means all objects*/
    if(obj_handle_parent == 0x00000000)
        list_files(stor_id, (const struct dircache_entry *)0xffffffff, true); /* recursive, at root */
    else
    {
        const struct dircache_entry *entry=mtp_handle_to_dircache_entry(obj_handle_parent, true); /* handle root */
        if(entry == NULL)
            return fail_op_with(ERROR_INVALID_OBJ_HANDLE, SEND_DATA_PHASE);
        else
            list_files(stor_id, entry, false); /* not recursive, at entry */
    }
}

static uint32_t get_object_storage_id(const struct dircache_entry *entry);

void get_object_info(uint32_t object_handle)
{
    const struct dircache_entry *entry = mtp_handle_to_dircache_entry(object_handle, false);
    struct tm filetm;
    logf("mtp: get object info: entry=\"%s\" attr=0x%x", entry->d_name, entry->attribute);
    
    struct object_info oi;
    oi.storage_id = get_object_storage_id(entry);
    oi.object_format = (entry->attribute & ATTR_DIRECTORY) ? OBJ_FMT_ASSOCIATION : OBJ_FMT_UNDEFINED;
    oi.protection = 0x0000;
    oi.compressed_size = entry->size;
    oi.thumb_fmt = 0;
    oi.thumb_compressed_size = 0;
    oi.thumb_pix_width = 0;
    oi.thumb_pix_height = 0;
    oi.image_pix_width = 0;
    oi.image_pix_height = 0;
    oi.image_bit_depth = 0;
    oi.parent_handle = dircache_entry_to_mtp_handle(entry->up); /* works also for root(s) */
    oi.association_type = (entry->attribute & ATTR_DIRECTORY) ? ASSOC_TYPE_FOLDER : ASSOC_TYPE_NONE;
    oi.association_desc = (entry->attribute & ATTR_DIRECTORY) ? 0x1 : 0x0;
    oi.sequence_number = 0;
    
    start_pack_data_block();
    pack_data_block_ptr(&oi, sizeof(oi));
    pack_data_block_string_charz(entry->d_name); /* Filename */
    fat2tm(&filetm, entry->wrtdate, entry->wrttime);
    pack_data_block_date_time(&filetm); /* Date Created */
    pack_data_block_date_time(&filetm); /* Date Modified */
    pack_data_block_string_charz(NULL); /* Keywords */
    finish_pack_data_block();
    
    mtp_cur_resp.code = ERROR_OK;
    mtp_cur_resp.nb_parameters = 0;
    
    send_data_block();
}

struct get_object_st
{
    uint32_t offset;
    uint32_t size;
    int fd;
};

static int get_object_split_routine(void *dest, int size, void *user)
{
    struct get_object_st *st = user;
    /*logf("mtp: continue get object: size=%d dest=0x%lx offset=%lu fd=%d", size, (uint32_t)dest, st->offset, st->fd);*/
    
    lseek(st->fd, st->offset, SEEK_SET);
    if(read(st->fd, dest, size) != size)
    {
        logf("mtp: read failed");
        close(st->fd);
        return -1;
    }
    else
    {
        st->offset += size;
        /*logf("mtp: ok for %d bytes", size);*/
        return size;
    }
}

static void finish_get_object_split_routine(bool error, void *user)
{
    struct get_object_st *st = user;
    close(st->fd);
    
    generic_finish_split_routine(error, user);
}

void get_object(uint32_t object_handle)
{
    const struct dircache_entry *entry = mtp_handle_to_dircache_entry(object_handle, false);
    static struct get_object_st st;
    static char buffer[MAX_PATH];
    
    logf("mtp: get object: entry=\"%s\" attr=0x%x size=%ld", entry->d_name, entry->attribute, entry->size);
    
    /* can't be invalid handle, can't be root, can't be a directory */
    if(entry == NULL || (entry->attribute & ATTR_DIRECTORY))
        return fail_op_with(ERROR_INVALID_OBJ_HANDLE, SEND_DATA_PHASE);
    
    dircache_copy_path(entry, buffer, MAX_PATH);
    /*logf("mtp: get_object: path=\"%s\"", buffer);*/
    
    st.offset = 0;
    st.size = entry->size;
    st.fd = open(buffer, O_RDONLY);
    if(st.fd < 0)
        return fail_op_with(ERROR_GENERAL_ERROR, SEND_DATA_PHASE);
    
    mtp_cur_resp.code = ERROR_OK;
    mtp_cur_resp.nb_parameters = 0;
    
    send_split_data(entry->size, &get_object_split_routine, &finish_get_object_split_routine, &st);
}

void get_partial_object(uint32_t object_handle,uint32_t offset,uint32_t max_size)
{
    const struct dircache_entry *entry = mtp_handle_to_dircache_entry(object_handle, false);
    static struct get_object_st st;
    static char buffer[MAX_PATH];
    
    logf("mtp: get partial object: entry=\"%s\" attr=0x%x size=%ld off=%lu max_asked_size=%lu",
        entry->d_name, entry->attribute, entry->size, offset, max_size);
    
    /* can't be invalid handle, can't be root, can't be a directory */
    if(entry == NULL || (entry->attribute & ATTR_DIRECTORY))
        return fail_op_with(ERROR_INVALID_OBJ_HANDLE, SEND_DATA_PHASE);
    /* offset can't be beyond end of file */
    if(offset > (uint32_t)entry->size)
        return fail_op_with(ERROR_INVALID_PARAMETER, SEND_DATA_PHASE);
    
    dircache_copy_path(entry, buffer, MAX_PATH);
    /*logf("mtp: get partial object: path=\"%s\"", buffer);*/
    
    st.offset = offset;
    st.size = MIN(entry->size - offset, max_size);
    st.fd = open(buffer, O_RDONLY);
    if(st.fd < 0)
        return fail_op_with(ERROR_GENERAL_ERROR, SEND_DATA_PHASE);
    
    mtp_cur_resp.code = ERROR_OK;
    mtp_cur_resp.nb_parameters = 1;
    mtp_cur_resp.param[0] = st.size;
    
    send_split_data(entry->size, &get_object_split_routine, &finish_get_object_split_routine, &st);
}

struct send_object_info_st
{
    uint32_t stor_id;
    uint32_t obj_handle_parent;
};

static void send_object_info_split_routine(unsigned char *data, int length, uint32_t rem_bytes, void *user)
{
    struct send_object_info_st *st = user;
    struct object_info oi;
    static char filename[MAX_PATH];
    static char path[MAX_PATH];
    uint32_t path_len;
    uint32_t filename_len;
    const struct dircache_entry *this_entry;
    const struct dircache_entry *parent_entry = mtp_handle_to_dircache_entry(st->obj_handle_parent, false);

    logf("mtp: send_object_info_split_routine stor_id=0x%lx obj_handle_parent=0x%lx", st->stor_id, st->obj_handle_parent);

    if (st->obj_handle_parent == 0xffffffff)
    {
        path_len = 0;
    }
    else
    {
        dircache_copy_path(parent_entry, path, MAX_PATH);
        path_len = strlen(path);
    }

    if (0 != rem_bytes) /* Does the ObjectInfo span multiple packets? (unhandled case) */
        return fail_op_with(ERROR_INVALID_DATASET, RECV_DATA_PHASE); /* NOTE continue reception and throw data */

    start_unpack_data_block(data, length);
    if (!unpack_data_block_ptr(&oi, sizeof(oi)))
        return fail_op_with(ERROR_INVALID_DATASET, NO_DATA_PHASE);
    if (!unpack_data_block_string_charz(filename, sizeof(filename))) /* Filename */
        return fail_op_with(ERROR_INVALID_DATASET, NO_DATA_PHASE);
    if (!unpack_data_block_string_charz(NULL, 0)) /* Date Created */
        return fail_op_with(ERROR_INVALID_DATASET, NO_DATA_PHASE);
    if (!unpack_data_block_string_charz(NULL, 0)) /* Date Modified */
        return fail_op_with(ERROR_INVALID_DATASET, NO_DATA_PHASE);
    if (!unpack_data_block_string_charz(NULL, 0)) /* Keywords */
        return fail_op_with(ERROR_INVALID_DATASET, NO_DATA_PHASE);
    if (!finish_unpack_data_block())
        return fail_op_with(ERROR_INVALID_DATASET, NO_DATA_PHASE);

    logf("mtp: successfully unpacked");

    if (path_len < MAX_PATH-1)
    {
        path[path_len] = '/';
        path[path_len+1] = 0;
        path_len++;
    }
    else
        return fail_op_with(ERROR_GENERAL_ERROR, NO_DATA_PHASE);

    filename_len = strlen(filename);
    if ((path_len + filename_len) < MAX_PATH-1)
        strlcpy(path+path_len, filename, MAX_PATH-path_len);
    else
        return fail_op_with(ERROR_GENERAL_ERROR, NO_DATA_PHASE);

    logf("mtp: path = %s", path);

    if(oi.object_format == OBJ_FMT_ASSOCIATION)
    {
        /* sanity check */
        /* Too strong check
        if(oi.association_type != ASSOC_TYPE_FOLDER)
            return fail_op_with(ERROR_INVALID_DATASET, NO_DATA_PHASE);
        */
        /* create directory */
        if(mkdir(path) < 0)
            return fail_op_with(ERROR_GENERAL_ERROR, NO_DATA_PHASE);
    }
    else
    {
        /* create empty file */
        int fd = open(path, O_RDWR|O_CREAT|O_TRUNC);
        if(fd < 0)
            return fail_op_with(ERROR_GENERAL_ERROR, NO_DATA_PHASE);
        close(fd);
    }
    
    logf("mtp: object created");

    /* get object pointer */
    this_entry = dircache_get_entry_ptr(path);
    /* to one level up for directory */
    if(this_entry != NULL && oi.object_format == OBJ_FMT_ASSOCIATION)
        this_entry = this_entry->up;
    if(this_entry == NULL)
        return fail_op_with(ERROR_GENERAL_ERROR, NO_DATA_PHASE);
 
    /* if file size is nonzero, add a pending OI (except if there is one) */
    if(oi.object_format != OBJ_FMT_ASSOCIATION && oi.compressed_size!=0)
    {
        if(mtp_state.has_pending_oi)
            return fail_op_with(ERROR_GENERAL_ERROR, NO_DATA_PHASE);
        mtp_state.pending_oi.handle = dircache_entry_to_mtp_handle(this_entry);
        mtp_state.pending_oi.size = oi.compressed_size;
        mtp_state.has_pending_oi = true;
    }
 
    mtp_cur_resp.code = ERROR_OK;
    mtp_cur_resp.nb_parameters = 3;
    mtp_cur_resp.param[0] = st->stor_id;
    mtp_cur_resp.param[1] = st->obj_handle_parent;
    mtp_cur_resp.param[2] = dircache_entry_to_mtp_handle(this_entry);
}

static void finish_send_object_info_split_routine(bool error, void *user)
{
    generic_finish_split_routine(error, user);
}

void send_object_info(int nb_params, uint32_t stor_id, uint32_t obj_handle_parent)
{
    static struct send_object_info_st st;
    const struct dircache_entry *parent_entry = mtp_handle_to_dircache_entry(obj_handle_parent, false);

    logf("mtp: send object info: stor_id=0x%lx obj_handle_parent=0x%lx", stor_id, obj_handle_parent);

    /* default store is main store */
    if (nb_params < 1)
        stor_id = 0x00010001;

    if (stor_id != 0x00000000 && stor_id != 0x00010001)
        return fail_op_with(ERROR_INVALID_STORAGE_ID, RECV_DATA_PHASE);

    stor_id = 0x00010001;
    /* default parent if root */
    if (nb_params < 2 || obj_handle_parent == 0x00000000)
        obj_handle_parent = 0xffffffff;

    /* check parent is root or a valid directory */
    if (obj_handle_parent != 0xffffffff)
    {
        if (parent_entry == NULL)
            return fail_op_with(ERROR_INVALID_OBJ_HANDLE, RECV_DATA_PHASE);

        if (!(parent_entry->attribute & ATTR_DIRECTORY))
            return fail_op_with(ERROR_INVALID_PARENT_OBJ, RECV_DATA_PHASE);
    }

    st.stor_id = stor_id;
    st.obj_handle_parent = obj_handle_parent;

    logf("mtp: stor_id=0x%lx obj_handle_parent=0x%lx", stor_id, obj_handle_parent);

    receive_split_data(&send_object_info_split_routine, &finish_send_object_info_split_routine, &st);
}

struct send_object_st
{
    int fd;
    bool first_xfer;
};

static void send_object_split_routine(unsigned char *data, int length, uint32_t rem_bytes, void *user)
{
    struct send_object_st *st = user;
    
    if(st->first_xfer)
    {
        /* compute total xfer size */
        uint32_t total_size=length + rem_bytes;
        /* check it's equal to expected size */
        if(total_size != mtp_state.pending_oi.size)
            return fail_op_with(ERROR_INCOMPLETE_TRANSFER, RECV_DATA_PHASE);
        
        st->first_xfer = false;
    }
    
    if(write(st->fd, data, length) < 0)
    {
        logf("mtp: write error: errno=%d", errno);
        return fail_op_with(ERROR_GENERAL_ERROR, RECV_DATA_PHASE);
    }
}

static void finish_send_object_split_routine(bool error, void *user)
{
    (void)error;
    struct send_object_st *st = user;
    
    logf("mtp: finish send file");
    
    close(st->fd);
    mtp_state.has_pending_oi = false;
    
    mtp_cur_resp.code = ERROR_OK;
    mtp_cur_resp.nb_parameters = 0;
    
    send_response();
}

void send_object(void)
{
    static struct send_object_st st;
    static char path[MAX_PATH];
    const struct dircache_entry *entry = mtp_handle_to_dircache_entry(mtp_state.pending_oi.handle, false);
    /* check there is a pending OI */
    if(!mtp_state.has_pending_oi)
        return fail_op_with(ERROR_NO_VALID_OBJECTINFO, RECV_DATA_PHASE);
    
    logf("mtp: send object: associated objectinfo=0x%lx", mtp_state.pending_oi.handle);
    
    if(entry == NULL)
        return fail_op_with(ERROR_NO_VALID_OBJECTINFO, RECV_DATA_PHASE);
    dircache_copy_path(entry, path, sizeof(path));
    
    st.fd = open(path, O_RDWR|O_TRUNC);
    if(st.fd < 0)
        return fail_op_with(ERROR_NO_VALID_OBJECTINFO, RECV_DATA_PHASE);
    st.first_xfer = true;
    /* wait data */
    receive_split_data(&send_object_split_routine, &finish_send_object_split_routine, &st);
}

static bool recursive_delete(const struct dircache_entry *entry)
{
    static char path[MAX_PATH];
    const struct dircache_entry *cur;
    bool bret;
    
    if(entry->attribute & ATTR_DIRECTORY)
    {
        for(cur = entry->down; cur != NULL; cur = cur->next)
        {
            if(!is_dircache_entry_valid(cur))
                continue;
            /* skip "." and ".." and files that begin with "<"*/
            if(cur->d_name[0] == '.' && cur->d_name[1] == '\0')
                continue;
            if(cur->d_name[0] == '.' && cur->d_name[1] == '.'  && cur->d_name[2] == '\0')
                continue;
            if(cur->d_name[0] == '<')
                continue;

            dircache_copy_path(cur, path, MAX_PATH);
            bret = recursive_delete(cur);
            if(!bret)
                return false;
            /* NOTE: assume removal does not invalidates valid dircache entries and 
             does not the changed their relative order
            */
        }
        
        dircache_copy_path(entry, path, MAX_PATH);
        logf("mtp: delete dir '%s'", path);
        int ret = rmdir(path);
        if(ret != 0)
            logf("mtp: error: rmdir ret=%d", ret);
        return ret == 0;
    }
    else
    {
        dircache_copy_path(entry, path, MAX_PATH);
        logf("mtp: delete file '%s'", path);
        int ret = remove(path);
        if(ret != 0)
            logf("mtp: error: remove ret=%d", ret);
        return ret == 0;
    }
}

void delete_object(int nb_params, uint32_t obj_handle, uint32_t __unused)
{
    if(nb_params == 2 && __unused != 0x00000000)
        return fail_op_with(ERROR_SPEC_BY_FMT_UNSUPPORTED, NO_DATA_PHASE);
    
    const struct dircache_entry *entry = mtp_handle_to_dircache_entry(obj_handle, false); /* don't allow to destroy / */
    if(entry == NULL)
        return fail_op_with(ERROR_INVALID_OBJ_HANDLE, NO_DATA_PHASE);
    
    bool ret = recursive_delete(entry);
    
    mtp_cur_resp.code = ret ? ERROR_OK : ERROR_PARTIAL_DELETION;
    mtp_cur_resp.nb_parameters = 0;
    
    send_response();
}

void copy_object(int nb_params, uint32_t obj_handle, uint32_t stor_id, uint32_t obj_parent_handle)
{
    const struct dircache_entry *entry = mtp_handle_to_dircache_entry(obj_handle, false);
    if(stor_id != 0x00010001)
        return fail_op_with(ERROR_INVALID_STORAGE_ID, NO_DATA_PHASE);
    
    if(nb_params == 2)
        obj_parent_handle = 0xffffffff;
    
    const struct dircache_entry *parent_entry = mtp_handle_to_dircache_entry(obj_parent_handle, true);
    if(parent_entry == NULL)
        return fail_op_with(ERROR_INVALID_PARENT_OBJ, NO_DATA_PHASE);
    if((uint32_t)parent_entry != 0xffffffff && !(parent_entry->attribute & ATTR_DIRECTORY))
        return fail_op_with(ERROR_INVALID_PARENT_OBJ, NO_DATA_PHASE);
    
    static char source_path[MAX_PATH];
    static char dest_path[MAX_PATH];
    
    dircache_copy_path(entry, source_path, MAX_PATH);
    if((uint32_t)parent_entry != 0xffffffff)
        dircache_copy_path(parent_entry, dest_path, MAX_PATH);
    else
        strcpy(dest_path,"");

    logf("mtp: copy object: '%s' -> '%s'", source_path, dest_path);
    
    return fail_op_with(ERROR_GENERAL_ERROR, NO_DATA_PHASE);
}

void move_object(int nb_params, uint32_t obj_handle, uint32_t stor_id, uint32_t obj_parent_handle)
{
    /* FIXME it's impossible to implement the current specification in a simple way because
      the spec requires that the object handle doesn't change. As the mvoe will change the handle,
      the only way to implement that is to have conversion a table and even with this it would fail in
      tricky cases because empty entries are reused by dircache.
      Perhaps we should not implement move_object or perhaps this detail in the specification
      was not taken into account it any MTP implementation */
    const struct dircache_entry *entry = mtp_handle_to_dircache_entry(obj_handle, false);
    if(stor_id != 0x00010001)
        return fail_op_with(ERROR_INVALID_STORAGE_ID, NO_DATA_PHASE);
    
    if(nb_params == 2)
        obj_parent_handle = 0xffffffff;
    
    const struct dircache_entry *parent_entry = mtp_handle_to_dircache_entry(obj_parent_handle, true);
    if(parent_entry == NULL)
        return fail_op_with(ERROR_INVALID_PARENT_OBJ, NO_DATA_PHASE);
    if((uint32_t)parent_entry != 0xffffffff && !(parent_entry->attribute & ATTR_DIRECTORY))
        return fail_op_with(ERROR_INVALID_PARENT_OBJ, NO_DATA_PHASE);
    
    static char source_path[MAX_PATH];
    static char dest_path[MAX_PATH];
    
    dircache_copy_path(entry, source_path, MAX_PATH);
    if((uint32_t)parent_entry != 0xffffffff)
        dircache_copy_path(parent_entry, dest_path, MAX_PATH);
    else
        strcpy(dest_path,"");

    logf("mtp: move object: '%s' -> '%s'", source_path, dest_path);
    
    return fail_op_with(ERROR_GENERAL_ERROR, NO_DATA_PHASE);
}

void get_object_references(uint32_t object_handle)
{
    const struct dircache_entry *entry=mtp_handle_to_dircache_entry(object_handle, false);
    if(entry == NULL)
        return fail_op_with(ERROR_INVALID_OBJ_HANDLE, SEND_DATA_PHASE);
    
    logf("mtp: get object references: handle=0x%lx (\"%s\")", object_handle, entry->d_name);
    
    if(entry->attribute & ATTR_DIRECTORY)
        return list_files(0, entry, false); /* not recursive, at entry */
    else
    {
        start_pack_data_block();
        start_pack_data_block_array();
        finish_pack_data_block_array();
        finish_pack_data_block();
        
        mtp_cur_resp.code = ERROR_OK;
        mtp_cur_resp.nb_parameters = 0;
    
        send_data_block();
    }
}

void reset_device(void)
{
    logf("mtp: reset device");
    
    close_session(true);
}

/*
 * Device properties
 */

static void get_battery_level(bool want_desc)
{
    logf("mtp: get battery level desc/value");
    
    start_pack_data_block();
    if(want_desc)
    {
        pack_data_block_uint16_t(DEV_PROP_BATTERY_LEVEL); /* Device Prop Code */
        pack_data_block_uint16_t(TYPE_UINT8); /* Data Type */
        pack_data_block_uint8_t(DEV_PROP_GET); /* Get/Set */
        pack_data_block_uint8_t(battery_level()); /* Factory Default Value */
    }
    pack_data_block_uint8_t(battery_level()); /* Current Value */
    if(want_desc)
    {
        pack_data_block_uint8_t(DEV_PROP_FORM_RANGE); /* Form Flag */
        /* Form */
        pack_data_block_uint8_t(0); /* Minimum Value */
        pack_data_block_uint8_t(100); /* Maximum Value */
        pack_data_block_uint8_t(1); /* Step Size */
    }
    finish_pack_data_block();
    
    mtp_cur_resp.code = ERROR_OK;
    mtp_cur_resp.nb_parameters = 0;
    
    send_data_block();
}

static void get_date_time(bool want_desc)
{
    logf("mtp: get date time desc/value");
    
    start_pack_data_block();
    if(want_desc)
    {
        pack_data_block_uint16_t(DEV_PROP_BATTERY_LEVEL); /* Device Prop Code */
        pack_data_block_uint16_t(TYPE_STR); /* Data Type */
        pack_data_block_uint8_t(DEV_PROP_GET); /* Get/Set */
        pack_data_block_date_time(get_time()); /* Factory Default Value */
    }
    pack_data_block_date_time(get_time()); /* Current Value */
    if(want_desc)
        pack_data_block_uint8_t(DEV_PROP_FORM_NONE); /* Form Flag */
    finish_pack_data_block();
    
    mtp_cur_resp.code = ERROR_OK;
    mtp_cur_resp.nb_parameters = 0;
    
    send_data_block();
}

static void get_friendly_name(bool want_desc)
{
    logf("mtp: get friendly name desc");
    
    start_pack_data_block();
    if(want_desc)
    {
        pack_data_block_uint16_t(DEV_PROP_BATTERY_LEVEL); /* Device Prop Code */
        pack_data_block_uint16_t(TYPE_STR); /* Data Type */
        pack_data_block_uint8_t(DEV_PROP_GET_SET); /* Get/Set */
        pack_data_block_string(&device_friendly_name); /* Factory Default Value */
    }
    pack_data_block_string(&device_friendly_name); /* Current Value */
    if(want_desc)
        pack_data_block_uint8_t(DEV_PROP_FORM_NONE); /* Form Flag */
    finish_pack_data_block();
    
    mtp_cur_resp.code = ERROR_OK;
    mtp_cur_resp.nb_parameters = 0;
    
    send_data_block();
}

void get_device_prop_desc(uint32_t device_prop)
{
    switch(device_prop)
    {
        case DEV_PROP_BATTERY_LEVEL: return get_battery_level(true);
        case DEV_PROP_DATE_TIME: return get_date_time(true);
        case DEV_PROP_FRIENDLY_NAME: return get_friendly_name(true);
        default: 
            logf("mtp: unsupported device property %lx", device_prop);
            return fail_op_with(ERROR_DEV_PROP_NOT_SUPPORTED, SEND_DATA_PHASE);
    }
}

void get_device_prop_value(uint32_t device_prop)
{
    switch(device_prop)
    {
        case DEV_PROP_BATTERY_LEVEL: return get_battery_level(false);
        case DEV_PROP_DATE_TIME: return get_date_time(false);
        case DEV_PROP_FRIENDLY_NAME: return get_friendly_name(false);
        default: 
            logf("mtp: unsupported device property %lx", device_prop);
            return fail_op_with(ERROR_DEV_PROP_NOT_SUPPORTED, SEND_DATA_PHASE);
    }
}

/*
 * Object properties
 */
static const struct mtp_array_uint16_t 
prop_fmt_all = {2, {OBJ_FMT_UNDEFINED, OBJ_FMT_ASSOCIATION}} ,
prop_fmt_undefined = {1, {OBJ_FMT_UNDEFINED}} ,
prop_fmt_association = {1, {OBJ_FMT_ASSOCIATION}};

static const uint32_t
prop_stor_id_default = 0x00000000,
prop_assoc_desc_default = 0x00000000,
prop_obj_size_default = 0x00000000,
prop_parent_obj_default = 0x00000000;

static const uint16_t
prop_obj_fmt_default = OBJ_FMT_UNDEFINED,
prop_assoc_type_default = ASSOC_TYPE_FOLDER,
prop_hidden_default = 0x0000,
prop_system_default = 0x0000,
prop_assoc_type_enum[] = {1, ASSOC_TYPE_FOLDER},
prop_hidden_enum[] = {2, 0x0000, 0x0001},
prop_system_enum[] = {2, 0x0000, 0x0001};

static const struct mtp_string
prop_filename_default = {0, {}},
prop_name_default = {0, {}},
prop_c_date_default = {0, {}},
prop_m_date_default = {0, {}};

static void prop_stor_id_get(const struct dircache_entry *);
static void prop_obj_fmt_get(const struct dircache_entry *);
static void prop_assoc_type_get(const struct dircache_entry *);
static void prop_assoc_desc_get(const struct dircache_entry *);
static void prop_obj_size_get(const struct dircache_entry *);
static void prop_filename_get(const struct dircache_entry *);
static void prop_c_date_get(const struct dircache_entry *);
static void prop_m_date_get(const struct dircache_entry *);
static void prop_parent_obj_get(const struct dircache_entry *);
static void prop_hidden_get(const struct dircache_entry *);
static void prop_system_get(const struct dircache_entry *);
static void prop_name_get(const struct dircache_entry *);

static const struct mtp_obj_prop mtp_obj_prop_desc[] =
{
    /* Storage ID */
    {&prop_fmt_all, OBJ_PROP_STORAGE_ID, TYPE_UINT32, OBJ_PROP_GET, 
     &prop_stor_id_default, OBJ_PROP_FORM_NONE, NULL, &prop_stor_id_get},
    /* Object Format */
    {&prop_fmt_all, OBJ_PROP_OBJ_FMT, TYPE_UINT16, OBJ_PROP_GET,
     &prop_obj_fmt_default, OBJ_PROP_FORM_NONE, NULL, &prop_obj_fmt_get},
    /* Association Type */
    {&prop_fmt_association, OBJ_PROP_ASSOC_TYPE, TYPE_UINT16, OBJ_PROP_GET,
     &prop_assoc_type_default, OBJ_PROP_FORM_ENUM, &prop_assoc_type_enum, &prop_assoc_type_get},
    /* Association Desc */
    {&prop_fmt_association, OBJ_PROP_ASSOC_DESC, TYPE_UINT32, OBJ_PROP_GET,
     &prop_assoc_desc_default, OBJ_PROP_FORM_NONE, NULL, &prop_assoc_desc_get},
    /* Object Size */
    {&prop_fmt_all, OBJ_PROP_OBJ_SIZE, TYPE_UINT32, OBJ_PROP_GET,
     &prop_obj_size_default, OBJ_PROP_FORM_NONE, NULL, &prop_obj_size_get},
    /* Filename */
    {&prop_fmt_all, OBJ_PROP_FILENAME, TYPE_STR, OBJ_PROP_GET,
     &prop_filename_default, OBJ_PROP_FORM_NONE, NULL, &prop_filename_get},
    /* Creation Date */
    {&prop_fmt_all, OBJ_PROP_C_DATE, TYPE_STR, OBJ_PROP_GET,
     &prop_c_date_default, OBJ_PROP_FORM_DATE, NULL, &prop_c_date_get},
    /* Modification Date */
    {&prop_fmt_all, OBJ_PROP_M_DATE, TYPE_STR, OBJ_PROP_GET,
     &prop_m_date_default, OBJ_PROP_FORM_DATE, NULL, &prop_m_date_get},
    /* Parent Object */
    {&prop_fmt_all, OBJ_PROP_PARENT_OBJ, TYPE_UINT32, OBJ_PROP_GET,
     &prop_parent_obj_default, OBJ_PROP_FORM_NONE, NULL, &prop_parent_obj_get},
    /* Hidden */
    {&prop_fmt_all, OBJ_PROP_HIDDEN, TYPE_UINT16, OBJ_PROP_GET,
     &prop_hidden_default, OBJ_PROP_FORM_ENUM, &prop_hidden_enum, &prop_hidden_get},
    /* System Object */
    {&prop_fmt_all, OBJ_PROP_SYS_OBJ, TYPE_UINT16, OBJ_PROP_GET,
     &prop_system_default, OBJ_PROP_FORM_ENUM, &prop_system_enum, &prop_system_get},
    /* Name */
    {&prop_fmt_all, OBJ_PROP_NAME, TYPE_STR, OBJ_PROP_GET,
     &prop_name_default, OBJ_PROP_FORM_NONE, NULL, &prop_name_get}
};

void get_object_props_supported(uint32_t object_fmt)
{
    logf("mtp: get object props supported: fmt=0x%lx", object_fmt);
    
    uint32_t i, j;
    
    start_pack_data_block();
    start_pack_data_block_array();
    for(i = 0; i < sizeof(mtp_obj_prop_desc)/sizeof(mtp_obj_prop_desc[0]); i++)
        for(j = 0; j < mtp_obj_prop_desc[i].obj_fmt->length; j++)
            if(mtp_obj_prop_desc[i].obj_fmt->data[j] == object_fmt)
                pack_data_block_array_elem_uint16_t(mtp_obj_prop_desc[i].obj_prop_code);
    finish_pack_data_block_array();
    finish_pack_data_block();
    
    mtp_cur_resp.code = ERROR_OK;
    mtp_cur_resp.nb_parameters = 0;
    
    send_data_block();
}

void get_object_prop_desc(uint32_t obj_prop, uint32_t obj_fmt)
{
    logf("mtp: get object props desc: prop=0x%lx fmt=0x%lx", obj_prop, obj_fmt);
    
    uint32_t i, j;
    for(i = 0; i < sizeof(mtp_obj_prop_desc)/sizeof(mtp_obj_prop_desc[0]); i++)
        if(mtp_obj_prop_desc[i].obj_prop_code == obj_prop)
            for(j = 0; j < mtp_obj_prop_desc[i].obj_fmt->length; j++)
                if(mtp_obj_prop_desc[i].obj_fmt->data[j] == obj_fmt)
                    goto Lok;
            
    return fail_op_with(ERROR_INVALID_OBJ_PROP_CODE, SEND_DATA_PHASE);
    
    Lok:
    start_pack_data_block();
    pack_data_block_uint16_t(mtp_obj_prop_desc[i].obj_prop_code);
    pack_data_block_uint16_t(mtp_obj_prop_desc[i].data_type);
    pack_data_block_uint8_t(mtp_obj_prop_desc[i].get_set); /* Get */
    pack_data_block_typed_ptr(mtp_obj_prop_desc[i].default_value,
        mtp_obj_prop_desc[i].data_type); /* Factory Default Value */
    pack_data_block_uint32_t(0xffffffff); /* Group */
    pack_data_block_uint8_t(mtp_obj_prop_desc[i].form); /* Form */
    switch(mtp_obj_prop_desc[i].form)
    {
        case OBJ_PROP_FORM_NONE:
        case OBJ_PROP_FORM_DATE:
            break;
        case OBJ_PROP_FORM_ENUM:
        {
            /* NOTE element have a fixed size so it's safe to use get_type_size */
            uint16_t nb_elems = *(uint16_t *)mtp_obj_prop_desc[i].form_value;
            pack_data_block_ptr(mtp_obj_prop_desc[i].form_value, 
                sizeof(uint16_t) + nb_elems * get_type_size(mtp_obj_prop_desc[i].data_type));
            break;
        }
        default:
            break;
    }
    finish_pack_data_block();
    
    mtp_cur_resp.code = ERROR_OK;
    mtp_cur_resp.nb_parameters = 0;
    
    send_data_block();
}

static uint16_t get_object_format(const struct dircache_entry *entry)
{
    if(entry->attribute & ATTR_DIRECTORY)
        return OBJ_FMT_ASSOCIATION;
    else
        return OBJ_FMT_UNDEFINED;
}

static uint32_t get_object_storage_id(const struct dircache_entry *entry)
{
    /* FIXME ugly but efficient */
    while(entry->up)
    {
        entry = entry->up;
        if(entry->d_name[0] == '<')
            return volume_to_storage_id(entry->d_name[VOL_ENUM_POS] - '0');
    }
    
    return volume_to_storage_id(0);
}

void get_object_prop_value(uint32_t obj_handle, uint32_t obj_prop)
{
    logf("mtp: get object props value: handle=0x%lx prop=0x%lx", obj_handle, obj_prop);
    
    const struct dircache_entry *entry = mtp_handle_to_dircache_entry(obj_handle, false);
    if(entry == NULL)
        return fail_op_with(ERROR_INVALID_OBJ_HANDLE, SEND_DATA_PHASE);
    
    uint16_t obj_fmt = get_object_format(entry);
    uint32_t i, j;
    for(i = 0; i < sizeof(mtp_obj_prop_desc)/sizeof(mtp_obj_prop_desc[0]); i++)
        if(mtp_obj_prop_desc[i].obj_prop_code == obj_prop)
            for(j = 0; j < mtp_obj_prop_desc[i].obj_fmt->length; j++)
                if(mtp_obj_prop_desc[i].obj_fmt->data[j] == obj_fmt)
                    goto Lok;
    return fail_op_with(ERROR_INVALID_OBJ_PROP_CODE, SEND_DATA_PHASE);
    
    Lok:
    start_pack_data_block();
    mtp_obj_prop_desc[i].get(entry);
    finish_pack_data_block();
    
    mtp_cur_resp.code = ERROR_OK;
    mtp_cur_resp.nb_parameters = 0;
    
    send_data_block();
}

static void prop_stor_id_get(const struct dircache_entry *entry)
{
    pack_data_block_uint32_t(get_object_storage_id(entry));
}

static void prop_obj_fmt_get(const struct dircache_entry *entry)
{
    pack_data_block_uint16_t(get_object_format(entry));
}

static void prop_assoc_type_get(const struct dircache_entry *entry)
{
    (void) entry;
    pack_data_block_uint16_t(ASSOC_TYPE_FOLDER);
}

static void prop_assoc_desc_get(const struct dircache_entry *entry)
{
    (void) entry;
    pack_data_block_uint32_t(0x1);
}

static void prop_obj_size_get(const struct dircache_entry *entry)
{
    pack_data_block_uint32_t(entry->size);
}

static void prop_filename_get(const struct dircache_entry *entry)
{
    pack_data_block_string_charz(entry->d_name);
}

static void prop_c_date_get(const struct dircache_entry *entry)
{
    struct tm filetm;
    fat2tm(&filetm, entry->wrtdate, entry->wrttime);
    pack_data_block_date_time(&filetm);
}

static void prop_m_date_get(const struct dircache_entry *entry)
{
    struct tm filetm;
    fat2tm(&filetm, entry->wrtdate, entry->wrttime);
    pack_data_block_date_time(&filetm);
}

static void prop_parent_obj_get(const struct dircache_entry *entry)
{
    pack_data_block_uint32_t(dircache_entry_to_mtp_handle(entry->up));
}

static void prop_hidden_get(const struct dircache_entry *entry)
{
    if(entry->attribute & ATTR_HIDDEN)
        pack_data_block_uint16_t(0x1);
    else
        pack_data_block_uint16_t(0x0);
}

static void prop_system_get(const struct dircache_entry *entry)
{
    if(entry->attribute & ATTR_SYSTEM)
        pack_data_block_uint16_t(0x1);
    else
        pack_data_block_uint16_t(0x0);
}

static void prop_name_get(const struct dircache_entry *entry)
{
    static char path[MAX_PATH];
    dircache_copy_path(entry, path, MAX_PATH);
    pack_data_block_string_charz(path);
}

