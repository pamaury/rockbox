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
#ifndef USB_MTP_H
#define USB_MTP_H

#include "usb_ch9.h"

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
#define ERROR_STORAGE_NOT_AVAILABLE 0x2013
#define ERROR_SPEC_BY_FMT_UNSUPPORTED   0x2014
#define ERROR_DEV_BUSY              0x2019
#define ERROR_INVALID_PARAMETER     0x201d
#define ERROR_SESSION_ALREADY_OPEN  0x201e
#define ERROR_TRANSACTION_CANCELLED 0x201f
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
#define MTP_OP_RESET_DEVICE         0x1010
#define MTP_OP_POWER_DOWN           0x1013
#define MTP_OP_GET_DEV_PROP_DESC    0x1014
#define MTP_OP_GET_DEV_PROP_VALUE   0x1015
#define MTP_OP_GET_OBJ_PROPS_SUPPORTED  0x9801

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

/*
 * Object Properties
 */
#define OBJ_PROP_

int usb_mtp_request_endpoints(struct usb_class_driver *);
int usb_mtp_set_first_interface(int interface);
int usb_mtp_get_config_descriptor(unsigned char *dest,int max_packet_size);
void usb_mtp_init_connection(void);
void usb_mtp_init(void);
void usb_mtp_disconnect(void);
void usb_mtp_transfer_complete(int ep,int dir, int status, int length);
bool usb_mtp_control_request(struct usb_ctrlrequest* req, unsigned char *dest);

#endif

