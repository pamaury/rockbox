/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 * $Id$
 *
 * Copyright (C) 2007 by Björn Stenberg
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
#ifndef _USB_DRV_H
#define _USB_DRV_H
#include "usb_ch9.h"
#include "kernel.h"

void usb_drv_startup(void);
void usb_drv_usb_detect_event(void); /* Target implemented */
void usb_drv_int_enable(bool enable); /* Target implemented */
void usb_drv_reset(void);
void usb_drv_init(void);
void usb_drv_exit(void);
void usb_drv_attach(void);
void usb_drv_int(void); /* Call from target INT handler */
void usb_drv_stall(int endpoint, bool stall,bool in);
bool usb_drv_stalled(int endpoint,bool in);
void usb_drv_set_address(int address);
void usb_drv_reset_endpoint(int endpoint, bool send);
bool usb_drv_powered(void);
int usb_drv_port_speed(void);
void usb_drv_cancel_all_transfers(void);
void usb_drv_set_test_mode(int mode);
bool usb_drv_connected(void);
int usb_drv_request_endpoint(int type, int dir);
void usb_drv_release_endpoint(int ep);

/* old api */
#ifndef HAVE_NEW_USB_API
int usb_drv_send(int endpoint, void* ptr, int length);
int usb_drv_recv(int endpoint, void* ptr, int length);
#endif
/* mid api */
/* these are internal routed to the usb_drv_queue_* functions for the new api
 * and to usb_drv_{send, recv} for the old api */
int usb_drv_send_blocking(int endpoint, void* ptr, int length);
int usb_drv_send_nonblocking(int endpoint, void* ptr, int length);
int usb_drv_recv_blocking(int endpoint, void* ptr, int length);
int usb_drv_recv_nonblocking(int endpoint, void* ptr, int length);

/* new api */
#ifdef HAVE_NEW_USB_API
/* Returns the maximum packet size of an endpoint. */
int usb_drv_max_endpoint_packet_size(int ep);
/* Allocate slots to be used for transfers an endpoint. The buffer must have USB_DRV_SLOT_ATTR
 * attribute. The driver will use exactly floor(buffer_size / USB_DRV_SLOT_SIZE) slots. So the buffer
 * should be of size N * USB_DRV_SLOT_SIZE to have exactly N slots.
 * The USB driver will use slots until there are explicitely released or the endpoint is released.
 * Returns 0 on success and <0 on error. */
int usb_drv_allocate_slots(int ep, int buffer_size, void *buffer);
/* Release the slots previously allocated.
 * Returns 0 on success and <0 on error. */
int usb_drv_release_slots(int ep);
/* Returns the number of allocated slots for the endpoint
 * Returns the actual value on success and <0 on error. */
int usb_drv_nb_endpoint_slots(int ep);

/* Void mode (default mode):
 * The endpoint is not activated: you cannot send data / received data is ignored */
#define USB_DRV_ENDPOINT_MODE_VOID      0
/* Queue mode:
 * The endpoint is activated and the slots are used to queue transfers up to the maximum number of slots.
 * Use usb_drv_queue_{send,send_nonblocking,recv} to add transfers to the queue. In this mode, there is no
 * limit on the size of a transfer: the driver is responsible for splitting bit transfers and perhaps use
 * several slots for one transfer. As a consequence, the number of slots does not represent the maximum 
 * number of queueable transfers. The only guarantee is that a slot must suffice for a transfer as big as
 * the maximum packet size. */
#define USB_DRV_ENDPOINT_MODE_QUEUE     1
/* Repeat mode:
 * The endpoint is activated and the slots are as a circular transfer queue. Contrary to the queueing mode,
 * when a transfer completes on a slot, the transfer is immediately re-setuped. As a consequence, in repeat
 * mode, one starts by filling each slot with parameters using usb_drv_fill_repeat_slot. Then, the transfers
 * are started using usb_drv_start_repeat and are stopped using usb_drv_stop_repeat. In this mode, no attention
 * is paid to data corruption: if the completion handlers are not fast enough, a buffer can be reused and the 
 * data overwritten. This mode shoud only be used for isochronous endpoints.
 * WARNING: for speed reasons, in this mode, the completion handler might me called from the interrupt handler,
 *          special care must be taken about the action done is this completion handler ! */
#define USB_DRV_ENDPOINT_MODE_REPEAT    2

/* Select endpoint mode.
 * Returns 0 on success and <0 on error. */
int usb_drv_select_endpoint_mode(int ep, int mode);

/* Queue a transfer on an endpoint.
 * Returns >=0 on success and <0 on error. */
/* To ack a control transfer, pass a NULL pointer and 0 as length */
int usb_drv_queue_send_blocking(int endpoint, void *ptr, int length);
int usb_drv_queue_send_nonblocking(int endpoint, void *ptr, int length);
int usb_drv_queue_recv_blocking(int endpoint, void *ptr, int length);
int usb_drv_queue_recv_nonblocking(int endpoint, void *ptr, int length);

/* Fill slot parameters
 * Returns 0 on success and <0 on error. */
int usb_drv_fill_repeat_slot(int ep, int slot, void *ptr, int length);
/* Start/stop repeat mode
 * Returns 0 on success and <0 on error */
int usb_drv_start_repeat(int ep);
int usb_drv_stop_repeat(int ep);
#endif /* HAVE_NEW_USB_API */

#endif /* _USB_DRV_H */
