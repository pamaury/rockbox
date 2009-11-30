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
#ifndef USB_MTP_INT_H
#define USB_MTP_INT_H

#include "system.h"
#include "sprintf.h"
#include "dir.h"
#include "powermgmt.h"
#include "timefuncs.h"
#include "file.h"
#include "errno.h"
#include "string.h"

#include <dircache.h>

#define USB_MTP_SUBCLASS    0x1
#define USB_MTP_PROTO       0x1

/*
 * Types
 */
#define TYPE_UNIFORM_ARRAY  0x4000

#define TYPE_UNDEF      0x0000
#define TYPE_INT8       0x0001
#define TYPE_UINT8      0x0002
#define TYPE_INT16      0x0003
#define TYPE_UINT16     0x0004
#define TYPE_INT32      0x0005
#define TYPE_UINT32     0x0006
#define TYPE_INT64      0x0007
#define TYPE_UINT64     0x0008
#define TYPE_INT128     0x0009
#define TYPE_UINT128    0x000a
#define TYPE_UNDEF      0x0000
/* arrays of ...*/
#define TYPE_AINT8      (TYPE_AINT8|TYPE_UNIFORM_ARRAY)
#define TYPE_AUINT8     (TYPE_UINT8|TYPE_UNIFORM_ARRAY)
#define TYPE_AINT16     (TYPE_INT16|TYPE_UNIFORM_ARRAY)
#define TYPE_AUINT16    (TYPE_UINT16|TYPE_UNIFORM_ARRAY)
#define TYPE_AINT32     (TYPE_INT32|TYPE_UNIFORM_ARRAY)
#define TYPE_AUINT32    (TYPE_UINT32|TYPE_UNIFORM_ARRAY)
#define TYPE_AINT64     (TYPE_INT64|TYPE_UNIFORM_ARRAY)
#define TYPE_AUINT64    (TYPE_UINT64|TYPE_UNIFORM_ARRAY)
#define TYPE_AINT128    (TYPE_INT128|TYPE_UNIFORM_ARRAY)
#define TYPE_AUINT128   (TYPE_UINT128|TYPE_UNIFORM_ARRAY)a
/* Variable-length UNICODE String */
#define TYPE_STR        0xffff

struct mtp_string
{
    uint8_t length; /* number of characters */
    uint16_t data[]; /* data is empty of length=0, otherwise, it's null-terminated */
} __attribute__ ((packed));

struct generic_container
{
    uint32_t length;
    uint16_t type;
    uint16_t code;
    uint32_t transaction_id;
} __attribute__ ((packed));

struct cancel_data
{
    uint16_t cancel_code;
    uint32_t transaction_id;
} __attribute__ ((packed));

struct device_status
{
    uint16_t length;
    uint16_t code;
} __attribute__ ((packed));

struct object_info
{
    uint32_t storage_id;
    uint16_t object_format;
    uint16_t protection;
    uint32_t compressed_size;
    uint16_t thumb_fmt;
    uint32_t thumb_compressed_size;
    uint32_t thumb_pix_width;
    uint32_t thumb_pix_height;
    uint32_t image_pix_width;
    uint32_t image_pix_height;
    uint32_t image_bit_depth;
    uint32_t parent_handle;
    uint16_t association_type;
    uint32_t association_desc;
    uint32_t sequence_number;
    /* variable fields */
} __attribute__ ((packed));

struct mtp_command
{
    uint16_t code;
    uint16_t transaction_id;
    int nb_parameters;
    uint32_t param[5];
};

struct mtp_pending_objectinfo
{
    uint32_t handle;
    uint32_t size;
};

struct mtp_response
{
    uint16_t code;
    int nb_parameters;
    uint32_t param[5];
};

#define define_mtp_array(type) \
    struct mtp_array_##type \
    { \
        uint32_t length; \
        type data[]; \
    }; \

define_mtp_array(uint16_t)

typedef void (*mtp_obj_prop_value_get)(const struct dircache_entry *);

struct mtp_obj_prop
{
    const struct mtp_array_uint16_t *obj_fmt;
    uint16_t obj_prop_code;
    uint16_t data_type;
    uint8_t get_set;
    const void *default_value;
    uint8_t form;
    const void *form_value;
    mtp_obj_prop_value_get get;
};

struct device_info
{
    uint16_t std_version; /* Standard Version */
    uint32_t vendor_ext; /* Vendor Extension ID */
    uint16_t mtp_version; /* MTP Version */
    uint16_t mode; /* Functional Mode */
};

/*
 * Error codes
 */
#define ERROR_OK                    0x2001
#define ERROR_GENERAL_ERROR         0x2002
#define ERROR_SESSION_NOT_OPEN      0x2003
#define ERROR_INVALID_TRANSACTION_ID    0x2004
#define ERROR_OP_NOT_SUPPORTED      0x2005
#define ERROR_PARAM_NOT_SUPPORTED   0x2006
#define ERROR_INCOMPLETE_TRANSFER   0x2007
#define ERROR_INVALID_STORAGE_ID    0x2008
#define ERROR_INVALID_OBJ_HANDLE    0x2009
#define ERROR_DEV_PROP_NOT_SUPPORTED    0x200a
#define ERROR_INVALID_OBJ_FORMAT    0x200b
#define ERROR_STORE_FULL            0x200c
#define ERROR_OBJ_WRITE_PROTECTED   0x200d
#define ERROR_STORE_READ_ONLY       0x200e
#define ERROR_ACCESS_DENIED         0x200f
#define ERROR_PARTIAL_DELETION      0x2012
#define ERROR_STORAGE_NOT_AVAILABLE 0x2013
#define ERROR_SPEC_BY_FMT_UNSUPPORTED   0x2014
#define ERROR_NO_VALID_OBJECTINFO   0x2015
#define ERROR_DEV_BUSY              0x2019
#define ERROR_INVALID_PARENT_OBJ    0x201a
#define ERROR_INVALID_PARAMETER     0x201d
#define ERROR_SESSION_ALREADY_OPEN  0x201e
#define ERROR_TRANSACTION_CANCELLED 0x201f
#define ERROR_INVALID_OBJ_PROP_CODE 0xa801
#define ERROR_INVALID_OBJ_PROP_FMT  0xa802
#define ERROR_INVALID_DATASET       0xa806

/*
 * Container Type
 */
#define CONTAINER_COMMAND_BLOCK     1
#define CONTAINER_DATA_BLOCK        2
#define CONTAINER_RESPONSE_BLOCK    3
#define CONTAINER_EVENT_BLOCK       4

/*
 * Class-Specific Requests
 */
#define USB_CTRL_CANCEL_REQUEST     0x64
#define USB_CTRL_GET_EXT_EVT_DATA   0x65
#define USB_CTRL_DEV_RESET_REQUEST  0x66
#define USB_CTRL_GET_DEV_STATUS     0x67

#define USB_CANCEL_CODE 0x4001

/*
 * Operations
 */
#define MTP_OP_GET_DEV_INFO         0x1001
#define MTP_OP_OPEN_SESSION         0x1002
#define MTP_OP_CLOSE_SESSION        0x1003
#define MTP_OP_GET_STORAGE_IDS      0x1004
#define MTP_OP_GET_STORAGE_INFO     0x1005
#define MTP_OP_GET_NUM_OBJECTS      0x1006
#define MTP_OP_GET_OBJECT_HANDLES   0x1007
#define MTP_OP_GET_OBJECT_INFO      0x1008
#define MTP_OP_GET_OBJECT           0x1009
#define MTP_OP_DELETE_OBJECT        0x100B
#define MTP_OP_SEND_OBJECT_INFO     0x100C
#define MTP_OP_SEND_OBJECT          0x100D
#define MTP_OP_RESET_DEVICE         0x1010
#define MTP_OP_POWER_DOWN           0x1013
#define MTP_OP_GET_DEV_PROP_DESC    0x1014
#define MTP_OP_GET_DEV_PROP_VALUE   0x1015
#define MTP_OP_MOVE_OBJECT          0x1019
#define MTP_OP_COPY_OBJECT          0x101A
#define MTP_OP_GET_PARTIAL_OBJECT   0x101B
#define MTP_OP_GET_OBJ_PROPS_SUPPORTED  0x9801
#define MTP_OP_GET_OBJ_PROP_DESC    0x9802
#define MTP_OP_GET_OBJ_PROP_VALUE   0x9803
#define MTP_OP_GET_OBJ_REFERENCES   0x9810

/*
 * Storage Types
 */
#define STOR_TYPE_FIXED_RAM         0x0003
#define STOR_TYPE_REMOVABLE_RAM     0x0004

/*
 * Filesystem Types
 */
#define FS_TYPE_GENERIC_FLAT            0x0001
#define FS_TYPE_GENERIC_HIERARCHICAL    0x0002

/*
 * Access Capabilities
 */
#define ACCESS_CAP_RW           0x0000
#define ACCESS_CAP_RO_WITHOUT   0x0001
#define ACCESS_CAP_RO_WITH      0x0002

/*
 * Device Properties
 */
#define DEV_PROP_GET        0x00
#define DEV_PROP_GET_SET    0x01

#define DEV_PROP_FORM_NONE  0x00
#define DEV_PROP_FORM_RANGE 0x01
#define DEV_PROP_FORM_ENUM  0x02

#define DEV_PROP_BATTERY_LEVEL  0x5001
#define DEV_PROP_DATE_TIME      0x5011
#define DEV_PROP_FRIENDLY_NAME  0xd402

/*
 * Object Formats
 */
#define OBJ_FMT_UNDEFINED   0x3000
#define OBJ_FMT_ASSOCIATION 0x3001

/*
 * Association Types
 */
#define ASSOC_TYPE_NONE     0x0000
#define ASSOC_TYPE_FOLDER   0x0001

/*
 * Object Properties
 */
#define OBJ_PROP_GET        0x00
#define OBJ_PROP_GET_SET    0x01

#define OBJ_PROP_FORM_NONE  0x00
#define OBJ_PROP_FORM_ENUM  0x02
#define OBJ_PROP_FORM_DATE  0x03
 
#define OBJ_PROP_STORAGE_ID 0xdc01
#define OBJ_PROP_OBJ_FMT    0xdc02
#define OBJ_PROP_OBJ_SIZE   0xdc04
#define OBJ_PROP_ASSOC_TYPE 0xdc05
#define OBJ_PROP_ASSOC_DESC 0xdc06
#define OBJ_PROP_FILENAME   0xdc07
#define OBJ_PROP_C_DATE     0xdc08
#define OBJ_PROP_M_DATE     0xdc09
#define OBJ_PROP_PARENT_OBJ 0xdc0b
#define OBJ_PROP_HIDDEN     0xdc0d
#define OBJ_PROP_SYS_OBJ    0xdc0e
#define OBJ_PROP_NAME       0xdc44

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

struct mtp_state_t
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
};

/*
 * usb_mtp.c:
 * - low-level protocol code
 */
extern struct mtp_state_t mtp_state;
extern struct mtp_command mtp_cur_cmd;
extern struct mtp_response mtp_cur_resp;

void fail_with(uint16_t error_code);
void fail_op_with(uint16_t error_code, enum data_phase_type dht);

#define define_pack_array(type) \
    void pack_data_block_array_##type(const struct mtp_array_##type *arr);
    
#define define_pack_unpack(type) \
    void pack_data_block_##type(type val); \
    bool unpack_data_block_##type(type *val);

#define define_pack_array_elem(type) \
    void pack_data_block_array_elem_##type(type val);



void start_pack_data_block(void);
void start_unpack_data_block(void *data, uint32_t data_len);

void start_pack_data_block_array(void);
uint32_t finish_pack_data_block_array(void);

define_pack_array(uint16_t)

define_pack_unpack(uint8_t)
define_pack_unpack(uint16_t)
define_pack_unpack(uint32_t)
define_pack_unpack(uint64_t)

define_pack_array_elem(uint16_t)
define_pack_array_elem(uint32_t)

void pack_data_block_ptr(const void *ptr, int length);
bool unpack_data_block_ptr(void *ptr, size_t length);
void pack_data_block_string(const struct mtp_string *str);
void pack_data_block_string_charz(const char *str);
uint32_t get_type_size(uint16_t type);
void pack_data_block_typed_ptr(const void *ptr, uint16_t type);
bool unpack_data_block_string_charz(unsigned char *dest, uint32_t dest_len);
void pack_data_block_date_time(struct tm *time);

void finish_pack_data_block(void);
bool finish_unpack_data_block(void);

unsigned char *get_data_block_ptr(void);

void receive_split_data(recv_split_routine rct, finish_recv_split_routine frst, void *user);
void send_split_data(uint32_t nb_bytes, send_split_routine fn, finish_send_split_routine fsst, void *user);
void generic_finish_split_routine(bool error, void *user);
void send_data_block(void);
void send_response(void);

#undef define_pack_array
#undef define_pack_unpack
#undef define_pack_array_elem

/*
 * usb_mtp_storage_id.c
 * - storage id abstraction api
 */
void probe_storages(void);

uint32_t get_first_storage_id(void);
uint32_t get_next_storage_id(uint32_t stor_id);
bool is_valid_storage_id(uint32_t stor_id);

int storage_id_to_volume(uint32_t stor_id);
uint32_t volume_to_storage_id(int volume);

const char *get_storage_description(uint32_t stor_id);
const char *get_volume_identifier(uint32_t stor_id);
const char *get_storage_id_mount_point(uint32_t stor_id);
uint32_t get_storage_size(uint32_t stor_id);
uint32_t get_storage_free_space(uint32_t stor_id);

/*
 * usb_mtp_operations.c
 * - operations handling
 */
void open_session(uint32_t session_id);
void close_session(bool send_resp);
void get_device_info(void);
void get_storage_ids(void);
void get_storage_info(uint32_t stor_id);
void get_num_objects(int nb_params, uint32_t stor_id, uint32_t obj_fmt, uint32_t obj_handle_parent);
void get_object_handles(int nb_params, uint32_t stor_id, uint32_t obj_fmt, uint32_t obj_handle_parent);
void get_object_info(uint32_t object_handle);
void get_object(uint32_t object_handle);
void get_partial_object(uint32_t object_handle,uint32_t offset,uint32_t max_size);
void send_object_info(int nb_params, uint32_t stor_id, uint32_t obj_handle_parent);
void send_object(void);
void delete_object(int nb_params, uint32_t obj_handle, uint32_t __unused);
void copy_object(int nb_params, uint32_t obj_handle, uint32_t stor_id, uint32_t obj_parent_handle);
void move_object(int nb_params, uint32_t obj_handle, uint32_t stor_id, uint32_t obj_parent_handle);
void get_object_references(uint32_t object_handle);
void reset_device(void);
void get_device_prop_desc(uint32_t device_prop);
void get_device_prop_value(uint32_t device_prop);
void get_object_props_supported(uint32_t object_fmt);
void get_object_prop_desc(uint32_t obj_prop, uint32_t obj_fmt);
void get_object_prop_value(uint32_t obj_handle, uint32_t obj_prop);

#endif

