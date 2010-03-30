/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 * $Id$
 *
 * Copyright (C) 20010 Amaury Pouly
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
#include "string.h"
#include "system.h"
#include "usb_core.h"
#include "usb_drv.h"
#include "kernel.h"
#include "usb_test.h"
#include "usb_test_def.h"
#include "usb_class_driver.h"
#define LOGF_ENABLE
#include "logf.h"
#include "stdlib.h"
#include "audio.h"
#include "crc32.h"

#define USB_TEST_USE_REPEAT_MODE

#define USB_TEST_MIN_EP_INTERVAL    1
#define USB_TEST_MAX_EP_INTERVAL    6

enum
{
    USB_TEST_INTERFACE_STRING = 0
};

static const struct usb_string_descriptor __attribute__((aligned(2)))
    usb_test_interface_string =
{
    20*2+2,
    USB_DT_STRING,
    {'R', 'o', 'c', 'k', 'b', 'o', 'x', ' ',
     'T', 'e', 's', 't', ' ',
     'C', 'o', 'n', 't', 'r', 'o', 'l'}
};

static const struct usb_string_descriptor* const usb_strings_list[]=
{
    [USB_TEST_INTERFACE_STRING] = &usb_test_interface_string,
};

#define USB_STRINGS_LIST_SIZE   (sizeof(usb_strings_list)/sizeof(struct usb_string_descriptor *))

static struct usb_interface_descriptor __attribute__((aligned(2)))
    interface_descriptor =
{
    .bLength            = sizeof(struct usb_interface_descriptor),
    .bDescriptorType    = USB_DT_INTERFACE,
    .bInterfaceNumber   = 0,
    .bAlternateSetting  = 0,/* filled later */
    .bNumEndpoints      = 4,
    .bInterfaceClass    = USB_TEST_CLASS,
    .bInterfaceSubClass = USB_TEST_SUBCLASS,
    .bInterfaceProtocol = USB_TEST_PROTOCOL,
    .iInterface         = 0 /* filled later */
};


static struct usb_endpoint_descriptor __attribute__((aligned(2)))
    endpoint_descriptor =
{
    .bLength          = sizeof(struct usb_endpoint_descriptor),
    .bDescriptorType  = USB_DT_ENDPOINT,
    .bEndpointAddress = 0,
    .bmAttributes     = USB_ENDPOINT_XFER_BULK,
    .wMaxPacketSize   = 0,
    .bInterval        = 0 /* filled later */
};

#define NB_SLOTS    16
#define SLOT_SIZE   1024 * 3

#define BUFFER_SIZE NB_SLOTS * SLOT_SIZE
static unsigned char *usb_buffer;
#ifndef USB_TEST_USE_REPEAT_MODE
static unsigned int usb_buffer_bitmap;
#endif

static int ep_bulk_in, ep_bulk_out;
static int ep_iso_in, ep_iso_out;
static int usb_interface;
static int usb_string_index;
static int usb_test_cur_intf;

static int stat_nb_bytes;
static int stat_first_tick;
static int stat_crc_errors;

struct usb_test_data_request usb_data_req USB_DEVBSS_ATTR __attribute__((aligned(32)));
struct usb_test_iso_request usb_iso_req USB_DEVBSS_ATTR __attribute__((aligned(32)));
struct usb_test_stat_request usb_stat_req USB_DEVBSS_ATTR __attribute__((aligned(32)));

unsigned char ep_bulk_slots[2][USB_DRV_SLOT_SIZE] USB_DRV_SLOT_ATTR;
unsigned char ep_iso_slots[2][USB_DRV_SLOT_SIZE * NB_SLOTS] USB_DRV_SLOT_ATTR;

int usb_test_set_first_string_index(int string_index)
{
    usb_string_index = string_index;

    interface_descriptor.iInterface = string_index + USB_TEST_INTERFACE_STRING;

    return string_index + USB_STRINGS_LIST_SIZE;
}

const struct usb_string_descriptor *usb_test_get_string_descriptor(int string_index)
{
    logf("usbtest: get string %d", string_index);
    if(string_index < usb_string_index ||
            string_index >= (int)(usb_string_index + USB_STRINGS_LIST_SIZE))
        return NULL;
    else
        return usb_strings_list[string_index - usb_string_index];
}

int usb_test_request_endpoints(struct usb_class_driver *drv)
{
    ep_bulk_in = usb_core_request_endpoint(USB_ENDPOINT_XFER_BULK, USB_DIR_IN, drv);
    if (ep_bulk_in < 0)
    {
        logf("usbtest: cannot get bulk in ep");
        return -1;
    }

    ep_bulk_out = usb_core_request_endpoint(USB_ENDPOINT_XFER_BULK, USB_DIR_OUT, drv);
    if (ep_bulk_out < 0)
    {
        usb_core_release_endpoint(ep_bulk_in);
        logf("usbtest: cannot get bulk out ep");
        return -1;
    }

    ep_iso_in = usb_core_request_endpoint(USB_ENDPOINT_XFER_ISOC, USB_DIR_IN, drv);
    if(ep_iso_in < 0)
    {
        usb_core_release_endpoint(ep_bulk_in);
        usb_core_release_endpoint(ep_bulk_out);
        logf("usbtest: cannot get iso in ep");
        return -1;
    }

    ep_iso_out = usb_core_request_endpoint(USB_ENDPOINT_XFER_ISOC, USB_DIR_OUT, drv);
    if(ep_iso_out < 0)
    {
        usb_core_release_endpoint(ep_bulk_in);
        usb_core_release_endpoint(ep_bulk_out);
        usb_core_release_endpoint(ep_iso_in);
        logf("usbtest: cannot get iso out ep");
        return -1;
    }

    return 0;
}

int usb_test_set_first_interface(int interface)
{
    usb_interface = interface;
    return interface + 1;
}

int usb_test_set_interface(int intf, int alt)
{
    (void) intf;
    usb_test_cur_intf = alt;
    _logf("usbtest: use interface %d", alt);
    return 0;
}

int usb_test_get_interface(int intf)
{
    (void) intf;
    return usb_test_cur_intf;
}

int usb_test_get_config_descriptor(unsigned char *dest, int max_packet_size)
{
    (void) max_packet_size;
    unsigned char *orig_dest = dest;
    int i;

    for(i = 0; i < (USB_TEST_MAX_EP_INTERVAL - USB_TEST_MIN_EP_INTERVAL + 1); i++)
    {
        interface_descriptor.bInterfaceNumber = usb_interface;
        interface_descriptor.bAlternateSetting = i;
        PACK_DATA(dest, interface_descriptor);

        endpoint_descriptor.wMaxPacketSize = usb_drv_max_endpoint_packet_size(ep_bulk_in);
        endpoint_descriptor.bInterval = 0;
        endpoint_descriptor.bEndpointAddress = ep_bulk_in;
        endpoint_descriptor.bmAttributes = USB_ENDPOINT_XFER_BULK;
        PACK_DATA(dest, endpoint_descriptor);

        endpoint_descriptor.wMaxPacketSize = usb_drv_max_endpoint_packet_size(ep_bulk_out);
        endpoint_descriptor.bEndpointAddress = ep_bulk_out;
        PACK_DATA(dest, endpoint_descriptor);

        endpoint_descriptor.wMaxPacketSize = usb_drv_max_endpoint_packet_size(ep_iso_in) | 2 << 11;
        endpoint_descriptor.bEndpointAddress = ep_iso_in;
        endpoint_descriptor.bInterval = USB_TEST_MIN_EP_INTERVAL + i;
        endpoint_descriptor.bmAttributes = USB_ENDPOINT_XFER_ISOC | USB_ENDPOINT_SYNC_ADAPTIVE;
        PACK_DATA(dest, endpoint_descriptor);

        endpoint_descriptor.wMaxPacketSize = usb_drv_max_endpoint_packet_size(ep_iso_out) | 2 << 11;
        endpoint_descriptor.bEndpointAddress = ep_iso_out;
        PACK_DATA(dest, endpoint_descriptor);
    }

    return (dest - orig_dest);
}

bool usb_test_get_status(struct usb_ctrlrequest* req, unsigned char* dest)
{
    (void) req;
    
    logf("usbtest: get status");
    dest[0] = USB_TEST_STATUS_OK;
    usb_drv_recv_blocking(EP_CONTROL, NULL, 0); /* ack */
    usb_drv_send_blocking(EP_CONTROL, dest, 1); /* send */
    return true;
}

void generate_data(void)
{
}

void process_out_data(int length, void *buffer)
{
    if((usb_data_req.bReq & USB_TEST_DATA_OUT_MASK) == USB_TEST_DATA_OUT_CRC32)
    {
        uint32_t expected_crc;
        uint32_t actual_crc;
        
        if(length < 4)
        {
            logf("usbtest: short CRC block");
            stat_crc_errors++;
            return;
        }
        
        expected_crc = *(uint32_t *)buffer;
        actual_crc = crc_32(buffer + 4, length - 4, 0xffffffff);

        if(actual_crc != expected_crc)
        {
            logf("usbtest: CRC error on %d byte block", length);
            stat_crc_errors++;
        }
    }
}

bool usb_test_data_req(struct usb_ctrlrequest* req, unsigned char* dest)
{
    (void) req;
    (void) dest;
    usb_drv_recv_blocking(EP_CONTROL, &usb_data_req, sizeof(usb_data_req));
    usb_drv_send_blocking(EP_CONTROL, NULL, 0); /* ack */

    logf("usbtest: data");
    logf("usbtest: magic=0x%lx", usb_data_req.dwMagic);
    logf("usbtest: req.IN=%s req.OUT=%s",
        (usb_data_req.bReq & USB_TEST_DATA_IN_MASK) == USB_TEST_DATA_IN_GENERATE ? "GENERATE" : "UNKNOWN",
        (usb_data_req.bReq & USB_TEST_DATA_OUT_MASK) == USB_TEST_DATA_OUT_IGNORE ? "IGNORE" :
            ((usb_data_req.bReq & USB_TEST_DATA_OUT_MASK) == USB_TEST_DATA_OUT_CRC32 ? "CRC32" : "UNKNOWN"));

    return true;
}

#ifndef USB_TEST_USE_REPEAT_MODE
void enqueue_xfer(void)
{
    int i;
    
    if(usb_iso_req.dwLength == 0)
        return;
    
    /*
    if(usb_iso_req.bReq == USB_TEST_ISO_IN)
    {
        generate_data();
        usb_drv_send_nonblocking(ep_iso_in, usb_buffer, MIN((int)usb_iso_req.dwLength, endpoint_descriptor.wMaxPacketSize));
    }
    
    else*/ if(usb_iso_req.bReq == USB_TEST_ISO_OUT)
    {
        for(i = 0; i < NB_SLOTS; i++)
            if((usb_buffer_bitmap & (1 << i)) == 0)
            {
                logf("usbtest: schedule slot %d ptr 0x%x", i, (unsigned int)usb_buffer + i * SLOT_SIZE);
                usb_drv_recv_nonblocking(ep_iso_out,
                    usb_buffer + i * SLOT_SIZE, 
                    MIN((int)usb_iso_req.dwLength,
                    endpoint_descriptor.wMaxPacketSize));
                usb_buffer_bitmap |= 1 << i;
                return;
            }
        logf("usbtest: no free slot");
    }
}
#endif

bool usb_test_iso_test(struct usb_ctrlrequest* req, unsigned char* dest)
{
    (void) req;
    (void) dest;
    
    usb_drv_recv_blocking(EP_CONTROL, &usb_iso_req, sizeof(usb_iso_req));
    usb_drv_send_blocking(EP_CONTROL, NULL, 0); /* ack */

    logf("usbtest: iso test");
    logf("usbtest: magic=0x%lx", usb_iso_req.dwMagic);
    logf("usbtest: dir=%s", usb_iso_req.bReq == USB_TEST_ISO_IN ? "IN" :
            (usb_iso_req.bReq == USB_TEST_ISO_OUT ? "OUT" : "UNKNOWN"));
    logf("usbtest: length=%lu", usb_iso_req.dwLength);

    #ifdef USB_TEST_USE_REPEAT_MODE
    usb_drv_start_repeat(ep_iso_out);
    #else
    int i;
    /* fill queue */
    for(i = 0; i < NB_SLOTS; i++)
        enqueue_xfer();
    #endif

    return true;
}

bool usb_test_stat_req(struct usb_ctrlrequest* req, unsigned char* dest)
{
    (void) req;
    (void) dest;
    usb_drv_recv_blocking(EP_CONTROL, &usb_stat_req, sizeof(usb_stat_req));
    usb_drv_send_blocking(EP_CONTROL, NULL, 0); /* ack */

    logf("usbtest: stat");
    logf("usbtest: magic=0x%lx", usb_stat_req.dwMagic);
    logf("usbtest: req=%s", usb_stat_req.bReq == USB_TEST_STAT_CLEAR ? "CLEAR" :
            (usb_stat_req.bReq == USB_TEST_STAT_LOG ? "LOG" : "UNKNOWN"));

    if(usb_stat_req.bReq == USB_TEST_STAT_CLEAR)
    {
        stat_first_tick = current_tick;
        stat_nb_bytes = 0;
        stat_crc_errors = 0;
    }
    else if(usb_stat_req.bReq == USB_TEST_STAT_LOG)
    {
        uint32_t endtick = current_tick;
        _logf("usbtest: data transfered=%d B=%d KiB=%d MiB", stat_nb_bytes,
            stat_nb_bytes / 1024, stat_nb_bytes / 1024 / 1024);
        if((usb_data_req.bReq & USB_TEST_DATA_OUT_MASK) == USB_TEST_DATA_OUT_CRC32)
            _logf("usbtest: CRC errors=%d", stat_crc_errors);
        _logf("usbtest: time elapsed=%lu ms", ((endtick - stat_first_tick) * 1000) / HZ);
    }

    return true;
}

bool usb_test_cancel_req(struct usb_ctrlrequest* req, unsigned char* dest)
{
    (void) req;
    (void) dest;

    logf("usbtest: cancel");

    usb_iso_req.dwLength = 0;
    
    #ifdef USB_TEST_USE_REPEAT_MODE
    usb_drv_stop_repeat(ep_iso_out);
    #endif

    usb_drv_send_blocking(EP_CONTROL, NULL, 0); /* ack */

    return true;
}

/* called by usb_core_control_request() */
bool usb_test_control_request(struct usb_ctrlrequest* req, unsigned char* dest)
{
    (void) req;
    (void) dest;
    bool handled = false;

    (void)dest;
    switch (req->bRequest)
    {
        case USB_TEST_REQ_GET_STATUS:
            return usb_test_get_status(req, dest);
        case USB_TEST_REQ_DATA:
            return usb_test_data_req(req, dest);
        case USB_TEST_REQ_TEST_ISO:
            return usb_test_iso_test(req, dest);
        case USB_TEST_REQ_STAT:
            return usb_test_stat_req(req, dest);
        case USB_TEST_REQ_CANCEL:
            return usb_test_cancel_req(req, dest);
        default:
            logf("usbtest: unhandeld req %d", req->bRequest);
    }
    return handled;
}

void usb_test_init_connection(void)
{
    int i;
    
    cpu_boost(true);
    usb_test_cur_intf = 0;
    
    usb_drv_select_endpoint_mode(ep_bulk_out, USB_DRV_ENDPOINT_MODE_QUEUE);
    usb_drv_allocate_slots(ep_bulk_out, 1, ep_bulk_slots[0]);
    usb_drv_select_endpoint_mode(ep_bulk_in, USB_DRV_ENDPOINT_MODE_QUEUE);
    usb_drv_allocate_slots(ep_bulk_in, 1, ep_bulk_slots[1]);
    
    #ifdef USB_TEST_USE_REPEAT_MODE
    usb_drv_select_endpoint_mode(ep_iso_out, USB_DRV_ENDPOINT_MODE_REPEAT);
    usb_drv_allocate_slots(ep_iso_out, NB_SLOTS, ep_iso_slots[0]);
    usb_drv_select_endpoint_mode(ep_iso_in, USB_DRV_ENDPOINT_MODE_REPEAT);
    usb_drv_allocate_slots(ep_iso_in, NB_SLOTS, ep_iso_slots[1]);
    
    for(i = 0; i < NB_SLOTS; i++)
    {
        usb_drv_fill_repeat_slot(ep_iso_out, i, usb_buffer + i * SLOT_SIZE, SLOT_SIZE);
        usb_drv_fill_repeat_slot(ep_iso_in, i, usb_buffer + (i + NB_SLOTS) * SLOT_SIZE, SLOT_SIZE);
    }
    #else
    
    
    usb_drv_select_endpoint_mode(ep_iso_out, USB_DRV_ENDPOINT_MODE_QUEUE);
    usb_drv_allocate_slots(ep_iso_out, NB_SLOTS, ep_iso_slots[0]);
    usb_drv_select_endpoint_mode(ep_iso_in, USB_DRV_ENDPOINT_MODE_QUEUE);
    usb_drv_allocate_slots(ep_iso_in, NB_SLOTS, ep_iso_slots[1]);
    
    usb_buffer_bitmap = 0;
    #endif
}

void usb_test_init(void)
{
    unsigned char * audio_buffer;
    size_t bufsize;
    
    audio_buffer = audio_get_buffer(false,&bufsize);
#ifdef UNCACHED_ADDR
    usb_buffer = (void *)UNCACHED_ADDR((unsigned int)(audio_buffer+31) & 0xffffffe0);
#else
    usb_buffer = (void *)((unsigned int)(audio_buffer+31) & 0xffffffe0);
#endif
    cpucache_invalidate();
}

void usb_test_disconnect(void)
{
    cpu_boost(false);
}

/* called by usb_core_transfer_complete() */
void usb_test_transfer_complete(int ep, int dir, int status, int length, void *buffer)
{
    (void)dir;
    
    logf("usbtest: xfer complete");
    logf("usbtest: ep=%d dir=%d status=%d length=%d buf=0x%x (main=0x%x)", ep, dir, status, length, 
        (unsigned int)buffer, (unsigned int)usb_buffer);

    if(status == 0 && ep == ep_iso_out)
    {
        #ifndef USB_TEST_USE_REPEAT_MODE
        int slot = ((unsigned char *)buffer - usb_buffer) / SLOT_SIZE;
        usb_buffer_bitmap &= ~(1 << slot);
        logf("usbtest: free slot %d", slot);
        #endif
        
        stat_nb_bytes += length;

        if(usb_iso_req.dwLength < (uint32_t)length)
            usb_iso_req.dwLength = 0;
        else
        {
            usb_iso_req.dwLength -= length;
            #ifndef USB_TEST_USE_REPEAT_MODE
            enqueue_xfer();
            #endif
        }
        
        //process_out_data(length, buffer);
    }
}
