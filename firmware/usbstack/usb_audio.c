/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 * $Id:  $
 *
 * Copyright (C) 20010 by Amaury Pouly
 *
 * All files in this archive are subject to the GNU General Public License.
 * See the file COPYING in the source tree root for full license agreement.
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
#include "sound.h"
#include "usb_class_driver.h"
#include "usb_audio_def.h"
#include "pcm_sampr.h"
#include "audio.h"
#include "pcm.h"
#include "file.h"
#include "sound.h"

#define LOGF_ENABLE
#include "logf.h"

//#define USB_AUDIO_USE_FEEDBACK_EP
//#define USB_AUDIO_OUTPUT_TO_FILE
#define USB_AUDIO_OUTPUT_TO_AUDIO
#define USB_AUDIO_USE_INTERMEDIATE_BUFFER
//#define USB_AUDIO_ANON_CONTROLS

/* Strings */
enum
{
    USB_AUDIO_CONTROL_STRING = 0,
    USB_AUDIO_STREAMING_STRING_1,
    USB_AUDIO_STREAMING_STRING_2,
    USB_INPUT_TERMINAL_STRING,
    USB_OUTPUT_TERMINAL_STRING,
    USB_FEATURE_UNIT_STRING
};

static const struct usb_string_descriptor __attribute__((aligned(2)))
    usb_string_audio_control =
{
    2 + 2 * 21,
    USB_DT_STRING,
    {'R', 'o', 'c', 'k', 'b', 'o', 'x', ' ',
     'A', 'u', 'd', 'i', 'o', ' ',
     'C', 'o', 'n', 't', 'r', 'o', 'l'}
};

static const struct usb_string_descriptor __attribute__((aligned(2)))
    usb_string_audio_streaming_1 =
{
    2 + 2 * 29,
    USB_DT_STRING,
    {'R', 'o', 'c', 'k', 'b', 'o', 'x', ' ',
     'D', 'u', 'm', 'm', 'y', ' ',
     'A', 'u', 'd', 'i', 'o', ' ',
     'S', 't', 'r', 'e', 'a', 'm', 'i', 'n', 'g'}
};

static const struct usb_string_descriptor __attribute__((aligned(2)))
    usb_string_audio_streaming_2 =
{
    2 + 2 * 30,
    USB_DT_STRING,
    {'R', 'o', 'c', 'k', 'b', 'o', 'x', ' ',
     'A', 'u', 'd', 'i', 'o', ' ',
     'S', 't', 'r', 'e', 'a', 'm', 'i', 'n', 'g', ' ',
     'O', 'u', 't', 'p', 'u', 't'}
};

static const struct usb_string_descriptor __attribute__((aligned(2)))
    usb_string_input_terminal =
{
    2 + 2 * 3,
    USB_DT_STRING,
    {'U', 'S', 'B'}
};

static const struct usb_string_descriptor __attribute__((aligned(2)))
    usb_string_output_terminal =
{
    2 + 2 * 8,
    USB_DT_STRING,
    {'L', 'i', 'n', 'e', '-', 'O', 'u', 't'}
};

static const struct usb_string_descriptor __attribute__((aligned(2)))
    usb_string_feature_unit =
{
    2 + 2 * 3,
    USB_DT_STRING,
    {'P', 'C', 'M'}
};

static const struct usb_string_descriptor* const usb_strings_list[]=
{
    [USB_AUDIO_CONTROL_STRING] = &usb_string_audio_control,
    [USB_AUDIO_STREAMING_STRING_1] = &usb_string_audio_streaming_1,
    [USB_AUDIO_STREAMING_STRING_2] = &usb_string_audio_streaming_2,
    [USB_INPUT_TERMINAL_STRING] = &usb_string_input_terminal,
    [USB_OUTPUT_TERMINAL_STRING] = &usb_string_output_terminal,
    [USB_FEATURE_UNIT_STRING] = &usb_string_feature_unit
};

#define USB_STRINGS_LIST_SIZE   (sizeof(usb_strings_list)/sizeof(struct usb_string_descriptor *))

/* Audio Control Interface */
static struct usb_interface_descriptor
    ac_interface =
{
    .bLength            = sizeof(struct usb_interface_descriptor),
    .bDescriptorType    = USB_DT_INTERFACE,
    .bInterfaceNumber   = 0,
    .bAlternateSetting  = 0,
    .bNumEndpoints      = 0,
    .bInterfaceClass    = USB_CLASS_AUDIO,
    .bInterfaceSubClass = USB_SUBCLASS_AUDIO_CONTROL,
    .bInterfaceProtocol = 0,
    .iInterface         = 0
};

/* Audio Control Terminals/Units*/
static struct usb_ac_header ac_header =
{
    .bLength            = USB_AC_SIZEOF_HEADER(2), /* two interfaces */
    .bDescriptorType    = USB_DT_CS_INTERFACE,
    .bDescriptorSubType = USB_AC_HEADER,
    .bcdADC             = 0x0100,
    .wTotalLength       = 0, /* fill later */
    .bInCollection      = 2, /* two interfaces */
    .baInterfaceNr      = {0, 0}, /* fill later */
};

enum
{
    AC_INPUT_TERMINAL_ID = 1,
    AC_OUTPUT_TERMINAL_ID = 2,
    AC_FEATURE_ID = 3
};

static struct usb_ac_input_terminal ac_input =
{
    .bLength            = sizeof(struct usb_ac_input_terminal),
    .bDescriptorType    = USB_DT_CS_INTERFACE,
    .bDescriptorSubType = USB_AC_INPUT_TERMINAL,
    .bTerminalId        = AC_INPUT_TERMINAL_ID,
    .wTerminalType      = USB_AC_TERMINAL_STREAMING,
    .bAssocTerminal     = 0,
    .bNrChannels        = 2,
    .wChannelConfig     = USB_AC_CHANNELS_LEFT_RIGHT_FRONT,
    .iChannelNames      = 0,
    .iTerminal          = 0,
};

static struct usb_ac_output_terminal ac_output =
{
    .bLength            = sizeof(struct usb_ac_output_terminal),
    .bDescriptorType    = USB_DT_CS_INTERFACE,
    .bDescriptorSubType = USB_AC_OUTPUT_TERMINAL,
    .bTerminalId        = AC_OUTPUT_TERMINAL_ID,
    .wTerminalType      = USB_AC_OUTPUT_TERMINAL_HEADPHONES,
    .bAssocTerminal     = 0,
    .bSourceId          = AC_FEATURE_ID,
    .iTerminal          = 0,
};

/* Feature Unit with 0 logical channel (only master) and 2 bytes(16 bits) per control (the minimum) */
DEFINE_USB_AC_FEATURE_UNIT(16, 0)

static struct usb_ac_feature_unit_16_0 ac_feature =
{
    .bLength            = sizeof(struct usb_ac_feature_unit_16_0),
    .bDescriptorType    = USB_DT_CS_INTERFACE,
    .bDescriptorSubType = USB_AC_FEATURE_UNIT,
    .bUnitId            = AC_FEATURE_ID,
    .bSourceId          = AC_INPUT_TERMINAL_ID,
    .bControlSize       = 2, /* by definition */
    .bmaControls        = {
        [0] = USB_AC_FU_MUTE | USB_AC_FU_VOLUME
    },
    .iFeature = 0
};

/* Audio Streaming Interface */
/* Alternative 0: no streaming */
static struct usb_interface_descriptor
    as_interface_alt_0 =
{
    .bLength            = sizeof(struct usb_interface_descriptor),
    .bDescriptorType    = USB_DT_INTERFACE,
    .bInterfaceNumber   = 0,
    .bAlternateSetting  = 0,
    .bNumEndpoints      = 0,
    .bInterfaceClass    = USB_CLASS_AUDIO,
    .bInterfaceSubClass = USB_SUBCLASS_AUDIO_STREAMING,
    .bInterfaceProtocol = 0,
    .iInterface         = 0
};

/* Alternative 1: output streaming */
static struct usb_interface_descriptor
    as_interface_alt_1 =
{
    .bLength            = sizeof(struct usb_interface_descriptor),
    .bDescriptorType    = USB_DT_INTERFACE,
    .bInterfaceNumber   = 0,
    .bAlternateSetting  = 1,
#ifdef USB_AUDIO_USE_FEEDBACK_EP
    .bNumEndpoints      = 2,
#else
    .bNumEndpoints      = 1,
#endif
    .bInterfaceClass    = USB_CLASS_AUDIO,
    .bInterfaceSubClass = USB_SUBCLASS_AUDIO_STREAMING,
    .bInterfaceProtocol = 0,
    .iInterface         = 0
};

/* Class Specific Audio Streaming Interface */
static struct usb_as_interface
    as_cs_interface =
{
    .bLength            = sizeof(struct usb_as_interface),
    .bDescriptorType    = USB_DT_CS_INTERFACE,
    .bDescriptorSubType = USB_AS_GENERAL,
    .bTerminalLink      = AC_INPUT_TERMINAL_ID,
    .bDelay             = 1,
    .wFormatTag         = USB_AS_FORMAT_TYPE_I_PCM
};

static struct usb_as_format_type_i_discrete
    as_format_type_i =
{
    .bLength            = USB_AS_SIZEOF_FORMAT_TYPE_I_DISCRETE(HW_NUM_FREQ),
    .bDescriptorType    = USB_DT_CS_INTERFACE,
    .bDescriptorSubType = USB_AS_FORMAT_TYPE,
    .bFormatType        = USB_AS_FORMAT_TYPE_I,
    .bNrChannels        = 2, /* Stereo */
    .bSubframeSize      = 2, /* 2 bytes per sample */
    .bBitResolution     = 16, /* all 16-bits are used */
    .bSamFreqType       = HW_NUM_FREQ,
    .tSamFreq           = {
        [0 ... HW_NUM_FREQ-1 ] = {0}, /* filled later */
    }
};

static struct usb_iso_audio_endpoint_descriptor
    out_iso_ep =
{
    .bLength          = sizeof(struct usb_iso_audio_endpoint_descriptor),
    .bDescriptorType  = USB_DT_ENDPOINT,
    .bEndpointAddress = USB_DIR_OUT, /* filled later */
    .bmAttributes     = USB_ENDPOINT_XFER_ISOC | USB_ENDPOINT_SYNC_ASYNC,
    .wMaxPacketSize   = 0, /* filled later */
    .bInterval        = 1, /* the spec says it must be 1 */
    .bRefresh         = 0,
    .bSynchAddress    = 0 /* filled later */
};

static struct usb_as_iso_endpoint
    as_out_iso_ep =
{
    .bLength            = sizeof(struct usb_as_iso_endpoint),
    .bDescriptorType    = USB_DT_CS_ENDPOINT,
    .bDescriptorSubType = USB_AS_EP_GENERAL,
    .bmAttributes       = USB_AS_EP_CS_SAMPLING_FREQ_CTL,
    .bLockDelayUnits    = 1, /* milliseconds */
    .wLockDelay         = 1 /* the minimum ! */
};

#ifdef USB_AUDIO_USE_FEEDBACK_EP
static struct usb_iso_audio_endpoint_descriptor
    out_iso_sync_ep =
{
    .bLength          = sizeof(struct usb_iso_audio_endpoint_descriptor),
    .bDescriptorType  = USB_DT_ENDPOINT,
    .bEndpointAddress = USB_DIR_IN, /* filled later */
    .bmAttributes     = USB_ENDPOINT_XFER_ISOC,
    .wMaxPacketSize   = 0, /* filled later */
    .bInterval        = 1,
    .bRefresh         = 1, /* minimum: 2ms */
    .bSynchAddress    = 0
};
#endif

static const struct usb_descriptor_header* const ac_cs_descriptors_list[] =
{
    (struct usb_descriptor_header *) &ac_header,
    (struct usb_descriptor_header *) &ac_input,
    (struct usb_descriptor_header *) &ac_output,
    (struct usb_descriptor_header *) &ac_feature,
};

#define AC_CS_DESCRIPTORS_LIST_SIZE (sizeof(ac_cs_descriptors_list)/sizeof(ac_cs_descriptors_list[0]))

static const struct usb_descriptor_header* const usb_descriptors_list[] =
{
    /* Audio Control */
    (struct usb_descriptor_header *) &ac_interface,
    (struct usb_descriptor_header *) &ac_header,
    (struct usb_descriptor_header *) &ac_input,
    (struct usb_descriptor_header *) &ac_output,
    (struct usb_descriptor_header *) &ac_feature,
    /* Audio Streaming */
    (struct usb_descriptor_header *) &as_interface_alt_0,
    (struct usb_descriptor_header *) &as_interface_alt_1,
    (struct usb_descriptor_header *) &as_cs_interface,
    (struct usb_descriptor_header *) &as_format_type_i,
    (struct usb_descriptor_header *) &out_iso_ep,
    (struct usb_descriptor_header *) &as_out_iso_ep,
#ifdef USB_AUDIO_USE_FEEDBACK_EP
    (struct usb_descriptor_header *) &out_iso_sync_ep,
#endif
};

#define USB_DESCRIPTORS_LIST_SIZE (sizeof(usb_descriptors_list)/sizeof(usb_descriptors_list[0]))

static int usb_interface; /* first interface */
static int usb_string_index; /* first string index */
static int usb_as_intf_alt; /* streaming interface alternate setting */

static int as_freq_idx; /* audio streaming frequency index (in hw_freq_sampr) */

static int out_iso_ep_adr; /* output isochronous endpoint */
static int in_iso_ep_adr; /* input isochronous endpoint */

#ifdef USB_AUDIO_OUTPUT_TO_FILE
static int raw_fd;
#endif

static unsigned char usb_buffer[128] USB_DEVBSS_ATTR;

#define USB_AUDIO_BUFFER_SIZE   1024*1000

#ifdef USB_AUDIO_USE_INTERMEDIATE_BUFFER
static unsigned char *usb_audio_buffer;
static int usb_audio_buffer_start;
static int usb_audio_buffer_end;
static bool usb_audio_underflow;
#endif

#define USB_AUDIO_NB_SLOTS          16
#define USB_AUDIO_SLOT_SIZE         1024
static unsigned char *usb_audio_slot_buffers[USB_AUDIO_NB_SLOTS];
static unsigned char usb_audio_ep_slots[2][USB_AUDIO_NB_SLOTS * USB_DRV_SLOT_SIZE] USB_DRV_SLOT_ATTR;

#ifdef USB_AUDIO_USE_FEEDBACK_EP
static unsigned char usb_audio_feedback_buffer[4] USB_DEVBSS_ATTR;
#endif

static void encode3(uint8_t arr[3], unsigned long freq)
{
    /* ugly */
    arr[0] = freq & 0xff;
    arr[1] = (freq >> 8) & 0xff;
    arr[2] = (freq >> 16) & 0xff;
}

static unsigned long decode3(uint8_t arr[3])
{
    /*logf("arr=[0x%x,0x%x,0x%x]", arr[0], arr[1], arr[2]);*/
    return arr[0] | (arr[1] << 8) | (arr[2] << 16);
}

static void set_sampling_frequency(unsigned long f)
{
    int i = 0;

    while((i + 1) < HW_NUM_FREQ && hw_freq_sampr[i + 1] <= f)
        i++;

    if((i + 1) < HW_NUM_FREQ && (hw_freq_sampr[i + 1] - f) < (f - hw_freq_sampr[i]))
        i++;
    as_freq_idx = i;
    
    logf("usbaudio: set sampling frequency to %lu Hz, best match is %lu Hz", f, hw_freq_sampr[as_freq_idx]);
}

static unsigned long get_sampling_frequency(void)
{
    return hw_freq_sampr[as_freq_idx];
}

void usb_audio_init(void)
{
    unsigned int i;
    /* initialized tSamFreq array */
    for(i = 0; i < HW_NUM_FREQ; i++)
        encode3(as_format_type_i.tSamFreq[i], hw_freq_sampr[i]);

    unsigned char * audio_buffer;
    size_t bufsize;
    
    audio_buffer = audio_get_buffer(false, &bufsize);
#ifdef UNCACHED_ADDR
    audio_buffer = (void *)UNCACHED_ADDR((unsigned int)(audio_buffer+31) & 0xffffffe0);
#else
    audio_buffer = (void *)((unsigned int)(audio_buffer+31) & 0xffffffe0);
#endif
    cpucache_invalidate();
    
#ifdef USB_AUDIO_USE_INTERMEDIATE_BUFFER
    usb_audio_buffer = audio_buffer;
    for(i = 0; i < USB_AUDIO_NB_SLOTS; i++)
        usb_audio_slot_buffers[i] = audio_buffer + USB_AUDIO_BUFFER_SIZE + i * USB_AUDIO_SLOT_SIZE;
#endif
}

int usb_audio_request_endpoints(struct usb_class_driver *drv)
{
    out_iso_ep_adr = usb_core_request_endpoint(USB_ENDPOINT_XFER_ISOC, USB_DIR_OUT, drv);
    if(out_iso_ep_adr < 0)
    {
        logf("usbaudio: cannot get an out iso endpoint");
        return -1;
    }

    in_iso_ep_adr = usb_core_request_endpoint(USB_ENDPOINT_XFER_ISOC, USB_DIR_IN, drv);
    if(in_iso_ep_adr < 0)
    {
        usb_core_release_endpoint(out_iso_ep_adr);
        logf("usbaudio: cannot get an out iso endpoint");
        return -1;
    }

    logf("usbaudio: iso ep is 0x%x, sync ep is 0x%x", out_iso_ep_adr, in_iso_ep_adr);

    out_iso_ep.bEndpointAddress = out_iso_ep_adr;
    out_iso_ep.bSynchAddress = in_iso_ep_adr;

#ifdef USB_AUDIO_USE_FEEDBACK_EP
    out_iso_sync_ep.bEndpointAddress = in_iso_ep_adr;
#endif
    
    return 0;
}

int usb_audio_set_first_string_index(int string_index)
{
    usb_string_index = string_index;

    ac_interface.iInterface = string_index + USB_AUDIO_CONTROL_STRING;
    as_interface_alt_0.iInterface = string_index + USB_AUDIO_STREAMING_STRING_1;
    as_interface_alt_1.iInterface = string_index + USB_AUDIO_STREAMING_STRING_2;
    #ifndef USB_AUDIO_ANON_CONTROLS
    ac_input.iTerminal = string_index + USB_INPUT_TERMINAL_STRING;
    ac_output.iTerminal = string_index + USB_OUTPUT_TERMINAL_STRING;
    ac_feature.iFeature = string_index + USB_FEATURE_UNIT_STRING;
    #endif /* USB_AUDIO_ANON_CONTROLS */

    return string_index + USB_STRINGS_LIST_SIZE;
}

const struct usb_string_descriptor *usb_audio_get_string_descriptor(int string_index)
{
    logf("usbaudio: get string %d", string_index);
    if(string_index < usb_string_index ||
            string_index >= (int)(usb_string_index + USB_STRINGS_LIST_SIZE))
        return NULL;
    else
        return usb_strings_list[string_index - usb_string_index];
}

int usb_audio_set_first_interface(int interface)
{
    usb_interface = interface;
    return interface + 2; /* Audio Control and Audio Streaming */
}

int usb_audio_get_config_descriptor(unsigned char *dest, int max_packet_size)
{
    (void)max_packet_size;
    unsigned int i;
    unsigned char *orig_dest = dest;

    /** Configuration */
    
    /* header */
    ac_header.baInterfaceNr[0] = usb_interface;
    ac_header.baInterfaceNr[1] = usb_interface + 1;

    /* audio control interface */
    ac_interface.bInterfaceNumber = usb_interface;

    /* compute total size of AC headers*/
    for(i = 0; i < AC_CS_DESCRIPTORS_LIST_SIZE; i++)
        ac_header.wTotalLength += ac_cs_descriptors_list[i]->bLength;

    /* audio streaming */
    as_interface_alt_0.bInterfaceNumber = usb_interface + 1;
    as_interface_alt_1.bInterfaceNumber = usb_interface + 1;

    /* endpoints */
    out_iso_ep.wMaxPacketSize = usb_drv_max_endpoint_packet_size(out_iso_ep_adr) | 0 << 11;
#ifdef USB_AUDIO_USE_FEEDBACK_EP
    out_iso_sync_ep.wMaxPacketSize = usb_drv_max_endpoint_packet_size(out_iso_sync_ep_adr);
#endif

    /** Packing */

    for(i = 0; i < USB_DESCRIPTORS_LIST_SIZE; i++)
    {
        memcpy(dest, usb_descriptors_list[i], usb_descriptors_list[i]->bLength);
        dest += usb_descriptors_list[i]->bLength;
    }

    return dest - orig_dest;
}

static void usb_audio_pcm_get_more(unsigned char **start, size_t *size)
{
    #if defined(USB_AUDIO_USE_INTERMEDIATE_BUFFER) && defined(USB_AUDIO_OUTPUT_TO_AUDIO)
    /* copy start and end to avoid any modification at the same time */
    /* NOTE: the filler can only change the end pointer */
    int buf_start = usb_audio_buffer_start;
    int buf_end = usb_audio_buffer_end;
    
    *start = NULL;
    *size = 0;

    /*
    logf("usbaudio: get more !");
    logf("usbaudio: start=%d end=%d", buf_start, buf_end);
    */

    if(buf_start == buf_end)
    {
        /* when audio underflow, the callback is not called anymore
         * so it needs to be restarted */
        usb_audio_underflow = true;
        logf("UNDERFLOW UNDERFLOW");
        return;
    }
    
    if(buf_start <= buf_end)
    {
        *start = usb_audio_buffer + buf_start;
        *size = buf_end - buf_start;
        usb_audio_buffer_start = buf_end;
    }
    else
    {
        *start = usb_audio_buffer + buf_start;
        *size = USB_AUDIO_BUFFER_SIZE - buf_start;
        usb_audio_buffer_start = 0;
    }

    /*
    logf("=> start=%d end=%d", usb_audio_buffer_start, usb_audio_buffer_end);
    */
    #endif
}

static void usb_audio_start(void)
{
    usb_audio_buffer_start = 0;
    usb_audio_buffer_end = 0;
    
    #ifdef USB_AUDIO_OUTPUT_TO_AUDIO
    audio_set_input_source(AUDIO_SRC_PLAYBACK, SRCF_PLAYBACK);
    audio_set_output_source(AUDIO_SRC_PLAYBACK);
    pcm_set_frequency(hw_freq_sampr[as_freq_idx]);
    pcm_play_data(&usb_audio_pcm_get_more, NULL, 0);
    if(!pcm_is_playing())
        pcm_play_stop();
    #endif
}

static void usb_audio_stop(void)
{
    #ifdef USB_AUDIO_OUTPUT_TO_AUDIO
    if(pcm_is_playing())
        pcm_play_stop();
    #endif
}

int usb_audio_set_interface(int intf, int alt)
{
    logf("usbaudio: use AS interface alternate %d", alt);
    if(intf != (usb_interface + 1) || alt < 0 || alt > 1)
        return -1;
    usb_as_intf_alt = alt;

    if(usb_as_intf_alt == 1)
        usb_audio_start();
    else
        usb_audio_stop();

    return 0;
}

int usb_audio_get_interface(int intf)
{
    logf("usbaudio: get AS interface alternate: %d", usb_as_intf_alt);
    if(intf != (usb_interface + 1))
        return -1;
    else
        return usb_as_intf_alt;
}

static bool usb_audio_endpoint_request(struct usb_ctrlrequest* req)
{
    bool handled = false;
    /* only support sampling frequency */
    if(req->wValue != (USB_AS_EP_CS_SAMPLING_FREQ_CTL << 8))
    {
        logf("usbaudio: endpoint only handle sampling frequency control");
        return false;
    }
    
    switch(req->bRequest)
    {
        case USB_AC_SET_CUR:
            if(req->wLength != 3)
            {
                logf("usbaudio: bad length for SET_CUR");
                break;
            }
            logf("usbaudio: SET_CUR sampling freq");
            usb_drv_recv_blocking(EP_CONTROL, usb_buffer, req->wLength);
            /* FIXME: do we have to wait for completion or it works out of the box here ? */
            set_sampling_frequency(decode3(usb_buffer));
            usb_drv_send_blocking(EP_CONTROL, NULL, 0); /* ack */
            handled = true;
            break;
        case USB_AC_GET_CUR:
            if(req->wLength != 3)
            {
                logf("usbaudio: bad length for GET_CUR");
                break;
            }
            logf("usbaudio: GET_CUR sampling freq");
            encode3(usb_buffer, get_sampling_frequency());
            usb_drv_send_blocking(EP_CONTROL, usb_buffer, req->wLength);
            usb_drv_recv_blocking(EP_CONTROL, NULL, 0); /* ack */
            handled = true;
            break;
        default:
            logf("usbaudio: unhandled ep req 0x%x", req->bRequest);
    }
    
    return handled;
}

static bool feature_unit_set_mute(int value, uint8_t cmd)
{
    if(cmd != USB_AC_CUR_REQ)
    {
        logf("usbaudio: feature unit MUTE control only has a CUR setting");
        return false;
    }

    if(value == 1)
    {
        logf("usbaudio: mute !");
        return true;
    }
    else if(value == 0)
    {
        logf("usbaudio: not muted !");
        return true;
    }
    else
    {
        logf("usbaudio: invalid value for CUR setting of feature unit (%d)", value);
        return false;
    }
}

static bool feature_unit_get_mute(int *value, uint8_t cmd)
{
    if(cmd != USB_AC_CUR_REQ)
    {
        logf("usbaudio: feature unit MUTE control only has a CUR setting");
        return false;
    }

    *value = 0;
    return true;
}

static int db_to_usb_audio_volume(int db)
{
    return (int)(unsigned short)(signed short)(db*256);
}

static int usb_audio_volume_to_db(int vol)
{
    return ((signed short)(unsigned short)vol)/256;
}

#if defined(LOGF_ENABLE) && defined(ROCKBOX_HAS_LOGF)
static const char *usb_audio_ac_ctl_req_str(uint8_t cmd)
{
    switch(cmd)
    {
        case USB_AC_CUR_REQ: return "CUR";
        case USB_AC_MIN_REQ: return "MIN";
        case USB_AC_MAX_REQ: return "MAX";
        case USB_AC_RES_REQ: return "RES";
        case USB_AC_MEM_REQ: return "MEM";
        default: return "<unknown>";
    }
}
#endif

static bool feature_unit_set_volume(int value, uint8_t cmd)
{
    if(cmd != USB_AC_CUR_REQ)
    {
        logf("usbaudio: feature unit VOLUME doesn't support %s setting", usb_audio_ac_ctl_req_str(cmd));
        return false;
    }

    logf("usbaudio: set volume=%d dB)", usb_audio_volume_to_db(value));
    
    sound_set_volume(usb_audio_volume_to_db(value));
    return true;
}

static bool feature_unit_get_volume(int *value, uint8_t cmd)
{
    switch(cmd)
    {
        case USB_AC_CUR_REQ: *value = db_to_usb_audio_volume(sound_get_volume()); break;
        case USB_AC_MIN_REQ: *value = db_to_usb_audio_volume(sound_min(SOUND_VOLUME)); break;
        case USB_AC_MAX_REQ: *value = db_to_usb_audio_volume(sound_max(SOUND_VOLUME)); break;
        case USB_AC_RES_REQ: *value = db_to_usb_audio_volume(sound_steps(SOUND_VOLUME)); break;
        default:
            logf("usbaudio: feature unit VOLUME doesn't support %s setting", usb_audio_ac_ctl_req_str(cmd));
            return false;
    }
    
    logf("usbaudio: get %s volume=%d dB)", usb_audio_ac_ctl_req_str(cmd), usb_audio_volume_to_db(*value));
    return true;
}

static bool usb_audio_set_get_feature_unit(struct usb_ctrlrequest* req)
{
    int channel = req->wValue & 0xff;
    int selector = req->wValue >> 8;
    uint8_t cmd = (req->bRequest & ~USB_AC_GET_REQ);
    int value = 0;
    int i;
    bool handled;

    /* master channel only */
    if(channel != 0)
    {
        logf("usbaudio: set/get on feature unit only apply to master channel (%d)", channel);
        return false;
    }
    /* selectors */
    /* all send/received values are integers so already read data if necessary and store in it in an integer */
    if(req->bRequest & USB_AC_GET_REQ)
    {
        /* get */
        switch(selector)
        {
            case USB_AC_FU_MUTE:
                handled = (req->wLength == 1) && feature_unit_get_mute(&value, cmd);
                break;
            case USB_AC_VOLUME_CONTROL:
                handled = (req->wLength == 2) && feature_unit_get_volume(&value, cmd);
                break;
            default:
                handled = false;
                logf("usbaudio: unhandled control selector of feature unit (0x%x)", selector);
                break;
        }

        if(!handled)
        {
            logf("usbaudio: unhandled get control 0x%x selector 0x%x of feature unit", cmd, selector);
            return false;
        }
        
        if(req->wLength == 0 || req->wLength > 4)
        {
            logf("usbaudio: get data payload size is invalid (%d)", req->wLength);
            return false;
        }

        for(i = 0; i < req->wLength; i++)
            usb_buffer[i] = (value >> (8 * i)) & 0xff;
        
        usb_drv_send_blocking(EP_CONTROL, usb_buffer, req->wLength);
        usb_drv_recv_blocking(EP_CONTROL, NULL, 0); /* ack */
        return true;
    }
    else
    {
        /* set */
        if(req->wLength == 0 || req->wLength > 4)
        {
            logf("usbaudio: set data payload size is invalid (%d)", req->wLength);
            return false;
        }

        /* receive */
        usb_drv_recv_blocking(EP_CONTROL, usb_buffer, req->wLength);
        for(i = 0; i < req->wLength; i++)
            value = value | (usb_buffer[i] << (i * 8));
        
        switch(selector)
        {
            case USB_AC_FU_MUTE:
                handled = (req->wLength == 1) && feature_unit_set_mute(value, cmd);
                break;
            case USB_AC_VOLUME_CONTROL:
                handled = (req->wLength == 2) && feature_unit_set_volume(value, cmd);
                break;
            default:
                handled = false;
                logf("usbaudio: unhandled control selector of feature unit (0x%x)", selector);
                break;
        }

        if(!handled)
        {
            logf("usbaudio: unhandled set control 0x%x selector 0x%x of feature unit", cmd, selector);
            return false;
        }

        /* ack */
        usb_drv_send_blocking(EP_CONTROL, NULL, 0);
        return true;
    }
}

static bool usb_audio_set_get_request(struct usb_ctrlrequest* req)
{
    switch(req->wIndex >> 8)
    {
        case AC_FEATURE_ID:
            return usb_audio_set_get_feature_unit(req);
        default:
            logf("usbaudio: unhandled set/get on entity %d", req->wIndex >> 8);
            return false;
    }
}

static bool usb_audio_interface_request(struct usb_ctrlrequest* req)
{
    switch(req->bRequest)
    {
        case USB_AC_SET_CUR: case USB_AC_SET_MIN: case USB_AC_SET_MAX: case USB_AC_SET_RES:
        case USB_AC_SET_MEM: case USB_AC_GET_CUR: case USB_AC_GET_MIN: case USB_AC_GET_MAX:
        case USB_AC_GET_RES: case USB_AC_GET_MEM:
            return usb_audio_set_get_request(req);
        default:
            logf("usbaudio: unhandled intf req 0x%x", req->bRequest);
            return false;
    }
}

bool usb_audio_control_request(struct usb_ctrlrequest* req)
{
    switch(req->bRequestType & USB_RECIP_MASK)
    {
        case USB_RECIP_ENDPOINT:
            return usb_audio_endpoint_request(req);
        case USB_RECIP_INTERFACE:
            return usb_audio_interface_request(req);
        default:
            logf("usbaudio: unhandeld req 0x%x", req->bRequest);
            return false;
    }
}

void usb_audio_init_connection(void)
{
    int i;
    
    logf("usbaudio: init connection");
    
    usb_as_intf_alt = 0;
    set_sampling_frequency(HW_SAMPR_DEFAULT);

    cpu_boost(true);

    #ifdef USB_AUDIO_OUTPUT_TO_FILE
    raw_fd = open("/usb_audio.raw", O_RDWR | O_CREAT | O_TRUNC);
    if(raw_fd < 0)
        logf("usbaudio: cannot open file for recording");
    #endif
    
    usb_drv_select_endpoint_mode(out_iso_ep_adr, USB_DRV_ENDPOINT_MODE_REPEAT);
    usb_drv_allocate_slots(out_iso_ep_adr, USB_AUDIO_NB_SLOTS, usb_audio_ep_slots);
    for(i = 0; i < USB_AUDIO_NB_SLOTS; i++)
        usb_drv_fill_repeat_slot(out_iso_ep_adr, i, usb_audio_slot_buffers[i], USB_AUDIO_SLOT_SIZE);
    usb_drv_start_repeat(out_iso_ep_adr);
}

void usb_audio_disconnect(void)
{
    logf("usbaudio: disconnect");
    #ifdef USB_AUDIO_OUTPUT_TO_FILE
    if(raw_fd >= 0)
        close(raw_fd);
    #endif
    
    usb_audio_stop();

    cpu_boost(false);
}

void usb_audio_transfer_complete(int ep, int dir, int status, int length, void *buffer)
{
    (void) dir;
    int actual_length;

    /*
    logf("usbaudio: xfer %s", status < 0 ? "failure" : "completed");
    logf("usbaudio: %d bytes transfered", length);
    */

    if(ep == out_iso_ep_adr && usb_as_intf_alt == 1)
    {
        if(status == 0)
        {
            //logf("usbaudio: start=%d end=%d length=%d", usb_audio_buffer_start, usb_audio_buffer_end, length);
            while(length > 0)
            {
                actual_length = MIN(length, USB_AUDIO_BUFFER_SIZE - usb_audio_buffer_end);
                memcpy(usb_audio_buffer + usb_audio_buffer_end, buffer, actual_length);

                usb_audio_buffer_end += actual_length;
                buffer = (unsigned char *)buffer + actual_length;
                if(usb_audio_buffer_end >= USB_AUDIO_BUFFER_SIZE)
                    usb_audio_buffer_end = 0;
                length -= actual_length;
            }
            //logf("=> start=%d end=%d", usb_audio_buffer_start, usb_audio_buffer_end);
            
            #if defined(USB_AUDIO_OUTPUT_TO_FILE)
            if(raw_fd >= 0 && usb_audio_buffer_start < usb_audio_buffer_end && 
                    (usb_audio_buffer_end - usb_audio_buffer_start) >= USB_AUDIO_BUFFER_SIZE/2)
            {
                if(write(raw_fd, usb_audio_buffer + usb_audio_buffer_start, usb_audio_buffer_end - usb_audio_buffer_start) < 0)
                    logf("usbaudio: write failed");
                usb_audio_buffer_start = usb_audio_buffer_end = 0;
            }
            #endif

            #ifdef USB_AUDIO_OUTPUT_TO_AUDIO
            /* only start playback if there is sufficient data (to avoid repeated underflow) */
            if(usb_audio_underflow)
            {
                if(usb_audio_buffer_start <= usb_audio_buffer_end)
                    actual_length = usb_audio_buffer_end - usb_audio_buffer_start;
                else
                    actual_length = usb_audio_buffer_end + USB_AUDIO_BUFFER_SIZE - usb_audio_buffer_start;

                if(actual_length >= 200*1024)
                {
                    pcm_play_data(&usb_audio_pcm_get_more, NULL, 0);
                    usb_audio_underflow = false;
                }
            }
            #endif
        }
    }
}
