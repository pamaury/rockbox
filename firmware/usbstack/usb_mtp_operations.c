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
        static char path[MAX_PATH];
        copy_object_path(mtp_state.pending_oi.handle, path, sizeof(path));
        logf("mtp: remove unfinished pending object: \"%s\"", path);
        remove(path);
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
    pack_data_block_string_charz(MODEL_NAME);
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
    
    start_pack_data_block();
    pack_data_block_uint16_t(STOR_TYPE_FIXED_RAM); /* Storage Type */
    pack_data_block_uint16_t(FS_TYPE_GENERIC_HIERARCHICAL); /* Filesystem Type */
    pack_data_block_uint16_t(ACCESS_CAP_RW); /* Access Capability */
    pack_data_block_uint64_t(get_storage_size(stor_id)); /* Max Capacity (optional for read only) */
    pack_data_block_uint64_t(get_storage_free_space(stor_id)); /* Free Space in bytes (optional for read only) */
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

static unsigned list_and_pack_files_lff(uint32_t stor_id, uint32_t obj_handle, void *arg)
{
    bool *recursive = arg;
    (void) stor_id;
    
    pack_data_block_array_elem_uint32_t(obj_handle);
    
    if(*recursive)
        return LF_CONTINUE|LF_ENTER;
    else
        return LF_CONTINUE|LF_SKIP;
}

static void list_and_pack_files(uint32_t stor_id, uint32_t obj_handle, bool recursive)
{
    uint16_t err;
    
    start_pack_data_block();
    start_pack_data_block_array();
    
    err = generic_list_files(stor_id, obj_handle, &list_and_pack_files_lff, &recursive);
    logf("mtp: lapf h=0x%lx c=%lu",obj_handle,finish_pack_data_block_array());
    finish_pack_data_block();
    
    /* if an error occured, restart packing to have a zero size overhead */
    if(err != ERROR_OK)
        return fail_op_with(err, SEND_DATA_PHASE);

    mtp_cur_resp.code = err;
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
        list_and_pack_files(stor_id, 0xffffffff, true); /* recursive, at root */
    else
    {
        if(!is_valid_object_handle(obj_handle_parent, true)) /* handle root */
            return fail_op_with(ERROR_INVALID_OBJ_HANDLE, SEND_DATA_PHASE);
        else
            list_and_pack_files(stor_id, obj_handle_parent, false); /* not recursive, at entry */
    }
}

void get_object_info(uint32_t handle)
{
    if(!is_valid_object_handle(handle, false))
        return fail_op_with(ERROR_INVALID_OBJ_HANDLE, SEND_DATA_PHASE);
    
    struct tm filetm;
    logf("mtp: get object info: %s", get_object_filename(handle));
    
    struct object_info oi;
    oi.storage_id = get_object_storage_id(handle);
    oi.object_format = is_directory_object(handle) ? OBJ_FMT_ASSOCIATION : OBJ_FMT_UNDEFINED;
    oi.protection = 0x0000;
    oi.compressed_size = get_object_size(handle);
    oi.thumb_fmt = 0;
    oi.thumb_compressed_size = 0;
    oi.thumb_pix_width = 0;
    oi.thumb_pix_height = 0;
    oi.image_pix_width = 0;
    oi.image_pix_height = 0;
    oi.image_bit_depth = 0;
    oi.parent_handle = get_parent_object(handle); /* works also for root(s) */
    oi.association_type = is_directory_object(handle) ? ASSOC_TYPE_FOLDER : ASSOC_TYPE_NONE;
    oi.association_desc = is_directory_object(handle) ? 0x1 : 0x0;
    oi.sequence_number = 0;
    
    start_pack_data_block();
    pack_data_block_ptr(&oi, sizeof(oi));
    pack_data_block_string_charz(get_object_filename(handle)); /* Filename */
    copy_object_date_created(handle, &filetm);
    pack_data_block_date_time(&filetm); /* Date Created */
    copy_object_date_modified(handle, &filetm);
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

void get_object(uint32_t handle)
{
    static struct get_object_st st;
    static char buffer[MAX_PATH];
    
    /* can't be invalid handle, can't be root, can't be a directory */
    if(!is_valid_object_handle(handle, false) || is_directory_object(handle))
        return fail_op_with(ERROR_INVALID_OBJ_HANDLE, SEND_DATA_PHASE);
    
    logf("mtp: get object: %s size=%lu", get_object_filename(handle), get_object_size(handle));
    
    copy_object_path(handle, buffer, MAX_PATH);
    /*logf("mtp: get_object: path=\"%s\"", buffer);*/
    
    st.offset = 0;
    st.size = get_object_size(handle);
    st.fd = open(buffer, O_RDONLY);
    if(st.fd < 0)
        return fail_op_with(ERROR_GENERAL_ERROR, SEND_DATA_PHASE);
    
    mtp_cur_resp.code = ERROR_OK;
    mtp_cur_resp.nb_parameters = 0;
    
    send_split_data(st.size, &get_object_split_routine, &finish_get_object_split_routine, &st);
}

void get_partial_object(uint32_t handle,uint32_t offset,uint32_t max_size)
{
    static struct get_object_st st;
    static char buffer[MAX_PATH];
    
    /* can't be invalid handle, can't be root, can't be a directory */
    if(!is_valid_object_handle(handle, false) || is_directory_object(handle))
        return fail_op_with(ERROR_INVALID_OBJ_HANDLE, SEND_DATA_PHASE);
    
    logf("mtp: get partial object: %s size=%lu off=%lu max_size=%lu", get_object_filename(handle), 
        get_object_size(handle), offset, max_size);
    
    /* offset can't be beyond end of file */
    if(offset > get_object_size(handle))
        return fail_op_with(ERROR_INVALID_PARAMETER, SEND_DATA_PHASE);
    
    copy_object_path(handle, buffer, MAX_PATH);
    /*logf("mtp: get partial object: path=\"%s\"", buffer);*/
    
    st.offset = offset;
    st.size = MIN(get_object_size(handle) - offset, max_size);
    st.fd = open(buffer, O_RDONLY);
    if(st.fd < 0)
        return fail_op_with(ERROR_GENERAL_ERROR, SEND_DATA_PHASE);
    
    mtp_cur_resp.code = ERROR_OK;
    mtp_cur_resp.nb_parameters = 1;
    mtp_cur_resp.param[0] = st.size;
    
    send_split_data(st.size, &get_object_split_routine, &finish_get_object_split_routine, &st);
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
    uint32_t this_handle;

    logf("mtp: send_object_info_split_routine stor_id=0x%lx obj_handle_parent=0x%lx", st->stor_id, st->obj_handle_parent);

    if(st->obj_handle_parent == 0xffffffff)
    {
        strlcpy(path, get_storage_id_mount_point(st->stor_id), MAX_PATH);
        path_len = strlen(path);
    }
    else
    {
        copy_object_path(st->obj_handle_parent, path, MAX_PATH);
        /* add trailing '/' */
        strcat(path, "/"); /* possible overflow */
        path_len = strlen(path);
    }

    if(rem_bytes != 0) /* Does the ObjectInfo span multiple packets? (unhandled case) */
        return fail_op_with(ERROR_INVALID_DATASET, RECV_DATA_PHASE); /* NOTE continue reception and throw data */

    start_unpack_data_block(data, length);
    if(!unpack_data_block_ptr(&oi, sizeof(oi)))
        return fail_op_with(ERROR_INVALID_DATASET, NO_DATA_PHASE);
    if(!unpack_data_block_string_charz(filename, sizeof(filename))) /* Filename */
        return fail_op_with(ERROR_INVALID_DATASET, NO_DATA_PHASE);
    if(!unpack_data_block_string_charz(NULL, 0)) /* Date Created */
        return fail_op_with(ERROR_INVALID_DATASET, NO_DATA_PHASE);
    if(!unpack_data_block_string_charz(NULL, 0)) /* Date Modified */
        return fail_op_with(ERROR_INVALID_DATASET, NO_DATA_PHASE);
    if(!unpack_data_block_string_charz(NULL, 0)) /* Keywords */
        return fail_op_with(ERROR_INVALID_DATASET, NO_DATA_PHASE);
    if(!finish_unpack_data_block())
        return fail_op_with(ERROR_INVALID_DATASET, NO_DATA_PHASE);

    logf("mtp: successfully unpacked");

    filename_len = strlen(filename);
    if((path_len + filename_len) < MAX_PATH-1)
        strlcpy(path + path_len, filename, MAX_PATH-path_len);
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

    this_handle = get_object_handle_by_name(path);
    if(this_handle == 0x00000000)
        return fail_op_with(ERROR_GENERAL_ERROR, NO_DATA_PHASE);
 
    /* if file size is nonzero, add a pending OI (except if there is one) */
    if(oi.object_format != OBJ_FMT_ASSOCIATION && oi.compressed_size != 0)
    {
        if(mtp_state.has_pending_oi)
            return fail_op_with(ERROR_GENERAL_ERROR, NO_DATA_PHASE);
        mtp_state.pending_oi.handle = this_handle;
        mtp_state.pending_oi.size = oi.compressed_size;
        mtp_state.has_pending_oi = true;
    }
 
    mtp_cur_resp.code = ERROR_OK;
    mtp_cur_resp.nb_parameters = 3;
    mtp_cur_resp.param[0] = st->stor_id;
    mtp_cur_resp.param[1] = st->obj_handle_parent;
    mtp_cur_resp.param[2] = this_handle;
}

static void finish_send_object_info_split_routine(bool error, void *user)
{
    generic_finish_split_routine(error, user);
}

void send_object_info(int nb_params, uint32_t stor_id, uint32_t obj_handle_parent)
{
    static struct send_object_info_st st;

    logf("mtp: send object info: stor_id=0x%lx obj_handle_parent=0x%lx", stor_id, obj_handle_parent);

    /* default store is main store */
    if(nb_params < 1 || stor_id == 0x00000000)
        stor_id = get_first_storage_id();

    if(!is_valid_storage_id(stor_id))
        return fail_op_with(ERROR_INVALID_STORAGE_ID, RECV_DATA_PHASE);

    /* default parent if root */
    if(nb_params < 2 || obj_handle_parent == 0x00000000)
        obj_handle_parent = 0xffffffff;

    /* check parent is root or a valid directory */
    if(obj_handle_parent != 0xffffffff)
    {
        if(!is_valid_object_handle(obj_handle_parent, false))
            return fail_op_with(ERROR_INVALID_OBJ_HANDLE, RECV_DATA_PHASE);

        if(!is_directory_object(obj_handle_parent))
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
    /* check there is a pending OI */
    if(!mtp_state.has_pending_oi)
        return fail_op_with(ERROR_NO_VALID_OBJECTINFO, RECV_DATA_PHASE);
    
    logf("mtp: send object: associated objectinfo=0x%lx", mtp_state.pending_oi.handle);
    
    /* sanity check */
    if(!is_valid_object_handle(mtp_state.pending_oi.handle, false))
        return fail_op_with(ERROR_NO_VALID_OBJECTINFO, RECV_DATA_PHASE);
    copy_object_path(mtp_state.pending_oi.handle, path, sizeof(path));
    
    st.fd = open(path, O_RDWR|O_TRUNC);
    if(st.fd < 0)
        return fail_op_with(ERROR_NO_VALID_OBJECTINFO, RECV_DATA_PHASE);
    st.first_xfer = true;
    /* wait data */
    receive_split_data(&send_object_split_routine, &finish_send_object_split_routine, &st);
}

#if 0
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
#endif

void get_object_references(uint32_t object_handle)
{
    /* can't be root it seems */
    if(!is_valid_object_handle(object_handle, false))
        return fail_op_with(ERROR_INVALID_OBJ_HANDLE, SEND_DATA_PHASE);
    
    logf("mtp: get object references: handle=0x%lx (\"%s\")", object_handle, get_object_filename(object_handle));
    
    if(is_directory_object(object_handle))
        return list_and_pack_files(0x00000000, object_handle, false); /* not recursive, at entry */
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

static const persistent_unique_id_t
prop_persistent_unique_id_default = {{0}};

static void prop_stor_id_get(uint32_t handle);
static void prop_obj_fmt_get(uint32_t handle);
static void prop_assoc_type_get(uint32_t handle);
static void prop_assoc_desc_get(uint32_t handle);
static void prop_obj_size_get(uint32_t handle);
static void prop_filename_get(uint32_t handle);
static void prop_c_date_get(uint32_t handle);
static void prop_m_date_get(uint32_t handle);
static void prop_parent_obj_get(uint32_t handle);
static void prop_hidden_get(uint32_t handle);
static void prop_system_get(uint32_t handle);
static void prop_name_get(uint32_t handle);
static void prop_persistent_unique_id_get(uint32_t handle);

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
     &prop_name_default, OBJ_PROP_FORM_NONE, NULL, &prop_name_get},
    /* Persistent Unique Object Id */
    {&prop_fmt_all, OBJ_PROP_PERSISTENT, TYPE_UINT128, OBJ_PROP_GET,
     &prop_persistent_unique_id_default, OBJ_PROP_FORM_NONE, NULL, &prop_persistent_unique_id_get}
};

void get_object_props_supported(uint32_t object_fmt)
{
    //logf("mtp: get object props supported: fmt=0x%lx", object_fmt);
    
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
    //logf("mtp: get object props desc: prop=0x%lx fmt=0x%lx", obj_prop, obj_fmt);
    
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

static uint16_t get_object_format(uint32_t handle)
{
    if(is_directory_object(handle))
        return OBJ_FMT_ASSOCIATION;
    else
        return OBJ_FMT_UNDEFINED;
}

void get_object_prop_value(uint32_t handle, uint32_t obj_prop)
{
    logf("mtp: get object props value: handle=0x%lx prop=0x%lx", handle, obj_prop);
    
    if(!is_valid_object_handle(handle, false))
        return fail_op_with(ERROR_INVALID_OBJ_HANDLE, SEND_DATA_PHASE);
    
    uint16_t obj_fmt = get_object_format(handle);
    uint32_t i, j;
    for(i = 0; i < sizeof(mtp_obj_prop_desc)/sizeof(mtp_obj_prop_desc[0]); i++)
        if(mtp_obj_prop_desc[i].obj_prop_code == obj_prop)
            for(j = 0; j < mtp_obj_prop_desc[i].obj_fmt->length; j++)
                if(mtp_obj_prop_desc[i].obj_fmt->data[j] == obj_fmt)
                    goto Lok;
    return fail_op_with(ERROR_INVALID_OBJ_PROP_CODE, SEND_DATA_PHASE);
    
    Lok:
    start_pack_data_block();
    mtp_obj_prop_desc[i].get(handle);
    finish_pack_data_block();
    
    mtp_cur_resp.code = ERROR_OK;
    mtp_cur_resp.nb_parameters = 0;
    
    send_data_block();
}

#if 0
#define prop_logf logf
#else
#define prop_logf(...)
#endif

static void prop_stor_id_get(uint32_t handle)
{
    prop_logf("mtp: %s -> stor_id=0x%lx",get_object_filename(handle),get_object_storage_id(handle));
    pack_data_block_uint32_t(get_object_storage_id(handle));
}

static void prop_obj_fmt_get(uint32_t handle)
{
    prop_logf("mtp: %s -> obj_fmt=0x%x",get_object_filename(handle),get_object_format(handle));
    pack_data_block_uint16_t(get_object_format(handle));
}

static void prop_assoc_type_get(uint32_t handle)
{
    (void) handle;
    prop_logf("mtp: %s -> assoc_type=0x%x",get_object_filename(handle),ASSOC_TYPE_FOLDER);
    pack_data_block_uint16_t(ASSOC_TYPE_FOLDER);
}

static void prop_assoc_desc_get(uint32_t handle)
{
    (void) handle;
    prop_logf("mtp: %s -> assoc_desc=%u",get_object_filename(handle),0x1);
    pack_data_block_uint32_t(0x1);
}

static void prop_obj_size_get(uint32_t handle)
{
    prop_logf("mtp: %s -> obj_size=%lu",get_object_filename(handle),get_object_size(handle));
    pack_data_block_uint32_t(get_object_size(handle));
}

static void prop_filename_get(uint32_t handle)
{
    prop_logf("mtp: %s -> filename=%s",get_object_filename(handle),get_object_filename(handle));
    pack_data_block_string_charz(get_object_filename(handle));
}

static void prop_c_date_get(uint32_t handle)
{
    struct tm filetm;
    prop_logf("mtp: %s -> c_date=<date>",get_object_filename(handle));
    copy_object_date_created(handle, &filetm);
    pack_data_block_date_time(&filetm);
}

static void prop_m_date_get(uint32_t handle)
{
    struct tm filetm;
    prop_logf("mtp: %s -> m_date=<date>",get_object_filename(handle));
    copy_object_date_modified(handle, &filetm);
    pack_data_block_date_time(&filetm);
}

static void prop_parent_obj_get(uint32_t handle)
{
    prop_logf("mtp: %s -> parent=%lu(%s)",get_object_filename(handle),get_parent_object(handle),
        get_parent_object(handle)==0?NULL:get_object_filename(get_parent_object(handle)));
    pack_data_block_uint32_t(get_parent_object(handle));
}

static void prop_hidden_get(uint32_t handle)
{
    prop_logf("mtp: %s -> hidden=%d",get_object_filename(handle),is_hidden_object(handle));
    if(is_hidden_object(handle))
        pack_data_block_uint16_t(0x1);
    else
        pack_data_block_uint16_t(0x0);
}

static void prop_system_get(uint32_t handle)
{
    prop_logf("mtp: %s -> system=%d",get_object_filename(handle),is_hidden_object(handle));
    if(is_system_object(handle))
        pack_data_block_uint16_t(0x1);
    else
        pack_data_block_uint16_t(0x0);
}

static void prop_name_get(uint32_t handle)
{
    prop_logf("mtp: %s -> name=%s",get_object_filename(handle),get_object_filename(handle));
    pack_data_block_string_charz(get_object_filename(handle));
}

static void prop_persistent_unique_id_get(uint32_t handle)
{
    persistent_unique_id_t pui=get_object_persistent_unique_id(handle);
    prop_logf("mtp: %s -> pui=[0x%lx,0x%lx,0x%lx,0x%lx]",get_object_filename(handle),
        pui.u32[0],pui.u32[1],pui.u32[2],pui.u32[3]);
    pack_data_block_typed_ptr(&pui,TYPE_UINT128);
}

