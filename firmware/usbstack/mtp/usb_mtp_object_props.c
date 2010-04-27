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
#include "tagcache.h"

static const struct mtp_array_uint16_t 
prop_fmt_all = {2 + NB_MTP_AUDIO_FORMATS, {OBJ_FMT_UNDEFINED, OBJ_FMT_ASSOCIATION, ALL_MTP_AUDIO_FORMATS}} ,
prop_fmt_undefined = {1, {OBJ_FMT_UNDEFINED}} ,
prop_fmt_association = {1, {OBJ_FMT_ASSOCIATION}} ,
prop_fmt_all_audio = {NB_MTP_AUDIO_FORMATS, {ALL_MTP_AUDIO_FORMATS}};

static const uint32_t
prop_stor_id_default = 0x00000000,
prop_assoc_desc_default = 0x00000000,
prop_obj_size_default = 0x00000000,
prop_parent_obj_default = 0x00000000,
prop_duration_default = 0x00000000,
prop_use_count_default = 0x00000000,
#if 0
prop_sample_rate_default = 0x00000000,
#endif
prop_bitrate_default = 0x00000000;

static const uint16_t
prop_obj_fmt_default = OBJ_FMT_UNDEFINED,
prop_assoc_type_default = ASSOC_TYPE_FOLDER,
prop_hidden_default = 0x0000,
prop_system_default = 0x0000,
prop_rating_default = 0x0000,
prop_track_default = 0x0000,
prop_effective_rating_default = 0x0000
#if 0
,prop_nb_channels_default = 0x0000
#endif
;

static const struct mtp_enum_form_uint16_t
prop_assoc_type_enum = {1, {ASSOC_TYPE_FOLDER}},
prop_hidden_enum = {2, {0x0000, 0x0001}},
prop_system_enum = {2, {0x0000, 0x0001}}
#if 0
,prop_nb_channels_enum = {2, {AUDIO_CHANNELS_MONO, AUDIO_CHANNELS_STEREO}}
#endif
;

static const struct mtp_range_form_uint16_t
prop_rating_range = {0, 100, 1},
prop_effective_rating_range = {0, 100, 1};

static const struct mtp_range_form_uint32_t
#if 0
prop_sample_rate_range = {8000, 96000, 1},
#endif
prop_bitrate_range = {8000, 1500000, 1};

static const struct mtp_string
prop_filename_default = {0, {}},
prop_name_default = {0, {}},
prop_c_date_default = {0, {}},
prop_m_date_default = {0, {}},
prop_artist_default = {0, {}},
prop_genre_default = {0, {}},
prop_composer_default = {0, {}},
prop_release_date_default = {0, {}},
prop_album_name_default = {0, {}},
prop_album_artist_default = {0, {}};

static const persistent_unique_id_t
prop_persistent_unique_id_default = {{0}};

static uint16_t prop_stor_id_get(uint32_t handle);
static uint16_t prop_obj_fmt_get(uint32_t handle);
static uint16_t prop_assoc_type_get(uint32_t handle);
static uint16_t prop_assoc_desc_get(uint32_t handle);
static uint16_t prop_obj_size_get(uint32_t handle);
static uint16_t prop_filename_get(uint32_t handle);
static uint16_t prop_c_date_get(uint32_t handle);
static uint16_t prop_m_date_get(uint32_t handle);
static uint16_t prop_parent_obj_get(uint32_t handle);
static uint16_t prop_hidden_get(uint32_t handle);
static uint16_t prop_system_get(uint32_t handle);
static uint16_t prop_name_get(uint32_t handle);
static uint16_t prop_persistent_unique_id_get(uint32_t handle);

static uint16_t prop_artist_get(uint32_t handle);
static uint16_t prop_duration_get(uint32_t handle);
static uint16_t prop_rating_get(uint32_t handle);
static uint16_t prop_track_get(uint32_t handle);
static uint16_t prop_genre_get(uint32_t handle);
static uint16_t prop_use_count_get(uint32_t handle);
static uint16_t prop_composer_get(uint32_t handle);
static uint16_t prop_effective_rating_get(uint32_t handle);
static uint16_t prop_release_date_get(uint32_t handle);
static uint16_t prop_album_name_get(uint32_t handle);
static uint16_t prop_album_artist_get(uint32_t handle);
static uint16_t prop_bitrate_get(uint32_t handle);

/* translate rockbox music type to MTP object format */
static uint32_t audio_format_to_object_format(unsigned int type);

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
     &prop_persistent_unique_id_default, OBJ_PROP_FORM_NONE, NULL, &prop_persistent_unique_id_get},
    /* Artist */
    {&prop_fmt_all_audio, OBJ_PROP_ARTIST, TYPE_STR, OBJ_PROP_GET,
     &prop_artist_default, OBJ_PROP_FORM_NONE, NULL, &prop_artist_get},
    /* Duration */
    {&prop_fmt_all_audio, OBJ_PROP_DURATION, TYPE_UINT32, OBJ_PROP_GET,
     &prop_duration_default, OBJ_PROP_FORM_NONE, NULL, &prop_duration_get},
    /* Rating */
    {&prop_fmt_all_audio, OBJ_PROP_RATING, TYPE_UINT16, OBJ_PROP_GET,
     &prop_rating_default, OBJ_PROP_FORM_RANGE, &prop_rating_range, &prop_rating_get},
    /* Track */
    {&prop_fmt_all_audio, OBJ_PROP_TRACK, TYPE_UINT16, OBJ_PROP_GET,
     &prop_track_default, OBJ_PROP_FORM_NONE, NULL, &prop_track_get},
    /* Genre */
    {&prop_fmt_all_audio, OBJ_PROP_GENRE, TYPE_STR, OBJ_PROP_GET,
     &prop_genre_default, OBJ_PROP_FORM_NONE, NULL, &prop_genre_get},
    /* Use Count */
    {&prop_fmt_all_audio, OBJ_PROP_USE_COUNT, TYPE_UINT32, OBJ_PROP_GET,
     &prop_use_count_default, OBJ_PROP_FORM_NONE, NULL, &prop_use_count_get},
    /* Composer */
    {&prop_fmt_all_audio, OBJ_PROP_COMPOSER, TYPE_STR, OBJ_PROP_GET,
     &prop_composer_default, OBJ_PROP_FORM_NONE, NULL, &prop_composer_get},
    /* Effective rating */
    {&prop_fmt_all_audio, OBJ_PROP_EFFECT_RATING, TYPE_UINT16, OBJ_PROP_GET,
     &prop_effective_rating_default, OBJ_PROP_FORM_RANGE, &prop_effective_rating_range, &prop_effective_rating_get},
    /* Original Release Date */
    {&prop_fmt_all_audio, OBJ_PROP_RELEASE_DATE, TYPE_STR, OBJ_PROP_GET,
     &prop_release_date_default, OBJ_PROP_FORM_DATE, NULL, &prop_release_date_get},
    /* Album Name */
    {&prop_fmt_all_audio, OBJ_PROP_ALBUM_NAME, TYPE_STR, OBJ_PROP_GET,
     &prop_album_name_default, OBJ_PROP_FORM_NONE, NULL, &prop_album_name_get},
    /* Album Artist */
    {&prop_fmt_all_audio, OBJ_PROP_ALBUM_ARTIST, TYPE_STR, OBJ_PROP_GET,
     &prop_album_artist_default, OBJ_PROP_FORM_NONE, NULL, &prop_album_artist_get},
    #if 0
    /* Sample Rate */
    {&prop_fmt_all_audio, OBJ_PROP_SAMPLE_RATE, TYPE_UINT32, OBJ_PROP_GET,
     &prop_sample_rate_default, OBJ_PROP_FORM_RANGE, &prop_sample_rate_range, &prop_sample_rate_get},
    /* Number Of Channels */
    {&prop_fmt_all_audio, OBJ_PROP_NB_CHANNELS, TYPE_UINT16, OBJ_PROP_GET,
     &prop_nb_channels_default, OBJ_PROP_FORM_ENUM, &prop_nb_channels_enum, &prop_nb_channels_get},
    #endif
    /* Audio Bit Rate */
    {&prop_fmt_all_audio, OBJ_PROP_AUDIO_BITRATE, TYPE_UINT32, OBJ_PROP_GET,
     &prop_bitrate_default, OBJ_PROP_FORM_RANGE, &prop_bitrate_range, &prop_bitrate_get},
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
            
    return fail_op_with_ex(ERROR_OBJ_PROP_UNSUPPORTED, SEND_DATA_PHASE, "object prop unsupported");
    
    Lok:
    start_pack_data_block();
    pack_data_block_uint16_t(mtp_obj_prop_desc[i].obj_prop_code); /* Object Prop Code */
    pack_data_block_uint16_t(mtp_obj_prop_desc[i].data_type); /* Data Type */
    pack_data_block_uint8_t(mtp_obj_prop_desc[i].get_set); /* Get/Set */
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
        case OBJ_PROP_FORM_RANGE:
            pack_data_block_ptr(mtp_obj_prop_desc[i].form_value,
                3 * get_type_size(mtp_obj_prop_desc[i].data_type));
            break;
        default:
            logf("mtp: unsupported object prop form flag");
            break;
    }
    finish_pack_data_block();
    
    mtp_cur_resp.code = ERROR_OK;
    mtp_cur_resp.nb_parameters = 0;
    
    send_data_block();
}

uint16_t get_object_format(uint32_t handle)
{
    if(is_directory_object(handle))
        return OBJ_FMT_ASSOCIATION;
    else
    {
        char path[MAX_PATH];
        copy_object_path(handle, path, sizeof path);
        return audio_format_to_object_format(probe_file_format(path));
    }
}

void get_object_prop_value(uint32_t handle, uint32_t obj_prop)
{
    logf("mtp: get object props value: handle=0x%lx prop=0x%lx", handle, obj_prop);
    
    if(!is_valid_object_handle(handle, false))
        return fail_op_with_ex(ERROR_INVALID_OBJ_HANDLE, SEND_DATA_PHASE, "invalid object handle");
    
    uint16_t obj_fmt = get_object_format(handle);
    uint32_t i, j;
    for(i = 0; i < sizeof(mtp_obj_prop_desc)/sizeof(mtp_obj_prop_desc[0]); i++)
        if(mtp_obj_prop_desc[i].obj_prop_code == obj_prop)
            for(j = 0; j < mtp_obj_prop_desc[i].obj_fmt->length; j++)
                if(mtp_obj_prop_desc[i].obj_fmt->data[j] == obj_fmt)
                    goto Lok;
    return fail_op_with_ex(ERROR_OBJ_PROP_UNSUPPORTED, SEND_DATA_PHASE, "object prop unsupported");
    
    Lok:
    start_pack_data_block();
    uint16_t err_code = mtp_obj_prop_desc[i].get(handle);
    finish_pack_data_block();
    
    mtp_cur_resp.code = err_code;
    mtp_cur_resp.nb_parameters = 0;
    
    if(err_code == ERROR_OK)
        send_data_block();
    else
        fail_op_with_ex(err_code, SEND_DATA_PHASE, "object prop get error");
}

#if 0
#define prop_logf logf
#else
#define prop_logf(...)
#endif

static uint16_t prop_stor_id_get(uint32_t handle)
{
    prop_logf("mtp: %s -> stor_id=0x%lx",get_object_filename(handle),get_object_storage_id(handle));
    pack_data_block_uint32_t(get_object_storage_id(handle));
    return ERROR_OK;
}

static uint16_t prop_obj_fmt_get(uint32_t handle)
{
    prop_logf("mtp: %s -> obj_fmt=0x%x",get_object_filename(handle),get_object_format(handle));
    pack_data_block_uint16_t(get_object_format(handle));
    return ERROR_OK;
}

static uint16_t prop_assoc_type_get(uint32_t handle)
{
    (void) handle;
    prop_logf("mtp: %s -> assoc_type=0x%x",get_object_filename(handle),ASSOC_TYPE_FOLDER);
    pack_data_block_uint16_t(ASSOC_TYPE_FOLDER);
    return ERROR_OK;
}

static uint16_t prop_assoc_desc_get(uint32_t handle)
{
    (void) handle;
    prop_logf("mtp: %s -> assoc_desc=%u",get_object_filename(handle),0x1);
    pack_data_block_uint32_t(0x1);
    return ERROR_OK;
}

static uint16_t prop_obj_size_get(uint32_t handle)
{
    prop_logf("mtp: %s -> obj_size=%lu",get_object_filename(handle),get_object_size(handle));
    pack_data_block_uint32_t(get_object_size(handle));
    return ERROR_OK;
}

static uint16_t prop_filename_get(uint32_t handle)
{
    prop_logf("mtp: %s -> filename=%s",get_object_filename(handle),get_object_filename(handle));
    pack_data_block_string_charz(get_object_filename(handle));
    return ERROR_OK;
}

static uint16_t prop_c_date_get(uint32_t handle)
{
    struct tm filetm;
    prop_logf("mtp: %s -> c_date=<date>",get_object_filename(handle));
    copy_object_date_created(handle, &filetm);
    pack_data_block_date_time(&filetm);
    return ERROR_OK;
}

static uint16_t prop_m_date_get(uint32_t handle)
{
    struct tm filetm;
    prop_logf("mtp: %s -> m_date=<date>",get_object_filename(handle));
    copy_object_date_modified(handle, &filetm);
    pack_data_block_date_time(&filetm);
    return ERROR_OK;
}

static uint16_t prop_parent_obj_get(uint32_t handle)
{
    prop_logf("mtp: %s -> parent=%lu(%s)",get_object_filename(handle),get_parent_object(handle),
        get_parent_object(handle)==0?NULL:get_object_filename(get_parent_object(handle)));
    pack_data_block_uint32_t(get_parent_object(handle));
    return ERROR_OK;
}

static uint16_t prop_hidden_get(uint32_t handle)
{
    prop_logf("mtp: %s -> hidden=%d",get_object_filename(handle),is_hidden_object(handle));
    if(is_hidden_object(handle))
        pack_data_block_uint16_t(0x1);
    else
        pack_data_block_uint16_t(0x0);
    return ERROR_OK;
}

static uint16_t prop_system_get(uint32_t handle)
{
    prop_logf("mtp: %s -> system=%d",get_object_filename(handle),is_hidden_object(handle));
    if(is_system_object(handle))
        pack_data_block_uint16_t(0x1);
    else
        pack_data_block_uint16_t(0x0);
    return ERROR_OK;
}

static uint16_t prop_name_get(uint32_t handle)
{
    prop_logf("mtp: %s -> name=%s",get_object_filename(handle),get_object_filename(handle));
    pack_data_block_string_charz(get_object_filename(handle));
    return ERROR_OK;
}

static uint16_t prop_persistent_unique_id_get(uint32_t handle)
{
    persistent_unique_id_t pui=get_object_persistent_unique_id(handle);
    prop_logf("mtp: %s -> pui=[0x%lx,0x%lx,0x%lx,0x%lx]",get_object_filename(handle),
        pui.u32[0],pui.u32[1],pui.u32[2],pui.u32[3]);
    pack_data_block_typed_ptr(&pui,TYPE_UINT128);
    return ERROR_OK;
}

/* Audio properties */
static char mtp_path[MAX_PATH];
static struct mp3entry mtp_id3;
static char mtp_tag_str[200];
static int mtp_tag_num;
static struct tagcache_search tcs;
static struct tagcache_search_clause clause;

bool audio_info(struct mp3entry* id3, const char* trackname)
{
    int fd = open(trackname, O_RDONLY);
    if(fd < 0)
        return false;

    bool res = get_metadata(id3, fd, trackname);
    close(fd);
    return res;
}

static bool tagcache_copy_tag(char *filename, int tag, bool numeric)
{
    return false;
    
    if(!tagcache_search(&tcs, tag))
        return false;
    clause.tag = tag_filename;
    clause.type = clause_is;
    clause.numeric = false;
    clause.str = filename;
    if(!tagcache_search_add_clause(&tcs, &clause))
        return false;
    if(!tagcache_get_next(&tcs))
        return false;
    
    if(numeric)
    {
        mtp_tag_num = tagcache_get_numeric(&tcs, tag);
        tagcache_search_finish(&tcs);
        return mtp_tag_num >= 0;
    }
    else
    {
        bool rc = tagcache_retrieve(&tcs, tcs.idx_id, tag, mtp_tag_str, sizeof mtp_tag_str);
        tagcache_search_finish(&tcs);
        return rc;
    }
}

#ifdef HAVE_TC_RAMCACHE
#define prop_getter_tcram_part(id3name) \
    /* Try RAM tagcacache */ \
    if(tagcache_fill_tags(&mtp_id3, mtp_path)) \
        result = mtp_id3.id3name; \
    else
#else
#define prop_getter_tcram_part(id3name)
#endif

#define prop_getter_string(tagname, id3name, humanname) \
    const char *result; \
    \
    copy_object_path(handle, mtp_path, sizeof mtp_path); \
    prop_getter_tcram_part(id3name) \
    /* Try tagcache */ \
    if(tagcache_copy_tag(mtp_path, tagname, false)) \
        result = mtp_tag_str; \
    /* Try metadata */ \
    else if(audio_info(&mtp_id3, mtp_path)) \
        result = mtp_id3.id3name; \
    /* Failure */ \
    else \
        return ERROR_OP_NOT_SUPPORTED; \
    \
    if(result == NULL) \
    { \
        pack_data_block_string_charz(""); \
        logf("mtp: no " humanname); \
    } \
    else \
    { \
        pack_data_block_string_charz(result); \
        logf("mtp: " humanname ": %s", result); \
    } \
    \
    return ERROR_OK;

#define prop_getter_numeric_db(tagname, id3name, humanname, type) \
    type result; \
    \
    copy_object_path(handle, mtp_path, sizeof mtp_path); \
    prop_getter_tcram_part(id3name) \
    /* Try tagcache */ \
    if(tagcache_copy_tag(mtp_path, tagname, false)) \
        result = mtp_tag_num; \
    else \
        return ERROR_OP_NOT_SUPPORTED; \
    \
    pack_data_block_##type(result); \
    logf("mtp: " humanname ": %lu", (uint32_t)result); \
    \
    return ERROR_OK;

#define prop_getter_numeric_result(tagname, id3name, humanname, type) \
    type result; \
    \
    copy_object_path(handle, mtp_path, sizeof mtp_path); \
    prop_getter_tcram_part(id3name) \
    /* Try tagcache */ \
    if(tagcache_copy_tag(mtp_path, tagname, false)) \
        result = mtp_tag_num; \
    /* Try metadata */ \
    else if(audio_info(&mtp_id3, mtp_path)) \
        result = mtp_id3.id3name; \
    /* Failure */ \
    else \
        return ERROR_OP_NOT_SUPPORTED; \
    \
    logf("mtp: " humanname ": %lu", (uint32_t)result); \

#define prop_getter_numeric(tagname, id3name, humanname, type) \
    prop_getter_numeric_result(tagname, id3name, humanname, type) \
    pack_data_block_##type(result); \
    \
    return ERROR_OK;

static uint16_t prop_artist_get(uint32_t handle)
{
    prop_getter_string(tag_artist, artist, "artist")
}

static uint16_t prop_duration_get(uint32_t handle)
{
    prop_getter_numeric(tag_length, length, "duration", uint32_t)
}

static uint16_t prop_rating_get(uint32_t handle)
{
    prop_getter_numeric_db(tag_length, rating, "rating", uint8_t)
}

static uint16_t prop_track_get(uint32_t handle)
{
    prop_getter_numeric(tag_tracknumber, tracknum, "track", uint16_t)
}

static uint16_t prop_genre_get(uint32_t handle)
{
    prop_getter_string(tag_genre, genre_string, "genre")
}

static uint16_t prop_use_count_get(uint32_t handle)
{
    prop_getter_numeric_db(tag_playcount, playcount, "play count", uint32_t)
}

static uint16_t prop_composer_get(uint32_t handle)
{
    prop_getter_string(tag_composer, composer, "composer")
}

static uint16_t prop_effective_rating_get(uint32_t handle)
{
    prop_getter_numeric_db(tag_virt_autoscore, score, "effective rating", uint8_t)
}

static uint16_t prop_release_date_get(uint32_t handle)
{
    prop_getter_numeric_result(tag_year, year, "year", int)
    
    struct tm tm;
    memset(&tm, 0, sizeof(struct tm));
    tm.tm_year = result - 1900; // years after 1900
    
    pack_data_block_date_time(&tm);
    
    return ERROR_OK;
}

static uint16_t prop_album_name_get(uint32_t handle)
{
    prop_getter_string(tag_album, album, "album name")
}

static uint16_t prop_album_artist_get(uint32_t handle)
{
    prop_getter_string(tag_albumartist, albumartist, "album artist")
}

static uint16_t prop_bitrate_get(uint32_t handle)
{
    prop_getter_numeric(tag_bitrate, bitrate, "bitrate", uint32_t)
}


uint32_t audio_format_to_object_format(unsigned int type)
{
    switch(type)
    {
        case AFMT_MPA_L1:       /* MPEG Audio layer 1 */
        case AFMT_MPA_L2:       /* MPEG Audio layer 2 */
        case AFMT_MPA_L3:       /* MPEG Audio layer 3 */
            return OBJ_FMT_MPEG;
#if CONFIG_CODEC == SWCODEC
        case AFMT_AIFF:         /* Audio Interchange File Format */
            return OBJ_FMT_AIFF;
        case AFMT_PCM_WAV:      /* Uncompressed PCM in a WAV file */
            return OBJ_FMT_WAV;
        case AFMT_OGG_VORBIS:   /* Ogg Vorbis */
            return OBJ_FMT_OGG;
        case AFMT_FLAC:         /* FLAC */
            return OBJ_FMT_FLAC;
        case AFMT_MPC:          /* Musepack */
        case AFMT_A52:          /* A/52 (aka AC3) audio */
        case AFMT_WAVPACK:      /* WavPack */
        case AFMT_MP4_ALAC:     /* Apple Lossless Audio Codec */
        case AFMT_MP4_AAC:      /* Advanced Audio Coding (AAC) in M4A container */
        case AFMT_SHN:          /* Shorten */
        case AFMT_SID:          /* SID File Format */
        case AFMT_ADX:          /* ADX File Format */
        case AFMT_NSF:          /* NESM (NES Sound Format) */
        case AFMT_SPEEX:        /* Ogg Speex speech */
        case AFMT_SPC:          /* SPC700 save state */
        case AFMT_APE:          /* Monkey's Audio (APE) */
            return OBJ_FMT_UNDEF_AUDIO;
        case AFMT_WMA:          /* WMAV1/V2 in ASF */
            return OBJ_FMT_WMA;
        case AFMT_MOD:          /* Amiga MOD File Format */
        case AFMT_SAP:          /* Atari 8Bit SAP Format */
        case AFMT_RM_COOK:      /* Cook in RM/RA */
            return OBJ_FMT_UNDEF_AUDIO;
        case AFMT_RM_AAC:       /* AAC in RM/RA */
            return OBJ_FMT_AAC;
        case AFMT_RM_AC3:       /* AC3 in RM/RA */
        case AFMT_RM_ATRAC3:    /* ATRAC3 in RM/RA */
        case AFMT_CMC:          /* Atari 8bit cmc format */
        case AFMT_CM3:          /* Atari 8bit cm3 format */
        case AFMT_CMR:          /* Atari 8bit cmr format */
        case AFMT_CMS:          /* Atari 8bit cms format */
        case AFMT_DMC:          /* Atari 8bit dmc format */
        case AFMT_DLT:          /* Atari 8bit dlt format */
        case AFMT_MPT:          /* Atari 8bit mpt format */
        case AFMT_MPD:          /* Atari 8bit mpd format */
        case AFMT_RMT:          /* Atari 8bit rmt format */
        case AFMT_TMC:          /* Atari 8bit tmc format */
        case AFMT_TM8:          /* Atari 8bit tm8 format */
        case AFMT_TM2:          /* Atari 8bit tm2 format */
            return OBJ_FMT_UNDEF_AUDIO;
#endif /*  CONFIG_CODEC == SWCODEC */   
        default:
            return OBJ_FMT_UNDEFINED;
    }
}

