#define KFLAG_DISALLOW_PAGING   0x00000001
#define KFLAG_NOTALLKMODE       0x00000002
#define KFLAG_TRUSTROMONLY      0x00000010

#ifdef x86
#define KFLAG_NOFLUSHPT         0x00000020  // use this flag to not flushing soft TLB on x86.
                                            // potential security hole if using this flag,
                                            // but improve RT performance
#endif

#define KFLAG_HONOR_DLL_BASE    0x00000040  // honor DLL's preferred load address.
                                            // Loading DLL at its preferred load address will release kernel
                                            // from reloacating the DLL. However, there might be potential Backward
                                            // compatibility issue since there might be existing DLL binaries
                                            // out there that sets it's preferred load address really low and thus
                                            // not able to be loaded.

#define ROM_SIGNATURE_OFFSET   0x40         // Offset from the image's physfirst address to the ROM signature.
#define ROM_SIGNATURE          0x43454345
#define ROM_TOC_POINTER_OFFSET 0x44         // Offset from the image's physfirst address to the TOC pointer.
#define ROM_TOC_OFFSET_OFFSET  0x48         // Offset from the image's physfirst address to the TOC offset (from physfirst).

#ifndef ASM_ONLY

#define ROM_EXTRA 9

struct info {                       /* Extra information header block      */
    uint32_t   rva;            /* Virtual relative address of info    */
    uint32_t   size;           /* Size of information block           */
};

typedef struct e32_rom {
    uint16_t  e32_objcnt;     /* Number of memory objects            */
    uint16_t  e32_imageflags; /* Image flags                         */
    uint32_t   e32_entryrva;   /* Relative virt. addr. of entry point */
    uint32_t   e32_vbase;      /* Virtual base address of module      */
    uint16_t  e32_subsysmajor;/* The subsystem major version number  */
    uint16_t  e32_subsysminor;/* The subsystem minor version number  */
    uint32_t   e32_stackmax;   /* Maximum stack size                  */
    uint32_t   e32_vsize;      /* Virtual size of the entire image    */
    uint32_t   e32_sect14rva;  /* section 14 rva */
    uint32_t   e32_sect14size; /* section 14 size */
    uint32_t   e32_timestamp;  /* Time EXE/DLL was created/modified   */
    struct info     e32_unit[ROM_EXTRA]; /* Array of extra info units      */
    uint16_t  e32_subsys;     /* The subsystem type                  */
} e32_rom;

typedef struct o32_rom {
    uint32_t       o32_vsize;      /* Virtual memory size              */
    uint32_t       o32_rva;        /* Object relative virtual address  */
    uint32_t       o32_psize;      /* Physical file size of init. data */
    uint32_t       o32_dataptr;    /* Image pages offset               */
    uint32_t       o32_realaddr;   /* pointer to actual                */
    uint32_t       o32_flags;      /* Attribute flags for the object   */
} o32_rom;

typedef struct _ROMINFO {
    uint32_t   dwSlot_0_DllBase;
    uint32_t   dwSlot_1_DllBase;
    uint32_t     nROMs;
    // hi/lo pair follows
    // LARGE_INTEGER    rwInfo[nROMs];
} ROMINFO, *PROMINFO;


#endif

#define ROM_KERNEL_DEBUGGER_ENABLED 0x00000001
#define ROM_CONTAINS_KERNEL         0x00000002

#define IMAGE_FILE_MACHINE_ARM               0x01c0  // ARM Little-Endian
#define IMAGE_FILE_MACHINE_THUMB             0x01c2  // Thumb

//
//  ROM Header Structure - pTOC in NK points here
//
#ifdef ASM_ONLY
#define ROMHDR_dllfirst         0
#define ROMHDR_dlllast          4
#define ROMHDR_physfirst        8
#define ROMHDR_physlast         12
#define ROMHDR_nummods          16
#define ROMHDR_ulRAMStart       20
#define ROMHDR_ulRAMFree        24
#define ROMHDR_ulRAMEnd         28
#define ROMHDR_ulCopyEntries    32
#define ROMHDR_ulCopyOffset     36
#define ROMHDR_ulProfileLen     40
#define ROMHDR_ulProfileOffset  44
#define ROMHDR_numfiles         48
#define ROMHDR_ulObjstoreStart  52
#define ROMHDR_ulObjstoreLen    56
#define ROMHDR_ulDrivglobStart  60
#define ROMHDR_ulDrivglobLen    64
#define ROMHDR_usCPUType        68
#define ROMHDR_usMiscFlags      70
#define ROMHDR_pExtensions       72
#define ROMHDR_ulTrackingStart  76
#define ROMHDR_ulTrackingLen    80
#define SIZEOF_ROMHDR           84
#else
typedef struct ROMHDR {
    uint32_t   dllfirst;               // first DLL address
    uint32_t   dlllast;                // last DLL address
    uint32_t   physfirst;              // first physical address
    uint32_t   physlast;               // highest physical address
    uint32_t   nummods;                // number of TOCentry's
    uint32_t   ulRAMStart;             // start of RAM
    uint32_t   ulRAMFree;              // start of RAM free space
    uint32_t   ulRAMEnd;               // end of RAM
    uint32_t   ulCopyEntries;          // number of copy section entries
    uint32_t   ulCopyOffset;           // offset to copy section
    uint32_t   ulProfileLen;           // length of PROFentries RAM
    uint32_t   ulProfileOffset;        // offset to PROFentries
    uint32_t   numfiles;               // number of FILES
    uint32_t   ulKernelFlags;          // optional kernel flags from ROMFLAGS .bib config option
    uint32_t   ulFSRamPercent;         // Percentage of RAM used for filesystem
                                        // from FSRAMPERCENT .bib config option
                                        // byte 0 = #4K chunks/Mbyte of RAM for filesystem 0-2Mbytes 0-255
                                        // byte 1 = #4K chunks/Mbyte of RAM for filesystem 2-4Mbytes 0-255
                                        // byte 2 = #4K chunks/Mbyte of RAM for filesystem 4-6Mbytes 0-255
                                        // byte 3 = #4K chunks/Mbyte of RAM for filesystem > 6Mbytes 0-255

    uint32_t   ulDrivglobStart;        // device driver global starting address
    uint32_t   ulDrivglobLen;          // device driver global length
    uint16_t  usCPUType;              // CPU (machine) Type
    uint16_t  usMiscFlags;            // Miscellaneous flags
    uint32_t   pExtensions;            // pointer to ROM Header extensions
    uint32_t   ulTrackingStart;        // tracking memory starting address
    uint32_t   ulTrackingLen;          // tracking memory ending address
} ROMHDR;

typedef struct ROMChain_t {
    struct ROMChain_t *pNext;
    ROMHDR *pTOC;
} ROMChain_t;

#endif //ASM_ONLY

//
// ROM Header extension: PID
//
#define PID_LENGTH 10

#ifdef ASM_ONLY
#define ROMPID_dwPID 0
#define ROMPID_pNextExt (4 * PID_LENGTH)
#define ROMPID_name 0
#define ROMPID_type (4 * (PID_LENGTH - 4))
#define ROMPID_pdata (4 * (PID_LENGTH - 3))
#define ROMPID_length (4 * (PID_LENGTH - 2))
#define ROMPID_reserved (4 * (PID_LENGTH - 1))
#else
typedef struct ROMPID {
  union{
    uint32_t dwPID[PID_LENGTH];        // PID
    struct{
      char  name[(PID_LENGTH - 4) * sizeof(uint32_t)];
      uint32_t type;
      uint32_t pdata;
      uint32_t length;
      uint32_t reserved;
    }s;
  }u;
  uint32_t pNextExt;                 // pointer to next extension if any
} ROMPID, EXTENSION;

#endif // ASM_ONLY

//
//  Module Table of Contents - follows ROMHDR in image
//
#define FILE_ATTRIBUTE_READONLY			0x00000001
#define FILE_ATTRIBUTE_HIDDEN			0x00000002
#define FILE_ATTRIBUTE_SYSTEM			0x00000004
#define FILE_ATTRIBUTE_DIRECTORY		0x00000010
#define FILE_ATTRIBUTE_ARCHIVE			0x00000020
#define FILE_ATTRIBUTE_DEVICE			0x00000040
#define FILE_ATTRIBUTE_NORMAL			0x00000080

#ifdef ASM_ONLY
#define TOCentry_dwFileAttributes 0
#define TOCentry_ftTime         4
#define TOCentry_lpszFileSize   12
#define TOCentry_lpszFileName   16
#define TOCentry_ulE32Offset    20
#define TOCentry_ulO32Offset    24
#define TOCentry_ulLoadOffset   28
#define SIZEOF_TOCentry         32
#else
typedef struct _FILETIME {
  uint32_t dwLowDateTime;
  uint32_t dwHighDateTime;
} FILETIME, *PFILETIME;


typedef struct TOCentry {           // MODULE BIB section structure
    uint32_t dwFileAttributes;
    FILETIME ftTime;
    uint32_t nFileSize;
    uint32_t   lpszFileName;
    uint32_t   ulE32Offset;            // Offset to E32 structure
    uint32_t   ulO32Offset;            // Offset to O32 structure
    uint32_t   ulLoadOffset;           // MODULE load buffer offset
} TOCentry, *LPTOCentry;
#endif

//
//  Files Section Structure
//
#ifdef ASM_ONLY
#else
typedef struct FILESentry {         // FILES BIB section structure
    uint32_t dwFileAttributes;
    FILETIME ftTime;
    uint32_t nRealFileSize;
    uint32_t nCompFileSize;
    uint32_t   lpszFileName;
    uint32_t   ulLoadOffset;           // FILES load buffer offset
} FILESentry, *LPFILESentry;
#endif


//
//  Copy Section Structure
//
#ifdef ASM_ONLY
#define COPYentry_ulSource      0
#define COPYentry_ulDest        4
#define COPYentry_ulCopyLen     8
#define COPYentry_ulDestLen     12
#define SIZEOF_COPYentry        16
#else
typedef struct COPYentry {
    uint32_t   ulSource;               // copy source address
    uint32_t   ulDest;                 // copy destination address
    uint32_t   ulCopyLen;              // copy length
    uint32_t   ulDestLen;              // copy destination length
                                    // (zero fill to end if > ulCopyLen)
} COPYentry;
#endif

//
//  Profile Code Section Structure
//
#ifdef ASM_ONLY
#define PROFentry_ulModNum      0
#define PROFentry_ulSectNum     4
#define PROFentry_ulStartAddr   8
#define PROFentry_ulEndAddr     12
#define PROFentry_ulHits        16
#define PROFentry_ulNumSym      20
#define PROFentry_ulHitAddress  24
#define PROFentry_ulSymAddress  28
#define SIZEOF_PROFentry        32
#else
typedef struct PROFentry {          // code section profile entry
    uint32_t   ulModNum;               // module number in table of contents
    uint32_t   ulSectNum;              // section number in o32
    uint32_t   ulStartAddr;            // starting address of section
    uint32_t   ulEndAddr;              // ending address of section
    uint32_t   ulHits;                 // number of hits in section
    uint32_t   ulNumSym;               // number of symbols in section
    uint32_t   ulHitAddress;           // address to hit table
    uint32_t   ulSymAddress;           // address to symbol table
} PROFentry;
#endif

//
//  Profile Symbol Section
//
//  Profile symbol section contains function address and hit counter for
//  each function followed by ASCIIZ symbols.
//
#ifdef ASM_ONLY
#define SYMentry_ulFuncAddress  0
#define SYMentry_ulFuncHits     4
#define SIZEOF_SYMentry         8
#else
typedef struct SYMentry {
    uint32_t   ulFuncAddress;          // function starting address
    uint32_t   ulFuncHits;             // function hit counter
} SYMentry;
#endif

//
// Multiple-XIP data structures
//
#define MAX_ROM                 32      // max numbler of XIPs
#define XIP_NAMELEN             32      // max name length of XIP
#define ROM_CHAIN_OFFSET        0       // offset for XIPCHAIN_INFO

//
// flags for XIP entries
//
#define ROMXIP_OK_TO_LOAD       0x0001
#define ROMXIP_IS_SIGNED        0x0002

//
// XIP chain entry
//
#ifdef ASM_ONLY
#define XIPCHAIN_ENTRY_pvAddr           0
#define XIPCHAIN_ENTRY_dwLength         4
#define XIPCHAIN_ENTRY_dwMaxLength      8
#define XIPCHAIN_ENTRY_usOrder          12
#define XIPCHAIN_ENTRY_usFlags          14
#define XIPCHAIN_ENTRY_dwVersion        16
#define XIPCHAIN_ENTRY_szName           20
#define XIPCHAIN_ENTRY_dwAlgoFlags      (20 + sizeof(CHAR) * XIP_NAMELEN)
#define XIPCHAIN_ENTRY_dwKeyLen         (24 + sizeof(CHAR) * XIP_NAMELEN)
#define XIPCHAIN_ENTRY_byPublicKey      (28 + sizeof(CHAR) * XIP_NAMELEN)
#define SIZEOF_XIPCHAIN_ENTRY           656

#define XIPCHAIN_INFO_cXIPs             0
#define XIPCHAIN_INFO_xipEntryStart     4
#define SIZEOF_XIPCHAIN_INFO            (SIZEOF_XIPCHAIN_ENTRY + 4)

#define XIPCHAIN_SUMMARY_pvAddr           0
#define XIPCHAIN_SUMMARY_dwMaxLength      4
#define XIPCHAIN_SUMMARY_usOrder          8
#define XIPCHAIN_SUMMARY_usFlags          10
#define XIPCHAIN_SUMMARY_reserved         12

#else
typedef struct _XIPCHAIN_ENTRY {
    uint32_t  pvAddr;                 // address of the XIP
    uint32_t   dwLength;               // the size of the XIP
    uint32_t   dwMaxLength;            // the biggest it can grow to
    uint16_t  usOrder;                // where to put into ROMChain_t
    uint16_t  usFlags;                // flags/status of XIP
    uint32_t   dwVersion;              // version info
    char    szName[XIP_NAMELEN];    // Name of XIP, typically the bin file's name, w/o .bin
    uint32_t   dwAlgoFlags;            // algorithm to use for signature verification
    uint32_t   dwKeyLen;               // length of key in byPublicKey
    uint8_t    byPublicKey[596];       // public key data
} __attribute__((packed)) XIPCHAIN_ENTRY, *PXIPCHAIN_ENTRY;

typedef struct _XIPCHAIN_SUMMARY {
    uint32_t  pvAddr;                 // address of the XIP
    uint32_t   dwMaxLength;            // the biggest it can grow to
    uint16_t  usOrder;                // where to put into ROMChain_t
    uint16_t  usFlags;                // flags/status of XIP
    uint32_t   reserved;               // for future use
} __attribute__((packed)) XIPCHAIN_SUMMARY, *PXIPCHAIN_SUMMARY;

typedef struct _XIPCHAIN_INFO {
    uint32_t cXIPs;
    //
    // may contain more than one entry, but we only need the address of first one
    //
    XIPCHAIN_ENTRY xipEntryStart;
} XIPCHAIN_INFO, *PXIPCHAIN_INFO;
#endif

//
// Compressed RAM-image data structures
//
#define RAMIMAGE_COMPRESSION_VERSION       5
#define RAMIMAGE_COMPRESSION_BLOCK_SIZE    4096
#define RAMIMAGE_COMPRESSION_SIGNATURE     0x58505253

#ifdef ASM_ONLY
#else
typedef struct __COMPRESSED_RAMIMAGE_HEADER {
    uint8_t bReserved[48];
    uint32_t dwVersion;
    uint32_t dwHeaderSize;
    uint32_t dwCompressedBlockCount;
    uint32_t dwCompressedBlockSize;
    uint32_t dwSignature;              // keep at ROM_OFFSET (64 bytes)
    uint16_t  wBlockSizeTable[2];       // block size table
} COMPRESSED_RAMIMAGE_HEADER, *PCOMPRESSED_RAMIMAGE_HEADER;
#endif // ASM_ONLY
