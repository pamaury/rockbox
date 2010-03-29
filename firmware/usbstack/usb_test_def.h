/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 * $Id$
 *
 * Copyright (C) 20010 by amaury Pouly
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
#ifndef USB_TEST_DEF_H
#define USB_TEST_DEF_H

#include "usb_ch9.h"

#define USB_TEST_CLASS      USB_CLASS_APP_SPEC
#define USB_TEST_SUBCLASS   0xc0
#define USB_TEST_PROTOCOL   0xde

#define USB_TEST_STATUS_OK          0x00

#define USB_TEST_REQ_GET_STATUS     0x77
#define USB_TEST_REQ_DATA           0x78
#define USB_TEST_REQ_TEST_ISO       0x79
#define USB_TEST_REQ_STAT           0x80
#define USB_TEST_REQ_CANCEL         0x81
#define USB_TEST_REQ_TEST_BULK      0x82
#define USB_TEST_REQ_TEST_INT       0x83

#define USB_TEST_MAGIC          0xdeadbeef

/* IN transfers */
#define USB_TEST_DATA_IN_MASK       0x0f
#define USB_TEST_DATA_IN_GENERATE   0x00
/* OUT transfers */
#define USB_TEST_DATA_OUT_MASK      0xf0
#define USB_TEST_DATA_OUT_IGNORE    0x00
#define USB_TEST_DATA_OUT_CRC32     0x10

#define USB_TEST_ISO_OUT            0x00
#define USB_TEST_ISO_IN             0x01

#define USB_TEST_BULK_OUT           0x00
#define USB_TEST_BULK_IN            0x01

#define USB_TEST_INT_IN             0x00

#define USB_TEST_STAT_CLEAR         0x00
#define USB_TEST_STAT_LOG           0x01

struct usb_test_data_request
{
    uint32_t dwMagic;
    uint8_t  bReq;
} __attribute__ ((packed));

struct usb_test_iso_request
{
    uint32_t dwMagic;
    uint8_t  bReq;
    uint32_t dwLength;
} __attribute__ ((packed));

struct usb_test_bulk_request
{
    uint32_t dwMagic;
    uint8_t  bReq;
    uint32_t dwLength;
} __attribute__ ((packed));

struct usb_test_int_request
{
    uint32_t dwMagic;
    uint8_t  bReq;
    uint32_t dwLength;
} __attribute__ ((packed));

struct usb_test_stat_request
{
    uint32_t dwMagic;
    uint8_t  bReq;
} __attribute__ ((packed));

#endif

