/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 * $Id$
 *
 * Copyright (C) 2011 by Amaury Pouly
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
#include "system.h"
#include "config.h"
#include "string.h"
#include "usb_ch9.h"
#include "usb_core.h"
#include "kernel.h"
#include "panic.h"
#include "usb_drv.h"
#include "power.h"

int usb_drv_port_speed(void)
{
    return 0;
}

int usb_drv_request_endpoint(int type, int dir)
{
    (void) type;
    (void) dir;
    return -1;
}

void usb_drv_release_endpoint(int ep)
{
    (void) ep;
}

void usb_drv_set_address(int address)
{
    (void)address;
}

int usb_drv_send(int endpoint, void *ptr, int length)
{
    (void) endpoint;
    (void) ptr;
    (void) length;
    return -1;
}

int usb_drv_send_nonblocking(int endpoint, void *ptr, int length)
{
    (void) endpoint;
    (void) ptr;
    (void) length;
    return -1;
}

int usb_drv_recv(int endpoint, void* ptr, int length)
{
    (void) endpoint;
    (void) ptr;
    (void) length;
    return -1;
}

void usb_drv_cancel_all_transfers(void)
{
}

void usb_drv_set_test_mode(int mode)
{
    (void)mode;
}

bool usb_drv_stalled(int endpoint, bool in)
{
    (void) endpoint;
    (void) in;
    return false;
}

void usb_drv_stall(int endpoint, bool stall, bool in)
{
    (void) endpoint;
    (void) stall;
    (void) in;
}

void usb_drv_init(void)
{
}

void usb_drv_exit(void)
{
}

void usb_init_device(void)
{
    usb_drv_exit();
}

void usb_enable(bool on)
{
    if(on) usb_core_init();
    else usb_core_exit();
}

void usb_attach(void)
{
    usb_enable(true);
}

int usb_detect(void)
{
    if(charger_inserted())
        return USB_INSERTED;
    else
        return USB_EXTRACTED;
}
