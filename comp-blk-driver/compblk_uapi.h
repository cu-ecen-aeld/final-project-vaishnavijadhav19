#ifndef COMPBLK_UAPI_H
#define COMPBLK_UAPI_H

#include <linux/types.h>
#include <linux/ioctl.h>

#define COMPBLK_IOCTL_MAGIC      'C'
#define COMPBLK_STORED_MAX       4096  /* debug dump cap */

/* --- Stats you want to read from kernel --- */
struct compblk_ioctl_stats {
    __u64 cstore_used;
    __u64 blocks_compressed_ok;
    __u64 blocks_stored_raw;
    __u64 compress_fail;
    __u64 store_enospc;
    __u64 bytes_in_total;
    __u64 bytes_out_total;
};

/* --- Query a single map entry: user sets bi, kernel fills rest --- */
struct compblk_ioctl_map_entry {
    __u32 bi;      /* in */
    __u32 off;     /* out */
    __u32 len;     /* out */
    __u8  flags;   /* out */
    __u8  valid;   /* out */
    __u16 _pad;
};

/* --- Shared struct for EXPORT + IMPORT --- */
struct compblk_ioctl_blockdump {
    __u32 bi;          /* in */
    __u32 stored_len;  /* out for export, in for import */
    __u8  flags;       /* out for export, in for import */
    __u8  valid;       /* out for export, in (optional) for import */
    __u16 _pad;
    __u8  data[COMPBLK_STORED_MAX];  /* stored blob bytes */
};

#define COMPBLK_IOCTL_GET_STATS        _IOR (COMPBLK_IOCTL_MAGIC, 0x01, struct compblk_ioctl_stats)
#define COMPBLK_IOCTL_GET_MAP_ENTRY    _IOWR(COMPBLK_IOCTL_MAGIC, 0x02, struct compblk_ioctl_map_entry)
#define COMPBLK_IOCTL_DUMP_STORED      _IOWR(COMPBLK_IOCTL_MAGIC, 0x03, struct compblk_ioctl_blockdump)

/* NEW: import stored blob for block bi (user -> kernel) */
#define COMPBLK_IOCTL_IMPORT_STORED    _IOWR(COMPBLK_IOCTL_MAGIC, 0x04, struct compblk_ioctl_blockdump)

#endif

