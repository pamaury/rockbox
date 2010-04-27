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

#if 1
#define errorf  _logf
#else
#define errorf  logf
#endif

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

#define mtp_string_fixed(len) \
    struct \
    { \
        uint8_t length; \
        uint16_t data[len]; \
    } __attribute__ ((packed))

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
    } __attribute__ ((packed));

define_mtp_array(uint16_t)

#define define_mtp_enum_form(type) \
    struct mtp_enum_form_##type \
    { \
        uint16_t length; \
        type data[]; \
    } __attribute__ ((packed));

define_mtp_enum_form(uint16_t)

#define define_mtp_range_form(type) \
    struct mtp_range_form_##type \
    { \
        type min, max, step; \
    } __attribute__ ((packed));

define_mtp_range_form(uint8_t)
define_mtp_range_form(uint16_t)
define_mtp_range_form(uint32_t)

typedef uint16_t (*mtp_obj_prop_value_get)(uint32_t obj_handle);

typedef uint16_t (*mtp_dev_prop_value_get)(void);
typedef uint16_t (*mtp_dev_prop_value_set)(void);
typedef uint16_t (*mtp_dev_prop_value_reset)(void);

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

struct mtp_dev_prop
{
    uint16_t dev_prop_code;
    uint16_t data_type;
    uint8_t get_set;
    const void *default_value;
    uint8_t form;
    const void *form_value;
    mtp_dev_prop_value_get get;
    mtp_dev_prop_value_set set;
    mtp_dev_prop_value_reset reset;
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
#define ERROR_DEV_PROP_UNSUPPORTED   0x200a
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
#define ERROR_INVALID_DEV_PROP_FMT  0x201b
#define ERROR_INVALID_DEV_PROP_VAL  0x201c
#define ERROR_INVALID_PARAMETER     0x201d
#define ERROR_SESSION_ALREADY_OPEN  0x201e
#define ERROR_TRANSACTION_CANCELLED 0x201f
#define ERROR_INVALID_OBJ_PROP_CODE 0xa801
#define ERROR_INVALID_OBJ_PROP_FMT  0xa802
#define ERROR_INVALID_OBJ_PROP_VAL  0xa803
#define ERROR_INVALID_DATASET       0xa806
#define ERROR_OBJ_PROP_UNSUPPORTED  0xa80a

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
#define MTP_OP_SET_DEV_PROP_VALUE   0x1016
#define MTP_OP_RESET_DEV_PROP_VALUE 0x1017
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
#define DEV_PROP_FORM_DATE  0x03

#define DEV_PROP_BATTERY_LEVEL  0x5001
#define DEV_PROP_DATE_TIME      0x5011
#define DEV_PROP_FRIENDLY_NAME  0xd402

/*
 * Object Formats
 */
#define OBJ_FMT_UNDEFINED   0x3000
#define OBJ_FMT_ASSOCIATION 0x3001
#define OBJ_FMT_AIFF        0x3007
#define OBJ_FMT_WAV         0x3008
#define OBJ_FMT_MP3         0x3009
#define OBJ_FMT_MPEG        0x300B
#define OBJ_FMT_ASF         0x300C
#define OBJ_FMT_UNDEF_AUDIO 0xB900
#define OBJ_FMT_WMA         0xB901
#define OBJ_FMT_OGG         0xB902
#define OBJ_FMT_AAC         0xB903
#define OBJ_FMT_FLAC        0xB906
#define OBJ_FMT_MP2         0xB983

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
#define OBJ_PROP_FORM_RANGE 0x01
#define OBJ_PROP_FORM_ENUM  0x02
#define OBJ_PROP_FORM_DATE  0x03
 
/* Common */
#define OBJ_PROP_STORAGE_ID 0xdc01
#define OBJ_PROP_OBJ_FMT    0xdc02
#define OBJ_PROP_OBJ_SIZE   0xdc04
/* Association */
#define OBJ_PROP_ASSOC_TYPE 0xdc05
#define OBJ_PROP_ASSOC_DESC 0xdc06
/* Common (cont.) */
#define OBJ_PROP_FILENAME   0xdc07
#define OBJ_PROP_C_DATE     0xdc08
#define OBJ_PROP_M_DATE     0xdc09
#define OBJ_PROP_PARENT_OBJ 0xdc0b
#define OBJ_PROP_HIDDEN     0xdc0d
#define OBJ_PROP_SYS_OBJ    0xdc0e
#define OBJ_PROP_PERSISTENT 0xdc41
#define OBJ_PROP_NAME       0xdc44
/* Audio */
#define OBJ_PROP_ARTIST         0xdc46
#define OBJ_PROP_DURATION       0xdc89
#define OBJ_PROP_RATING         0xdc8a
#define OBJ_PROP_TRACK          0xdc8b
#define OBJ_PROP_GENRE          0xdc8c
#define OBJ_PROP_USE_COUNT      0xdc91
#define OBJ_PROP_LAST_ACCESSED  0xdc93
#define OBJ_PROP_COMPOSER       0xdc96
#define OBJ_PROP_EFFECT_RATING  0xdc97
#define OBJ_PROP_RELEASE_DATE   0xdc99
#define OBJ_PROP_ALBUM_NAME     0xdc9a
#define OBJ_PROP_ALBUM_ARTIST   0xdc9b
#define OBJ_PROP_SAMPLE_RATE    0xde93
#define OBJ_PROP_NB_CHANNELS    0xde94
#define OBJ_PROP_AUDIO_BITRATE  0xdc9a

/* Audio Channels */
#define AUDIO_CHANNELS_MONO     0x0001
#define AUDIO_CHANNELS_STEREO   0x0002


/* List of all supported (by the firmware) audio formats */
#define ALL_MTP_AUDIO_FORMATS \
    OBJ_FMT_AIFF, OBJ_FMT_WAV, OBJ_FMT_MP3, OBJ_FMT_MPEG, OBJ_FMT_ASF, \
    OBJ_FMT_UNDEF_AUDIO, OBJ_FMT_WMA, OBJ_FMT_OGG, OBJ_FMT_AAC, OBJ_FMT_FLAC, \
    OBJ_FMT_MP2

#define NB_MTP_AUDIO_FORMATS 11

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

/* 128-bit unique identifier (persistent) */
union persistent_unique_id_t
{
    uint32_t u32[4];
    uint16_t u16[8];
    uint8_t u8[16];
} __attribute__ ((packed));

typedef union persistent_unique_id_t persistent_unique_id_t;

/*
 * usb_mtp.c:
 * - low-level protocol code
 * - misc
 */
extern struct mtp_state_t mtp_state;
extern struct mtp_command mtp_cur_cmd;
extern struct mtp_response mtp_cur_resp;

void unsafe_copy_mtp_string(struct mtp_string *to, const struct mtp_string *from);

void fail_with_ex(uint16_t error_code, const char *debug_message);
void fail_op_with_ex(uint16_t error_code, enum data_phase_type dht, const char *debug_message);

#define _STR(a) #a
#define STR(a)  _STR(a)
#define fail_with(code) fail_with_ex(code, __FILE__ ":" STR(__LINE__))
#define fail_op_with(code, phase) fail_op_with_ex(code, phase, __FILE__ ":" STR(__LINE__))

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
bool unpack_data_block_string(struct mtp_string *str, uint32_t max_len); /* max_len inclused terminating null */
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
uint64_t get_storage_size(uint32_t stor_id);
uint64_t get_storage_free_space(uint32_t stor_id);

/*
 *
 * usb_mtp_object_handle.c
 * - object handle abstraction api
 *
 */
enum
{
    /* stop or continue the whole research */
    LF_STOP=0x0,
    LF_CONTINUE=0x1,
    /* (directory only) skip or enter the directory */
    LF_SKIP=0x0,
    LF_ENTER=0x2
};

typedef unsigned (*list_file_func_t)(uint32_t stor_id, uint32_t obj_handle, void *arg);

void init_object_mgr(void);
void deinit_object_mgr(void);

bool is_valid_object_handle(uint32_t obj_handle, bool accept_root);
void copy_object_path(uint32_t obj_handle, char *buffer, int size);
const char *get_object_filename(uint32_t handle);
/* return 0x00000000 on error */
uint32_t get_object_handle_by_name(const char *ptr);
uint32_t get_object_storage_id(uint32_t obj_handle);
bool is_directory_object(uint32_t handle);
bool is_hidden_object(uint32_t handle);
bool is_system_object(uint32_t handle);
uint32_t get_object_size(uint32_t handle);
persistent_unique_id_t get_object_persistent_unique_id(uint32_t handle);
/* return 0x00000000 if at root level */
uint32_t get_parent_object(uint32_t handle);
void copy_object_date_created(uint32_t handle, struct tm *filetm);
void copy_object_date_modified(uint32_t handle, struct tm *filetm);
/* implemented in usb_mtp_object_props.c */
uint16_t get_object_format(uint32_t handle);

/* accept stor_id=0xffffffff whichs means all storages */
/* accept stor_id=0x00000000 if obj_handle!=0xffffffff */
/* depth first search */
/* returns mtp error code */
uint16_t generic_list_files(uint32_t stor_id, uint32_t obj_handle, list_file_func_t lff, void *arg);

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
/*
 * usb_mtp_dev_props.c
 * - device properties handling
 *
 */
void get_device_prop_desc(uint32_t device_prop);
void get_device_prop_value(uint32_t device_prop);
void set_device_prop_value(uint32_t device_prop);
void reset_device_prop_value(uint32_t device_prop);
/*
 * usb_mtp_object_props.c
 * - object properties handling
 *
 */
void get_object_props_supported(uint32_t object_fmt);
void get_object_prop_desc(uint32_t obj_prop, uint32_t obj_fmt);
void get_object_prop_value(uint32_t obj_handle, uint32_t obj_prop);

#endif

