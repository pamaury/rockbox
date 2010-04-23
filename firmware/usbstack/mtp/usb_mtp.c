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
#include "usb_core.h"
#include "usb_drv.h"
#include "usb_mtp.h"
#include "usb_mtp_internal.h"
#include "usbstack/usb_class_driver.h"
/*#define LOGF_ENABLE*/
#include "logf.h"

/*
 * NOTE:
 * With such a descriptor, the device will appear as a PTP device. If by chance, the device is not recognized
 * as a MTP device, then it can probably be used in read-only mode in PTP mode but this is unsupported.
 * This settings is here to allow maximum compatibility because Windows Vista and Windows 7 allow
 * probing of MTP device using this class. Although the best way to have a device recognized as a MTP device
 * is still to use the Microsoft OS Descriptor (which works at least on Windows>=XP and Linux/... with libmtp).
 */

/* Interface */
static struct usb_interface_descriptor __attribute__((aligned(2)))
    interface_descriptor =
{
    .bLength            = sizeof(struct usb_interface_descriptor),
    .bDescriptorType    = USB_DT_INTERFACE,
    .bInterfaceNumber   = 0,
    .bAlternateSetting  = 0,
    .bNumEndpoints      = 3, /* three endpoints: interrupt and bulk*2 */
    #if 0
    .bInterfaceClass    = USB_CLASS_STILL_IMAGE,
    .bInterfaceSubClass = USB_MTP_SUBCLASS,
    .bInterfaceProtocol = USB_MTP_PROTO,
    #else
    .bInterfaceClass    = USB_CLASS_VENDOR_SPEC,
    .bInterfaceSubClass = USB_CLASS_VENDOR_SPEC,
    .bInterfaceProtocol  = USB_CLASS_VENDOR_SPEC,
    #endif
    .iInterface         = 0
};

/* Interrupt endpoint (currently unused) */
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

/* Bulk endpoint (x2) */
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

#ifdef USB_ENABLE_MS_DESCRIPTOR
static struct usb_ms_compat_id_descriptor_header compat_id_header=
{
    0, /* Length: filled later */
    0x0100, /* Version */
    USB_MS_DT_COMPAT_ID, /* Index */
    0, /* Count : filled later */
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } /* Reserved */
};

static struct usb_ms_compat_id_descriptor_function compat_id_mtp_function=
{
    0, /* FirstInterfaceNumber: filled later */
    0x00, /* Reserved */
    {0x4D, 0x54, 0x50, 0x00, 0x00, 0x00, 0x00, 0x00}, /* Compat ID */
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, /* Sub-compat ID */
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } /* Reserved */
};
#endif

static enum
{
    WAITING_FOR_COMMAND,
    BUSY,
    SENDING_DATA_BLOCK,
    SENDING_RESPONSE,
    RECEIVING_DATA_BLOCK,
    ERROR_WAITING_RESET /* the driver has stalled endpoint and is waiting for device reset set up */
} state = WAITING_FOR_COMMAND;

struct mtp_state_t mtp_state;

struct mtp_command mtp_cur_cmd;
struct mtp_response mtp_cur_resp;

static bool active = false;

static int usb_interface;
static int ep_int;
static int ep_bulk_in;
static int ep_bulk_out;
static unsigned char *recv_buffer;
static unsigned char *send_buffer;

/*
 *
 * Low-Level protocol code
 *
 */
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

void fail_with(uint16_t error_code)
{
    logf("mtp: fail with error code 0x%x", error_code);
    mtp_state.error = error_code;
    usb_drv_stall(ep_bulk_in, true, true);
    usb_drv_stall(ep_bulk_out, true, false);
    state = ERROR_WAITING_RESET;
}

void send_response(void)
{
    struct generic_container *cont = (struct generic_container *) send_buffer;
    
    cont->length = sizeof(struct generic_container) + 4 * mtp_cur_resp.nb_parameters;
    cont->type = CONTAINER_RESPONSE_BLOCK;
    cont->code = mtp_cur_resp.code;
    cont->transaction_id = mtp_cur_cmd.transaction_id;
    
    memcpy(send_buffer + sizeof(struct generic_container), &mtp_cur_resp.param[0], 4 * mtp_cur_resp.nb_parameters);
    
    state = SENDING_RESPONSE;
    usb_drv_send_nonblocking(ep_bulk_in, send_buffer, cont->length);
    
    /*logf("mtp: send response 0x%x", cont->code);*/
}

void start_pack_data_block(void)
{
    struct generic_container *cont = (struct generic_container *) send_buffer;
    
    cont->length = 0; /* filled by finish_pack_data_block */
    cont->type = CONTAINER_DATA_BLOCK;
    cont->code = mtp_cur_cmd.code;
    cont->transaction_id = mtp_cur_cmd.transaction_id;
    mtp_state.data = send_buffer + sizeof(struct generic_container);
}

void start_unpack_data_block(void *data, uint32_t data_len)
{
    mtp_state.data = data;
    mtp_state.data_len = data_len;
}

void pack_data_block_ptr(const void *ptr, int length)
{
    memcpy(mtp_state.data, ptr, length);
    mtp_state.data += length;
}

bool unpack_data_block_ptr(void *ptr, size_t length)
{
    if(mtp_state.data_len < length) return false;
    if(ptr) memcpy(ptr, mtp_state.data, length);
    mtp_state.data += length;
    mtp_state.data_len -= length;
    return true;
}

#define define_pack_array(type) \
    void pack_data_block_array_##type(const struct mtp_array_##type *arr) \
    { \
        pack_data_block_ptr(arr, 4 + arr->length * sizeof(type)); \
    } \
    
#define define_pack_unpack(type) \
    void pack_data_block_##type(type val) \
    { \
        pack_data_block_ptr(&val, sizeof(type)); \
    } \
\
    bool unpack_data_block_##type(type *val) \
    { \
        return unpack_data_block_ptr(val, sizeof(type)); \
    }

define_pack_array(uint16_t)

define_pack_unpack(uint8_t)
define_pack_unpack(uint16_t)
define_pack_unpack(uint32_t)
define_pack_unpack(uint64_t)

#undef define_pack_array
#undef define_pack_unpack

void pack_data_block_string(const struct mtp_string *str)
{
    if(str->length == 0)
        return pack_data_block_ptr(str, 1);
    else
        return pack_data_block_ptr(str, 1 + 2 * str->length);
}

void pack_data_block_string_charz(const char *str)
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

uint32_t get_type_size(uint16_t type)
{
    switch(type)
    {
        case TYPE_UINT8:case TYPE_INT8: return 1;
        case TYPE_UINT16:case TYPE_INT16: return 2;
        case TYPE_UINT32:case TYPE_INT32: return 4;
        case TYPE_UINT64:case TYPE_INT64: return 8;
        case TYPE_UINT128:case TYPE_INT128: return 16;
        default:
            logf("mtp: error: get_type_size called with an unknown type(%hu)", type);
            return 0;
    }
}

void unsafe_copy_mtp_string(struct mtp_string *to, const struct mtp_string *from)
{
    to->length = from->length;
    memcpy(to->data, from->data, sizeof(from->data[0]) * from->length);
}

void pack_data_block_typed_ptr(const void *ptr, uint16_t type)
{
    if(type == TYPE_STR)
        pack_data_block_string(ptr);
    else
        pack_data_block_ptr(ptr, get_type_size(type));
}

bool unpack_data_block_string(struct mtp_string *str, uint32_t max_len)
{
    if(!unpack_data_block_uint8_t(&str->length))
        return false;
    
    if(str->length > max_len)
        return false;
    
    return unpack_data_block_ptr(str->data, sizeof(uint16_t) * str->length);
}

bool unpack_data_block_string_charz(unsigned char *dest, uint32_t dest_len)
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
    
void pack_data_block_date_time(struct tm *time)
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

void finish_pack_data_block(void)
{
    struct generic_container *cont = (struct generic_container *) send_buffer;
    cont->length = mtp_state.data - send_buffer;
}

bool finish_unpack_data_block(void)
{
    return (mtp_state.data_len == 0);
}

void start_pack_data_block_array(void)
{
    mtp_state.cur_array_length_ptr = (uint32_t *) mtp_state.data;
    *mtp_state.cur_array_length_ptr = 0; /* zero length for now */
    mtp_state.data += 4;
}

#define define_pack_array_elem(type) \
    void pack_data_block_array_elem_##type(type val) \
    { \
        pack_data_block_##type(val); \
        (*mtp_state.cur_array_length_ptr)++; \
    } \

define_pack_array_elem(uint16_t)
define_pack_array_elem(uint32_t)

#undef define_pack_array_elem

uint32_t finish_pack_data_block_array(void)
{
    /* return number of elements */
    return *mtp_state.cur_array_length_ptr;
}

unsigned char *get_data_block_ptr(void)
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
        if(cont->code != mtp_cur_cmd.code)
            return fail_with(ERROR_INVALID_DATASET);
        if(cont->transaction_id != mtp_cur_cmd.transaction_id)
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

void receive_split_data(recv_split_routine rct, finish_recv_split_routine frst, void *user)
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

void send_split_data(uint32_t nb_bytes, send_split_routine fn, finish_send_split_routine fsst, void *user)
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
    cont->code = mtp_cur_cmd.code;
    cont->transaction_id = mtp_cur_cmd.transaction_id;
    
    int size = MIN(max_usb_send_xfer_size() - sizeof(struct generic_container), nb_bytes);
    int ret = fn(send_buffer + sizeof(struct generic_container),
                size,
                user);
    if(ret < 0)
        return mtp_state.finish_send_split(true, mtp_state.user);
    
    mtp_state.rem_bytes -= ret;
    usb_drv_send_nonblocking(ep_bulk_in, send_buffer, ret + sizeof(struct generic_container));
}

void generic_finish_split_routine(bool error, void *user)
{
    (void) user;
    if(error)
    {
        mtp_cur_resp.code = ERROR_INCOMPLETE_TRANSFER;
        mtp_cur_resp.nb_parameters = 0;
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

void send_data_block(void)
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

void fail_op_with(uint16_t error_code, enum data_phase_type dht)
{
    logf("mtp: fail operation with error code 0x%x", error_code);
    mtp_cur_resp.code = error_code;
    mtp_cur_resp.nb_parameters = 0;
    
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
 * Command dispatching
 */

void handle_command2(void)
{
    #define want_nb_params(p, data_phase) \
        if(mtp_cur_cmd.nb_parameters != p) return fail_op_with(ERROR_INVALID_DATASET, data_phase);
    #define want_nb_params_range(pi, pa, data_phase) \
        if(mtp_cur_cmd.nb_parameters < pi || mtp_cur_cmd.nb_parameters > pa) return fail_op_with(ERROR_INVALID_DATASET, data_phase);
    #define want_session(data_phase) \
        if(mtp_state.session_id == 0x00000000) return fail_op_with(ERROR_SESSION_NOT_OPEN, data_phase);
    
    switch(mtp_cur_cmd.code)
    {
        case MTP_OP_GET_DEV_INFO:
            want_nb_params(0, SEND_DATA_PHASE) /* no parameter */
            return get_device_info();
        case MTP_OP_OPEN_SESSION:
            want_nb_params(1, NO_DATA_PHASE) /* one parameter: session id */
            return open_session(mtp_cur_cmd.param[0]);
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
            return get_storage_info(mtp_cur_cmd.param[0]);
        case MTP_OP_GET_NUM_OBJECTS:
            /*  There are two optional parameters and one mandatory */
            want_nb_params_range(1, 3, NO_DATA_PHASE)
            want_session(NO_DATA_PHASE) /* must be called in a session */
            return get_num_objects(mtp_cur_cmd.nb_parameters, mtp_cur_cmd.param[0], mtp_cur_cmd.param[1], mtp_cur_cmd.param[2]);
        case MTP_OP_GET_OBJECT_HANDLES:
            /*  There are two optional parameters and one mandatory */
            want_nb_params_range(1, 3, SEND_DATA_PHASE)
            want_session(SEND_DATA_PHASE) /* must be called in a session */
            return get_object_handles(mtp_cur_cmd.nb_parameters, mtp_cur_cmd.param[0], mtp_cur_cmd.param[1], mtp_cur_cmd.param[2]);
        case MTP_OP_GET_OBJECT_INFO:
            want_nb_params(1, SEND_DATA_PHASE) /* one parameter */
            want_session(SEND_DATA_PHASE) /* must be called in a session */
            return get_object_info(mtp_cur_cmd.param[0]);
        case MTP_OP_GET_OBJECT:
            want_nb_params(1, SEND_DATA_PHASE) /* one parameter */
            want_session(SEND_DATA_PHASE) /* must be called in a session */
            return get_object(mtp_cur_cmd.param[0]);
        case MTP_OP_DELETE_OBJECT:
            want_nb_params_range(1, 2, NO_DATA_PHASE)
            want_session(NO_DATA_PHASE)
            return delete_object(mtp_cur_cmd.nb_parameters, mtp_cur_cmd.param[0], mtp_cur_cmd.param[1]);
        case MTP_OP_SEND_OBJECT_INFO:
            want_nb_params_range(0, 2, SEND_DATA_PHASE)
            want_session(RECV_DATA_PHASE)
            return send_object_info(mtp_cur_cmd.nb_parameters, mtp_cur_cmd.param[0], mtp_cur_cmd.param[1]);
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
            return get_device_prop_desc(mtp_cur_cmd.param[0]);
        case MTP_OP_GET_DEV_PROP_VALUE:
            want_nb_params(1, SEND_DATA_PHASE) /* one parameter */
            want_session(SEND_DATA_PHASE)
            return get_device_prop_value(mtp_cur_cmd.param[0]);
        case MTP_OP_SET_DEV_PROP_VALUE:
            want_nb_params(1, RECV_DATA_PHASE) /* one parameter */
            want_session(RECV_DATA_PHASE)
            return set_device_prop_value(mtp_cur_cmd.param[0]);
        case MTP_OP_RESET_DEV_PROP_VALUE:
            want_nb_params(1, NO_DATA_PHASE) /* one parameter */
            want_session(NO_DATA_PHASE)
            return reset_device_prop_value(mtp_cur_cmd.param[0]);
        #if 0
        case MTP_OP_MOVE_OBJECT:
            want_nb_params_range(2, 3, NO_DATA_PHASE) /* two or three parameters */
            want_session(NO_DATA_PHASE)
            return move_object(mtp_cur_cmd.nb_parameters, mtp_cur_cmd.param[0], mtp_cur_cmd.param[1], mtp_cur_cmd.param[2]);
        case MTP_OP_COPY_OBJECT:
            want_nb_params_range(2, 3, NO_DATA_PHASE) /* two or three parameters */
            want_session(NO_DATA_PHASE)
            return copy_object(mtp_cur_cmd.nb_parameters, mtp_cur_cmd.param[0], mtp_cur_cmd.param[1], mtp_cur_cmd.param[2]);
        #endif
        case MTP_OP_GET_PARTIAL_OBJECT:
            want_nb_params(3, SEND_DATA_PHASE)
            want_session(SEND_DATA_PHASE)
            return get_partial_object(mtp_cur_cmd.param[0],mtp_cur_cmd.param[1],mtp_cur_cmd.param[2]);
        case MTP_OP_GET_OBJ_PROPS_SUPPORTED:
            want_nb_params(1, SEND_DATA_PHASE)
            want_session(SEND_DATA_PHASE)
            return get_object_props_supported(mtp_cur_cmd.param[0]);
        case MTP_OP_GET_OBJ_PROP_DESC:
            want_nb_params(2, SEND_DATA_PHASE)
            want_session(SEND_DATA_PHASE)
            return get_object_prop_desc(mtp_cur_cmd.param[0], mtp_cur_cmd.param[1]);
        case MTP_OP_GET_OBJ_PROP_VALUE:
            want_nb_params(2, SEND_DATA_PHASE)
            want_session(SEND_DATA_PHASE)
            return get_object_prop_value(mtp_cur_cmd.param[0], mtp_cur_cmd.param[1]);
        case MTP_OP_GET_OBJ_REFERENCES:
            want_nb_params(1, SEND_DATA_PHASE) /* one parameter */
            want_session(SEND_DATA_PHASE)
            return get_object_references(mtp_cur_cmd.param[0]);
        default:
            logf("mtp: unknown command code 0x%x", mtp_cur_cmd.code);
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
    
    mtp_cur_cmd.code = cont->code;
    mtp_cur_cmd.transaction_id = cont->transaction_id;
    mtp_cur_cmd.nb_parameters = cont->length - sizeof(struct generic_container);
    
    if((mtp_cur_cmd.nb_parameters % 4) != 0)
        return fail_with(ERROR_INVALID_DATASET);
    else
        mtp_cur_cmd.nb_parameters /= 4;
    
    memcpy(&mtp_cur_cmd.param[0], recv_buffer + sizeof(struct generic_container), 4*mtp_cur_cmd.nb_parameters);
    
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

static int usb_ack_control(void)
{
    return usb_drv_recv(EP_CONTROL, NULL, 0);
}


#ifdef USB_ENABLE_MS_DESCRIPTOR
int usb_mtp_get_ms_descriptor(uint16_t wValue, uint16_t wIndex, unsigned char *dest, int max_length)
{
    logf("mtp: get_ms_descriptor(%d,%d)", wValue, wIndex);
    
    if(wIndex == USB_MS_DT_COMPAT_ID)
    {
        logf("mtp: get compatible ID");
        
        compat_id_header.bCount = 1; /* One function */
        compat_id_header.dwLength = sizeof(struct usb_ms_compat_id_descriptor_header) + 
            compat_id_header.bCount * sizeof(struct usb_ms_compat_id_descriptor_function);
        
        compat_id_mtp_function.bRESERVED1 = 0x1; /* written in the spec */
        compat_id_mtp_function.bFirstInterfaceNumber = usb_interface;
        
        if((int)compat_id_header.dwLength <= max_length)
        {
            PACK_DATA(dest, compat_id_header);
            PACK_DATA(dest, compat_id_mtp_function);

            return compat_id_header.dwLength;
        }
    }
    else
    {
        #if 0
        logf("mtp: invalid request (probably from libmtp)");
        usb_ack_control();
        usb_drv_stall(EP_CONTROL, true, true);
        #else
        logf("mtp: invalid request (probably from libmtp), answer the same as compat ID");
        compat_id_header.bCount = 1; /* One function */
        compat_id_header.dwLength = sizeof(struct usb_ms_compat_id_descriptor_header) + 
            compat_id_header.bCount * sizeof(struct usb_ms_compat_id_descriptor_function);
        
        compat_id_mtp_function.bRESERVED1 = 0x1; /* written in the spec */
        compat_id_mtp_function.bFirstInterfaceNumber = usb_interface;
        
        if((int)compat_id_header.dwLength <= max_length)
        {
            PACK_DATA(dest, compat_id_header);
            PACK_DATA(dest, compat_id_mtp_function);

            return compat_id_header.dwLength;
        }
        #endif
    }
    
    return 0;
}
#endif

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
            usb_ack_control();
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
            
            usb_ack_control();
            
            if(!usb_drv_send(EP_CONTROL, dest, sizeof(struct device_status)))
                handled = true;
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
    
    probe_storages();
    
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

