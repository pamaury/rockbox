/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 * $Id$
 *
 * USB driver for the simulator
 *
 * Copyright (C) 2010 Amaury Pouly
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

/* parts of this driver are based on the usb-arc driver */
#define USB_VHCI_DRIVER_DEBUG

#include "errno.h"
#include "libusb_vhci/libusb_vhci.h"

#include "debug.h"
#include "kernel.h"
#include "string.h"
#include "usb_ch9.h"
#include "usb_core.h"
#include "usb_drv.h"

#ifdef USB_VHCI_DRIVER_DEBUG
#define USB_DEBUGF DEBUGF
#else
#define USB_DEBUGF(...) ({})
#endif

/* endpoint direction */
/* if an endpoint is a control endpoint, is must be bidirectional so only the [0]
 * is used regardeless of the endpoint direction */
struct usb_endpoint_t
{
    int type[2];/* type of endpoint, must be the same on both directions */
    bool allocated[2];/* is the endpoint allocated ? */
    bool has_urb[2];/* is there an pending URB ? */
    struct usb_vhci_urb urb[2];/* copy of the URB */
    void *buffer[2];/* is there a pending buffer ? NULL if no, address otherwise */
    int buffer_length[2];/* length of the buffer */
    struct usb_ctrlrequest setup_data;/* copy of setup data sent to core */
    struct mutex mutex[2];/* mutex for buffer/buffer_length/has_urb/urb */
    struct wakeup xfer_completion[2];/* wakeup for blocking transfers (see drv_send) */
    int xfer_status[2];/* status of the transfer (used for blocking transfers) */
    bool stalled[2];/* is endpoint stalled ? */    
};

static int vhci_hcd_fd = -1;/* handle of the vhci control device */
static int usb_status = USB_UNPOWERED;
static const int vhci_hcd_port = 1;

static const char vhci_hcd_thread_name[] = "vhci-hcd";
static unsigned int vhci_hcd_thread_entry = 0;

static struct usb_endpoint_t endpoints[USB_NUM_ENDPOINTS];

#define usb_vhci_ep_num(epadr) (epadr & 0x07)

#define XFER_DIR_STR(dir) ((dir) ? "IN" : "OUT")
#define XFER_TYPE_STR(type) \
    ((type) == USB_ENDPOINT_XFER_CONTROL ? "CTRL" : \
     ((type) == USB_ENDPOINT_XFER_ISOC ? "ISOC" : \
      ((type) == USB_ENDPOINT_XFER_BULK ? "BULK" : \
       ((type) == USB_ENDPOINT_XFER_INT ? "INTR" : "INVL"))))

#define usb_vhci_type_str(type) \
    ((type) == USB_VHCI_URB_TYPE_CONTROL ? "CTRL" : \
     ((type) == USB_VHCI_URB_TYPE_ISO ? "ISOC" : \
      ((type) == USB_VHCI_URB_TYPE_BULK ? "BULK" : \
       ((type) == USB_VHCI_URB_TYPE_INT ? "INTR" : "INVL"))))

#define usb_vhci_type_to_usb_xfr_type(type) \
    ((type) == USB_VHCI_URB_TYPE_CONTROL ? USB_ENDPOINT_XFER_CONTROL : \
     ((type) == USB_VHCI_URB_TYPE_ISO ? USB_ENDPOINT_XFER_ISOC : \
      ((type) == USB_VHCI_URB_TYPE_BULK ? USB_ENDPOINT_XFER_BULK : \
       ((type) == USB_VHCI_URB_TYPE_INT ? USB_ENDPOINT_XFER_INT : 4))))

void bus_reset(void);

/* general part */
void change_usb_status(int status)
{
    usb_status = status;
    usb_status_event(status);
}

void usb_attach(void)
{
    USB_DEBUGF("usb_attach\n");
    usb_drv_init();
}

int usb_detect(void)
{
    return usb_status;
}

void usb_enable(bool on)
{
    USB_DEBUGF("usb_enable(%d)\n", on);
    if(on)
    {
        usb_core_init();
        USB_DEBUGF("change usb status\n");
        change_usb_status(USB_INSERTED);
        USB_DEBUGF("done\n");
    }
    else
    {
        usb_core_exit();
    }
}

int stall_urb(struct usb_vhci_urb *urb)
{
    if(urb == NULL)
    {
        USB_DEBUGF("vhci-hcd: hey, I can't stall a NULL urb !\n");
        return -1;
    }
    urb->status = -EPIPE;
    
    USB_DEBUGF("vhci-hcd: stall urb on EP%d %s\n", usb_vhci_ep_num(urb->epadr),
        XFER_DIR_STR(usb_vhci_is_in(urb->epadr)));
    int ret= usb_vhci_giveback(vhci_hcd_fd, urb);
    if(ret < 0)
        USB_DEBUGF("vhci-hcd: giveback failed (ret=%d)!\n", ret);
    return ret;
}

int complete_urb(struct usb_vhci_urb *urb)
{
    if(urb == NULL)
    {
        USB_DEBUGF("vhci-hcd: hey, I can't complete a NULL urb !\n");
        return -1;
    }
    urb->status = 0;
    
    USB_DEBUGF("vhci-hcd: complete urb on EP%d %s\n", usb_vhci_ep_num(urb->epadr),
        XFER_DIR_STR(usb_vhci_is_in(urb->epadr)));
    int ret= usb_vhci_giveback(vhci_hcd_fd, urb);
    if(ret < 0)
        USB_DEBUGF("vhci-hcd: giveback failed (ret=%d)!\n", ret);
    return ret;
}

void clear_endpoint(int ep_num, bool ep_in)
{
    endpoints[ep_num].buffer[ep_in] = NULL;
    endpoints[ep_num].buffer_length[ep_in] = 0;
    endpoints[ep_num].has_urb[ep_in] = false;
    memset(&endpoints[ep_num].urb[ep_in], 0, sizeof(struct usb_vhci_urb));
}

int cancel_endpoint_urb(int ep_num, bool ep_in)
{
    /* FIXME seynchro issue here */
    if(!endpoints[ep_num].has_urb[ep_in])
    {
        USB_DEBUGF("vhci-hcd: hey I can't meet up a NULL urb on EP%d %s !\n", ep_num, XFER_DIR_STR(ep_in));
        return -1;
    }
    
    stall_urb(&endpoints[ep_num].urb[ep_in]);
    clear_endpoint(ep_num, ep_in);
    /* notify the core that the transfer failed */
    usb_core_transfer_complete(ep_num, ep_in ? USB_DIR_IN : USB_DIR_OUT, 1, 0);
    
    endpoints[ep_num].xfer_status[ep_in] = -1;
    wakeup_signal(&endpoints[ep_num].xfer_completion[ep_in]);
    
    return 0;
}

int usb_meet_up(int ep_num, bool ep_in)
{
    /* WARNING: for control endpoints, ep_in=false because of ^^^^ */
    /* But, the real direction is still available in the urb */
    if(!endpoints[ep_num].has_urb[ep_in])
    {
        USB_DEBUGF("vhci-hcd: hey I can't meet up a NULL urb on EP%d %s !\n", ep_num, XFER_DIR_STR(ep_in));
        return -1;
    }
    
    if(endpoints[ep_num].buffer[ep_in] == NULL)
    {
        USB_DEBUGF("vhci-hcd: hey I can't meet up a NULL buffer EP%d %s\n", ep_num, XFER_DIR_STR(ep_in));
        return -1;
    }
    
    struct usb_vhci_urb *urb = &endpoints[ep_num].urb[ep_in];
    void *buffer = endpoints[ep_num].buffer[ep_in];
    int buffer_length = endpoints[ep_num].buffer_length[ep_in];
    
    if(ep_num != usb_vhci_ep_num(urb->epadr))
    {
        USB_DEBUGF("vhci-hcd: WARNING: there is a problem, I complete on EP%d %s but the urb says it's on EP%d %s\n",
            ep_num, XFER_DIR_STR(ep_in), usb_vhci_ep_num(urb->epadr),
            XFER_DIR_STR(usb_vhci_is_in(urb->epadr)));
        return -1;
    }
    
    USB_DEBUGF("vhci-hcd: On EP%d %s, urb: buffer_length=%d buffer_actual=%d\n", ep_num, XFER_DIR_STR(ep_in),
        urb->buffer_length, urb->buffer_actual);
    USB_DEBUGF("vhci-hcd: buffer: ptr=%p buffer_length=%d\n", buffer, buffer_length);
    
    if(usb_vhci_is_in(urb->epadr))
    {
        urb->buffer = buffer;
        /* NOTE: for send(aka IN) transfers, buffer_actual is used ! */
        urb->buffer_actual = buffer_length;
        
        int ret = complete_urb(urb);
        if(ret < 0)
            USB_DEBUGF("vhci-hcd: meet up failed !\n");
        
        USB_DEBUGF("vhci-hcd: transfer complete on EP%d IN, %d bytes transfered\n", ep_num, urb->buffer_actual);
        usb_core_transfer_complete(ep_num, USB_DIR_IN, ret < 0 ? 1 : 0, urb->buffer_actual);
        
        clear_endpoint(ep_num, ep_in);
        
        endpoints[ep_num].xfer_status[ep_in] = ret;
        wakeup_signal(&endpoints[ep_num].xfer_completion[ep_in]);
        
        return ret;
    }
    else
    {
        urb->buffer = buffer;
        /* NOTE: for recv(aka OUT) transfers, buffer_length is used ! */
        urb->buffer_length = buffer_length;
        
        int ret = usb_vhci_fetch_data(vhci_hcd_fd, urb);
        if(ret < 0)
            USB_DEBUGF("vhci-hcd: fetch data failed (ret=%d)!\n", ret);
        
        USB_DEBUGF("vhci-hcd: transfer complete on EP%d OUT, %d bytes transfered\n", ep_num, urb->buffer_length);
        usb_core_transfer_complete(ep_num, USB_DIR_OUT, ret < 0 ? 1 : 0, urb->buffer_length);
        
        if(ret < 0)
            stall_urb(urb);
        else
            complete_urb(urb);
        
        clear_endpoint(ep_num, ep_in);
        
        endpoints[ep_num].xfer_status[ep_in] = ret;
        wakeup_signal(&endpoints[ep_num].xfer_completion[ep_in]);
        
        return ret;
    }
}

void dump_pending_urbs(void)
{
    #ifdef USB_VHCI_DRIVER_DEBUG
    int ep_num;
    int ep_in;
    
    USB_DEBUGF("[-----------pending URBs------------]\n");
    for(ep_num=0;ep_num<USB_NUM_ENDPOINTS;ep_num++)
    {
        for(ep_in=0;ep_in<=1;ep_in++)
        {
            if(endpoints[ep_num].has_urb[ep_in])
            {
                struct usb_vhci_urb *urb = &endpoints[ep_num].urb[ep_in];
                USB_DEBUGF("  EP%d %s/%s: type %s on EP%d %s\n",
                    ep_num, XFER_DIR_STR(ep_in),
                    XFER_TYPE_STR(endpoints[ep_num].type[ep_in]),
                    usb_vhci_type_str(urb->type),
                    usb_vhci_ep_num(urb->epadr), XFER_DIR_STR(usb_vhci_is_in(urb->epadr)));
            }
        }
    }
    USB_DEBUGF("[-----------------------------------]\n");
    #endif
}

void process_urb(struct usb_vhci_urb *urb)
{
    int ep_num = usb_vhci_ep_num(urb->epadr);
    bool ep_in = usb_vhci_is_in(urb->epadr);
    
    dump_pending_urbs();
    
    switch(urb->type)
    {
        case USB_VHCI_URB_TYPE_ISO:
            USB_DEBUGF("Rockbox doesn't handle isochronous transfers !\n");
            stall_urb(urb);
            break;
        case USB_VHCI_URB_TYPE_INT:
        case USB_VHCI_URB_TYPE_BULK:
            {
                USB_DEBUGF("vhci-hcd: %s transfer on EP%d %s\n", usb_vhci_type_str(urb->type), ep_num, XFER_DIR_STR(ep_in));
                
                /* check that:
                 * -endpoint is allocated
                 * -urb type match endpoint type
                 * -endpoint is not stalled
                 * -endpoint has no pending urb
                */
                if(!endpoints[ep_num].allocated[ep_in] ||
                        endpoints[ep_num].type[ep_in] != usb_vhci_type_to_usb_xfr_type(urb->type) ||
                        endpoints[ep_num].stalled[ep_in] ||
                        endpoints[ep_num].has_urb[ep_in])
                {
                    /* <debug> */
                    if(!endpoints[ep_num].allocated[ep_in])
                        USB_DEBUGF("vhci-hcd: endpoint EP%d %s is not allocated\n", ep_num, XFER_DIR_STR(ep_in));
                    else if(endpoints[ep_num].type[ep_in] != usb_vhci_type_to_usb_xfr_type(urb->type))
                        USB_DEBUGF("vhci-hcd: endpoint EP%d %s is not a(n) %s endpoint\n", ep_num, XFER_DIR_STR(ep_in),
                            usb_vhci_type_str(urb->type));
                    else if(endpoints[ep_num].stalled[ep_in])
                        USB_DEBUGF("vhci-hcd: endpoint EP%d %s is stalled\n", ep_num, XFER_DIR_STR(ep_in));
                    else if(endpoints[ep_num].has_urb[ep_in])
                        USB_DEBUGF("vhci-hcd: endpoint EP%d %s is already busy with an urb\n", ep_num, XFER_DIR_STR(ep_in));
                    /* </debug> */
                    stall_urb(urb);
                    return;
                }
                 
                mutex_lock(&endpoints[ep_num].mutex[ep_in]);
                 /* endpoint is busy starting from now */
                endpoints[ep_num].has_urb[ep_in] = true;
                /* copy urb */
                memcpy(&endpoints[ep_num].urb[ep_in], urb, sizeof(struct usb_vhci_urb));
                USB_DEBUGF("vhci-hcd: length=%d\n", urb->buffer_length);
                
                /* check if there is a pending buffer */
                /* FIXME there a synchronisation problem here */
                
                if(endpoints[ep_num].buffer[ep_in] != NULL)
                {
                    USB_DEBUGF("vhci-hcd: interrupt transfer with pending buffer, immediate handling\n");
                    usb_meet_up(ep_num, ep_in);
                }
                else
                {
                    USB_DEBUGF("vhci-hcd: interrupt transfer with no pending buffer, delayed handling\n");
                }
                mutex_unlock(&endpoints[ep_num].mutex[ep_in]);
            }
            break;
        case USB_VHCI_URB_TYPE_CONTROL:
            {
                struct usb_ctrlrequest *req = &endpoints[ep_num].setup_data;
                /* control transfers use [0] instead of [ep_on] for urb and buffer */
                
                USB_DEBUGF("vhci-hcd: control transfer\n");
                /* check that:
                 * -endpoint is allocated
                 * -urb type match endpoint type
                 * -endpoint is not stalled
                 * -endpoint has no pending urb
                */
                if(!endpoints[ep_num].allocated[0] ||
                        endpoints[ep_num].type[0] != usb_vhci_type_to_usb_xfr_type(urb->type) ||
                        endpoints[ep_num].stalled[ep_in] ||
                        endpoints[ep_num].has_urb[0])
                {
                    /* <debug> */
                    if(!endpoints[ep_num].allocated[0])
                        USB_DEBUGF("vhci-hcd: endpoint EP%d is not allocated\n", ep_num);
                    else if(endpoints[ep_num].type[0] != usb_vhci_type_to_usb_xfr_type(urb->type))
                        USB_DEBUGF("vhci-hcd: endpoint EP%d is not a control endpoint\n", ep_num);
                    else if(endpoints[ep_num].stalled[ep_in])
                        USB_DEBUGF("vhci-hcd: endpoint EP%d is stalled\n", ep_num);
                    else if(endpoints[ep_num].has_urb[0])
                        USB_DEBUGF("vhci-hcd: endpoint EP%d is already busy with an urb\n", ep_num);
                    /* </debug> */
                    stall_urb(urb);
                    return;
                }
                
                /* endpoint is busy starting from now */
                endpoints[ep_num].has_urb[0] = true;
                memcpy(&endpoints[ep_num].urb[0], urb, sizeof(struct usb_vhci_urb));
                
                USB_DEBUGF("vhci-hcd: bmRequestType=0x%02x bRequest=0x%02x wValue=0x%04x\n", urb->bmRequestType, urb->bRequest,
                    urb->wValue);
                USB_DEBUGF("vhci-hcd: wIndex=0x%04x wLength=0x%04x epadr=0x%02x urb_length=0x%x\n", urb->wIndex,
                    urb->wLength, urb->epadr, urb->buffer_length);
                
                req->bRequestType = urb->bmRequestType;
                req->bRequest = urb->bRequest;
                req->wValue = urb->wValue;
                req->wIndex = urb->wIndex;
                req->wLength = urb->wLength;
                
                if(ep_num == 0)
                    usb_core_control_request(req);
                
                /* FIXME should call usb_core_transfer_complete at some point ? */
                /* FIXME should handle no data control transfers only ? */
                USB_DEBUGF("vhci-hcd: Not sending a transfer complete message for now !\n");
            }
            break;
        default:
            break;
    }
    
    dump_pending_urbs();
}   

#define USB_VHCI_PORT_STAT_TRIGGER_DISABLE   0x01
#define USB_VHCI_PORT_STAT_TRIGGER_SUSPEND   0x02
#define USB_VHCI_PORT_STAT_TRIGGER_RESUMING  0x04
#define USB_VHCI_PORT_STAT_TRIGGER_RESET     0x08
#define USB_VHCI_PORT_STAT_TRIGGER_POWER_ON  0x10
#define USB_VHCI_PORT_STAT_TRIGGER_POWER_OFF 0x20

uint16_t compute_trigger(uint16_t prev, uint16_t cur)
{
    uint16_t trigger = 0;
    #define CHECK_GAIN(val, trig_val) if(!(prev & val) && (cur & val)) trigger |= trig_val;
    #define CHECK_LOSS(val, trig_val) if((prev & val) && !(cur & val)) trigger |= trig_val;
    
    CHECK_LOSS(USB_VHCI_PORT_STAT_CONNECTION, USB_VHCI_PORT_STAT_TRIGGER_DISABLE)
    CHECK_GAIN(USB_VHCI_PORT_STAT_SUSPEND, USB_VHCI_PORT_STAT_TRIGGER_SUSPEND)
    CHECK_LOSS(USB_VHCI_PORT_STAT_SUSPEND, USB_VHCI_PORT_STAT_TRIGGER_RESUMING)
    CHECK_GAIN(USB_VHCI_PORT_STAT_RESET, USB_VHCI_PORT_STAT_TRIGGER_RESET)
    CHECK_GAIN(USB_VHCI_PORT_STAT_POWER, USB_VHCI_PORT_STAT_TRIGGER_POWER_ON)
    CHECK_LOSS(USB_VHCI_PORT_STAT_POWER, USB_VHCI_PORT_STAT_TRIGGER_POWER_OFF)
    
    return trigger;
}

void vhci_hcd_thread(void)
{
    struct usb_vhci_work work;
    uint16_t last_status = 0;
    
    while(1)
    {
        if(usb_vhci_fetch_work(vhci_hcd_fd, &work) < 0)
        {
            /* in case of repeat error, avoid using 100% cpu */
            sleep(HZ/10);
            continue;
        }
        
        if(work.type == USB_VHCI_WORK_TYPE_PORT_STAT)
        {
            uint16_t trigger = compute_trigger(last_status, work.work.port_stat.status);
            last_status = work.work.port_stat.status;
            
            USB_DEBUGF("vhci-hcd: port: status:");
            if(work.work.port_stat.status & USB_VHCI_PORT_STAT_CONNECTION) USB_DEBUGF(" connected");
            if(work.work.port_stat.status & USB_VHCI_PORT_STAT_ENABLE) USB_DEBUGF(" enabled");
            if(work.work.port_stat.status & USB_VHCI_PORT_STAT_SUSPEND) USB_DEBUGF(" suspended");
            if(work.work.port_stat.status & USB_VHCI_PORT_STAT_OVERCURRENT) USB_DEBUGF(" overcurrent");
            if(work.work.port_stat.status & USB_VHCI_PORT_STAT_RESET) USB_DEBUGF(" reset");
            if(work.work.port_stat.status & USB_VHCI_PORT_STAT_POWER) USB_DEBUGF(" powered");
            if(work.work.port_stat.status & USB_VHCI_PORT_STAT_LOW_SPEED) USB_DEBUGF(" low-speed");
            if(work.work.port_stat.status & USB_VHCI_PORT_STAT_HIGH_SPEED) USB_DEBUGF(" high-speed");
            USB_DEBUGF("\n");
            USB_DEBUGF("vhci-hcd: port: change:");
            if(work.work.port_stat.status & USB_VHCI_PORT_STAT_C_CONNECTION) USB_DEBUGF(" connected");
            if(work.work.port_stat.status & USB_VHCI_PORT_STAT_C_ENABLE) USB_DEBUGF(" enabled");
            if(work.work.port_stat.status & USB_VHCI_PORT_STAT_C_SUSPEND) USB_DEBUGF(" suspended");
            if(work.work.port_stat.status & USB_VHCI_PORT_STAT_C_OVERCURRENT) USB_DEBUGF(" overcurrent");
            if(work.work.port_stat.status & USB_VHCI_PORT_STAT_C_RESET) USB_DEBUGF(" reset");
            USB_DEBUGF("\n");
            USB_DEBUGF("vhci-hcd: port: triggers:");
            if(trigger & USB_VHCI_PORT_STAT_TRIGGER_DISABLE) USB_DEBUGF(" disable");
            if(trigger & USB_VHCI_PORT_STAT_TRIGGER_SUSPEND) USB_DEBUGF(" suspend");
            if(trigger & USB_VHCI_PORT_STAT_TRIGGER_RESUMING) USB_DEBUGF(" resume");
            if(trigger & USB_VHCI_PORT_STAT_TRIGGER_RESET) USB_DEBUGF(" reset");
            if(trigger & USB_VHCI_PORT_STAT_TRIGGER_POWER_ON) USB_DEBUGF(" power-on");
            if(trigger & USB_VHCI_PORT_STAT_TRIGGER_POWER_OFF) USB_DEBUGF(" power-off");
            USB_DEBUGF("\n");
            
            if(trigger & USB_VHCI_PORT_STAT_TRIGGER_POWER_ON)
                change_usb_status(USB_POWERED);
            if(trigger & USB_VHCI_PORT_STAT_TRIGGER_POWER_OFF)
                change_usb_status(USB_UNPOWERED);
            if(trigger & USB_VHCI_PORT_STAT_TRIGGER_RESET)
                bus_reset();
        }
        else if(work.type == USB_VHCI_WORK_TYPE_PROCESS_URB)
        {
            /*USB_DEBUGF("vhci-hcd: process urb\n");*/
            process_urb(&work.work.urb);
        }
        else if(work.type == USB_VHCI_WORK_TYPE_CANCEL_URB)
        {
            USB_DEBUGF("vhci-hcd: cancel urb\n");
            /* Search for urb
             * Note that during the search, the usb thread could complete the urb but that's not a problem */
            int ep_num, ep_in;
            for(ep_num = 0; ep_num < USB_NUM_ENDPOINTS; ep_num++)
                for(ep_in = 0; ep_in <= 1; ep_in++)
                {
                    /* FIXME synchro issue here */
                    if(endpoints[ep_num].has_urb[ep_in] &&
                            endpoints[ep_num].urb[ep_in].handle == work.work.handle)
                    {
                        USB_DEBUGF("vhci-hcd: cancel urb on EP%d %s\n", ep_num, XFER_DIR_STR(ep_in));
                        stall_urb(&endpoints[ep_num].urb[ep_in]);
                        clear_endpoint(ep_num, ep_in);
                    }
                }
        }
        else
        {
            USB_DEBUGF("vhci-hcd: unknown work type\n");
        }
    }
}

void usb_simulate_power()
{
    int32_t id,usb_busnum;
    char *bus_id;
    /* one port only */
    vhci_hcd_fd = usb_vhci_open(1, &id, &usb_busnum, &bus_id);
    
    if(vhci_hcd_fd >= 0)
    {
        USB_DEBUGF("usb-vhci: host controller at %d:%d (%s)\n", usb_busnum, id, bus_id);
        vhci_hcd_thread_entry = create_thread(vhci_hcd_thread, NULL, 0, 0, vhci_hcd_thread_name);
    }
    else
        USB_DEBUGF("usb-vhci: couldn't add host controller\n");
}

void usb_simulate_unpower()
{
    change_usb_status(USB_UNPOWERED);
}

void usb_ask(unsigned int what)
{
    USB_DEBUGF("usb_ask(%d)\n", what);
    
    switch(what)
    {
        case USB_ASK_SIMULATE_INSERTION:
            usb_simulate_power();
            break;
        case USB_ASK_SIMULATE_EXTRACTION:
            usb_simulate_unpower();
            break;
        default:
            USB_DEBUGF("  unknown usb_ask request\n");
    }
}

/* driver part */
void usb_init_device(void)
{
    int ep_num, ep_dir;
    USB_DEBUGF("usb_init_device\n");
    /* preallocate EP0 (to avoid special cases)*/
    endpoints[0].allocated[0] = endpoints[0].allocated[1] = true,
    endpoints[0].type[0] = endpoints[0].type[1] = USB_ENDPOINT_XFER_CONTROL;
    
    /* init mutexes and wakeups */
    for(ep_num = 0; ep_num < USB_NUM_ENDPOINTS; ep_num++)
        for(ep_dir = 0; ep_dir <= 1; ep_dir++)
        {
            mutex_init(&endpoints[ep_num].mutex[ep_dir]);
            wakeup_init(&endpoints[ep_num].xfer_completion[ep_dir]);
        }
}

void usb_drv_init(void)
{
    USB_DEBUGF("usb_drv_init\n");
    
    if(usb_vhci_port_connect(vhci_hcd_fd, vhci_hcd_port, USB_VHCI_DATA_RATE_HIGH) < 0)
    {
        USB_DEBUGF("usb: vhci-hcd: couldn't connect port !");
        return;
    }
}

void usb_drv_exit(void)
{
    USB_DEBUGF("usb_drv_exit\n");
    
    usb_vhci_port_disconnect(vhci_hcd_fd, vhci_hcd_port);
    usb_vhci_close(vhci_hcd_fd);
}

void bus_reset(void)
{
    int ep_num;
    USB_DEBUGF("usb: bus reset\n");
    usb_core_bus_reset();
    usb_drv_cancel_all_transfers();
    
    for(ep_num = 0; ep_num < USB_NUM_ENDPOINTS; ep_num++)
        endpoints[ep_num].stalled[0] = endpoints[ep_num].stalled[1] = false;
    
    usb_vhci_port_reset_done(vhci_hcd_fd, vhci_hcd_port, 1); /* enable */
}

void usb_drv_stall(int endpoint, bool stall,bool in)
{
    USB_DEBUGF("usb: stall endpoint=%d stall=%d in=%d\n", endpoint, stall, in);
    
    endpoints[endpoint].stalled[in] = stall;
    
    if(endpoints[endpoint].type[in] == USB_ENDPOINT_XFER_CONTROL)
        in = false;
    
    if(stall && endpoints[endpoint].has_urb[in])
    {
        cancel_endpoint_urb(endpoint,in);
    }
}

bool usb_drv_stalled(int endpoint,bool in)
{
    USB_DEBUGF("usb: ask_stalled endpoint=%d in=%d\n", endpoint, in);
    return endpoints[endpoint].stalled[in];
}

int usb_drv_ack_send_recv(int num, bool in)
{
    if(num != 0 && !endpoints[num].allocated[in])
    {
        USB_DEBUGF("usb: oops, endpoint EP%d %s is not allocated !\n", num, XFER_DIR_STR(in));
        return -1;
    }
    if(num != 0 && endpoints[num].type[in] != USB_ENDPOINT_XFER_CONTROL)
    {
        USB_DEBUGF("usb: oops, endpoint EP%d is not a control endpoint !\n", num);
        return -1;
    }
    
    /* ack can be used for two things:
     * -ack the reception of a data packet and say that we're ready to read/write data
     * -ack the reception of a nodata packet and say w've done
     *
     * The first kind of ack is useless here because there's no bus so we ignore it */
    
    /* use [0], see previous comments */
    if(!endpoints[num].has_urb[0])
    {
        USB_DEBUGF("usb: no pending urb to ack on EP%d\n", num);
        return -1;
    }
    else
    {
        if(endpoints[num].urb[0].buffer_length != 0)
        {
            USB_DEBUGF("usb: ignore ack for data control message on EP%d\n", num);
            return 0;
        }
        else
        {
            USB_DEBUGF("usb: ack urb on EP%d\n", num);
            complete_urb(&endpoints[num].urb[0]);
            clear_endpoint(num, 0);
            return 0;
        }
    }
}

int usb_drv_send_recv(int epadr, bool send, void *ptr, int length, bool wait)
{
    /* check epadr */
    int ep_num = EP_NUM(epadr);
    bool ep_in = (EP_DIR(epadr) == DIR_IN);
    
    /* EP0 is special */
    if(ep_num != 0 && ep_in != send)
    {
        USB_DEBUGF("usb: oops, I can't %s on EP%d %s !\n", send ? "send" : "recv", ep_num, XFER_DIR_STR(ep_in));
        return -1;
    }
    if(ep_num !=0 && !endpoints[ep_num].allocated[ep_in])
    {
        USB_DEBUGF("usb: oops, EP%d %s is not allocated !\n", ep_num, XFER_DIR_STR(ep_in));
        return -1;
    }
    /* if control endpoint, set ep_in to false */
    if(endpoints[ep_num].type[ep_in] == USB_ENDPOINT_XFER_CONTROL)
        ep_in = false;

    if(endpoints[ep_num].buffer[ep_in] != NULL)
    {
        USB_DEBUGF("usb: oops, send/recv on EP%d %s with already set buffer, overwrite !\n",
            ep_num, XFER_DIR_STR(ep_in));
    }
    
    mutex_lock(&endpoints[ep_num].mutex[ep_in]);
    endpoints[ep_num].buffer[ep_in] = ptr;
    endpoints[ep_num].buffer_length[ep_in] = length;
    
    if(!endpoints[ep_num].has_urb[ep_in])
    {
        mutex_unlock(&endpoints[ep_num].mutex[ep_in]);
        USB_DEBUGF("usb: send/recv on EP%d %s with no urb: add pending buffer\n", ep_num, XFER_DIR_STR(ep_in));
        
        if(wait)
        {
            /* wait for transfer completion */
            USB_DEBUGF("usb: wait for transfer completion...\n");
            
            wakeup_wait(&endpoints[ep_num].xfer_completion[ep_in], TIMEOUT_BLOCK);
            
            USB_DEBUGF("usb: Back again on EP%d %s: wait finished, result is %d !\n", 
                ep_num, XFER_DIR_STR(ep_in), endpoints[ep_num].xfer_status[ep_in]);
            return endpoints[ep_num].xfer_status[ep_in];
        }
        else
            return 0;
    }
    else
    {
        mutex_unlock(&endpoints[ep_num].mutex[ep_in]);
        USB_DEBUGF("usb: send/recv on EP%d %s with urb: meet up !\n", ep_num, XFER_DIR_STR(ep_in));
        return usb_meet_up(ep_num, ep_in);
    }
    
}

int usb_drv_send(int endpoint, void* ptr, int length)
{
    /* check for ack */
    if(ptr == NULL && length == 0)
        return usb_drv_ack_send_recv(endpoint, false);
    else
        return usb_drv_send_recv(endpoint, true, ptr, length, true);
}

int usb_drv_send_nonblocking(int endpoint, void* ptr, int length)
{
    /* check for ack */
    if(ptr == NULL && length == 0)
        return usb_drv_ack_send_recv(endpoint, false);
    else
        return usb_drv_send_recv(endpoint, true, ptr, length, false);
}

int usb_drv_recv(int endpoint, void* ptr, int length)
{
    /* check for ack */
    if(ptr == NULL && length == 0)
        return usb_drv_ack_send_recv(endpoint, true);
    else
        return usb_drv_send_recv(endpoint, false, ptr, length, false);
}

void usb_drv_set_address(int address)
{
    USB_DEBUGF("usb: set_address addr=%d\n", address);
    (void)address;
}

int usb_drv_port_speed(void)
{
    USB_DEBUGF("usb: ask_port_speed\n");
    return 1; /* FIXME: 1 for high speed ? */
}

void usb_drv_cancel_all_transfers(void)
{
    USB_DEBUGF("usb: cancel_all_transfers\n");
    dump_pending_urbs();
    /* FIXME not sure it does the right thing */
    int ep_num, ep_in;
    for(ep_num = 0; ep_num < USB_NUM_ENDPOINTS; ep_num++)
        for(ep_in = 0; ep_in <= 1; ep_in++)
        {
            /* FIXME synchro issue here */
            if(!endpoints[ep_num].has_urb[ep_in])
                continue;
            
            cancel_endpoint_urb(ep_num, ep_in);
        }
}

void usb_drv_set_test_mode(int mode)
{
    USB_DEBUGF("usb: set_test_mode mode=%d, stub\n", mode);
    (void)mode;
}

bool usb_drv_connected(void)
{
    USB_DEBUGF("usb: ask_connected\n");
    return usb_status == USB_INSERTED;
}

int usb_drv_request_endpoint(int type, int dir)
{
    int ep_num, ep_dir;
    short ep_type;

    /* Safety */
    ep_dir = EP_DIR(dir);
    ep_type = type & USB_ENDPOINT_XFERTYPE_MASK;

    USB_DEBUGF("usb: request %s %s\n", XFER_DIR_STR(ep_dir), XFER_TYPE_STR(ep_type));

    /* Find an available ep/dir pair */
    for(ep_num=1;ep_num<USB_NUM_ENDPOINTS;ep_num++)
    {
        struct usb_endpoint_t *endpoint=&endpoints[ep_num];
        int other_dir=(ep_dir ? 0:1);

        if(endpoint->allocated[ep_dir])
            continue;

        if(endpoint->allocated[other_dir] &&
                endpoint->type[other_dir] != ep_type)
            /* different type */
            continue;

        endpoint->allocated[ep_dir] = true;
        endpoint->type[ep_dir] = ep_type;

        /* clear everything */
        clear_endpoint(ep_num, ep_dir);
        USB_DEBUGF("usb: allocate EP%d %s\n", ep_num, XFER_DIR_STR(ep_dir));
        return (ep_num | (dir & USB_ENDPOINT_DIR_MASK));
    }
    
    USB_DEBUGF("usb: fail to allocate\n");
    return -1;
}

void usb_drv_release_endpoint(int ep)
{
    int ep_num = EP_NUM(ep);
    int ep_dir = EP_DIR(ep);

    USB_DEBUGF("usb: release EP%d %s\n", ep_num, XFER_DIR_STR(ep_dir));
    /* don't release EP0 for technical reasons */
    if(ep_num != 0)
        endpoints[ep_num].allocated[ep_dir] = false;
}


