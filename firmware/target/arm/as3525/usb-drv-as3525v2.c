/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 * $Id$
 *
 * Copyright © 2010 Amaury Pouly
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

#include "usb.h"
#include "usb_drv.h"
#include "as3525v2.h"
#include "clock-target.h"
#include "ascodec.h"
#include "as3514.h"
#include "stdbool.h"
#include "string.h"
#include "stdio.h"
#include "panic.h"
#include "mmu-arm.h"
#include "system.h"
#define LOGF_ENABLE
#include "logf.h"
#include "usb-drv-as3525v2.h"
#include "usb_core.h"

static const uint8_t in_ep_list[NUM_IN_EP + 1] = {0, IN_EP_LIST};
static const uint8_t out_ep_list[NUM_OUT_EP + 1] = {0, OUT_EP_LIST};

/* iterate through each in/out ep except EP0
 * 'i' is the counter, 'ep' is the actual value */
#define FOR_EACH_EP(list, start, i, ep) \
    for(ep = list[i = start]; \
        i < (sizeof(list)/sizeof(*list)); \
        i++, ep = list[i])

#define FOR_EACH_IN_EP_EX(include_ep0, i, ep) \
    FOR_EACH_EP(in_ep_list, (include_ep0) ? 0 : 1, i, ep)

#define FOR_EACH_OUT_EP_EX(include_ep0, i, ep) \
    FOR_EACH_EP(out_ep_list, (include_ep0) ? 0 : 1, i, ep)

#define FOR_EACH_IN_EP(i, ep)           FOR_EACH_IN_EP_EX (false, i, ep)
#define FOR_EACH_IN_EP_AND_EP0(i, ep)   FOR_EACH_IN_EP_EX (true,  i, ep)
#define FOR_EACH_OUT_EP(i, ep)          FOR_EACH_OUT_EP_EX(false, i, ep)
#define FOR_EACH_OUT_EP_AND_EP0(i, ep)  FOR_EACH_OUT_EP_EX(true,  i, ep)

/* store per transfer information */
struct usb_transfer_description
{
    unsigned int len; /* length of the data buffer */
    void *buffer; /* we need to save the buffer */
};

/* store per endpoint, per direction, information */
struct usb_endpoint
{
    int ep; /* endpoint number, easier to work with */
    
    struct wakeup complete; /* wait object */
    short mode; /* endpoint mode */
    short max_pkt_size; /* maximum packet size */
    int nb_tds; /* number of transfer descriptors */
    struct usb_transfer_description *tds; /* transfer descriptors */
    int head_td; /* index of the first td of the queue (also used for repeat mode) */
    int tail_td; /* index of the last td of the queue */
    int wait; /* only valid when there is one transfer in the queue */
    int status; /* only valid when there is one transfer in the queue and waiting */
    bool active; /* used by a driver */
};

/* state of EP0 (to correctly schedule setup packet enqueing) */
enum ep0state
{
    /* Setup packet is enqueud, waiting for actual data */
    EP0_WAIT_SETUP = 0,
    /* Waiting for ack (either IN or OUT) */
    EP0_WAIT_ACK = 1,
    /* Ack complete, waiting for data (either IN or OUT)
     * This state is necessary because if both ack and data complete in the
     * same interrupt, we might process data completion before ack completion
     * so we need this bizarre state */
    EP0_WAIT_DATA = 2,
    /* Setup packet complete, waiting for ack and data */
    EP0_WAIT_DATA_ACK = 3,
};

/* endpoints[ep_num][DIR_IN/DIR_OUT] */
static struct usb_endpoint endpoints[USB_NUM_ENDPOINTS][2];
/* setup packet for EP0 */
static struct usb_ctrlrequest _ep0_setup_pkt __attribute__((aligned(32)));
static struct usb_ctrlrequest *ep0_setup_pkt = AS3525_UNCACHED_ADDR(&_ep0_setup_pkt);

static int g_usbreset_count = 0;

/* state of EP0 */
static enum ep0state ep0_state;

void usb_attach(void)
{
    logf("usb-drv: attach");
    usb_enable(true);
}

static inline void usb_delay(void)
{
    int i = 0;
    while(i < 0x300)
    {
        asm volatile("nop");
        i++;
    }
}

static void as3525v2_connect(void)
{
    logf("usb-drv: init as3525v2");
    /* 1) enable usb core clock */
    bitset32(&CGU_PERI, CGU_USB_CLOCK_ENABLE);
    usb_delay();
    /* 2) enable usb phy clock */
    CCU_USB = (CCU_USB & ~(3<<24)) | (1 << 24); /* ?? */
    /* PHY clock */
    CGU_USB = 1<<5  /* enable */
        | 0 << 2
        | 0; /* source = ? (24MHz crystal?) */
    usb_delay();
    /* 3) clear "stop pclk" */
    PCGCCTL &= ~0x1;
    usb_delay();
    /* 4) clear "power clamp" */
    PCGCCTL &= ~0x4;
    usb_delay();
    /* 5) clear "reset power down module" */
    PCGCCTL &= ~0x8;
    usb_delay();
    /* 6) set "power on program done" */
    DCTL |= DCTL_pwronprgdone;
    usb_delay();
    /* 7) core soft reset */
    GRSTCTL |= GRSTCTL_csftrst;
    usb_delay();
    /* 8) hclk soft reset */
    GRSTCTL |= GRSTCTL_hsftrst;
    usb_delay();
    /* 9) flush and reset everything */
    GRSTCTL |= 0x3f;
    usb_delay();
    /* 10) force device mode*/
    GUSBCFG &= ~GUSBCFG_force_host_mode;
    GUSBCFG |= GUSBCFG_force_device_mode;
    usb_delay();
    /* 11) Do something that is probably CCU related but undocumented*/
    CCU_USB |= 0x1000;
    CCU_USB &= ~0x300000;
    usb_delay();
    /* 12) reset usb core parameters (dev addr, speed, ...) */
    DCFG = 0;
    usb_delay();
}

static void as3525v2_disconnect(void)
{
    /* Disable clock */
    CGU_USB = 0;
    usb_delay();
    bitclr32(&CGU_PERI, CGU_USB_CLOCK_ENABLE);
}

static void enable_device_interrupts(void)
{
    /* Clear any pending interrupt */
    GINTSTS = 0xffffffff;
    /* Clear any pending otg interrupt */
    GOTGINT = 0xffffffff;
    /* Enable interrupts */
    GINTMSK = GINTMSK_usbreset
            | GINTMSK_enumdone
            | GINTMSK_inepintr
            | GINTMSK_outepintr
            | GINTMSK_disconnect
            | GINTMSK_usbsuspend
            | GINTMSK_wkupintr
            | GINTMSK_otgintr;
}

static void flush_tx_fifos(int nums)
{
    unsigned int i = 0;

    GRSTCTL = (nums << GRSTCTL_txfnum_bitp)
            | GRSTCTL_txfflsh_flush;
    while(GRSTCTL & GRSTCTL_txfflsh_flush && i < 0x300)
        i++;
    if(GRSTCTL & GRSTCTL_txfflsh_flush)
        panicf("usb-drv: hang of flush tx fifos (%x)", nums);
    /* wait 3 phy clocks */
    udelay(1);
}

static void prepare_setup_ep0(void)
{
    logf("usb-drv: prepare EP0");
    /* setup DMA */
    DOEPDMA(0) = (unsigned long)AS3525_PHYSICAL_ADDR(&_ep0_setup_pkt);

    /* Setup EP0 OUT with the following parameters:
     * packet count = 1
     * setup packet count = 1
     * transfer size = 8 (setup packet)
     */
    DOEPTSIZ(0) = (1 << DEPTSIZ0_supcnt_bitp)
                | (1 << DEPTSIZ0_pkcnt_bitp)
                | 8;

    /* Enable endpoint, clear nak */
    ep0_state = EP0_WAIT_SETUP;
    DOEPCTL(0) |= DEPCTL_epena | DEPCTL_cnak;
}

static void handle_ep0_complete(bool is_ack)
{
    switch(ep0_state)
    {
        case EP0_WAIT_SETUP:
            panicf("usb-drv: EP0 completion while waiting for SETUP");
        case EP0_WAIT_ACK:
            if(is_ack)
                /* everything is done, prepare next setup */
                prepare_setup_ep0();
            else
                panicf("usb-drv: EP0 data completion while waiting for ACK");
            break;
        case EP0_WAIT_DATA:
            if(is_ack)
                panicf("usb-drv: EP0 ACK while waiting for data completion");
            else
                /* everything is done, prepare next setup */
                prepare_setup_ep0();
            break;
        case EP0_WAIT_DATA_ACK:
            /* update state */
            if(is_ack)
                ep0_state = EP0_WAIT_DATA;
            else
                ep0_state = EP0_WAIT_ACK;
            break;
        default:
            panicf("usb-drv: invalid EP0 state");
    }
    logf("usb-drv: EP0 state updated to %d", ep0_state);
}

static void handle_ep0_setup(void)
{
    if(ep0_state != EP0_WAIT_SETUP)
    {
        logf("usb-drv: EP0 SETUP while in state %d", ep0_state);
        return;
    }
    /* determine is there is a data phase */
    if(ep0_setup_pkt->wLength == 0)
        /* no: wait for ack */
        ep0_state = EP0_WAIT_ACK;
    else
        /* yes: wait ack and data */
        ep0_state = EP0_WAIT_DATA_ACK;
    logf("usb-drv: EP0 state updated to %d", ep0_state);
}

static void complete_head_td(struct usb_endpoint *endpoint, int status, int xfered)
{
    void *buffer = endpoint->tds[endpoint->head_td].buffer;

    if(endpoint->mode == USB_DRV_ENDPOINT_MODE_QUEUE)
    {
        if(endpoint->head_td == endpoint->tail_td)
            endpoint->head_td = -1;
        else
            endpoint->head_td = (endpoint->head_td + 1) % endpoint->nb_tds;
    }
    else if(endpoint->mode == USB_DRV_ENDPOINT_MODE_REPEAT)
    {
        endpoint->head_td = (endpoint->head_td + 1) % endpoint->nb_tds;
    }

    logf("usb-drv: complete head td on EP%d: status=%d xfered=%d", endpoint->ep,
        status, xfered);
    logf("usb-drv: queue state: head=%d tail=%d", endpoint->head_td, endpoint->tail_td);

    usb_core_transfer_complete(EP_NUM(endpoint->ep), endpoint->ep & USB_DIR_IN, status, xfered, buffer);
}

static void cancel_all_tds(struct usb_endpoint *endpoint)
{
    if(endpoint->mode == USB_DRV_ENDPOINT_MODE_QUEUE)
    {
        while(endpoint->head_td != -1)
            complete_head_td(endpoint, -1, 0);
    }
}

static void reset_endpoint(struct usb_endpoint *endpoint)
{
    int ep = EP_NUM(endpoint->ep);
    if(EP_DIR(endpoint->ep) == DIR_IN)
        DIEPCTL(ep) = (DIEPCTL(ep) & ~DEPCTL_usbactep) | DEPCTL_snak;
    else
        DOEPCTL(ep) = (DOEPCTL(ep) & ~DEPCTL_usbactep) | DEPCTL_snak;
    
    endpoint->status = 1;
    if(endpoint->wait)
        wakeup_signal(&endpoint->complete);
    endpoint->wait = false;
    cancel_all_tds(endpoint);
}

static void reset_endpoints(void)
{
    unsigned i;
    int ep;
    /* disable all endpoints except EP0 */
    FOR_EACH_IN_EP_AND_EP0(i, ep)
        reset_endpoint(&endpoints[ep][DIR_IN]);
    FOR_EACH_OUT_EP_AND_EP0(i, ep)
        reset_endpoint(&endpoints[ep][DIR_OUT]);
    /* 64 bytes packet size, active endpoint */
    DOEPCTL(0) = (DEPCTL_MPS_64 << DEPCTL_mps_bitp) | DEPCTL_usbactep | DEPCTL_snak;
    DIEPCTL(0) = (DEPCTL_MPS_64 << DEPCTL_mps_bitp) | DEPCTL_usbactep | DEPCTL_snak;
    /* Setup next chain for IN eps */
    FOR_EACH_IN_EP_AND_EP0(i, ep)
    {
        int next_ep = in_ep_list[(i + 1) % (NUM_IN_EP + 1)];
        DIEPCTL(ep) = (DIEPCTL(ep) & ~bitm(DEPCTL, nextep)) | (next_ep << DEPCTL_nextep_bitp);
    }
}

static void start_head_td(struct usb_endpoint *endpoint)
{
    int len = endpoint->tds[endpoint->head_td].len;
    void *ptr = endpoint->tds[endpoint->head_td].buffer;
    
    int ep = EP_NUM(endpoint->ep);
    bool dir_in = EP_DIR(endpoint->ep);

    logf("usb-drv: start xfer EP%d, len=%d, dir_in=%d", ep, len, dir_in);
    logf("usb-drv: head_td=%d tail_td=%d", endpoint->head_td, endpoint->tail_td);

    volatile unsigned long *epctl = dir_in ? &DIEPCTL(ep) : &DOEPCTL(ep);
    volatile unsigned long *eptsiz = dir_in ? &DIEPTSIZ(ep) : &DOEPTSIZ(ep);
    volatile unsigned long *epdma = dir_in ? &DIEPDMA(ep) : &DOEPDMA(ep);
    #define DEPCTL  *epctl
    #define DEPTSIZ *eptsiz
    #define DEPDMA  *epdma

    endpoint->status = 1;

    DEPCTL &= ~DEPCTL_stall;
    DEPCTL |= DEPCTL_usbactep;

    int mps = endpoint->max_pkt_size;
    int nb_packets = (len + mps - 1) / mps;

    if(len == 0)
    {
        DEPDMA = 0x10000000;
        DEPTSIZ = 1 << DEPTSIZ_pkcnt_bitp;
    }
    else
    {
        DEPDMA = (unsigned long)AS3525_PHYSICAL_ADDR(ptr);
        DEPTSIZ = (nb_packets << DEPTSIZ_pkcnt_bitp) | len;
        if(dir_in)
            clean_dcache_range(ptr, len);
        else
            dump_dcache_range(ptr, len);
    }

    logf("pkt=%d dma=%lx", nb_packets, DEPDMA);

    DEPCTL |= DEPCTL_epena | DEPCTL_cnak;

    #undef DEPCTL
    #undef DEPTSIZ
    #undef DEPDMA
}

static void complete_wakeup_start_head_td_if_any(struct usb_endpoint *endpoint, int status, int xfered)
{
    complete_head_td(endpoint, status, xfered);
    if(endpoint->wait)
        wakeup_signal(&endpoint->complete);
    endpoint->wait = false;
    /* start next transfer if any */
    if(endpoint->head_td != -1)
        start_head_td(endpoint);
}

void usb_drv_reset_endpoint(int endpoint, bool send)
{
    return reset_endpoint(&endpoints[EP_NUM(endpoint)][send]);
}

static void cancel_all_transfers(bool cancel_ep0)
{
    logf("usb-drv: cancel all transfers");
    return;
    int flags = disable_irq_save();
    int ep;
    unsigned i;

    /* BUG: check if this is right */
    FOR_EACH_IN_EP_EX(cancel_ep0, i, ep)
        reset_endpoint(&endpoints[ep][DIR_IN]);
    FOR_EACH_OUT_EP_EX(cancel_ep0, i, ep)
        reset_endpoint(&endpoints[ep][DIR_OUT]);

    restore_irq(flags);
}

static void core_dev_init(void)
{
    int ep;
    unsigned int i;
    /* Restart the phy clock */
    PCGCCTL = 0;
    /* Set phy speed : high speed */
    DCFG = (DCFG & ~bitm(DCFG, devspd)) | DCFG_devspd_hs_phy_hs;

    /* Check hardware capabilities */
    if(extract(GHWCFG2, arch) != GHWCFG2_ARCH_INTERNAL_DMA)
        panicf("usb-drv: wrong architecture (%ld)", extract(GHWCFG2, arch));
    if(extract(GHWCFG2, hs_phy_type) != GHWCFG2_PHY_TYPE_UTMI)
        panicf("usb-drv: wrong HS phy type (%ld)", extract(GHWCFG2, hs_phy_type));
    if(extract(GHWCFG2, fs_phy_type) != GHWCFG2_PHY_TYPE_UNSUPPORTED)
        panicf("usb-drv: wrong FS phy type (%ld)", extract(GHWCFG2, fs_phy_type));
    if(extract(GHWCFG4, utmi_phy_data_width) != 0x2)
        panicf("usb-drv: wrong utmi data width (%ld)", extract(GHWCFG4, utmi_phy_data_width));
    if(!(GHWCFG4 & GHWCFG4_ded_fifo_en)) /* it seems to be multiple tx fifo support */
        panicf("usb-drv: no multiple tx fifo");

    #ifdef USE_CUSTOM_FIFO_LAYOUT
    if(!(GHWCFG2 & GHWCFG2_dyn_fifo))
        panicf("usb-drv: no dynamic fifo");
    if(GRXFSIZ != DATA_FIFO_DEPTH)
        panicf("usb-drv: wrong data fifo size");
    #endif /* USE_CUSTOM_FIFO_LAYOUT */

    if(USB_NUM_ENDPOINTS != extract(GHWCFG2, num_ep))
        panicf("usb-drv: wrong endpoint number");

    FOR_EACH_IN_EP_AND_EP0(i, ep)
    {
        int type = (GHWCFG1 >> GHWCFG1_epdir_bitp(ep)) & GHWCFG1_epdir_bits;
        if(type != GHWCFG1_EPDIR_BIDIR && type != GHWCFG1_EPDIR_IN)
            panicf("usb-drv: EP%d is no IN or BIDIR", ep);
    }
    FOR_EACH_OUT_EP_AND_EP0(i, ep)
    {
        int type = (GHWCFG1 >> GHWCFG1_epdir_bitp(ep)) & GHWCFG1_epdir_bits;
        if(type != GHWCFG1_EPDIR_BIDIR && type != GHWCFG1_EPDIR_OUT)
            panicf("usb-drv: EP%d is no OUT or BIDIR", ep);
    }

    /* Setup FIFOs */
    GRXFSIZ = 512;
    GNPTXFSIZ = MAKE_FIFOSIZE_DATA(512, 512);

    /* Setup interrupt masks for endpoints */
    /* Setup interrupt masks */
    DOEPMSK = DOEPINT_setup | DOEPINT_xfercompl | DOEPINT_ahberr;
    DIEPMSK = DIEPINT_xfercompl | DIEPINT_timeout | DIEPINT_ahberr;
    DAINTMSK = 0xffffffff;

    reset_endpoints();

    prepare_setup_ep0();

    /* enable USB interrupts */
    enable_device_interrupts();
}

static void core_init(void)
{
    /* Disconnect */
    DCTL |= DCTL_sftdiscon;
    /* Select UTMI+ 16 */
    GUSBCFG |= GUSBCFG_phy_if;
    GUSBCFG = (GUSBCFG & ~bitm(GUSBCFG, toutcal)) | 7 << GUSBCFG_toutcal_bitp;

    /* fixme: the current code is for internal DMA only, the clip+ architecture
     *        define the internal DMA model */
    /* Set burstlen and enable DMA*/
    GAHBCFG = (GAHBCFG_INT_DMA_BURST_INCR << GAHBCFG_hburstlen_bitp)
                | GAHBCFG_dma_enable;
    /* Disable HNP and SRP, not sure it's useful because we already forced dev mode */
    GUSBCFG &= ~(GUSBCFG_srpcap | GUSBCFG_hnpcapp);

    /* perform device model specific init */
    core_dev_init();

    /* Reconnect */
    DCTL &= ~DCTL_sftdiscon;
}

static void enable_global_interrupts(void)
{
    VIC_INT_ENABLE = INTERRUPT_USB;
    GAHBCFG |= GAHBCFG_glblintrmsk;
}

static void disable_global_interrupts(void)
{
    GAHBCFG &= ~GAHBCFG_glblintrmsk;
    VIC_INT_EN_CLEAR = INTERRUPT_USB;
}

void usb_drv_init(void)
{
    unsigned i, ep;
    logf("usb_drv_init");
    /* Boost cpu */
    cpu_boost(1);
    /* Enable PHY and clocks (but leave pullups disabled) */
    as3525v2_connect();
    logf("usb-drv: synopsis id: %lx", GSNPSID);
    /* Core init */
    core_init();
    /* Setup endpoints */
    FOR_EACH_IN_EP_AND_EP0(i, ep)
    {
        endpoints[ep][DIR_IN].ep = ep | USB_DIR_IN;
        wakeup_init(&endpoints[ep][DIR_IN].complete);
        endpoints[ep][DIR_IN].nb_tds = 0;
        endpoints[ep][DIR_IN].head_td = -1;
        endpoints[ep][DIR_IN].wait = false;
        endpoints[ep][DIR_IN].mode = USB_DRV_ENDPOINT_MODE_VOID;
    }
    FOR_EACH_OUT_EP_AND_EP0(i, ep)
    {
        endpoints[ep][DIR_OUT].ep = ep | USB_DIR_OUT;
        wakeup_init(&endpoints[ep][DIR_OUT].complete);
        endpoints[ep][DIR_OUT].nb_tds = 0;
        endpoints[ep][DIR_OUT].head_td = -1;
        endpoints[ep][DIR_OUT].wait = false;
        endpoints[ep][DIR_OUT].mode = USB_DRV_ENDPOINT_MODE_VOID;
    }
    /* Enable global interrupts */
    enable_global_interrupts();

    g_usbreset_count = 0;
}

void usb_drv_exit(void)
{
    logf("usb_drv_exit");

    disable_global_interrupts();
    as3525v2_disconnect();
    cpu_boost(0);
}

static void handle_ep_in_int(int ep)
{
    struct usb_endpoint *endpoint = &endpoints[ep][DIR_IN];
    unsigned long sts = DIEPINT(ep);
    if(sts & DIEPINT_ahberr)
        panicf("usb-drv: ahb error on EP%d IN", ep);
    if(sts & DIEPINT_xfercompl)
    {
        if(endpoint->head_td != -1)
        {
            endpoint->status = 0;
            /* works even for EP0 */
            int len = endpoint->tds[endpoint->head_td].len;
            int size = (DIEPTSIZ(ep) & DEPTSIZ_xfersize_bits);
            int transfered = len - size;
            logf("len=%d reg=%d xfer=%d", len, size, transfered);
            /* handle EP0 state if necessary,
             * this is a ack if length is 0 */
            if(ep == 0)
                handle_ep0_complete(len == 0);
            complete_wakeup_start_head_td_if_any(endpoint, 0, transfered);
        }
    }
    if(sts & DIEPINT_timeout)
    {
        panicf("usb-drv: timeout on EP%d IN", ep);
        if(endpoint->head_td != -1)
        {
            endpoint->status = 1;
            /* for safety, act as if no bytes as been transfered */
            complete_wakeup_start_head_td_if_any(endpoint, 1, 0);
        }
    }
    /* clear interrupts */
    DIEPINT(ep) = sts;
}

static void handle_ep_out_int(int ep)
{
    struct usb_endpoint *endpoint = &endpoints[ep][DIR_OUT];
    unsigned long sts = DOEPINT(ep);
    if(sts & DOEPINT_ahberr)
        panicf("usb-drv: ahb error on EP%d OUT", ep);
    if(sts & DOEPINT_xfercompl)
    {
        logf("usb-drv: xfer complete on EP%d OUT", ep);
        if(endpoint->head_td != -1)
        {
            endpoint->status = 0;
            /* works even for EP0 */
            int len = endpoint->tds[endpoint->head_td].len;
            int transfered = len - (DOEPTSIZ(ep) & DEPTSIZ_xfersize_bits);
            logf("len=%d reg=%ld xfer=%d", len,
                (DOEPTSIZ(ep) & DEPTSIZ_xfersize_bits),
                transfered);
            /* handle EP0 state if necessary,
             * this is a ack if length is 0 */
            if(ep == 0)
                handle_ep0_complete(len == 0);
            complete_wakeup_start_head_td_if_any(endpoint, 0, transfered);
        }
    }
    if(sts & DOEPINT_setup)
    {
        logf("usb-drv: setup on EP%d OUT", ep);
        if(ep != 0)
            panicf("usb-drv: setup not on EP0, this is impossible");
        if((DOEPTSIZ(ep) & DEPTSIZ_xfersize_bits) != 0)
        {
            logf("usb-drv: ignore spurious setup (xfersize=%ld)", DOEPTSIZ(ep) & DEPTSIZ_xfersize_bits);
            prepare_setup_ep0();
        }
        else
        {
            /* handle EP0 state */
            handle_ep0_setup();
            logf("  rt=%x r=%x", ep0_setup_pkt->bRequestType, ep0_setup_pkt->bRequest);
            /* handle set address */
            if(ep0_setup_pkt->bRequestType == USB_TYPE_STANDARD &&
                    ep0_setup_pkt->bRequest == USB_REQ_SET_ADDRESS)
            {
                /* Set address now */
                DCFG = (DCFG & ~bitm(DCFG, devadr)) | (ep0_setup_pkt->wValue << DCFG_devadr_bitp);
            }
            usb_core_control_request(ep0_setup_pkt);
        }
    }
    /* clear interrupts */
    DOEPINT(ep) = sts;
}

static void handle_ep_ints(void)
{
    logf("usb-drv: ep int");
    /* we must read it */
    unsigned long daint = DAINT;
    unsigned i, ep;

    FOR_EACH_IN_EP_AND_EP0(i, ep)
        if(daint & DAINT_IN_EP(ep))
            handle_ep_in_int(ep);

    FOR_EACH_OUT_EP_AND_EP0(i, ep)
        if(daint & DAINT_OUT_EP(ep))
            handle_ep_out_int(ep);

    /* write back to clear status */
    DAINT = daint;
}

int usb_drv_port_speed(void)
{
    static const uint8_t speed[4] = {
        [DSTS_ENUMSPD_HS_PHY_30MHZ_OR_60MHZ] = 1,
        [DSTS_ENUMSPD_FS_PHY_30MHZ_OR_60MHZ] = 0,
        [DSTS_ENUMSPD_FS_PHY_48MHZ]          = 0,
        [DSTS_ENUMSPD_LS_PHY_6MHZ]           = 0,
    };

    unsigned enumspd = extract(DSTS, enumspd);

    if(enumspd == DSTS_ENUMSPD_LS_PHY_6MHZ)
        panicf("usb-drv: LS is not supported");

    return speed[enumspd & 3];
}

static inline unsigned long usb_drv_mps_by_type(int type)
{
    static const uint16_t mps[4][2] = {
        /*         type                 fs      hs   */
        [USB_ENDPOINT_XFER_CONTROL] = { 64,     64   },
        [USB_ENDPOINT_XFER_ISOC]    = { 1023,   1024 },
        [USB_ENDPOINT_XFER_BULK]    = { 64,     512  },
        [USB_ENDPOINT_XFER_INT]     = { 64,     1024 },
    };
    return mps[type & 3][usb_drv_port_speed() & 1];
}

static void fill_max_packet_sizes(void)
{
    unsigned i, ep;
    FOR_EACH_IN_EP_AND_EP0(i, ep)
    {
        #define DEPCTL  DIEPCTL(ep)
        endpoints[ep][DIR_IN].max_pkt_size = usb_drv_mps_by_type(extract(DEPCTL, eptype));
        logf("usb-drv: EP%d IN: mps=%d", ep, endpoints[ep][DIR_IN].max_pkt_size);
        #undef DEPCTL
    }
    FOR_EACH_OUT_EP_AND_EP0(i, ep)
    {
        #define DEPCTL  DOEPCTL(ep)
        endpoints[ep][DIR_OUT].max_pkt_size = usb_drv_mps_by_type(extract(DEPCTL, eptype));
        logf("usb-drv: EP%d OUT: mps=%d", ep, endpoints[ep][DIR_OUT].max_pkt_size);
        #undef DEPCTL
    }
}

/* interrupt service routine */
void INT_USB(void)
{
    /* some bits in GINTSTS can be set even though we didn't enable the interrupt source
     * so AND it with the actual mask */
    unsigned long sts = GINTSTS & GINTMSK;

    if(sts & GINTMSK_usbreset)
    {
        logf("usb-drv: bus reset");

        g_usbreset_count++;

        if(g_usbreset_count == 2)
        {
            logf("usb-drv: hard reset");
            logf("--------");
            logf("--------");
            DCTL |= DCTL_sftdiscon;
            usb_delay();
            usb_drv_exit();
            usb_core_exit();
            usb_core_init(); // calls usb_drv_init() which reset g_usbreset_count
            // usb_drv_init(); /* reset g_usbreset_count here */
        }

        /* Clear the Remote Wakeup Signalling */
        DCTL &= ~DCTL_rmtwkupsig;

        /* Flush FIFOs */
        flush_tx_fifos(0x10);

        /* Flush the Learning Queue */
        GRSTCTL = GRSTCTL_intknqflsh;

        /* Reset Device Address */
        DCFG &= ~bitm(DCFG, devadr);

        reset_endpoints();
        prepare_setup_ep0();

        usb_core_bus_reset();
    }

    if(sts & GINTMSK_enumdone)
    {
        logf("usb-drv: enum done");

        /* read speed */
        if(usb_drv_port_speed())
            logf("usb-drv: HS");
        else
            logf("usb-drv: FS");

        /* set max packet size */
        fill_max_packet_sizes();
    }

    if(sts & GINTMSK_otgintr)
    {
        logf("usb-drv: otg int");
        GOTGINT = 0xffffffff;
    }

    if(sts & (GINTMSK_outepintr | GINTMSK_inepintr))
    {
        handle_ep_ints();
    }

    if(sts & GINTMSK_disconnect)
    {
        panicf("usb-drv: disconnect");
        cancel_all_transfers(true);
        usb_enable(false);
    }

    GINTSTS = sts;
}

int usb_drv_request_endpoint(int type, int dir)
{
    int ep, ret = -1;
    unsigned i;
    logf("usb-drv: request endpoint (type=%d,dir=%s)", type, dir == USB_DIR_IN ? "IN" : "OUT");

    if(dir == USB_DIR_IN)
        FOR_EACH_IN_EP(i, ep)
        {
            if(endpoints[ep][DIR_IN].active)
                continue;
            endpoints[ep][DIR_IN].active = true;
            ret = ep | dir;
            break;
        }
    else
        FOR_EACH_OUT_EP(i, ep)
        {
            if(endpoints[ep][DIR_OUT].active)
                continue;
            endpoints[ep][DIR_OUT].active = true;
            ret = ep | dir;
            break;
        }

    if(ret == -1)
    {
        logf("usb-drv: request failed");
        return -1;
    }

    unsigned long data = DEPCTL_setd0pid | (type << DEPCTL_eptype_bitp)
                        | (usb_drv_mps_by_type(type) << DEPCTL_mps_bitp)
                        | DEPCTL_usbactep | DEPCTL_snak;
    unsigned long mask = ~(bitm(DEPCTL, eptype) | bitm(DEPCTL, mps));

    if(dir == USB_DIR_IN) DIEPCTL(ep) = (DIEPCTL(ep) & mask) | data;
    else DOEPCTL(ep) = (DOEPCTL(ep) & mask) | data;

    return ret;
}

void usb_drv_release_endpoint(int ep)
{
    logf("usb-drv: release EP%d %s", EP_NUM(ep), EP_DIR(ep) == DIR_IN ? "IN" : "OUT");
    usb_drv_reset_endpoint(EP_NUM(ep), EP_DIR(ep));
    usb_drv_release_slots(ep);
    usb_drv_select_endpoint_mode(ep, USB_DRV_ENDPOINT_MODE_VOID);
    endpoints[EP_NUM(ep)][EP_DIR(ep)].active = false;
}

void usb_drv_cancel_all_transfers()
{
    cancel_all_transfers(false);
}

int usb_drv_fill_repeat_slot(int ep, int slot, void *ptr, int length)
{
    struct usb_endpoint *endpoint = &endpoints[EP_NUM(ep)][EP_DIR(ep)];

    endpoint->tds[slot].len = length;
    endpoint->tds[slot].buffer = ptr;

    return 0;
}

int usb_drv_start_repeat(int ep)
{
    struct usb_endpoint *endpoint = &endpoints[EP_NUM(ep)][EP_DIR(ep)];
    endpoint->head_td = 0;
    start_head_td(endpoint);

    return 0;
}

int usb_drv_stop_repeat(int ep)
{
    struct usb_endpoint *endpoint = &endpoints[EP_NUM(ep)][EP_DIR(ep)];
    reset_endpoint(endpoint);
    endpoint->head_td = -1;
    
    return 0;
}

int usb_drv_max_endpoint_packet_size(int ep)
{
    return endpoints[EP_NUM(ep)][EP_DIR(ep)].max_pkt_size;
}

int usb_drv_allocate_slots(int ep, int buffer_size, void *buffer)
{
    struct usb_endpoint *endpoint = &endpoints[EP_NUM(ep)][EP_DIR(ep)];
    
    endpoint->tds = (struct usb_transfer_description *)buffer;
    endpoint->nb_tds = buffer_size / USB_DRV_SLOT_SIZE;
    endpoint->head_td = -1;
    
    return 0;
}

/* Release the slots previously allocated.
 * Returns 0 on success and <0 on error. */
int usb_drv_release_slots(int ep)
{
    struct usb_endpoint *endpoint = &endpoints[EP_NUM(ep)][EP_DIR(ep)];

    endpoint->tds = NULL;
    endpoint->nb_tds = 0;
    endpoint->head_td = -1;
    
    return 0;
}

int usb_drv_select_endpoint_mode(int ep, int mode)
{
    struct usb_endpoint *endpoint = &endpoints[EP_NUM(ep)][EP_DIR(ep)];
    
    endpoint->mode = mode;
    endpoint->head_td = -1;
    
    return 0;
}

/* queue api */
static int usb_drv_queue_transfer(int ep_num, bool send, bool wait, void *ptr, int length)
{
    struct usb_endpoint *endpoint = &endpoints[ep_num][send];
    logf("usb-drv: queue transfer on EP%d: dir_in=%d len=%d wait=%d", ep_num, send, length, wait);
    logf("usb-drv: queue state: nb=%d head=%d tail=%d", endpoint->nb_tds, endpoint->head_td, endpoint->tail_td);
    if(endpoint->mode != USB_DRV_ENDPOINT_MODE_QUEUE)
    {
        logf("usb-drv: cannot queue on non-queue endpoint");
        return 1;
    }

    if(endpoint->head_td == -1)
    {
        endpoint->tds[0].buffer = ptr;
        endpoint->tds[0].len = length;
        
        endpoint->head_td = 0;
        endpoint->tail_td = 0;
        endpoint->wait = wait;
        start_head_td(endpoint);
    }
    else
    {
        /* FIXME: check this handles completion of head_td at every time in a safe way */
        int next_tail = (endpoint->tail_td + 1) % endpoint->nb_tds;
        if(next_tail == endpoint->head_td)
        {
            logf("usb-drv: queue full");
            return 1;
        }
        /* don't change tail_td until the data is valid */
        endpoint->tds[next_tail].len = length;
        endpoint->tds[next_tail].buffer = ptr;
        /* update tail_td */
        endpoint->tail_td = next_tail;
        /* recheck head_td, the head transfer might be finished now */
        if(endpoint->head_td == -1)
        {
            endpoint->head_td = 0;
            endpoint->tail_td = 0;
            endpoint->wait = wait;
            start_head_td(endpoint);
        }
    }

    if(wait)
    {
        wakeup_wait(&endpoint->complete, TIMEOUT_BLOCK);
        return endpoint->status;
    }
    else
        return 0;
}

int usb_drv_queue_send_blocking(int endpoint, void *ptr, int length)
{
    return usb_drv_queue_transfer(EP_NUM(endpoint), true, true, ptr, length);
}

int usb_drv_queue_send_nonblocking(int endpoint, void *ptr, int length)
{
    return usb_drv_queue_transfer(EP_NUM(endpoint), true, false, ptr, length);
}

int usb_drv_queue_recv_nonblocking(int endpoint, void *ptr, int length)
{
    return usb_drv_queue_transfer(EP_NUM(endpoint), false, false, ptr, length);
}

int usb_drv_queue_recv_blocking(int endpoint, void *ptr, int length)
{
    return usb_drv_queue_transfer(EP_NUM(endpoint), false, true, ptr, length);
}

/* mid api */
int usb_drv_send_nonblocking(int endpoint, void* ptr, int length)
{
    return usb_drv_queue_send_nonblocking(endpoint, ptr, length);
}

int usb_drv_send_blocking(int endpoint, void* ptr, int length)
{
    return usb_drv_queue_send_blocking(endpoint, ptr, length);
}

int usb_drv_recv_blocking(int endpoint, void* ptr, int length)
{
    return usb_drv_queue_recv_blocking(endpoint, ptr, length);
}

int usb_drv_recv_nonblocking(int endpoint, void* ptr, int length)
{
    return usb_drv_queue_recv_nonblocking(endpoint, ptr, length);
}

void usb_drv_set_test_mode(int mode)
{
    /* there is a perfect matching between usb test mode code
     * and the register field value */
    DCTL = (DCTL & ~bitm(DCTL, tstctl)) | (mode << DCTL_tstctl_bitp);
}

void usb_drv_set_address(int address)
{
    (void) address;
}

void usb_drv_stall(int ep, bool stall, bool in)
{
    logf("usb-drv: %sstall EP%d %s", stall ? "" : "un", ep, in ? "IN" : "OUT");
    if(in)
    {
        if(stall) DIEPCTL(ep) |= DEPCTL_stall;
        else DIEPCTL(ep) &= ~DEPCTL_stall;
    }
    else
    {
        if(stall) DOEPCTL(ep) |= DEPCTL_stall;
        else DOEPCTL(ep) &= ~DEPCTL_stall;
    }
}

bool usb_drv_stalled(int ep, bool in)
{
    return (in ? DIEPCTL(ep) : DOEPCTL(ep)) & DEPCTL_stall;
}
