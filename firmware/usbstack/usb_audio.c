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

#define USB_AUDIO_USE_INTERMEDIATE_BUFFER
//#define USE_AUDIO_INTF_HAVE_NAMES

/* Strings */
enum
{
    USB_AUDIO_CONTROL_STRING = 0,
    USB_AUDIO_STREAMING_STRING_IDLE_PLAYBACK,
    USB_AUDIO_STREAMING_STRING_PLAYBACK,
};

#ifdef USE_AUDIO_INTF_HAVE_NAMES
static const struct usb_string_descriptor __attribute__((aligned(2)))
    usb_string_audio_control =
{
    2 + 2 * 13,
    USB_DT_STRING,
    {'A', 'u', 'd', 'i', 'o', ' ',
     'C', 'o', 'n', 't', 'r', 'o', 'l'}
};

static const struct usb_string_descriptor __attribute__((aligned(2)))
    usb_string_audio_streaming_idle_playback =
{
    2 + 2 * 13,
    USB_DT_STRING,
    {'I', 'd', 'l', 'e', ' ',
     'P', 'l' ,'a', 'y', 'b', 'a', 'c', 'k'}
};

static const struct usb_string_descriptor __attribute__((aligned(2)))
    usb_string_audio_streaming_playback =
{
    2 + 2 * 8,
    USB_DT_STRING,
    {'P', 'l' ,'a', 'y', 'b', 'a', 'c', 'k'}
};
#endif

static const struct usb_string_descriptor* const usb_strings_list[]=
{
    #ifdef USE_AUDIO_INTF_HAVE_NAMES
    [USB_AUDIO_CONTROL_STRING] = &usb_string_audio_control,
    [USB_AUDIO_STREAMING_STRING_IDLE_PLAYBACK] = &usb_string_audio_streaming_idle_playback,
    [USB_AUDIO_STREAMING_STRING_PLAYBACK] = &usb_string_audio_streaming_playback,
    #endif
};

#define USB_STRINGS_LIST_SIZE   (sizeof(usb_strings_list) / sizeof(struct usb_string_descriptor *))

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
    .bLength            = USB_AC_SIZEOF_HEADER(1), /* one interface */
    .bDescriptorType    = USB_DT_CS_INTERFACE,
    .bDescriptorSubType = USB_AC_HEADER,
    .bcdADC             = 0x0100,
    .wTotalLength       = 0, /* fill later */
    .bInCollection      = 1, /* one interface */
    .baInterfaceNr      = {0}, /* fill later */
};

enum
{
    AC_PLAYBACK_INPUT_TERMINAL_ID = 1,
    AC_PLAYBACK_FEATURE_ID,
    AC_PLAYBACK_OUTPUT_TERMINAL_ID,
};

static struct usb_ac_input_terminal ac_playback_input =
{
    .bLength            = sizeof(struct usb_ac_input_terminal),
    .bDescriptorType    = USB_DT_CS_INTERFACE,
    .bDescriptorSubType = USB_AC_INPUT_TERMINAL,
    .bTerminalId        = AC_PLAYBACK_INPUT_TERMINAL_ID,
    .wTerminalType      = USB_AC_TERMINAL_STREAMING,
    .bAssocTerminal     = 0,
    .bNrChannels        = 2,
    .wChannelConfig     = USB_AC_CHANNELS_LEFT_RIGHT_FRONT,
    .iChannelNames      = 0,
    .iTerminal          = 0,
};

static struct usb_ac_output_terminal ac_playback_output =
{
    .bLength            = sizeof(struct usb_ac_output_terminal),
    .bDescriptorType    = USB_DT_CS_INTERFACE,
    .bDescriptorSubType = USB_AC_OUTPUT_TERMINAL,
    .bTerminalId        = AC_PLAYBACK_OUTPUT_TERMINAL_ID,
    .wTerminalType      = USB_AC_OUTPUT_TERMINAL_HEADPHONES,
    .bAssocTerminal     = 0,
    .bSourceId          = AC_PLAYBACK_FEATURE_ID,
    .iTerminal          = 0,
};

/* Feature Unit with 2 logical channels and 1 byte(8 bits) per control */
DEFINE_USB_AC_FEATURE_UNIT(8, 2)

static struct usb_ac_feature_unit_8_2 ac_playback_feature =
{
    .bLength            = sizeof(struct usb_ac_feature_unit_8_2),
    .bDescriptorType    = USB_DT_CS_INTERFACE,
    .bDescriptorSubType = USB_AC_FEATURE_UNIT,
    .bUnitId            = AC_PLAYBACK_FEATURE_ID,
    .bSourceId          = AC_PLAYBACK_INPUT_TERMINAL_ID,
    .bControlSize       = 1, /* by definition */
    .bmaControls        = {
        [0] = /*USB_AC_FU_MUTE |*/ USB_AC_FU_VOLUME,
        [1] = 0,
        [2] = 0
    },
    .iFeature = 0
};

/* Audio Streaming Interface */
/* Alternative: no streaming */
static struct usb_interface_descriptor
    as_interface_alt_idle_playback =
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

/* Alternative: output streaming */
static struct usb_interface_descriptor
    as_interface_alt_playback =
{
    .bLength            = sizeof(struct usb_interface_descriptor),
    .bDescriptorType    = USB_DT_INTERFACE,
    .bInterfaceNumber   = 0,
    .bAlternateSetting  = 1,
    .bNumEndpoints      = 1,
    .bInterfaceClass    = USB_CLASS_AUDIO,
    .bInterfaceSubClass = USB_SUBCLASS_AUDIO_STREAMING,
    .bInterfaceProtocol = 0,
    .iInterface         = 0
};

/* Class Specific Audio Streaming Interface */
static struct usb_as_interface
    as_playback_cs_interface =
{
    .bLength            = sizeof(struct usb_as_interface),
    .bDescriptorType    = USB_DT_CS_INTERFACE,
    .bDescriptorSubType = USB_AS_GENERAL,
    .bTerminalLink      = AC_PLAYBACK_INPUT_TERMINAL_ID,
    .bDelay             = 1,
    .wFormatTag         = USB_AS_FORMAT_TYPE_I_PCM
};

static struct usb_as_format_type_i_discrete
    as_playback_format_type_i =
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
    .bmAttributes     = USB_ENDPOINT_XFER_ISOC | USB_ENDPOINT_SYNC_ADAPTIVE,
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
    .bLockDelayUnits    = 0, /* undefined */
    .wLockDelay         = 0 /* undefined */
};

static const struct usb_descriptor_header* const ac_cs_descriptors_list[] =
{
    (struct usb_descriptor_header *) &ac_header,
    (struct usb_descriptor_header *) &ac_playback_input,
    (struct usb_descriptor_header *) &ac_playback_output,
    (struct usb_descriptor_header *) &ac_playback_feature
};

#define AC_CS_DESCRIPTORS_LIST_SIZE (sizeof(ac_cs_descriptors_list)/sizeof(ac_cs_descriptors_list[0]))

static const struct usb_descriptor_header* const usb_descriptors_list[] =
{
    /* Audio Control */
    (struct usb_descriptor_header *) &ac_interface,
    (struct usb_descriptor_header *) &ac_header,
    (struct usb_descriptor_header *) &ac_playback_input,
    (struct usb_descriptor_header *) &ac_playback_feature,
    (struct usb_descriptor_header *) &ac_playback_output,
    /* Audio Streaming */
    /*   Idle Playback */
    (struct usb_descriptor_header *) &as_interface_alt_idle_playback,
    /*   Playback */
    (struct usb_descriptor_header *) &as_interface_alt_playback,
    (struct usb_descriptor_header *) &as_playback_cs_interface,
    (struct usb_descriptor_header *) &as_playback_format_type_i,
    (struct usb_descriptor_header *) &out_iso_ep,
    (struct usb_descriptor_header *) &as_out_iso_ep,
};

#define USB_DESCRIPTORS_LIST_SIZE (sizeof(usb_descriptors_list)/sizeof(usb_descriptors_list[0]))

static int usb_interface; /* first interface */
static int usb_string_index; /* first string index */
static int usb_as_playback_intf_alt; /* playback streaming interface alternate setting */

static int as_playback_freq_idx; /* audio playback streaming frequency index (in hw_freq_sampr) */

static int out_iso_ep_adr; /* output isochronous endpoint */
static int in_iso_ep_adr; /* input isochronous endpoint */

static unsigned char usb_buffer[128] USB_DEVBSS_ATTR;

#ifdef USB_AUDIO_USE_INTERMEDIATE_BUFFER
/* Size of the intermediate audio buffer used for playback */
#define PLAYBACK_AUDIO_BUFFER_SIZE      40 * 1024
/* When playback start, the code doesn't start pcm output directly to avoid early overflow.
 * This setting control the amount of data that is initial buffered before playback starts. */
#define PLAYBACK_INITIAL_BUFFERING_SIZE 1024
#endif

#ifdef USB_AUDIO_USE_INTERMEDIATE_BUFFER
static unsigned char *playback_audio_buffer;
static int playback_audio_buffer_start;
static int playback_audio_buffer_end;
static bool playback_audio_underflow;
#endif

#define USB_AUDIO_NB_SLOTS          16
#define USB_AUDIO_SLOT_SIZE         1024
static unsigned char *usb_audio_slot_buffers[USB_AUDIO_NB_SLOTS];
static unsigned char usb_audio_ep_slots[2][USB_AUDIO_NB_SLOTS * USB_DRV_SLOT_SIZE] USB_DRV_SLOT_ATTR;

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

static void set_playback_sampling_frequency(unsigned long f)
{
    int i = 0;

    while((i + 1) < HW_NUM_FREQ && hw_freq_sampr[i + 1] <= f)
        i++;

    if((i + 1) < HW_NUM_FREQ && (hw_freq_sampr[i + 1] - f) < (f - hw_freq_sampr[i]))
        i++;
    as_playback_freq_idx = i;
    
    logf("usbaudio: set playback sampling frequency to %lu Hz", hw_freq_sampr[as_playback_freq_idx]);
}

static unsigned long get_playback_sampling_frequency(void)
{
    return hw_freq_sampr[as_playback_freq_idx];
}

void usb_audio_init(void)
{
    unsigned int i;
    /* initialized tSamFreq array */
    for(i = 0; i < HW_NUM_FREQ; i++)
        encode3(as_playback_format_type_i.tSamFreq[i], hw_freq_sampr[i]);

    unsigned char *audio_buffer;
    size_t bufsize;
    
    audio_buffer = audio_get_buffer(false, &bufsize);
#ifdef UNCACHED_ADDR
    audio_buffer = (void *)UNCACHED_ADDR((unsigned int)(audio_buffer+31) & 0xffffffe0);
#else
    audio_buffer = (void *)((unsigned int)(audio_buffer+31) & 0xffffffe0);
#endif
    cpucache_invalidate();
    
#ifdef USB_AUDIO_USE_INTERMEDIATE_BUFFER
    playback_audio_buffer = audio_buffer;
    audio_buffer += PLAYBACK_AUDIO_BUFFER_SIZE;
#endif

    for(i = 0; i < USB_AUDIO_NB_SLOTS; i++)
    {
        usb_audio_slot_buffers[i] = audio_buffer;
        audio_buffer += USB_AUDIO_SLOT_SIZE;
    }
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

    logf("usbaudio: iso out ep is 0x%x, in ep is 0x%x", out_iso_ep_adr, in_iso_ep_adr);

    out_iso_ep.bEndpointAddress = out_iso_ep_adr;
    out_iso_ep.bSynchAddress = 0;
    
    return 0;
}

int usb_audio_set_first_string_index(int string_index)
{
    usb_string_index = string_index;

    return string_index;
    #ifdef USE_AUDIO_INTF_HAVE_NAMES
    ac_interface.iInterface = string_index + USB_AUDIO_CONTROL_STRING;
    as_interface_alt_idle_playback.iInterface = string_index + USB_AUDIO_STREAMING_STRING_IDLE_PLAYBACK;
    as_interface_alt_playback.iInterface = string_index + USB_AUDIO_STREAMING_STRING_PLAYBACK;
    #endif
    
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
    logf("usbaudio: usb_interface=%d", usb_interface);
    return interface + 2; /* Audio Control and Audio Streaming */
}

int usb_audio_get_config_descriptor(unsigned char *dest, int max_packet_size)
{
    (void)max_packet_size;
    unsigned int i;
    unsigned char *orig_dest = dest;

    /** Configuration */
    
    /* header */
    ac_header.baInterfaceNr[0] = usb_interface + 1;

    /* audio control interface */
    ac_interface.bInterfaceNumber = usb_interface;

    /* compute total size of AC headers*/
    ac_header.wTotalLength = 0;
    for(i = 0; i < AC_CS_DESCRIPTORS_LIST_SIZE; i++)
        ac_header.wTotalLength += ac_cs_descriptors_list[i]->bLength;

    /* audio streaming */
    as_interface_alt_idle_playback.bInterfaceNumber = usb_interface + 1;
    as_interface_alt_playback.bInterfaceNumber = usb_interface + 1;

    /* endpoints */
    out_iso_ep.wMaxPacketSize = MIN(usb_drv_max_endpoint_packet_size(out_iso_ep_adr), 512) | (0 << 11);

    /** Packing */
    for(i = 0; i < USB_DESCRIPTORS_LIST_SIZE; i++)
    {
        memcpy(dest, usb_descriptors_list[i], usb_descriptors_list[i]->bLength);
        dest += usb_descriptors_list[i]->bLength;
    }

    return dest - orig_dest;
}

static void playback_audio_pcm_get_more(unsigned char **start, size_t *size)
{
    #ifdef USB_AUDIO_USE_INTERMEDIATE_BUFFER
    /* copy start and end to avoid any modification at the same time */
    /* NOTE: the filler can only change the end pointer */
    int buf_start = playback_audio_buffer_start;
    int buf_end = playback_audio_buffer_end;
    
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
        playback_audio_underflow = true;
        logf("UNDERFLOW UNDERFLOW");
        return;
    }
    
    if(buf_start <= buf_end)
    {
        *start = playback_audio_buffer + buf_start;
        *size = buf_end - buf_start;
        playback_audio_buffer_start = buf_end;
    }
    else
    {
        *start = playback_audio_buffer + buf_start;
        *size = PLAYBACK_AUDIO_BUFFER_SIZE - buf_start;
        playback_audio_buffer_start = 0;
    }

    /*
    logf("=> start=%d end=%d", usb_audio_buffer_start, usb_audio_buffer_end);
    */
    #endif
}

static void usb_audio_start_playback(void)
{
    playback_audio_buffer_start = 0;
    playback_audio_buffer_end = 0;
    
    audio_set_input_source(AUDIO_SRC_PLAYBACK, SRCF_PLAYBACK);
    audio_set_output_source(AUDIO_SRC_PLAYBACK);
    pcm_set_frequency(hw_freq_sampr[as_playback_freq_idx]);
    pcm_play_data(&playback_audio_pcm_get_more, NULL, 0);
    if(!pcm_is_playing())
        pcm_play_stop();
}

static void usb_audio_stop_playback(void)
{
    if(pcm_is_playing())
        pcm_play_stop();
}

int usb_audio_set_interface(int intf, int alt)
{
    if(intf == usb_interface)
    {
        if(alt != 0)
        {
            logf("usbaudio: control interface has no alternate %d", alt);
            return -1;
        }

        return 0;
    }
    if(intf == (usb_interface + 1))
    {
        if(alt < 0 || alt > 1)
        {
            logf("usbaudio: playback interface has no alternate %d", alt);
            return -1;
        }
        usb_as_playback_intf_alt = alt;

        if(usb_as_playback_intf_alt == 1)
            usb_audio_start_playback();
        else
            usb_audio_stop_playback();
        logf("usbaudio: use playback alternate %d", alt);

        return 0;
    }
    else
    {
        logf("usbaudio: interface %d has no alternate", intf);
        return -1;
    }
}

int usb_audio_get_interface(int intf)
{
    if(intf == usb_interface)
    {
        logf("usbaudio: control interface alternate is 0");
        return 0;
    }
    else if(intf == (usb_interface + 1))
    {
        logf("usbaudio: playback interface alternate  is %d", usb_as_playback_intf_alt);
        return usb_as_playback_intf_alt;
    }
    else
    {
        logf("usbaudio: unknown interface %d", intf);
        return -1;
    }
}

static bool usb_audio_as_playback_endpoint_request(struct usb_ctrlrequest* req)
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
            set_playback_sampling_frequency(decode3(usb_buffer));
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
            encode3(usb_buffer, get_playback_sampling_frequency());
            usb_drv_send_blocking(EP_CONTROL, usb_buffer, req->wLength);
            usb_drv_recv_blocking(EP_CONTROL, NULL, 0); /* ack */
            handled = true;
            break;
        default:
            logf("usbaudio: unhandled ep req 0x%x", req->bRequest);
    }
    
    return handled;
}

static bool usb_audio_endpoint_request(struct usb_ctrlrequest* req)
{
    int ep = req->wIndex & 0xff;

    if(ep == out_iso_ep_adr)
        return usb_audio_as_playback_endpoint_request(req);
    else
    {
        logf("usbaudio: unhandled ep req (ep=%d)", ep);
        return false;
    }
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

static bool usb_audio_ac_set_get_request(struct usb_ctrlrequest* req)
{
    switch(req->wIndex >> 8)
    {
        case AC_PLAYBACK_FEATURE_ID:
            return usb_audio_set_get_feature_unit(req);
        default:
            logf("usbaudio: unhandled set/get on entity %d", req->wIndex >> 8);
            return false;
    }
}

static bool usb_audio_interface_request(struct usb_ctrlrequest* req)
{
    int intf = req->wIndex & 0xff;

    if(intf == usb_interface)
    {
        switch(req->bRequest)
        {
            case USB_AC_SET_CUR: case USB_AC_SET_MIN: case USB_AC_SET_MAX: case USB_AC_SET_RES:
            case USB_AC_SET_MEM: case USB_AC_GET_CUR: case USB_AC_GET_MIN: case USB_AC_GET_MAX:
            case USB_AC_GET_RES: case USB_AC_GET_MEM:
                return usb_audio_ac_set_get_request(req);
            default:
                logf("usbaudio: unhandled ac intf req 0x%x", req->bRequest);
                return false;
        }
    }
    else
    {
        logf("usbaudio: unhandled intf req (intf=%d)", intf);
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
    
    usb_as_playback_intf_alt = 0;
    set_playback_sampling_frequency(HW_SAMPR_DEFAULT);

    cpu_boost(true);
    
    usb_drv_select_endpoint_mode(out_iso_ep_adr, USB_DRV_ENDPOINT_MODE_REPEAT);
    usb_drv_allocate_slots(out_iso_ep_adr, sizeof(usb_audio_ep_slots[0]), usb_audio_ep_slots[0]);
    for(i = 0; i < USB_AUDIO_NB_SLOTS; i++)
        usb_drv_fill_repeat_slot(out_iso_ep_adr, i, usb_audio_slot_buffers[i], USB_AUDIO_SLOT_SIZE);
    usb_drv_start_repeat(out_iso_ep_adr);
}

void usb_audio_disconnect(void)
{
    logf("usbaudio: disconnect");
    
    usb_audio_stop_playback();

    cpu_boost(false);
}

void usb_audio_transfer_complete(int ep, int dir, int status, int length, void *buffer)
{
    (void) dir;
    int actual_length;

    logf("iso: l=%d s=%d", length, status);
    /*
    logf("usbaudio: xfer %s", status < 0 ? "failure" : "completed");
    logf("usbaudio: %d bytes transfered", length);
    */

    if(ep == out_iso_ep_adr && usb_as_playback_intf_alt == 1)
    {
        if(status == 0)
        {
            #ifdef USB_AUDIO_USE_INTERMEDIATE_BUFFER
            //logf("usbaudio: start=%d end=%d length=%d", usb_audio_buffer_start, usb_audio_buffer_end, length);
            while(length > 0)
            {
                actual_length = MIN(length, PLAYBACK_AUDIO_BUFFER_SIZE - playback_audio_buffer_end);
                memcpy(playback_audio_buffer + playback_audio_buffer_end, buffer, actual_length);

                playback_audio_buffer_end += actual_length;
                buffer = (unsigned char *)buffer + actual_length;
                if(playback_audio_buffer_end >= PLAYBACK_AUDIO_BUFFER_SIZE)
                    playback_audio_buffer_end = 0;
                length -= actual_length;
            }
            //logf("=> start=%d end=%d", usb_audio_buffer_start, usb_audio_buffer_end);

            /* only start playback if there is sufficient data (to avoid repeated underflow) */
            if(playback_audio_underflow)
            {
                if(playback_audio_buffer_start <= playback_audio_buffer_end)
                    actual_length = playback_audio_buffer_end - playback_audio_buffer_start;
                else
                    actual_length = playback_audio_buffer_end + PLAYBACK_AUDIO_BUFFER_SIZE - playback_audio_buffer_start;

                if(actual_length >= PLAYBACK_INITIAL_BUFFERING_SIZE)
                {
                    pcm_play_data(&playback_audio_pcm_get_more, NULL, 0);
                    playback_audio_underflow = false;
                }
            }
            #endif
        }
    }
}
