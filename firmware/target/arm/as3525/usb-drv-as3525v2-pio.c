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

/* store per endpoint, per direction, information */
struct usb_endpoint
{
    unsigned int len; /* length of the data buffer */
    struct semaphore complete; /* wait object */
    int8_t status; /* completion status (0 for success) */
    bool active; /* true is endpoint has been requested (true for EP0) */
    bool wait; /* true if usb thread is blocked on completion */
    bool busy; /* true is a transfer is pending */
    void *buf_ptr; /* current buffer pointer (updated after each packet) */
    unsigned int rem_len; /* remaining length (ditto) */
    int mps; /* maximum packet size */
};

/* endpoints[ep_num][DIR_IN/DIR_OUT] */
static struct usb_endpoint endpoints[USB_NUM_ENDPOINTS][2];

void usb_attach(void)
{
    logf("usb-drv: attach");
    /* Nothing to do */
}

static inline void usb_delay(void)
{
    register int i = 0;
    asm volatile(
        "1: nop             \n"
        "   add %0, %0, #1  \n"
        "   cmp %0, #0x300  \n"
        "   bne 1b          \n"
        : "+r"(i)
    );
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
    /* Disconnect */
    DCTL |= DCTL_sftdiscon;
    sleep(HZ / 20);
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
    GINTMSK = GINTMSK_usbreset | GINTMSK_enumdone | GINTMSK_inepintr |
        GINTMSK_outepintr | GINTMSK_disconnect | GINTMSK_usbsuspend |
        GINTMSK_wkupintr | GINTMSK_otgintr | GINTMSK_rxstsqlvl;
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
}

static void flush_rx_fifo(void)
{
    unsigned int i = 0;

    GRSTCTL = GRSTCTL_rxfflsh_flush;
    while(GRSTCTL & GRSTCTL_rxfflsh_flush && i < 0x300)
        i++;
    if(GRSTCTL & GRSTCTL_rxfflsh_flush)
        panicf("usb-drv: hang of flush rx fifo");
}

static void reset_endpoints(void)
{
    unsigned i;
    int ep;
    /* disable all endpoints except EP0 */
    FOR_EACH_IN_EP_AND_EP0(i, ep)
    {
        endpoints[ep][DIR_IN].active = false;
        endpoints[ep][DIR_IN].busy = false;
        endpoints[ep][DIR_IN].status = -1;
        if(endpoints[ep][DIR_IN].wait)
        {
            endpoints[ep][DIR_IN].wait = false;
            semaphore_release(&endpoints[ep][DIR_IN].complete);
        }
        if(DIEPCTL(ep) & DEPCTL_epena)
            DIEPCTL(ep) |= DEPCTL_epdis;
    }

    FOR_EACH_OUT_EP_AND_EP0(i, ep)
    {
        endpoints[ep][DIR_OUT].active = false;
        endpoints[ep][DIR_OUT].busy = false;
        endpoints[ep][DIR_OUT].status = -1;
        if(endpoints[ep][DIR_OUT].wait)
        {
            endpoints[ep][DIR_OUT].wait = false;
            semaphore_release(&endpoints[ep][DIR_OUT].complete);
        }
        if(DOEPCTL(ep) & DEPCTL_epena)
            DOEPCTL(ep) |=  DEPCTL_epdis;
    }
    /* 64 bytes packet size, active endpoint */
    DOEPCTL(0) = (DEPCTL_MPS_64 << DEPCTL_mps_bitp) | DEPCTL_usbactep | DEPCTL_snak;
    DIEPCTL(0) = (DEPCTL_MPS_64 << DEPCTL_mps_bitp) | DEPCTL_usbactep | DEPCTL_snak;

    flush_tx_fifos(0x10);
    flush_rx_fifo();
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
    logf("GRXFSIZ=%lx GNPTXFSIZ=%lx", GRXFSIZ, GNPTXFSIZ);
    GRXFSIZ = 512;
    GNPTXFSIZ = MAKE_FIFOSIZE_DATA(512, 512);
    DOEPMSK = DOEPINT_xfercompl;
    DIEPMSK = DIEPINT_xfercompl | DIEPINT_timeout;
    DAINTMSK = 0xffffffff;

    reset_endpoints();

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

    /* Just to make sure, disable DMA */
    GAHBCFG &= ~GAHBCFG_dma_enable;
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
    FOR_EACH_IN_EP_AND_EP0(i, ep)
        semaphore_init(&endpoints[ep][DIR_IN].complete, 1, 0);
    FOR_EACH_OUT_EP_AND_EP0(i, ep)
        semaphore_init(&endpoints[ep][DIR_OUT].complete, 1, 0);
    /* Enable global interrupts */
    enable_global_interrupts();
}

void usb_drv_cancel_all_transfers()
{
    logf("usb_drv_cancel_all_transfers");
    reset_endpoints();
}

void usb_drv_exit(void)
{
    logf("usb_drv_exit");

    disable_global_interrupts();
    as3525v2_disconnect();
    cpu_boost(0);
}

/* if buffer is NULL, throw away data */
static void read_fifo_data(unsigned epnum, unsigned size, void *buffer)
{
    /* warning: register gives a 32-bit value but the buffer size might not be a
     *          multiple of 4 so be careful not to write past the buffer ! */
    if(buffer == NULL)
    {
        size = (size + 3) / 4;
        while(size-- > 0)
            (void)DEPFIFO(epnum);
    }
    else
    {
        while(size >= 4)
        {
            *(uint32_t *)buffer = DEPFIFO(epnum);
            size -= 4;
            buffer += 4;
        }
        /* handle remaining bytes (1, 2 or 3) */
        if(size > 0)
        {
            uint32_t v = DEPFIFO(epnum);
            memcpy(buffer, &v, size);
        }
    }
}

static void write_fifo_data(unsigned epnum, unsigned size, void *buffer)
{
    /* warning: register expects a 32-bit value but the buffer size might not be a
     *          multiple of 4 so be careful not to read past the buffer ! */
    while(size >= 4)
    {
        DEPFIFO(epnum) = *((uint32_t *)buffer);
        size -= 4;
        buffer += 4;
    }
    /* handle remaining bytes (1, 2 or 3) */
    if(size > 0)
    {
        uint32_t v = 0;
        memcpy(&v, buffer, size);
        DEPFIFO(epnum) = v;
    }
}

static void handle_rx_data(unsigned epnum, unsigned size)
{
    struct usb_endpoint *endpoint = &endpoints[epnum][DIR_OUT];
    
    logf("usb-drv: rx data on EP%d: size=%x", epnum, size);
    /* ignore data on inactivate endpoint or extra data (should never happen) */
    if(!endpoint->busy || size > endpoint->rem_len)
    {
        read_fifo_data(epnum, size, NULL);
        endpoint->status = 1;
        DOEPCTL(epnum) |= DEPCTL_epdis;
        logf("usb-drv: ignore data (busy=%d rem_len=%d)", endpoint->busy,
            endpoint->rem_len);
        return;
    }

    read_fifo_data(epnum, size, endpoint->buf_ptr);
    endpoint->rem_len -= size;
    endpoint->buf_ptr += size;
}

static void handle_rx_done(unsigned epnum)
{
    logf("usb-drv: rx done on EP%d", epnum);
}

static void handle_setup(unsigned epnum, unsigned size)
{
    if(epnum != 0)
    {
        logf("usb-drv: ignore setup packet on EP%d !", epnum);
        return;
    }
    if(size != 8)
    {
        logf("usb-drv: ignore invalid setup packet (size=%x) !", size);
        return;
    }
    struct usb_ctrlrequest setup_pkt;
    read_fifo_data(epnum, size, &setup_pkt);
    /* handle set address */
    if(setup_pkt.bRequestType == USB_TYPE_STANDARD &&
            setup_pkt.bRequest == USB_REQ_SET_ADDRESS)
    {
        /* Set address now */
        DCFG = (DCFG & ~bitm(DCFG, devadr)) | (setup_pkt.wValue << DCFG_devadr_bitp);
    }
    usb_core_control_request(&setup_pkt);
}

static void handle_rx_fifo(void)
{
    /* Handle multiple events at once */
    do
    {
        /* Read and POP !! */
        unsigned long rxstsp = GRXSTSP;
        unsigned epnum = vextract(rxstsp, GRXSTSR, epnum);
        unsigned status = vextract(rxstsp, GRXSTSR, pktsts);
        unsigned size = vextract(rxstsp, GRXSTSR, bcnt);
        
        logf("usb-drv: RX fifo event: ep=%d sts=%d size=%x", epnum, status, size);

        switch(status)
        {
            case GRXSTSR_pktsts_glboutnak:
                break;
            case GRXSTSR_pktsts_outdata:
                return handle_rx_data(epnum, size);
            case GRXSTSR_pktsts_setupdata:
                return handle_setup(epnum, size);
            case GRXSTSR_pktsts_outdone:
                return handle_rx_done(epnum);
            case GRXSTSR_pktsts_setupdone:
                break;
            default:
                return;
        }
    }while(GINTSTS & GINTMSK_rxstsqlvl);
}

static void ep_try_tx(int epnum)
{
    struct usb_endpoint *endpoint = &endpoints[epnum][DIR_IN];
    /* only try if there is something to send ! */
    if(!endpoint->busy || endpoint->rem_len == 0)
        return;
    /* FIXME: space available is in words = 4 bytes ! */
    unsigned fifo_avail = extract(GNPTXSTS, nptxfspcavail) * 4;
    unsigned nr_bytes = endpoint->rem_len;
    
    if(nr_bytes > fifo_avail)
    {
        /* If there is too much to send, round down to nearest multiple of a
         * packet size (it might be 0 !) and enable np tx fifo interrupt
         * to retry next time */
        nr_bytes -= nr_bytes % endpoint->mps;
        GINTMSK |= GINTMSK_nptxfempty;
    }
    /* there might be nothing to send at this point */
    if(nr_bytes == 0)
        return;
    /* Advance pointers, decrement remaining length and send data */
    write_fifo_data(epnum, nr_bytes, endpoint->buf_ptr);
    endpoint->rem_len -= nr_bytes;
    endpoint->buf_ptr += nr_bytes;
}

static void handle_tx_fifo(void)
{
    unsigned i, ep;
    /* first disable it and enable again if needed */
    GINTMSK &= ~GINTMSK_nptxfempty;
    /* go through all endpoints and try to send */
    FOR_EACH_IN_EP_AND_EP0(i, ep)
        ep_try_tx(ep);
}

static void handle_ep_in_int(int ep)
{
    struct usb_endpoint *endpoint = &endpoints[ep][DIR_IN];
    unsigned long sts = DIEPINT(ep);
    /* clear interrupts */
    DIEPINT(ep) = sts;
    /* ignore if unrelated */
    if(!endpoint->busy)
        return;
    
    if(sts & DIEPINT_xfercompl)
    {
        logf("usb-drv: xfer complete on EP%d IN", ep);
        endpoint->busy = false;
        int transfered = endpoint->len - endpoint->rem_len;
        logf("len=%d xfer=%d", endpoint->len, transfered);
        usb_core_transfer_complete(ep, USB_DIR_IN, 0, transfered);
        if(endpoint->wait)
        {
            endpoint->wait = false;
            semaphore_release(&endpoint->complete);
        }
    }
    if(sts & DIEPINT_timeout)
    {
        endpoint->busy = false;
        endpoint->status = 1;
        /* for safety, act as if no bytes were transfered */
        endpoint->len = 0;
        usb_core_transfer_complete(ep, USB_DIR_IN, 1, 0);
        if(endpoint->wait)
        {
            endpoint->wait = false;
            semaphore_release(&endpoint->complete);
        }
    }
}

static void handle_ep_out_int(int ep)
{
    struct usb_endpoint *endpoint = &endpoints[ep][DIR_OUT];
    unsigned long sts = DOEPINT(ep);
    /* clear interrupts */
    DOEPINT(ep) = sts;
    /* ignore if unrelated */
    if(!endpoint->busy)
        return;

    if(sts & DOEPINT_xfercompl)
    {
        logf("usb-drv: xfer complete on EP%d OUT", ep);
        endpoint->busy = false;
        /* works even for EP0 */
        int transfered = endpoint->len - endpoint->rem_len;
        logf("len=%d xfer=%d", endpoint->len, transfered);
        usb_core_transfer_complete(ep, USB_DIR_OUT, 0, transfered);
        if(endpoint->wait)
        {
            endpoint->wait = false;
            semaphore_release(&endpoint->complete);
        }
    }
    
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

/* interrupt service routine */
void INT_USB(void)
{
    /* some bits in GINTSTS can be set even though we didn't enable the interrupt source
     * so AND it with the actual mask */
    unsigned long sts = GINTSTS & GINTMSK;

    if(sts & GINTMSK_usbreset)
    {
        logf("usb-drv: bus reset");
        /* Reset Device Address */
        DCFG &= ~bitm(DCFG, devadr);
        /* Reset endpoints and flush fifos */
        reset_endpoints();
        /* Flush the Learning Queue */
        GRSTCTL = GRSTCTL_intknqflsh;
        /* notify bus reset */
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
    }

    if(sts & GINTMSK_otgintr)
    {
        logf("usb-drv: otg int");
        GOTGINT = 0xffffffff;
    }

    if(sts & GINTMSK_disconnect)
    {
        logf("usb-drv: disconnect");
        usb_drv_cancel_all_transfers();
    }

    if(sts & GINTMSK_rxstsqlvl)
    {
        logf("usb-drv: RX fifo non-empty");
        handle_rx_fifo();
    }

    if(sts & GINTMSK_nptxfempty)
    {
        logf("usb-drv: TX fifo empty");
        handle_tx_fifo();
    }

    if(sts & (GINTMSK_inepintr | GINTMSK_outepintr))
    {
        logf("usb-drv: IN ep interrupt");
        handle_ep_ints();
    }

    GINTSTS = sts;
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

static unsigned long usb_drv_mps_by_type(int type)
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

int usb_drv_request_endpoint(int type, int dir)
{
    int ep, ret = -1;
    unsigned i;

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

    logf("usb-drv: request endpoint (type=%d,dir=%s) -> %d", type,
        dir == USB_DIR_IN ? "IN" : "OUT", ret);

    unsigned long data = (type << DEPCTL_eptype_bitp)
                        | (usb_drv_mps_by_type(type) << DEPCTL_mps_bitp)
                        | DEPCTL_usbactep | DEPCTL_snak;

    if(dir == USB_DIR_IN) DIEPCTL(ep) = data;
    else DOEPCTL(ep) = data;

    return ret;
}

void usb_drv_release_endpoint(int ep)
{
    logf("usb-drv: release EP%d %s", EP_NUM(ep), EP_DIR(ep) == DIR_IN ? "IN" : "OUT");
    endpoints[EP_NUM(ep)][EP_DIR(ep)].active = false;
}

static int usb_drv_transfer(int ep, void *ptr, int len, bool dir_in, bool blocking)
{
    ep = EP_NUM(ep);

    logf("usb-drv: xfer EP%d, len=%d, dir_in=%d, blocking=%d", ep,
        len, dir_in, blocking);

    /* disable interrupts to avoid any race */
    int oldlevel = disable_irq_save();
    
    volatile unsigned long *epctl = dir_in ? &DIEPCTL(ep) : &DOEPCTL(ep);
    volatile unsigned long *eptsiz = dir_in ? &DIEPTSIZ(ep) : &DOEPTSIZ(ep);
    struct usb_endpoint *endpoint = &endpoints[ep][dir_in];
    #define DEPCTL  *epctl
    #define DEPTSIZ *eptsiz

    if(endpoint->busy)
    {
        logf("usb-drv: EP%d %s is already busy", ep, dir_in ? "IN" : "OUT");
        restore_irq(oldlevel);
        return -1;
    }

    endpoint->busy = true;
    endpoint->len = len;
    endpoint->wait = blocking;
    endpoint->status = -1;
    endpoint->buf_ptr = ptr;
    endpoint->rem_len = len;
    endpoint->mps = usb_drv_mps_by_type(extract(DEPCTL, eptype));

    DEPCTL &= ~DEPCTL_stall;
    DEPCTL |= DEPCTL_usbactep;

    int nb_packets = (len + endpoint->mps - 1) / endpoint->mps;

    /* If transfer size is a multiple of packet size, send a zero-length packet.
     * Also handles the case of ack transfer (len=0) */
    if((len % endpoint->mps) == 0)
        nb_packets++;
    DEPTSIZ = (nb_packets << DEPTSIZ_pkcnt_bitp) | len;

    logf("nb_packets=%d", nb_packets);

    DEPCTL |= DEPCTL_epena | DEPCTL_cnak;

    /* enable NPTX fifo empty interrupt */
    if(dir_in)
        GINTMSK |= GINTMSK_nptxfempty;

    /* restore interrupts */
    restore_irq(oldlevel);

    logf("depctl%d: %x", ep, dir_in ? DIEPCTL(ep) : DOEPCTL(ep));

    if(blocking)
    {
        semaphore_wait(&endpoint->complete, TIMEOUT_BLOCK);
        return endpoint->status;
    }

    return 0;

    #undef DEPCTL
    #undef DEPTSIZ
}

int usb_drv_recv(int ep, void *ptr, int len)
{
    return usb_drv_transfer(ep, ptr, len, false, false);
}

int usb_drv_send(int ep, void *ptr, int len)
{
    return usb_drv_transfer(ep, ptr, len, true, true);
}

int usb_drv_send_nonblocking(int ep, void *ptr, int len)
{
    return usb_drv_transfer(ep, ptr, len, true, false);
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
