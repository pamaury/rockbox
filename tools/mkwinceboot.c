/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 * $Id$
 *
 * Copyright (c) 2011 by Amaury Pouly
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

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>
#include <string.h>
#include <stdint.h>
#include "wincerom.h"

#define ADD_XIPCHAIN

#define BINFS_HDR_OFFSET    0x210

struct binfs_header
{
    uint32_t xip_kernel_start;
    uint32_t xip_kernel_len;
    uint32_t nk_entry;
    uint32_t ignore[5];
    uint32_t xip_kernel_start2;
    uint32_t xip_kernel_len2;
    uint32_t chain_start;
    uint32_t chain_len;
    uint32_t xip_start;
    uint32_t xip_len;
};

/* globals */
uint32_t g_start_address = 0;

void *load_file(char *name, size_t *size)
{
    FILE *f = fopen(name, "rb");
    if(f == NULL)
    {
        printf("Cannot open '%s' for reading !\n", name);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    *size = ftell(f);
    fseek(f, 0, SEEK_SET);
    void *buf = malloc(*size);
    if(buf != NULL)
        if(fread(buf, *size, 1, f) != 1)
        {
            printf("Cannot read '%s' !\n", name);
            free(buf);
            buf = NULL;
        }
    fclose(f);
    return buf;
}

void write_file(char *name, void *buf, size_t size)
{
    FILE *f = fopen(name, "wb");
    if(f == NULL)
    {
        printf("Cannot open '%s' for writing !\n", name);
        return;
    }
    if(fwrite(buf, size, 1, f) != 1)
    {
        printf("Cannot write '%s' !\n", name);
    }
    fclose(f);
}

void *build_image(void *fw, size_t fw_size, void *hdr, size_t hdr_size, size_t *binfs_size)
{
    #ifdef ADD_XIPCHAIN
    *binfs_size = fw_size + hdr_size + sizeof(XIPCHAIN_INFO);
    #else
    *binfs_size = fw_size + hdr_size;
    #endif
    void *binfs = malloc(*binfs_size);
    if(binfs == NULL)
        return NULL;
    memcpy(binfs, hdr, hdr_size);
    memcpy(binfs + hdr_size, fw, fw_size);

    uint32_t sig = *(uint32_t *)(fw + ROM_SIGNATURE_OFFSET);
    if(sig != ROM_SIGNATURE)
    {
        printf("Wrong ROM signature !\n");
        free(binfs);
        return NULL;
    }
    uint32_t toc_ptr = *(uint32_t *)(fw + ROM_TOC_POINTER_OFFSET);
    /* try to find the rom header, since we don't know the virtual base address
     * and that the toc_ptr is a virtual address. Assume image base is a multiple
     * of 0x1000.
     * Begin with a lowest possible address such that (fw + toc_ptr - g_start_address)
     * is the end of file.
     * End with the highest possible address such that fw + toc_ptr - g_start_address)
     * is the beginning of the file */
    #define VIRTUAL_BASE_ALIGN  0x1000 /* assume power of 2 */
    ROMHDR *romhdr = NULL;
    g_start_address = (toc_ptr - fw_size) & ~(VIRTUAL_BASE_ALIGN - 1);
    for(; g_start_address < toc_ptr; g_start_address += VIRTUAL_BASE_ALIGN)
    {
        romhdr = (ROMHDR *)(fw + toc_ptr - g_start_address);
        /* check physfirst */
        if(romhdr->physfirst == g_start_address)
            break;
        else
            romhdr = NULL;
    }
    printf("Virtual base address is 0x%08x\n", g_start_address);

    uint32_t fw_virt_size = romhdr->physlast - romhdr->physfirst;

    struct binfs_header *mod = (struct binfs_header *)(binfs + BINFS_HDR_OFFSET);
    mod->xip_kernel_start = g_start_address;
    mod->xip_kernel_len = fw_virt_size;
    mod->nk_entry = g_start_address;
    mod->xip_kernel_start2 = mod->xip_kernel_start;
    mod->xip_kernel_len2 = mod->xip_kernel_len;
    mod->chain_start = g_start_address + fw_virt_size;
    mod->chain_len = sizeof(XIPCHAIN_INFO);
    mod->xip_start = g_start_address;
    mod->xip_len = fw_virt_size;
    
    #ifdef ADD_XIPCHAIN
    XIPCHAIN_INFO *xipchain = (XIPCHAIN_INFO *)(binfs + hdr_size + fw_size);
    memset(xipchain, 0, sizeof(XIPCHAIN_INFO));
    xipchain->cXIPs = 1;
    xipchain->xipEntryStart.pvAddr = g_start_address;
    xipchain->xipEntryStart.dwLength = fw_size;
    xipchain->xipEntryStart.dwMaxLength = fw_size;
    xipchain->xipEntryStart.usOrder = 1;
    xipchain->xipEntryStart.usFlags = ROMXIP_OK_TO_LOAD;
    xipchain->xipEntryStart.dwVersion = 0;
    strcpy(xipchain->xipEntryStart.szName, "XIPKERNEL");
    xipchain->xipEntryStart.dwAlgoFlags = 0;
    xipchain->xipEntryStart.dwKeyLen = 0;
    #endif
    
    return binfs;
}

uint32_t checksum(uint8_t *data, int len)
{
    uint32_t result = 0;
    while(len-- > 0)
        result += *data++;
    return result;
}

int main(int argc, char **argv)
{
    if(argc != 4)
    {
        printf("usage: %s <binfs header file> <firmware file> <out binfs file>\n", argv[0]);
        return 1;
    }
    char *firmware = argv[2];
    char *header = argv[1];
    char *binfs = argv[3];

    size_t fw_size;
    void *fw_buf = load_file(firmware, &fw_size);
    if(fw_buf == NULL)
        return 1;

    size_t hdr_size;
    void *hdr_buf = load_file(header, &hdr_size);
    if(hdr_buf == NULL)
    {
        free(fw_buf);
        return 1;
    }

    size_t binfs_size;
    void *binfs_buf = build_image(fw_buf, fw_size, hdr_buf, hdr_size, &binfs_size);
    
    free(fw_buf);
    free(hdr_buf);

    write_file(binfs, binfs_buf, binfs_size);

    uint32_t ck = checksum(binfs_buf, binfs_size);

    char *cks_file = malloc(strlen(binfs) + 1);
    strcpy(cks_file, binfs);
    if(strcasecmp(cks_file + strlen(cks_file) - 4, ".bin") != 0)
    {
        printf("Output file name must end by .bin !");
        return 1;
    }
    strcpy(cks_file + strlen(cks_file) - 4, ".cks");
        
    FILE *fout = fopen(cks_file, "wb");
    if(fout == NULL)
    {
        printf("Cannot open '%s' for writing\n", argv[2]);
        return 1;
    }
    fprintf(fout, "binSize : %ld\n", binfs_size);
    fprintf(fout, "checksum: 0x%X", ck);
    fclose(fout);

    return 0;
}
