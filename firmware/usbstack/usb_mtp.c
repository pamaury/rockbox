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
#include "audio.h"
#include "string.h"
#include "system.h"
#include "usb_core.h"
#include "usb_drv.h"
#include "kernel.h"
#include "usb_mtp.h"
#include "usb_class_driver.h"
#define LOGF_ENABLE
#include "logf.h"
#include "dircache.h"
#include "storage.h"
#include "powermgmt.h"
#include "timefuncs.h"
#include "file.h"
#include "dir.h"
#include "fat.h"
#include "errno.h"

#if !defined(HAVE_DIRCACHE)
#error USB-MTP requires dircache to be enabled
#endif

/* Communications Class interface */
static struct usb_interface_descriptor __attribute__((aligned(2)))
    interface_descriptor =
{
    .bLength            = sizeof(struct usb_interface_descriptor),
    .bDescriptorType    = USB_DT_INTERFACE,
    .bInterfaceNumber   = 0,
    .bAlternateSetting  = 0,
    .bNumEndpoints      = 3, /* three endpoints: interrupt and bulk*2 */
    .bInterfaceClass    = USB_CLASS_STILL_IMAGE,
    .bInterfaceSubClass = USB_MTP_SUBCLASS,
    .bInterfaceProtocol = USB_MTP_PROTO,
    .iInterface         = 0
};

/* Interrupt Endpoint for Communications Class interface */
static struct usb_endpoint_descriptor __attribute__((aligned(2)))
    int_endpoint_descriptor =
{
    .bLength          = sizeof(struct usb_endpoint_descriptor),
    .bDescriptorType  = USB_DT_ENDPOINT,
    .bEndpointAddress = 0,
    .bmAttributes     = USB_ENDPOINT_XFER_INT,
    .wMaxPacketSize   = 0,
    .bInterval        = 0
};

/* 2* Bulk Endpoint for Communications Class interface */
static struct usb_endpoint_descriptor __attribute__((aligned(2)))
    bulk_endpoint_descriptor =
{
    .bLength          = sizeof(struct usb_endpoint_descriptor),
    .bDescriptorType  = USB_DT_ENDPOINT,
    .bEndpointAddress = 0,
    .bmAttributes     = USB_ENDPOINT_XFER_BULK,
    .wMaxPacketSize   = 0,
    .bInterval        = 0
};

static const struct mtp_string mtp_ext =
{
    0,
    {} /* empty strings don't have null at the end */
};

static const struct mtp_array_uint16_t mtp_op_supported =
{
    20,
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
    /* The specification says 0xffffffff but since then, 
     * it appears that MTP has become a PTP extension with the 0x6 extension ID
     */
    #if 0
    0xffffffff, /* Vendor Extension ID */
    #else
    0x00000006, /* Vendor Extension ID */
    #endif
    100, /* MTP Version */
    0x0000, /* Functional Mode */
};

static enum
{
    WAITING_FOR_COMMAND,
    BUSY,
    SENDING_DATA_BLOCK,
    SENDING_RESPONSE,
    RECEIVING_DATA_BLOCK,
    ERROR_WAITING_RESET /* the driver has stalled endpoint and is waiting for device reset set up */
} state = WAITING_FOR_COMMAND;

enum data_phase_type
{
    NO_DATA_PHASE,
    SEND_DATA_PHASE,
    RECV_DATA_PHASE
};

/* rem_bytes is the total number of data bytes remaining including this block */
typedef void (*recv_split_routine)(unsigned char *data, int length, uint32_t rem_bytes, void *user);
/* called when reception is finished or on error to complete transfer */
typedef void (*finish_recv_split_routine)(bool error, void *user);
/* should write to dest, return number of bytes written or <0 on error, write at most max_size bytes */
typedef int (*send_split_routine)(void *dest, int max_size, void *user);
/* called when send is finished or on error to complete transfer */
typedef void (*finish_send_split_routine)(bool error, void *user);

static struct
{
    uint16_t error;/* current error id (if state = ERROR_WAITING_RESET) */
    uint32_t session_id; /* current session id, 0x00000000 if none */
    uint32_t transaction_id; /* same thing */
    unsigned char *data; /* current data source/destination pointer */
    uint32_t data_len; /* remaining length of data if a source */
    uint32_t *cur_array_length_ptr; /* pointer to the length of the current array (if any) */
    union
    {
        recv_split_routine recv_split; /* function to call on receive split */
        send_split_routine send_split; /* function to call on send split */
    };
    union
    {
        finish_recv_split_routine finish_recv_split; /* function to call on completion or on error */
        finish_send_split_routine finish_send_split; /* function to call on completion or on error */
    };
    void *user; /* user data for send or receive split routine */
    uint32_t rem_bytes; /* remaining bytes to send or receive */
    
    bool has_pending_oi;/* is there an object with ObjectInfo but without ObjectBinary ? */
    struct mtp_pending_objectinfo pending_oi;
} mtp_state;

static struct mtp_command cur_cmd;
static struct mtp_response cur_resp;

static bool active = false;

static int usb_interface;
static int ep_int;
static int ep_bulk_in;
static int ep_bulk_out;
static unsigned char *recv_buffer;
static unsigned char *send_buffer;

/*
 *
 * Helpers
 *
 */
static void fail_op_with(uint16_t error_code, enum data_phase_type dht);

static uint32_t max_usb_send_xfer_size(void)
{
    /* FIXME expected to work on any target */
    /* FIXME keep coherent with allocated size for recv_buffer */
    return 32*1024;
}

static uint32_t max_usb_recv_xfer_size(void)
{
    /* FIXME expected to work on any target */
    /* FIXME keep coherent with allocated size for recv_buffer */
    return 1024; 
}

static void fail_with(uint16_t error_code)
{
    logf("mtp: fail with error code 0x%x", error_code);
    mtp_state.error = error_code;
    usb_drv_stall(ep_bulk_in, true, true);
    usb_drv_stall(ep_bulk_out, true, false);
    state = ERROR_WAITING_RESET;
}

static void send_response(void)
{
    struct generic_container *cont = (struct generic_container *) send_buffer;
    
    cont->length = sizeof(struct generic_container) + 4 * cur_resp.nb_parameters;
    cont->type = CONTAINER_RESPONSE_BLOCK;
    cont->code = cur_resp.code;
    cont->transaction_id = cur_cmd.transaction_id;
    
    memcpy(send_buffer + sizeof(struct generic_container), &cur_resp.param[0], 4 * cur_resp.nb_parameters);
    
    state = SENDING_RESPONSE;
    usb_drv_send_nonblocking(ep_bulk_in, send_buffer, cont->length);
    
    /*logf("mtp: send response 0x%x", cont->code);*/
}

static void start_pack_data_block(void)
{
    struct generic_container *cont = (struct generic_container *) send_buffer;
    
    cont->length = 0; /* filled by finish_pack_data_block */
    cont->type = CONTAINER_DATA_BLOCK;
    cont->code = cur_cmd.code;
    cont->transaction_id = cur_cmd.transaction_id;
    mtp_state.data = send_buffer + sizeof(struct generic_container);
}

static void start_unpack_data_block(void *data, uint32_t data_len)
{
    mtp_state.data = data;
    mtp_state.data_len = data_len;
}

static void pack_data_block_ptr(const void *ptr, int length)
{
    memcpy(mtp_state.data, ptr, length);
    mtp_state.data += length;
}

static bool unpack_data_block_ptr(void *ptr, size_t length)
{
    if (mtp_state.data_len < length) return false;
    if (ptr) memcpy(ptr, mtp_state.data, length);
    mtp_state.data += length;
    mtp_state.data_len -= length;
    return true;
}

#define define_pack_array(type) \
    static void pack_data_block_array_##type(const struct mtp_array_##type *arr) \
    { \
        pack_data_block_ptr(arr, 4 + arr->length * sizeof(type)); \
    } \
    
#define define_pack_unpack(type) \
    static void pack_data_block_##type(type val) \
    { \
        pack_data_block_ptr(&val, sizeof(type)); \
    } \
\
    static bool unpack_data_block_##type(type *val) \
    { \
        return unpack_data_block_ptr(val, sizeof(type)); \
    }

define_pack_array(uint16_t)

define_pack_unpack(uint8_t)
define_pack_unpack(uint16_t)
define_pack_unpack(uint32_t)
define_pack_unpack(uint64_t)

static void pack_data_block_string(const struct mtp_string *str)
{
    if(str->length == 0)
        return pack_data_block_ptr(str, 1);
    else
        return pack_data_block_ptr(str, 1 + 2 * str->length);
}

static void pack_data_block_string_charz(const char *str)
{
    if(str == NULL)
        return pack_data_block_uint8_t(0);
    
    int len=strlen(str);
    len++;
    pack_data_block_uint8_t(len);
    
    /* will put an ending zero */
    while(len-- != 0)
        pack_data_block_uint16_t(*str++);
}

static uint32_t get_type_size(uint16_t type)
{
    switch(type)
    {
        case TYPE_UINT8: return 1;
        case TYPE_UINT16: return 2;
        case TYPE_UINT32: return 4;
        default:
            logf("mtp: error: get_type_size called with an unknown type(%hu)", type);
            return 0;
    }
}

static void pack_data_block_typed_ptr(const void *ptr, uint16_t type)
{
    if(type == TYPE_STR)
        pack_data_block_string(ptr);
    else
        pack_data_block_ptr(ptr, get_type_size(type));
}

static bool unpack_data_block_string_charz(unsigned char *dest, uint32_t dest_len)
{
    uint8_t len;

    if(!unpack_data_block_uint8_t(&len)) return false;

    if(dest  && (uint32_t)(len + 1) > dest_len)
        return false;

    while(len-- != 0)
    {
        uint16_t chr;
        if (!unpack_data_block_uint16_t(&chr)) return false;
        if (dest) *dest++ = chr;
    }
    if (dest) *dest = 0;
    return true;
}

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
    
static void pack_data_block_date_time(struct tm *time)
{
    static char buf[16];
    buf[0] = '0' + (time->tm_year+1900) / 1000;
    buf[1] = '0' + (((time->tm_year+1900) / 100) % 10);
    buf[2] = '0' + ((time->tm_year / 10) % 10);
    buf[3] = '0' + (time->tm_year % 10);
    buf[4] = '0' + time->tm_mon / 10;
    buf[5] = '0' + (time->tm_mon % 10);
    buf[6] = '0' + time->tm_mday / 10;
    buf[7] = '0' + (time->tm_mday % 10);
    buf[8] = 'T';
    buf[9] = '0' + time->tm_hour / 10;
    buf[10] = '0' + (time->tm_hour % 10);
    buf[11] = '0' + time->tm_min / 10;
    buf[12] = '0' + (time->tm_min % 10);
    buf[13] = '0' + time->tm_sec / 10;
    buf[14] = '0' + (time->tm_sec % 10);
    buf[15] = '\0';
    
    pack_data_block_string_charz(buf);
}

static void finish_pack_data_block(void)
{
    struct generic_container *cont = (struct generic_container *) send_buffer;
    cont->length = mtp_state.data - send_buffer;
}

static bool finish_unpack_data_block(void)
{
    return (mtp_state.data_len == 0);
}

static void start_pack_data_block_array(void)
{
    mtp_state.cur_array_length_ptr = (uint32_t *) mtp_state.data;
    *mtp_state.cur_array_length_ptr = 0; /* zero length for now */
    mtp_state.data += 4;
}

#define define_pack_array_elem(type) \
    static void pack_data_block_array_elem_##type(type val) \
    { \
        pack_data_block_##type(val); \
        (*mtp_state.cur_array_length_ptr)++; \
    } \

define_pack_array_elem(uint16_t)
define_pack_array_elem(uint32_t)

static uint32_t finish_pack_data_block_array(void)
{
    /* return number of elements */
    return *mtp_state.cur_array_length_ptr;
}

static unsigned char *get_data_block_ptr(void)
{
    return mtp_state.data;
}

static void continue_recv_split_data(int length)
{
    logf("mtp: continue_recv_split_data");
    
    if(mtp_state.rem_bytes == 0)
    {
        struct generic_container *cont = (struct generic_container *) recv_buffer;
        mtp_state.rem_bytes = cont->length - sizeof(struct generic_container);
        logf("mtp: header: length=%d cont: length=%lu", length, cont->length);
        if(length > (int) cont->length)
            return fail_with(ERROR_INVALID_DATASET);
        if(cont->type != CONTAINER_DATA_BLOCK)
            return fail_with(ERROR_INVALID_DATASET);
        if(cont->code != cur_cmd.code)
            return fail_with(ERROR_INVALID_DATASET);
        if(cont->transaction_id != cur_cmd.transaction_id)
            return fail_with(ERROR_INVALID_DATASET);
        
        mtp_state.rem_bytes -= length - sizeof(struct generic_container);
        mtp_state.recv_split(recv_buffer + sizeof(struct generic_container),
            length - sizeof(struct generic_container), mtp_state.rem_bytes, mtp_state.user);
    }
    else
    {
        if((uint32_t)length > mtp_state.rem_bytes)
            return fail_op_with(ERROR_INVALID_DATASET, NO_DATA_PHASE);

        mtp_state.rem_bytes -= length;
        mtp_state.recv_split(recv_buffer, length, mtp_state.rem_bytes, mtp_state.user);
    }
    
    if(mtp_state.rem_bytes == 0)
        mtp_state.finish_recv_split(false, mtp_state.user);
    else
        usb_drv_recv(ep_bulk_out, recv_buffer, max_usb_recv_xfer_size());

    logf("mtp: rem_bytes=%lu", mtp_state.rem_bytes);
}

static void receive_split_data(recv_split_routine rct, finish_recv_split_routine frst, void *user)
{
    if(rct == NULL || frst == NULL)
        logf("mtp: error: receive_split_data called with a NULL function ptr, your DAP will soon explode");
    
    mtp_state.rem_bytes = 0;
    mtp_state.recv_split = rct;
    mtp_state.finish_recv_split = frst;
    mtp_state.user = user;
    state = RECEIVING_DATA_BLOCK;
    usb_drv_recv(ep_bulk_out, recv_buffer, max_usb_recv_xfer_size());
}

static void continue_send_split_data(void)
{
    int size = MIN(max_usb_send_xfer_size(), mtp_state.rem_bytes);
    
    if(size == 0)
        return mtp_state.finish_send_split(false, mtp_state.user);
    
    int ret = mtp_state.send_split(send_buffer, size, mtp_state.user);
    if(ret < 0)
        return mtp_state.finish_send_split(true, mtp_state.user);
    
    mtp_state.rem_bytes -= ret;
    usb_drv_send_nonblocking(ep_bulk_in, send_buffer, ret);
}

static void send_split_data(uint32_t nb_bytes, send_split_routine fn, finish_send_split_routine fsst, void *user)
{
    if(fn == NULL || fsst == NULL)
        logf("mtp: error: send_split_data called with a NULL function ptr, your DAP will soon explode");
    
    mtp_state.send_split = fn,
    mtp_state.finish_send_split = fsst;
    mtp_state.rem_bytes = nb_bytes;
    mtp_state.user = user;
    
    state = SENDING_DATA_BLOCK;
    
    struct generic_container *cont = (struct generic_container *) send_buffer;
    
    cont->length = nb_bytes + sizeof(struct generic_container);
    cont->type = CONTAINER_DATA_BLOCK;
    cont->code = cur_cmd.code;
    cont->transaction_id = cur_cmd.transaction_id;
    
    int size = MIN(max_usb_send_xfer_size() - sizeof(struct generic_container), nb_bytes);
    int ret = fn(send_buffer + sizeof(struct generic_container),
                size,
                user);
    if(ret < 0)
        return mtp_state.finish_send_split(true, mtp_state.user);
    
    mtp_state.rem_bytes -= ret;
    usb_drv_send_nonblocking(ep_bulk_in, send_buffer, ret + sizeof(struct generic_container));
}

static void generic_finish_split_routine(bool error, void *user)
{
    (void) user;
    if(error)
    {
        cur_resp.code = ERROR_INCOMPLETE_TRANSFER;
        cur_resp.nb_parameters = 0;
    }
    
    send_response();
}

static int send_data_block_split(void *dest, int max_size, void *user)
{
    /* FIXME hacky solution that works because of the current implementation of send_split_data */
    /* it works because pack_data_start_block put a generic_container header that will be overwritten by send_split_data */
    (void) user;
    
    memmove(dest, mtp_state.data, max_size);
    mtp_state.data += max_size;
    
    return max_size;
}

static void send_data_block(void)
{
    uint32_t size = mtp_state.data - send_buffer - sizeof(struct generic_container);
    /* use data to handle splitting */
    mtp_state.data = send_buffer + sizeof(struct generic_container);
    send_split_data(size, &send_data_block_split, &generic_finish_split_routine, NULL);
}

static void fail_op_with_recv_split_routine(unsigned char *data, int length, uint32_t rem_bytes, void *user)
{
    (void) data;
    (void) length;
    (void) rem_bytes;
    (void) user;
    /* throw data */
}

static void fail_op_with_finish_recv_split_routine(bool error, void *user)
{
    (void) user;
    (void) error;
    /* send response, ignoring error */
    send_response();
}

static void fail_op_with(uint16_t error_code, enum data_phase_type dht)
{
    logf("mtp: fail operation with error code 0x%x", error_code);
    cur_resp.code = error_code;
    cur_resp.nb_parameters = 0;
    
    switch(dht)
    {
        case NO_DATA_PHASE:
            /* send immediate response */
            state = SENDING_RESPONSE;
            mtp_state.error = error_code;
            send_response();
            break;
        case SEND_DATA_PHASE:
            /* send empty packet */
            start_pack_data_block();
            finish_pack_data_block();
            send_data_block();
            break;
        case RECV_DATA_PHASE:
            /* receive but throw away */
            receive_split_data(&fail_op_with_recv_split_routine, &fail_op_with_finish_recv_split_routine, NULL);
            break;
        default:
            logf("mtp: oops in fail_op_with");
            /* send immediate response */
            state = SENDING_RESPONSE;
            mtp_state.error = error_code;
            send_response();
            break;
    }
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
    return (uint32_t)entry;
}

/*
 *
 * Operation handling
 *
 */
static void open_session(uint32_t session_id)
{
    logf("mtp: open session %lu", session_id);
    
    if(mtp_state.session_id != 0x00000000)
        return fail_op_with(ERROR_SESSION_ALREADY_OPEN, NO_DATA_PHASE);
    
    mtp_state.session_id = session_id;
    cur_resp.code = ERROR_OK;
    cur_resp.nb_parameters = 0;
    send_response();
}

static void close_session(bool send_resp)
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
        cur_resp.code = ERROR_OK;
        cur_resp.nb_parameters = 0;
        send_response();
    }
}

static void get_device_info(void)
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
    
    cur_resp.code = ERROR_OK;
    cur_resp.nb_parameters = 0;
    
    send_data_block();
}

static void get_storage_ids(void)
{
    /* FIXME mostly incomplete ! */
    logf("mtp: get storage ids");
    
    start_pack_data_block();
    start_pack_data_block_array();
    /* for now, only report the main volume */
    pack_data_block_array_elem_uint32_t(0x00010001);
    finish_pack_data_block_array();
    finish_pack_data_block();
    
    cur_resp.code = ERROR_OK;
    cur_resp.nb_parameters = 0;
    
    send_data_block();
}

static void get_storage_info(uint32_t stor_id)
{
    logf("mtp: get storage info: stor_id=0x%lx", stor_id);
    if(stor_id!=0x00010001)
        return fail_op_with(ERROR_INVALID_STORAGE_ID, SEND_DATA_PHASE);
    
    /* FIXME: works only for first storage, we don't have stor->volume conversion */
    unsigned long size, free;
    fat_size(IF_MV2(0,) &size, &free);
    size *= SECTOR_SIZE;
    free *= SECTOR_SIZE;
    
    start_pack_data_block();
    pack_data_block_uint16_t(STOR_TYPE_FIXED_RAM); /* Storage Type */
    pack_data_block_uint16_t(FS_TYPE_GENERIC_HIERARCHICAL); /* Filesystem Type */
    pack_data_block_uint16_t(ACCESS_CAP_RO_WITHOUT); /* Access Capability */
    pack_data_block_uint64_t(size); /* Max Capacity (optional for read only) */
    pack_data_block_uint64_t(free); /* Free Space in bytes (optional for read only) */
    pack_data_block_uint32_t(0); /* Free Space in objects (optional for read only) */
    pack_data_block_string_charz("Storage description missing"); /* Storage Description */
    pack_data_block_string_charz("Volume identifier missing"); /* Volume Identifier */
    finish_pack_data_block();
    
    cur_resp.code = ERROR_OK;
    cur_resp.nb_parameters = 0;
    
    send_data_block();
}

/*
 * Objet operations
 */

static bool list_files2(const struct dircache_entry *direntry, bool recursive)
{
    const struct dircache_entry *entry;
    uint32_t *ptr;
    uint32_t nb_elems=0;
    
    logf("mtp: list_files entry=0x%lx rec=%s", (uint32_t)direntry, recursive ? "yes" : "no");
    
    if((uint32_t)direntry == 0xffffffff)
        direntry = dircache_get_entry_ptr("/");
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
            if (!list_files2(entry, recursive))
                return false;
    }

    return true;
}

static void list_files(const struct dircache_entry *direntry, bool recursive)
{
    start_pack_data_block();
    start_pack_data_block_array();

    if (!list_files2(direntry,recursive)) return;

    finish_pack_data_block_array();
    finish_pack_data_block();

    cur_resp.code = ERROR_OK;
    cur_resp.nb_parameters = 0;

    send_data_block();
}

static void get_num_objects(int nb_params, uint32_t stor_id, uint32_t obj_fmt, uint32_t obj_handle_parent)
{
    (void) nb_params;
    (void) stor_id;
    (void) obj_fmt;
    (void) obj_handle_parent;
    return fail_op_with(ERROR_OP_NOT_SUPPORTED, SEND_DATA_PHASE);
}

static void get_object_handles(int nb_params, uint32_t stor_id, uint32_t obj_fmt, uint32_t obj_handle_parent)
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
    
    if(stor_id != 0xffffffff && stor_id != 0x00010001)
        return fail_op_with(ERROR_INVALID_STORAGE_ID, SEND_DATA_PHASE);
    
    if(obj_handle_parent == 0x00000000)
        list_files((const struct dircache_entry *)0xffffffff, true); /* recursive, at root */
    else
    {
        const struct dircache_entry *entry=mtp_handle_to_dircache_entry(obj_handle_parent, true);
        if(entry == NULL)
            return fail_op_with(ERROR_INVALID_OBJ_HANDLE, SEND_DATA_PHASE);
        else
            list_files(entry, false); /* not recursive, at entry */
    }
}

static void get_object_info(uint32_t object_handle)
{
    const struct dircache_entry *entry = mtp_handle_to_dircache_entry(object_handle, false);
    struct tm filetm;
    logf("mtp: get object info: entry=\"%s\" attr=0x%x", entry->d_name, entry->attribute);
    
    struct object_info oi;
    oi.storage_id = 0x00010001;
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
    oi.parent_handle = dircache_entry_to_mtp_handle(entry->up); /* works also for root */
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
    
    cur_resp.code = ERROR_OK;
    cur_resp.nb_parameters = 0;
    
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

static void get_object(uint32_t object_handle)
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
    
    cur_resp.code = ERROR_OK;
    cur_resp.nb_parameters = 0;
    
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
 
    cur_resp.code = ERROR_OK;
    cur_resp.nb_parameters = 3;
    cur_resp.param[0] = st->stor_id;
    cur_resp.param[1] = st->obj_handle_parent;
    cur_resp.param[2] = dircache_entry_to_mtp_handle(this_entry);
}

static void finish_send_object_info_split_routine(bool error, void *user)
{
    generic_finish_split_routine(error, user);
}

static void send_object_info(int nb_params, uint32_t stor_id, uint32_t obj_handle_parent)
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
    
    cur_resp.code = ERROR_OK;
    cur_resp.nb_parameters = 0;
    
    send_response();
}

static void send_object(void)
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

static void delete_object(int nb_params, uint32_t obj_handle, uint32_t __unused)
{
    if(nb_params == 2 && __unused != 0x00000000)
        return fail_op_with(ERROR_SPEC_BY_FMT_UNSUPPORTED, NO_DATA_PHASE);
    
    const struct dircache_entry *entry = mtp_handle_to_dircache_entry(obj_handle, false); /* don't allow to destroy / */
    if(entry == NULL)
        return fail_op_with(ERROR_INVALID_OBJ_HANDLE, NO_DATA_PHASE);
    
    bool ret = recursive_delete(entry);
    
    cur_resp.code = ret ? ERROR_OK : ERROR_PARTIAL_DELETION;
    cur_resp.nb_parameters = 0;
    
    send_response();
}

static void copy_object(int nb_params, uint32_t obj_handle, uint32_t stor_id, uint32_t obj_parent_handle)
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

static void move_object(int nb_params, uint32_t obj_handle, uint32_t stor_id, uint32_t obj_parent_handle)
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

static void get_object_references(uint32_t object_handle)
{
    const struct dircache_entry *entry=mtp_handle_to_dircache_entry(object_handle, false);
    if(entry == NULL)
        return fail_op_with(ERROR_INVALID_OBJ_HANDLE, SEND_DATA_PHASE);
    
    logf("mtp: get object references: handle=0x%lx (\"%s\")", object_handle, entry->d_name);
    
    if(entry->attribute & ATTR_DIRECTORY)
        return list_files(entry, false); /* not recursive, at entry */
    else
    {
        start_pack_data_block();
        start_pack_data_block_array();
        finish_pack_data_block_array();
        finish_pack_data_block();
        
        cur_resp.code = ERROR_OK;
        cur_resp.nb_parameters = 0;
    
        send_data_block();
    }
}

static void reset_device(void)
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
    
    cur_resp.code = ERROR_OK;
    cur_resp.nb_parameters = 0;
    
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
    
    cur_resp.code = ERROR_OK;
    cur_resp.nb_parameters = 0;
    
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
    
    cur_resp.code = ERROR_OK;
    cur_resp.nb_parameters = 0;
    
    send_data_block();
}

static void get_device_prop_desc(uint32_t device_prop)
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

static void get_device_prop_value(uint32_t device_prop)
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

void prop_stor_id_get(const struct dircache_entry *);
void prop_obj_fmt_get(const struct dircache_entry *);
void prop_assoc_type_get(const struct dircache_entry *);
void prop_assoc_desc_get(const struct dircache_entry *);
void prop_obj_size_get(const struct dircache_entry *);
void prop_filename_get(const struct dircache_entry *);
void prop_c_date_get(const struct dircache_entry *);
void prop_m_date_get(const struct dircache_entry *);
void prop_parent_obj_get(const struct dircache_entry *);
void prop_hidden_get(const struct dircache_entry *);
void prop_system_get(const struct dircache_entry *);
void prop_name_get(const struct dircache_entry *);

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

static void get_object_props_supported(uint32_t object_fmt)
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
    
    cur_resp.code = ERROR_OK;
    cur_resp.nb_parameters = 0;
    
    send_data_block();
}

static void get_object_prop_desc(uint32_t obj_prop, uint32_t obj_fmt)
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
    
    cur_resp.code = ERROR_OK;
    cur_resp.nb_parameters = 0;
    
    send_data_block();
}

uint16_t get_object_format(const struct dircache_entry *entry)
{
    if(entry->attribute & ATTR_DIRECTORY)
        return OBJ_FMT_ASSOCIATION;
    else
        return OBJ_FMT_UNDEFINED;
}

static void get_object_prop_value(uint32_t obj_handle, uint32_t obj_prop)
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
    
    cur_resp.code = ERROR_OK;
    cur_resp.nb_parameters = 0;
    
    send_data_block();
}

void prop_stor_id_get(const struct dircache_entry *entry)
{
    (void) entry;
    pack_data_block_uint32_t(0x00010001);
}

void prop_obj_fmt_get(const struct dircache_entry *entry)
{
    pack_data_block_uint16_t(get_object_format(entry));
}

void prop_assoc_type_get(const struct dircache_entry *entry)
{
    (void) entry;
    pack_data_block_uint16_t(ASSOC_TYPE_FOLDER);
}

void prop_assoc_desc_get(const struct dircache_entry *entry)
{
    (void) entry;
    pack_data_block_uint32_t(0x1);
}

void prop_obj_size_get(const struct dircache_entry *entry)
{
    pack_data_block_uint32_t(entry->size);
}

void prop_filename_get(const struct dircache_entry *entry)
{
    pack_data_block_string_charz(entry->d_name);
}

void prop_c_date_get(const struct dircache_entry *entry)
{
    struct tm filetm;
    fat2tm(&filetm, entry->wrtdate, entry->wrttime);
    pack_data_block_date_time(&filetm);
}

void prop_m_date_get(const struct dircache_entry *entry)
{
    struct tm filetm;
    fat2tm(&filetm, entry->wrtdate, entry->wrttime);
    pack_data_block_date_time(&filetm);
}

void prop_parent_obj_get(const struct dircache_entry *entry)
{
    pack_data_block_uint32_t(dircache_entry_to_mtp_handle(entry->up));
}

void prop_hidden_get(const struct dircache_entry *entry)
{
    if(entry->attribute & ATTR_HIDDEN)
        pack_data_block_uint16_t(0x1);
    else
        pack_data_block_uint16_t(0x0);
}

void prop_system_get(const struct dircache_entry *entry)
{
    if(entry->attribute & ATTR_SYSTEM)
        pack_data_block_uint16_t(0x1);
    else
        pack_data_block_uint16_t(0x0);
}

void prop_name_get(const struct dircache_entry *entry)
{
    static char path[MAX_PATH];
    dircache_copy_path(entry, path, MAX_PATH);
    pack_data_block_string_charz(path);
}

/*
 * Command dispatching
 */

static void handle_command2(void)
{
    #define want_nb_params(p, data_phase) \
        if(cur_cmd.nb_parameters != p) return fail_op_with(ERROR_INVALID_DATASET, data_phase);
    #define want_nb_params_range(pi, pa, data_phase) \
        if(cur_cmd.nb_parameters < pi || cur_cmd.nb_parameters > pa) return fail_op_with(ERROR_INVALID_DATASET, data_phase);
    #define want_session(data_phase) \
        if(mtp_state.session_id == 0x00000000) return fail_op_with(ERROR_SESSION_NOT_OPEN, data_phase);
    
    switch(cur_cmd.code)
    {
        case MTP_OP_GET_DEV_INFO:
            want_nb_params(0, SEND_DATA_PHASE) /* no parameter */
            return get_device_info();
        case MTP_OP_OPEN_SESSION:
            want_nb_params(1, NO_DATA_PHASE) /* one parameter: session id */
            return open_session(cur_cmd.param[0]);
        case MTP_OP_CLOSE_SESSION:
            want_nb_params(0, NO_DATA_PHASE) /* no parameter */
            return close_session(true);
        case MTP_OP_GET_STORAGE_IDS:
            want_nb_params(0, SEND_DATA_PHASE) /* no parameter */
            want_session(SEND_DATA_PHASE) /* must be called in a session */
            return get_storage_ids();
        case MTP_OP_GET_STORAGE_INFO:
            want_nb_params(1, SEND_DATA_PHASE) /* one parameter */
            want_session(SEND_DATA_PHASE) /* must be called in a session */
            return get_storage_info(cur_cmd.param[0]);
        case MTP_OP_GET_NUM_OBJECTS:
            /*  There are two optional parameters and one mandatory */
            want_nb_params_range(1, 3, NO_DATA_PHASE)
            want_session(NO_DATA_PHASE) /* must be called in a session */
            return get_num_objects(cur_cmd.nb_parameters, cur_cmd.param[0], cur_cmd.param[1], cur_cmd.param[2]);
        case MTP_OP_GET_OBJECT_HANDLES:
            /*  There are two optional parameters and one mandatory */
            want_nb_params_range(1, 3, SEND_DATA_PHASE)
            want_session(SEND_DATA_PHASE) /* must be called in a session */
            return get_object_handles(cur_cmd.nb_parameters, cur_cmd.param[0], cur_cmd.param[1], cur_cmd.param[2]);
        case MTP_OP_GET_OBJECT_INFO:
            want_nb_params(1, SEND_DATA_PHASE) /* one parameter */
            want_session(SEND_DATA_PHASE) /* must be called in a session */
            return get_object_info(cur_cmd.param[0]);
        case MTP_OP_GET_OBJECT:
            want_nb_params(1, SEND_DATA_PHASE) /* one parameter */
            want_session(SEND_DATA_PHASE) /* must be called in a session */
            return get_object(cur_cmd.param[0]);
        case MTP_OP_DELETE_OBJECT:
            want_nb_params_range(1, 2, NO_DATA_PHASE)
            want_session(NO_DATA_PHASE)
            return delete_object(cur_cmd.nb_parameters, cur_cmd.param[0], cur_cmd.param[1]);
        case MTP_OP_SEND_OBJECT_INFO:
            want_nb_params_range(0, 2, SEND_DATA_PHASE)
            want_session(RECV_DATA_PHASE)
            return send_object_info(cur_cmd.nb_parameters, cur_cmd.param[0], cur_cmd.param[1]);
        case MTP_OP_SEND_OBJECT:
            want_nb_params(0, RECV_DATA_PHASE);
            want_session(RECV_DATA_PHASE);
            return send_object();
        case MTP_OP_RESET_DEVICE:
            want_nb_params(0, NO_DATA_PHASE) /* no parameter */
            return reset_device();
        case MTP_OP_GET_DEV_PROP_DESC:
            want_nb_params(1, SEND_DATA_PHASE) /* one parameter */
            want_session(SEND_DATA_PHASE)
            return get_device_prop_desc(cur_cmd.param[0]);
        case MTP_OP_GET_DEV_PROP_VALUE:
            want_nb_params(1, SEND_DATA_PHASE) /* one parameter */
            want_session(SEND_DATA_PHASE)
            return get_device_prop_value(cur_cmd.param[0]);
        case MTP_OP_MOVE_OBJECT:
            want_nb_params_range(2, 3, NO_DATA_PHASE) /* two or three parameters */
            want_session(NO_DATA_PHASE)
            return move_object(cur_cmd.nb_parameters, cur_cmd.param[0], cur_cmd.param[1], cur_cmd.param[2]);
        case MTP_OP_COPY_OBJECT:
            want_nb_params_range(2, 3, NO_DATA_PHASE) /* two or three parameters */
            want_session(NO_DATA_PHASE)
            return copy_object(cur_cmd.nb_parameters, cur_cmd.param[0], cur_cmd.param[1], cur_cmd.param[2]);
        case MTP_OP_GET_OBJ_PROPS_SUPPORTED:
            want_nb_params(1, SEND_DATA_PHASE)
            want_session(SEND_DATA_PHASE)
            return get_object_props_supported(cur_cmd.param[0]);
        case MTP_OP_GET_OBJ_PROP_DESC:
            want_nb_params(2, SEND_DATA_PHASE)
            want_session(SEND_DATA_PHASE)
            return get_object_prop_desc(cur_cmd.param[0], cur_cmd.param[1]);
        case MTP_OP_GET_OBJ_PROP_VALUE:
            want_nb_params(2, SEND_DATA_PHASE)
            want_session(SEND_DATA_PHASE)
            return get_object_prop_value(cur_cmd.param[0], cur_cmd.param[1]);
        case MTP_OP_GET_OBJ_REFERENCES:
            want_nb_params(1, SEND_DATA_PHASE) /* one parameter */
            want_session(SEND_DATA_PHASE)
            return get_object_references(cur_cmd.param[0]);
        default:
            logf("mtp: unknown command code 0x%x", cur_cmd.code);
            /* assume no data phase */
            return fail_op_with(ERROR_OP_NOT_SUPPORTED, NO_DATA_PHASE);
    }
    
    #undef want_nb_params
    #undef want_nb_params_range
    #undef want_session
}

static void handle_command(int length)
{
    struct generic_container * cont = (struct generic_container *) recv_buffer;
    
    if(length != (int)cont->length)
        return fail_with(ERROR_INVALID_DATASET);
    if(cont->type != CONTAINER_COMMAND_BLOCK)
        return fail_with(ERROR_INVALID_DATASET);
    
    cur_cmd.code = cont->code;
    cur_cmd.transaction_id = cont->transaction_id;
    cur_cmd.nb_parameters = cont->length - sizeof(struct generic_container);
    
    if((cur_cmd.nb_parameters % 4) != 0)
        return fail_with(ERROR_INVALID_DATASET);
    else
        cur_cmd.nb_parameters /= 4;
    
    memcpy(&cur_cmd.param[0], recv_buffer + sizeof(struct generic_container), 4*cur_cmd.nb_parameters);
    
    state = BUSY;
    
    return handle_command2();
}

/*
 *
 * USB Code
 *
 */

int usb_mtp_request_endpoints(struct usb_class_driver *drv)
{
    /* Data Class bulk endpoints */
    ep_bulk_in=usb_core_request_endpoint(USB_ENDPOINT_XFER_BULK,USB_DIR_IN,drv);
    if(ep_bulk_in<0)
    {
        logf("mtp: unable to request bulk in endpoint");
        return -1;
    }

    ep_bulk_out=usb_core_request_endpoint(USB_ENDPOINT_XFER_BULK,USB_DIR_OUT,drv);
    if(ep_bulk_out<0)
    {
        logf("mtp: unable to request bulk out endpoint");
        usb_core_release_endpoint(ep_bulk_in);
        return -1;
    }
    
    /* Communication Class interrupt endpoint */
    ep_int=usb_core_request_endpoint(USB_ENDPOINT_XFER_INT,USB_DIR_IN,drv);
    if(ep_int<0)
    {
        logf("mtp: unable to request interrupt endpoint");
        usb_core_release_endpoint(ep_bulk_in);
        usb_core_release_endpoint(ep_bulk_out);
        return -1;
    }
    
    return 0;
}

int usb_mtp_set_first_interface(int interface)
{
    usb_interface = interface;
    // one interface
    return interface + 1;
}

int usb_mtp_get_config_descriptor(unsigned char *dest, int max_packet_size)
{
    unsigned char *orig_dest = dest;

    /* MTP interface */
    interface_descriptor.bInterfaceNumber=usb_interface;
    PACK_DATA(dest,interface_descriptor);
    
    /* interrupt endpoint */
    int_endpoint_descriptor.wMaxPacketSize=8;
    int_endpoint_descriptor.bInterval=8;
    int_endpoint_descriptor.bEndpointAddress=ep_int;
    PACK_DATA(dest,int_endpoint_descriptor);
    
    /* bulk endpoints */
    bulk_endpoint_descriptor.wMaxPacketSize=max_packet_size;
    bulk_endpoint_descriptor.bEndpointAddress=ep_bulk_in;
    PACK_DATA(dest,bulk_endpoint_descriptor);
    bulk_endpoint_descriptor.bEndpointAddress=ep_bulk_out;
    PACK_DATA(dest,bulk_endpoint_descriptor);

    return (dest - orig_dest);
}

/* called by usb_core_control_request() */
bool usb_mtp_control_request(struct usb_ctrlrequest* req, unsigned char* dest)
{
    bool handled = false;
    
    if((req->bRequestType & USB_TYPE_MASK) != USB_TYPE_CLASS)
        return false;

    (void)dest;
    switch(req->bRequest)
    {
        case USB_CTRL_CANCEL_REQUEST:
            logf("mtp: cancel request: unimplemented");
            fail_with(ERROR_DEV_BUSY);
            break;
        case USB_CTRL_GET_EXT_EVT_DATA:
            fail_with(ERROR_OP_NOT_SUPPORTED);
            break;
        case USB_CTRL_DEV_RESET_REQUEST:
            logf("mtp: reset");
            usb_drv_stall(ep_bulk_in, false, true);
            usb_drv_stall(ep_bulk_out, false, false);
            /* close current session */
            /* FIXME should flush buffers or thing like that if any */
            mtp_state.session_id = 0x00000000;
            state = WAITING_FOR_COMMAND;
            handled = true;
            usb_core_ack_control(req);
            break;
        case USB_CTRL_GET_DEV_STATUS:
        {
            logf("mtp: get status");
            struct device_status *status = (struct device_status *) dest;
            
            if(req->wLength < sizeof(struct device_status))
            {
                fail_with(ERROR_INVALID_DATASET);
                break;
            }
            
            status->length = sizeof(struct device_status);
            if(state == ERROR_WAITING_RESET)
                status->code = mtp_state.error;
            else if(state == BUSY)
                status->code = ERROR_DEV_BUSY;
            else
                status->code = ERROR_OK;
            
            if(!usb_drv_send(EP_CONTROL, dest, sizeof(struct device_status)))
            {
                usb_core_ack_control(req);
                handled = true;
            }
            break;
        }
        default:
            logf("mtp: unhandeld req: bRequestType=%x bRequest=%x wValue=%x wIndex=%x wLength=%x",
                req->bRequestType,req->bRequest,req->wValue,req->wIndex,req->wLength);
    }
    
    return handled;
}

void usb_mtp_init_connection(void)
{
    logf("mtp: init connection");
    active = true;
    
    /* enable to cpu boost to enable transfers of big size (ie > ~90 bytes) */
    cpu_boost(true);
    
    if(!dircache_is_enabled())
    {
        logf("mtp: init dircache");
        dircache_init();
        dircache_build(/*dircache_get_cache_size()*/0);
        if(!dircache_is_enabled())
            fail_with(ERROR_GENERAL_ERROR);
    }
    
    mtp_state.session_id = 0x00000000;
    mtp_state.transaction_id = 0x00000000;
    mtp_state.has_pending_oi = false;
    
    size_t bufsize;
    unsigned char * audio_buffer;
    audio_buffer = audio_get_buffer(false,&bufsize);
    recv_buffer = (void *)UNCACHED_ADDR((unsigned int)(audio_buffer+31) & 0xffffffe0);
    send_buffer = recv_buffer + max_usb_send_xfer_size();
    cpucache_invalidate();
    
    /* wait for next command */
    state = WAITING_FOR_COMMAND;
    usb_drv_recv(ep_bulk_out, recv_buffer, 1024);
}

/* called by usb_code_init() */
void usb_mtp_init(void)
{
    logf("mtp: init");
    active = true;
}

void usb_mtp_disconnect(void)
{
    logf("mtp: disconnect");
    
    /* close current session */
    if(mtp_state.session_id != 0x00000000)
        close_session(false);
    
    active = false;
    cpu_boost(false);
}

/* called by usb_core_transfer_complete() */
void usb_mtp_transfer_complete(int ep,int dir, int status, int length)
{
    //logf("usb_mtp_xfer_comp ep=%d length=%d", ep, length);

    if(ep == EP_CONTROL)
        return;
    if(ep == ep_int)
        return;
    
    switch(state)
    {
        case WAITING_FOR_COMMAND:
            if(dir == USB_DIR_IN)
            {
                logf("mtp: IN received in WAITING_FOR_COMMAND");
                break;
            }
            handle_command(length);
            break;
        case SENDING_RESPONSE:
            if(dir == USB_DIR_OUT)
            {
                logf("mtp: OUT received in SENDING_RESULT");
                break;
            }
            if(status != 0)
                logf("mtp: response transfer error");
            /* wait for next command */
            state = WAITING_FOR_COMMAND;
            usb_drv_recv(ep_bulk_out, recv_buffer, 1024);
            break;
        case SENDING_DATA_BLOCK:
            if(dir == USB_DIR_OUT)
            {
                logf("mtp: OUT received in SENDING_DATA_BLOCK");
                break;
            }
            if(status != 0)
            {
                logf("mtp: send data transfer error");
                mtp_state.finish_send_split(true, mtp_state.user);
                break;
            }
            
            continue_send_split_data();
            
            break;
        case RECEIVING_DATA_BLOCK:
            if(dir == USB_DIR_IN)
            {
                logf("mtp: IN received in RECEIVING_DATA_BLOCK");
                break;
            }
            if(status != 0)
            {
                logf("mtp: receive data transfer error");
                mtp_state.finish_recv_split(true, mtp_state.user);
                break;
            }
            
            logf("mtp: received data: length = %d, mtp_state.rem_bytes = 0x%lu", length, mtp_state.rem_bytes);

            continue_recv_split_data(length);
                
            break;
        default:
            logf("mtp: unhandeld transfer complete ep=%d dir=%d status=%d length=%d",ep,dir,status,length);
            break;
    }
}


