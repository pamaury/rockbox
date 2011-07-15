#define ASM_ONLY
#include "wincerom.h"

#define HW_ADDR(x)      (0x30000000 + x)

#define _imagestart     HW_ADDR(_vectorstart)
#define _imageend       HW_ADDR(_initdata_end)
#define _imagesize      _binary_size

rom_ecec:
    .org        ROM_SIGNATURE_OFFSET
    .word       ROM_SIGNATURE
    .org        ROM_TOC_POINTER_OFFSET
    .word       HW_ADDR(romhdr)
    .org        ROM_TOC_OFFSET_OFFSET
    .word       0

.global romhdr
romhdr:
    .org        (romhdr + ROMHDR_dllfirst)
    .word       0
    .org        (romhdr + ROMHDR_dlllast)
    .word       0
    .org        (romhdr + ROMHDR_physfirst)
    .word       _imagestart
    .org        (romhdr + ROMHDR_physlast)
    .word       _imageend
    .org        (romhdr + ROMHDR_nummods)
    .word       1
    .org        (romhdr + ROMHDR_ulRAMStart)
    .word       _imagestart
    .org        (romhdr + ROMHDR_ulRAMFree)
    .word       _imageend
    .org        (romhdr + ROMHDR_ulRAMEnd)
    .word       _imageend
    .org        (romhdr + ROMHDR_ulCopyEntries)
    .word       0
    .org        (romhdr + ROMHDR_ulCopyOffset)
    .word       0
    .org        (romhdr + ROMHDR_ulProfileLen)
    .word       0
    .org        (romhdr + ROMHDR_ulProfileOffset)
    .word       0
    .org        (romhdr  + ROMHDR_numfiles)
    .word       0
    .org        (romhdr + ROMHDR_ulObjstoreStart)
    .word       0
    .org        (romhdr + ROMHDR_ulObjstoreLen)
    .word       0
    .org        (romhdr + ROMHDR_ulDrivglobStart)
    .word       0
    .org        (romhdr + ROMHDR_ulDrivglobLen)
    .word       0
    .org        (romhdr + ROMHDR_usCPUType)
    .hword      IMAGE_FILE_MACHINE_ARM
    .org        (romhdr + ROMHDR_usMiscFlags)
    .hword      0
    .org        (romhdr + ROMHDR_pExtensions)
    .word       HW_ADDR(rom_ext)
    .org        (romhdr + ROMHDR_ulTrackingStart)
    .word       0
    .org        (romhdr + ROMHDR_ulTrackingLen)
    .word       0
.global nk_exe_module
nk_exe_module:
    .org        (nk_exe_module + TOCentry_dwFileAttributes)
    .word       (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)
    .org        (nk_exe_module + TOCentry_ftTime)
    .word       0 /* created three centuries ago ;) */
    .word       0
    .org        (nk_exe_module + TOCentry_lpszFileSize)
    .word       _imagesize
    .org        (nk_exe_module + TOCentry_lpszFileName)
    .word       HW_ADDR(nk_exe_module_string)
    .org        (nk_exe_module + TOCentry_ulE32Offset)
    .word       0
    .org        (nk_exe_module + TOCentry_ulO32Offset)
    .word       0
    .org        (nk_exe_module + TOCentry_ulLoadOffset)
    .word       _imagestart
.global nk_exe_module_string
nk_exe_module_string:
    .asciz      "nk.exe"
.global rom_ext
rom_ext:
    .org        (rom_ext + ROMPID_dwPID)
    .fill       PID_LENGTH, 4, 0
    .org        (rom_ext + ROMPID_pNextExt)
    .word       HW_ADDR(rom_second_ext)
.global rom_second_ext
rom_second_ext:
    .org        (rom_second_ext + ROMPID_name)
    .asciz      "chain information"
    .org        (rom_second_ext + ROMPID_type)
    .word       0
    .org        (rom_second_ext + ROMPID_pdata)
    .word       HW_ADDR(rom_ext_chain_info)
    .org        (rom_second_ext + ROMPID_length)
    .word       (rom_ext_chain_info_end - rom_ext_chain_info)
    .org        (rom_second_ext + ROMPID_reserved)
    .word       0
    .org        (rom_second_ext + ROMPID_pNextExt)
    .word       0
.global rom_ext_chain_info
rom_ext_chain_info:
    .org        (rom_ext_chain_info + XIPCHAIN_SUMMARY_pvAddr)
    .word       0x8c200000
    .org        (rom_ext_chain_info + XIPCHAIN_SUMMARY_dwMaxLength)
    .word       0x1000
    .org        (rom_ext_chain_info + XIPCHAIN_SUMMARY_usOrder)
    .hword      0
    .org        (rom_ext_chain_info + XIPCHAIN_SUMMARY_usFlags)
    .hword      0
    .org        (rom_ext_chain_info + XIPCHAIN_SUMMARY_reserved)
    .word       0
rom_ext_chain_info_entry:
    .org        (rom_ext_chain_info_entry + XIPCHAIN_SUMMARY_pvAddr)
    .word       _imagestart
    .org        (rom_ext_chain_info_entry + XIPCHAIN_SUMMARY_dwMaxLength)
    .word       _imagesize
    .org        (rom_ext_chain_info_entry + XIPCHAIN_SUMMARY_usOrder)
    .hword      0
    .org        (rom_ext_chain_info_entry + XIPCHAIN_SUMMARY_usFlags)
    .hword      ROMXIP_OK_TO_LOAD
    .org        (rom_ext_chain_info_entry + XIPCHAIN_SUMMARY_reserved)
    .word       0
.global rom_ext_chain_info_end
rom_ext_chain_info_end:
