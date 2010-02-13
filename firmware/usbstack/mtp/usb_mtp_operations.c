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
    23,
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
     MTP_OP_SET_DEV_PROP_VALUE,
     MTP_OP_RESET_DEV_PROP_VALUE,
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
    2 + NB_MTP_AUDIO_FORMATS,
    {OBJ_FMT_UNDEFINED,
     OBJ_FMT_ASSOCIATION,
     ALL_MTP_AUDIO_FORMATS}
};

static const struct mtp_string mtp_manufacturer =
{
    12,
    {'R','o','c','k','b','o','x','.','o','r','g','\0'} /* null-terminated */
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
    oi.object_format = get_object_format(handle);
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
        {
            logf("mtp: oops, couldn't create file");
            return fail_op_with(ERROR_GENERAL_ERROR, NO_DATA_PHASE);
        }
        close(fd);
    }
    
    logf("mtp: object created (size=%lu)", oi.compressed_size);

    this_handle = get_object_handle_by_name(path);
    if(this_handle == 0x00000000)
    {
        logf("mtp: oops, file was no created ?");
        return fail_op_with(ERROR_GENERAL_ERROR, NO_DATA_PHASE);
    }
 
    /* if file size is nonzero, add a pending OI (except if there is one) */
    if(oi.object_format != OBJ_FMT_ASSOCIATION && oi.compressed_size != 0)
    {
        if(mtp_state.has_pending_oi)
        {
            logf("mtp: oops, there is a pending object info");
            return fail_op_with(ERROR_GENERAL_ERROR, NO_DATA_PHASE);
        }
        mtp_state.pending_oi.handle = this_handle;
        mtp_state.pending_oi.size = oi.compressed_size;
        mtp_state.has_pending_oi = true;
        logf("mtp: pending OI created");
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
    {
        logf("mtp: oops, no pending object info !");
        return fail_op_with(ERROR_NO_VALID_OBJECTINFO, RECV_DATA_PHASE);
    }
    
    logf("mtp: send object: associated objectinfo=0x%lx", mtp_state.pending_oi.handle);
    
    /* sanity check */
    if(!is_valid_object_handle(mtp_state.pending_oi.handle, false))
    {
        logf("mtp: oops, pending oi handle is not valid !");
        return fail_op_with(ERROR_NO_VALID_OBJECTINFO, RECV_DATA_PHASE);
    }
    copy_object_path(mtp_state.pending_oi.handle, path, sizeof(path));
    logf("mtp: path=%s", path);
    
    st.fd = open(path, O_RDWR|O_TRUNC);
    if(st.fd < 0)
    {
        logf("mtp: oops, couldn't open pending object !(errno=%d)", errno);
        return fail_op_with(ERROR_NO_VALID_OBJECTINFO, RECV_DATA_PHASE);
    }
    st.first_xfer = true;
    /* wait data */
    receive_split_data(&send_object_split_routine, &finish_send_object_split_routine, &st);
}

static bool recursive_delete(uint32_t handle);

/*
 * WARNING FIXME TODO WTF
 * This code assumes that deleting an entry of a directory does not confuse the list_dir function
 * This is true with dircache, this could be false with other systems, beware !
 */

unsigned lff_delete(uint32_t stor_id,uint32_t handle,void *arg)
{
    bool *bret = (bool *)arg;
    (void) stor_id;
    
    *bret = recursive_delete(handle);
    
    return LF_CONTINUE | LF_ENTER;
}

bool recursive_delete(uint32_t handle)
{
    static char path[MAX_PATH];
    
    if(is_directory_object(handle))
    {
        bool bret;
        uint16_t err = generic_list_files(0x0, handle, &lff_delete, (void *)&bret);
        if(err != ERROR_OK)
        {
            logf("mtp: oops, something went wrong with generic_list_file !");
            return false;
        }
        if(!bret)
            return false;
        
        copy_object_path(handle, path, MAX_PATH);
        logf("mtp: delete dir '%s'", path);
        /*
        int ret = rmdir(path);
        if(ret != 0)
            logf("mtp: error: rmdir ret=%d", ret);
        return ret == 0;
        */
        return true;
    }
    else
    {
        copy_object_path(handle, path, MAX_PATH);
        logf("mtp: delete file '%s'", path);
        /*
        int ret = remove(path);
        if(ret != 0)
            logf("mtp: error: remove ret=%d", ret);
        return ret == 0;
        */
        return true;
    }
}

void delete_object(int nb_params, uint32_t obj_handle, uint32_t obj_format)
{
    /* deletion of files of a certain kind is unsupported for now */
    if(nb_params == 2 && obj_format != 0x00000000)
        return fail_op_with(ERROR_SPEC_BY_FMT_UNSUPPORTED, NO_DATA_PHASE);
    
    /* don't allow to delete root */
    if(!is_valid_object_handle(obj_handle,false))
        return fail_op_with(ERROR_INVALID_OBJ_HANDLE, NO_DATA_PHASE);
    
    logf("mtp: delete object(0x%lx, %s)", obj_handle, get_object_filename(obj_handle));
    
    bool ret = recursive_delete(obj_handle);
    
    mtp_cur_resp.code = ret ? ERROR_OK : ERROR_PARTIAL_DELETION;
    mtp_cur_resp.nb_parameters = 0;
    
    send_response();
}

void copy_object(int nb_params, uint32_t obj_handle, uint32_t stor_id, uint32_t obj_parent_handle)
{
    if(nb_params == 2)
        obj_parent_handle = 0xffffffff;
    if(nb_params == 3 && obj_parent_handle == 0x00000000)
        obj_parent_handle = 0xffffffff;
    
    /* don't allow root */
    if(!is_valid_object_handle(obj_handle, false))
        return fail_op_with(ERROR_INVALID_OBJ_HANDLE, NO_DATA_PHASE);
    if(!is_valid_storage_id(stor_id))
        return fail_op_with(ERROR_INVALID_STORAGE_ID, NO_DATA_PHASE);
    /* allow root on any storage */
    if(!is_valid_object_handle(obj_parent_handle, true))
        return fail_op_with(ERROR_INVALID_PARENT_OBJ, NO_DATA_PHASE);
    if(!is_directory_object(obj_parent_handle))
        return fail_op_with(ERROR_INVALID_PARENT_OBJ, NO_DATA_PHASE);
    
    logf("mtp: copy object(0x%lx, %s) to storage 0x%lx under object 0x%lx", obj_handle,
        get_object_filename(obj_handle), stor_id, obj_parent_handle);
    
    return fail_op_with(ERROR_GENERAL_ERROR, NO_DATA_PHASE);
}

#if 0
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

