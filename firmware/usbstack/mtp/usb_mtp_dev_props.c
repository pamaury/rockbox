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
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY
 * KIND, either express or implied.
 *
 ****************************************************************************/
#include "usb_mtp_internal.h"
/*#define LOGF_ENABLE*/
#include "logf.h"

#if 1
#define dev_logf logf
#else
#define dev_logf(...)
#endif

static const struct mtp_string
prop_date_time_default = {0, {}},
prop_friendly_name_default =
{
    21,
    {'R','o','c','k','b','o','x',' ','m','e','d','i','a',' ','p','l','a','y','e','r','\0'} /* null-terminated */
};

static mtp_string_fixed(128)
prop_friendly_name =
{
    21,
    {'R','o','c','k','b','o','x',' ','m','e','d','i','a',' ','p','l','a','y','e','r','\0'} /* null-terminated */
};

static const uint8_t
prop_battery_level_default = 0;

static const struct mtp_range_form_uint8_t
prop_battery_level_range = {0, 100, 1};

static uint16_t prop_battery_level_get(void);
static uint16_t prop_date_time_get(void);
static uint16_t prop_friendly_name_get(void);
static uint16_t prop_friendly_name_set(void);
static uint16_t prop_friendly_name_reset(void);

static const struct mtp_dev_prop mtp_dev_prop_desc[] =
{
    /* Battery Level */
    {DEV_PROP_BATTERY_LEVEL, TYPE_UINT8, DEV_PROP_GET, &prop_battery_level_default,
     DEV_PROP_FORM_RANGE, &prop_battery_level_range,
     &prop_battery_level_get, NULL, NULL},
    /* DateTime */
    {DEV_PROP_DATE_TIME, TYPE_STR, DEV_PROP_GET, &prop_date_time_default,
     DEV_PROP_FORM_DATE, NULL,
     &prop_date_time_get, NULL, NULL},
    /* Device Friendly Name */
    {DEV_PROP_FRIENDLY_NAME, TYPE_STR, DEV_PROP_GET_SET, &prop_friendly_name_default,
     DEV_PROP_FORM_NONE, NULL,
     &prop_friendly_name_get, &prop_friendly_name_set, &prop_friendly_name_reset}
};

void get_device_prop_desc(uint32_t device_prop)
{
    uint32_t i;
    uint16_t err_code;
    
    for(i = 0; i < sizeof(mtp_dev_prop_desc)/sizeof(mtp_dev_prop_desc[0]); i++)
        if(mtp_dev_prop_desc[i].dev_prop_code == device_prop)
            goto Lok;
    
    return fail_op_with(ERROR_DEV_PROP_UNSUPPORTED, SEND_DATA_PHASE);
    
    Lok:
    start_pack_data_block();
    pack_data_block_uint16_t(mtp_dev_prop_desc[i].dev_prop_code);
    pack_data_block_uint16_t(mtp_dev_prop_desc[i].data_type);
    pack_data_block_uint8_t(mtp_dev_prop_desc[i].get_set); /* Get/Set */
    pack_data_block_typed_ptr(mtp_dev_prop_desc[i].default_value,
        mtp_dev_prop_desc[i].data_type); /* Factory Default Value */
    /* Current Value */
    err_code = mtp_dev_prop_desc[i].get();
    pack_data_block_uint8_t(mtp_dev_prop_desc[i].form); /* Form */
    switch(mtp_dev_prop_desc[i].form)
    {
        case DEV_PROP_FORM_NONE:
        case DEV_PROP_FORM_DATE:
            break;
        case DEV_PROP_FORM_ENUM:
        {
            /* NOTE element have a fixed size so it's safe to use get_type_size */
            uint16_t nb_elems = *(uint16_t *)mtp_dev_prop_desc[i].form_value;
            pack_data_block_ptr(mtp_dev_prop_desc[i].form_value, 
                sizeof(uint16_t) + nb_elems * get_type_size(mtp_dev_prop_desc[i].data_type));
            break;
        }
        case DEV_PROP_FORM_RANGE:
            pack_data_block_ptr(mtp_dev_prop_desc[i].form_value,
                3 * get_type_size(mtp_dev_prop_desc[i].data_type));
            break;
        default:
            logf("mtp: unsupported device prop form flag");
            break;
    }
    finish_pack_data_block();
    
    mtp_cur_resp.code = err_code;
    mtp_cur_resp.nb_parameters = 0;
    
    if(err_code == ERROR_OK)
        send_data_block();
    else
        fail_op_with(err_code, SEND_DATA_PHASE);
}

void get_device_prop_value(uint32_t device_prop)
{
    uint32_t i;
    uint16_t err_code;
    
    for(i = 0; i < sizeof(mtp_dev_prop_desc)/sizeof(mtp_dev_prop_desc[0]); i++)
        if(mtp_dev_prop_desc[i].dev_prop_code == device_prop)
            goto Lok;
    
    return fail_op_with(ERROR_DEV_PROP_UNSUPPORTED, SEND_DATA_PHASE);
    
    Lok:
    start_pack_data_block();
    err_code = mtp_dev_prop_desc[i].get();
    finish_pack_data_block();
    
    mtp_cur_resp.code = err_code;
    mtp_cur_resp.nb_parameters = 0;
    
    if(err_code == ERROR_OK)
        send_data_block();
    else
        fail_op_with(err_code, SEND_DATA_PHASE);
}

struct set_dev_prop_value_st
{
    uint32_t dev_prop_idx;
};

static void set_dev_prop_value_split_routine(unsigned char *data, int length, uint32_t rem_bytes, void *user)
{
    struct set_dev_prop_value_st *st = user;
    uint16_t err_code;
    /* If the transfer spans several packets, abort */
    if(rem_bytes != 0)
        return fail_op_with(ERROR_INVALID_DATASET, RECV_DATA_PHASE); /* continue reception and throw data */
    
    start_unpack_data_block(data, length);
    
    /* set function is supposed to call finish_unpack_data_block() to check exact length */
    err_code = mtp_dev_prop_desc[st->dev_prop_idx].set();
    if(err_code != ERROR_OK)
        return fail_op_with(err_code, NO_DATA_PHASE); /* no more receive data phase */
}

static void set_dev_prop_value_finish_routine(bool error, void *user)
{
    generic_finish_split_routine(error, user);
}

void set_device_prop_value(uint32_t device_prop)
{
    uint32_t i;
    static struct set_dev_prop_value_st st;
    
    for(i = 0; i < sizeof(mtp_dev_prop_desc)/sizeof(mtp_dev_prop_desc[0]); i++)
        if(mtp_dev_prop_desc[i].dev_prop_code == device_prop)
            goto Lok;
    
    return fail_op_with(ERROR_DEV_PROP_UNSUPPORTED, SEND_DATA_PHASE);
    
    Lok:
    if(mtp_dev_prop_desc[i].set == NULL)
        return fail_op_with(ERROR_ACCESS_DENIED, RECV_DATA_PHASE);
    
    st.dev_prop_idx = i;
    
    receive_split_data(&set_dev_prop_value_split_routine, &set_dev_prop_value_finish_routine, &st);
}

void reset_device_prop_value(uint32_t device_prop)
{
    uint32_t i;
    
    for(i = 0; i < sizeof(mtp_dev_prop_desc)/sizeof(mtp_dev_prop_desc[0]); i++)
        if(mtp_dev_prop_desc[i].dev_prop_code == device_prop)
            goto Lok;
    
    return fail_op_with(ERROR_DEV_PROP_UNSUPPORTED, NO_DATA_PHASE);
    
    Lok:
    mtp_cur_resp.code = mtp_dev_prop_desc[i].reset();
    mtp_cur_resp.nb_parameters = 0;
    
    send_response();
}

static uint16_t prop_battery_level_get(void)
{
    dev_logf("mtp: get battery level");
    
    pack_data_block_uint8_t(battery_level());
    
    return ERROR_OK;
}

static uint16_t prop_date_time_get(void)
{
    dev_logf("mtp: get date time");
    
    pack_data_block_date_time(get_time());
    
    return ERROR_OK;
}

static uint16_t prop_friendly_name_get(void)
{
    dev_logf("mtp: get friendly name");
    
    pack_data_block_string((struct mtp_string *)&prop_friendly_name);
    
    return ERROR_OK;
}

static uint16_t prop_friendly_name_set(void)
{
    dev_logf("mtp: set friendly name");
    
    if(!unpack_data_block_string((struct mtp_string *)&prop_friendly_name,
            sizeof(prop_friendly_name.data)/sizeof(uint16_t)))
        return ERROR_INVALID_DEV_PROP_FMT;
    if(!finish_unpack_data_block())
        return ERROR_INVALID_DEV_PROP_FMT;
    
    return ERROR_OK;
}

static uint16_t prop_friendly_name_reset(void)
{
    dev_logf("mtp: reset friendly name");
    
    unsafe_copy_mtp_string((struct mtp_string *)&prop_friendly_name, &prop_friendly_name_default);
    
    return ERROR_OK;
}

