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
#define LOGF_ENABLE
#include "logf.h"
#include "tagcache.h"

static const struct mtp_array_uint16_t 
prop_fmt_all = {2, {OBJ_FMT_UNDEFINED, OBJ_FMT_ASSOCIATION}} ,
prop_fmt_undefined = {1, {OBJ_FMT_UNDEFINED}} ,
prop_fmt_association = {1, {OBJ_FMT_ASSOCIATION}};

static const uint32_t
prop_stor_id_default = 0x00000000,
prop_assoc_desc_default = 0x00000000,
prop_obj_size_default = 0x00000000,
prop_parent_obj_default = 0x00000000;

static const uint16_t
prop_obj_fmt_default = OBJ_FMT_UNDEFINED,
prop_assoc_type_default = ASSOC_TYPE_FOLDER,
prop_hidden_default = 0x0000,
prop_system_default = 0x0000,
prop_assoc_type_enum[] = {1, ASSOC_TYPE_FOLDER},
prop_hidden_enum[] = {2, 0x0000, 0x0001},
prop_system_enum[] = {2, 0x0000, 0x0001};

static const struct mtp_string
prop_filename_default = {0, {}},
prop_name_default = {0, {}},
prop_c_date_default = {0, {}},
prop_m_date_default = {0, {}};

static const persistent_unique_id_t
prop_persistent_unique_id_default = {{0}};

static void prop_stor_id_get(uint32_t handle);
static void prop_obj_fmt_get(uint32_t handle);
static void prop_assoc_type_get(uint32_t handle);
static void prop_assoc_desc_get(uint32_t handle);
static void prop_obj_size_get(uint32_t handle);
static void prop_filename_get(uint32_t handle);
static void prop_c_date_get(uint32_t handle);
static void prop_m_date_get(uint32_t handle);
static void prop_parent_obj_get(uint32_t handle);
static void prop_hidden_get(uint32_t handle);
static void prop_system_get(uint32_t handle);
static void prop_name_get(uint32_t handle);
static void prop_persistent_unique_id_get(uint32_t handle);

static const struct mtp_obj_prop mtp_obj_prop_desc[] =
{
    /* Storage ID */
    {&prop_fmt_all, OBJ_PROP_STORAGE_ID, TYPE_UINT32, OBJ_PROP_GET, 
     &prop_stor_id_default, OBJ_PROP_FORM_NONE, NULL, &prop_stor_id_get},
    /* Object Format */
    {&prop_fmt_all, OBJ_PROP_OBJ_FMT, TYPE_UINT16, OBJ_PROP_GET,
     &prop_obj_fmt_default, OBJ_PROP_FORM_NONE, NULL, &prop_obj_fmt_get},
    /* Association Type */
    {&prop_fmt_association, OBJ_PROP_ASSOC_TYPE, TYPE_UINT16, OBJ_PROP_GET,
     &prop_assoc_type_default, OBJ_PROP_FORM_ENUM, &prop_assoc_type_enum, &prop_assoc_type_get},
    /* Association Desc */
    {&prop_fmt_association, OBJ_PROP_ASSOC_DESC, TYPE_UINT32, OBJ_PROP_GET,
     &prop_assoc_desc_default, OBJ_PROP_FORM_NONE, NULL, &prop_assoc_desc_get},
    /* Object Size */
    {&prop_fmt_all, OBJ_PROP_OBJ_SIZE, TYPE_UINT32, OBJ_PROP_GET,
     &prop_obj_size_default, OBJ_PROP_FORM_NONE, NULL, &prop_obj_size_get},
    /* Filename */
    {&prop_fmt_all, OBJ_PROP_FILENAME, TYPE_STR, OBJ_PROP_GET,
     &prop_filename_default, OBJ_PROP_FORM_NONE, NULL, &prop_filename_get},
    /* Creation Date */
    {&prop_fmt_all, OBJ_PROP_C_DATE, TYPE_STR, OBJ_PROP_GET,
     &prop_c_date_default, OBJ_PROP_FORM_DATE, NULL, &prop_c_date_get},
    /* Modification Date */
    {&prop_fmt_all, OBJ_PROP_M_DATE, TYPE_STR, OBJ_PROP_GET,
     &prop_m_date_default, OBJ_PROP_FORM_DATE, NULL, &prop_m_date_get},
    /* Parent Object */
    {&prop_fmt_all, OBJ_PROP_PARENT_OBJ, TYPE_UINT32, OBJ_PROP_GET,
     &prop_parent_obj_default, OBJ_PROP_FORM_NONE, NULL, &prop_parent_obj_get},
    /* Hidden */
    {&prop_fmt_all, OBJ_PROP_HIDDEN, TYPE_UINT16, OBJ_PROP_GET,
     &prop_hidden_default, OBJ_PROP_FORM_ENUM, &prop_hidden_enum, &prop_hidden_get},
    /* System Object */
    {&prop_fmt_all, OBJ_PROP_SYS_OBJ, TYPE_UINT16, OBJ_PROP_GET,
     &prop_system_default, OBJ_PROP_FORM_ENUM, &prop_system_enum, &prop_system_get},
    /* Name */
    {&prop_fmt_all, OBJ_PROP_NAME, TYPE_STR, OBJ_PROP_GET,
     &prop_name_default, OBJ_PROP_FORM_NONE, NULL, &prop_name_get},
    /* Persistent Unique Object Id */
    {&prop_fmt_all, OBJ_PROP_PERSISTENT, TYPE_UINT128, OBJ_PROP_GET,
     &prop_persistent_unique_id_default, OBJ_PROP_FORM_NONE, NULL, &prop_persistent_unique_id_get}
};

void get_object_props_supported(uint32_t object_fmt)
{
    //logf("mtp: get object props supported: fmt=0x%lx", object_fmt);
    
    uint32_t i, j;
    
    start_pack_data_block();
    start_pack_data_block_array();
    for(i = 0; i < sizeof(mtp_obj_prop_desc)/sizeof(mtp_obj_prop_desc[0]); i++)
        for(j = 0; j < mtp_obj_prop_desc[i].obj_fmt->length; j++)
            if(mtp_obj_prop_desc[i].obj_fmt->data[j] == object_fmt)
                pack_data_block_array_elem_uint16_t(mtp_obj_prop_desc[i].obj_prop_code);
    finish_pack_data_block_array();
    finish_pack_data_block();
    
    mtp_cur_resp.code = ERROR_OK;
    mtp_cur_resp.nb_parameters = 0;
    
    send_data_block();
}

void get_object_prop_desc(uint32_t obj_prop, uint32_t obj_fmt)
{
    //logf("mtp: get object props desc: prop=0x%lx fmt=0x%lx", obj_prop, obj_fmt);
    
    uint32_t i, j;
    for(i = 0; i < sizeof(mtp_obj_prop_desc)/sizeof(mtp_obj_prop_desc[0]); i++)
        if(mtp_obj_prop_desc[i].obj_prop_code == obj_prop)
            for(j = 0; j < mtp_obj_prop_desc[i].obj_fmt->length; j++)
                if(mtp_obj_prop_desc[i].obj_fmt->data[j] == obj_fmt)
                    goto Lok;
            
    return fail_op_with(ERROR_INVALID_OBJ_PROP_CODE, SEND_DATA_PHASE);
    
    Lok:
    start_pack_data_block();
    pack_data_block_uint16_t(mtp_obj_prop_desc[i].obj_prop_code);
    pack_data_block_uint16_t(mtp_obj_prop_desc[i].data_type);
    pack_data_block_uint8_t(mtp_obj_prop_desc[i].get_set); /* Get */
    pack_data_block_typed_ptr(mtp_obj_prop_desc[i].default_value,
        mtp_obj_prop_desc[i].data_type); /* Factory Default Value */
    pack_data_block_uint32_t(0xffffffff); /* Group */
    pack_data_block_uint8_t(mtp_obj_prop_desc[i].form); /* Form */
    switch(mtp_obj_prop_desc[i].form)
    {
        case OBJ_PROP_FORM_NONE:
        case OBJ_PROP_FORM_DATE:
            break;
        case OBJ_PROP_FORM_ENUM:
        {
            /* NOTE element have a fixed size so it's safe to use get_type_size */
            uint16_t nb_elems = *(uint16_t *)mtp_obj_prop_desc[i].form_value;
            pack_data_block_ptr(mtp_obj_prop_desc[i].form_value, 
                sizeof(uint16_t) + nb_elems * get_type_size(mtp_obj_prop_desc[i].data_type));
            break;
        }
        default:
            break;
    }
    finish_pack_data_block();
    
    mtp_cur_resp.code = ERROR_OK;
    mtp_cur_resp.nb_parameters = 0;
    
    send_data_block();
}

static uint16_t get_object_format(uint32_t handle)
{
    if(is_directory_object(handle))
        return OBJ_FMT_ASSOCIATION;
    else
        return OBJ_FMT_UNDEFINED;
}

void get_object_prop_value(uint32_t handle, uint32_t obj_prop)
{
    logf("mtp: get object props value: handle=0x%lx prop=0x%lx", handle, obj_prop);
    
    if(!is_valid_object_handle(handle, false))
        return fail_op_with(ERROR_INVALID_OBJ_HANDLE, SEND_DATA_PHASE);
    
    uint16_t obj_fmt = get_object_format(handle);
    uint32_t i, j;
    for(i = 0; i < sizeof(mtp_obj_prop_desc)/sizeof(mtp_obj_prop_desc[0]); i++)
        if(mtp_obj_prop_desc[i].obj_prop_code == obj_prop)
            for(j = 0; j < mtp_obj_prop_desc[i].obj_fmt->length; j++)
                if(mtp_obj_prop_desc[i].obj_fmt->data[j] == obj_fmt)
                    goto Lok;
    return fail_op_with(ERROR_INVALID_OBJ_PROP_CODE, SEND_DATA_PHASE);
    
    Lok:
    start_pack_data_block();
    mtp_obj_prop_desc[i].get(handle);
    finish_pack_data_block();
    
    mtp_cur_resp.code = ERROR_OK;
    mtp_cur_resp.nb_parameters = 0;
    
    send_data_block();
}

#if 0
#define prop_logf logf
#else
#define prop_logf(...)
#endif

static void prop_stor_id_get(uint32_t handle)
{
    prop_logf("mtp: %s -> stor_id=0x%lx",get_object_filename(handle),get_object_storage_id(handle));
    pack_data_block_uint32_t(get_object_storage_id(handle));
}

static void prop_obj_fmt_get(uint32_t handle)
{
    prop_logf("mtp: %s -> obj_fmt=0x%x",get_object_filename(handle),get_object_format(handle));
    pack_data_block_uint16_t(get_object_format(handle));
}

static void prop_assoc_type_get(uint32_t handle)
{
    (void) handle;
    prop_logf("mtp: %s -> assoc_type=0x%x",get_object_filename(handle),ASSOC_TYPE_FOLDER);
    pack_data_block_uint16_t(ASSOC_TYPE_FOLDER);
}

static void prop_assoc_desc_get(uint32_t handle)
{
    (void) handle;
    prop_logf("mtp: %s -> assoc_desc=%u",get_object_filename(handle),0x1);
    pack_data_block_uint32_t(0x1);
}

static void prop_obj_size_get(uint32_t handle)
{
    prop_logf("mtp: %s -> obj_size=%lu",get_object_filename(handle),get_object_size(handle));
    pack_data_block_uint32_t(get_object_size(handle));
}

static void prop_filename_get(uint32_t handle)
{
    prop_logf("mtp: %s -> filename=%s",get_object_filename(handle),get_object_filename(handle));
    pack_data_block_string_charz(get_object_filename(handle));
}

static void prop_c_date_get(uint32_t handle)
{
    struct tm filetm;
    prop_logf("mtp: %s -> c_date=<date>",get_object_filename(handle));
    copy_object_date_created(handle, &filetm);
    pack_data_block_date_time(&filetm);
}

static void prop_m_date_get(uint32_t handle)
{
    struct tm filetm;
    prop_logf("mtp: %s -> m_date=<date>",get_object_filename(handle));
    copy_object_date_modified(handle, &filetm);
    pack_data_block_date_time(&filetm);
}

static void prop_parent_obj_get(uint32_t handle)
{
    prop_logf("mtp: %s -> parent=%lu(%s)",get_object_filename(handle),get_parent_object(handle),
        get_parent_object(handle)==0?NULL:get_object_filename(get_parent_object(handle)));
    pack_data_block_uint32_t(get_parent_object(handle));
}

static void prop_hidden_get(uint32_t handle)
{
    prop_logf("mtp: %s -> hidden=%d",get_object_filename(handle),is_hidden_object(handle));
    if(is_hidden_object(handle))
        pack_data_block_uint16_t(0x1);
    else
        pack_data_block_uint16_t(0x0);
}

static void prop_system_get(uint32_t handle)
{
    prop_logf("mtp: %s -> system=%d",get_object_filename(handle),is_hidden_object(handle));
    if(is_system_object(handle))
        pack_data_block_uint16_t(0x1);
    else
        pack_data_block_uint16_t(0x0);
}

static void prop_name_get(uint32_t handle)
{
    prop_logf("mtp: %s -> name=%s",get_object_filename(handle),get_object_filename(handle));
    pack_data_block_string_charz(get_object_filename(handle));
}

static void prop_persistent_unique_id_get(uint32_t handle)
{
    persistent_unique_id_t pui=get_object_persistent_unique_id(handle);
    prop_logf("mtp: %s -> pui=[0x%lx,0x%lx,0x%lx,0x%lx]",get_object_filename(handle),
        pui.u32[0],pui.u32[1],pui.u32[2],pui.u32[3]);
    pack_data_block_typed_ptr(&pui,TYPE_UINT128);
}
