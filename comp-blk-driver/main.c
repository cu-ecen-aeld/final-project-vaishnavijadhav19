/*

   Author Name: Vaishnavi Jadhav
   File name: main.c 
   application: This file implements the complete compressed block-device driver. It registers
		   the block device, allocates a 64 MB virtual disk, manages raw and compressed
		   data storage, and provides full read/write, compression, decompression, and
		   IOCTL support. It sets up the blk-mq request queue and gendisk on module load
		   and frees all allocated resources when the module is unloaded.

	        
   Note: All Sprint 3 tasks have been completed, including adding full compression and decompression support,
	managing the compressed block store, updating map entries, and implementing custom IOCTL commands
	for exporting and importing compressed data and metadata.




*/

#include <linux/module.h>       
#include <linux/init.h>        
#include <linux/kernel.h>     
#include <linux/fs.h>          
#include <linux/blkdev.h>      
#include <linux/blk-mq.h>       
#include <linux/highmem.h>

#include <linux/vmalloc.h>      
#include <linux/mutex.h>      
#include <linux/atomic.h>       
#include <linux/uaccess.h>    
#include <linux/version.h>      

#include <linux/lzo.h>
#include <linux/slab.h>
#include <linux/minmax.h>
#include "compblk_uapi.h"




#include "comp_blkdrv.h"




#define MAX_WRITES  4096 

MODULE_LICENSE("Dual BSD/GPL");

#define DEVICE_NAME        "compblkdev"

#define COMPBLK_MINORS     1

#define COMPBLK_DISK_NAME  "compblk0"


#define COMPBLK_DISK_SIZE (64 * 1024 * 1024)

//#define COMPBLK_DISK_SIZE   (256 * 1024 * 1024)   // 256 MB virtual disk
#define COMPBLK_SECTOR_SIZE 512
#define COMPBLK_NSECTORS    (COMPBLK_DISK_SIZE / COMPBLK_SECTOR_SIZE)

#define COMPBLK_LOGICAL_BLK_SIZE     4096u
#define COMPBLK_LZO_BOUND(sz)        ((sz) + ((sz)/16u) + 64u + 3u)
#define COMPBLK_NUM_LBLKS            (COMPBLK_DISK_SIZE / COMPBLK_LOGICAL_BLK_SIZE)
#define BLKFLAG_RAW                  (1u << 0)




// this keeps info about one write (where it started and ended and how many bytes were written)
struct write_info
{
    sector_t start_sector;
    sector_t end_sector;
    size_t bytes_written;
};

// this is one entry in my write table. keeps info about each write that is stored
struct write_entry 
{
    sector_t start_sector;
    sector_t num_sectors;
    size_t byte_len;
    bool valid;
};

// this is one map entry for compressed data (offset and length in compression store)
struct compblk_map_entry 
{
    u32 off;
    u32 len;
    u8  flags;   
    u8  valid;
};

// this is the main device struct. 
//it holds everything for the compblk driver
struct compblk_dev
{
    struct gendisk       *gd;
    struct blk_mq_tag_set tag_set;
    int                   major;

    void                 *data;
    atomic_t              open_count;
    struct mutex          lock;

    size_t                used_space;
    
    struct write_info      last_write;

    struct write_entry     table[MAX_WRITES];
    
    int                   write_count;

    
    u8                   *cstore;
    
    size_t                cstore_used;
    
    struct compblk_map_entry *map;
    
    void                 *lzo_workmem;

    
    u64 blocks_compressed_ok;
    u64 blocks_stored_raw;
    u64 compress_fail;
    u64 store_enospc;
    u64 bytes_in_total;
    u64 bytes_out_total;
};

// this is my main device instance
static struct compblk_dev comp_dev;



static const struct block_device_operations compblk_fops = {
    .owner = THIS_MODULE,
    .open = compblk_open,
    .release = compblk_release,
    .ioctl = compblk_ioctl,
};





// this function stores one logical 4096-byte block into my compressed store
// it tries to compress using LZO, but if compression is bad, it stores raw data
static int compblk_store_block_lzo(struct compblk_dev *dev, u32 bi, const u8 *in4096)
{
    const u32 in_len = COMPBLK_LOGICAL_BLK_SIZE; 
    const u32 bound  = (u32)COMPBLK_LZO_BOUND(in_len); // get max possible compressed size

    u8 *tmp;  
    
    size_t out_len = 0; 
    
    int r; 


    // invalid block index
    if (bi >= COMPBLK_NUM_LBLKS)
        return -EINVAL;


   // allocate temp buffer for compression
    tmp = kmalloc(bound, GFP_KERNEL);
    if (!tmp)
        return -ENOMEM;

     // try to compress the block
    r = lzo1x_1_compress(in4096, (size_t)in_len, tmp, &out_len, dev->lzo_workmem);


    //  how many input bytes have been processed
    dev->bytes_in_total += in_len;

    
    if (r != LZO_E_OK || out_len >= in_len)
     {
         // no space left to store 4096 raw bytes
        if (dev->cstore_used + in_len > COMPBLK_DISK_SIZE) 
        {
            dev->store_enospc++;
            kfree(tmp);
            return -ENOSPC;
            
        }
        
        // record map entry for RAW storage
        dev->map[bi].off   = (u32)dev->cstore_used;
        dev->map[bi].len   = in_len;
        dev->map[bi].flags = BLKFLAG_RAW;
        dev->map[bi].valid = 1;



        // store the raw block
        memcpy(dev->cstore + dev->cstore_used, in4096, in_len);
        dev->cstore_used += in_len;


        // stats
        dev->blocks_stored_raw++;
        dev->bytes_out_total += in_len;


        // log messages
        if (r != LZO_E_OK) 
        {
            dev->compress_fail++;
            pr_info("compblk: blk=%u LZO_FAIL r=%d -> RAW in=%u out=%u\n",
                    bi, r, in_len, in_len);
        } 
        else 
        {
            pr_info("compblk: blk=%u RAW(no gain) in=%u out=%u\n",
                    bi, in_len, in_len);
        }
        

        kfree(tmp);
        
        return 0;
    }


 // CASE 2: compression SUCCEEDED and compressed size < 4096
    // check space for compressed block
    if (dev->cstore_used + out_len > COMPBLK_DISK_SIZE) 
    {
        dev->store_enospc++;
        
        kfree(tmp);
        
        return -ENOSPC;
    }

    dev->map[bi].off   = (u32)dev->cstore_used;
    dev->map[bi].len   = (u32)out_len;
    dev->map[bi].flags = 0;    
    dev->map[bi].valid = 1;


    // store compressed bytes
    memcpy(dev->cstore + dev->cstore_used, tmp, out_len);
    
    dev->cstore_used += out_len;

    dev->blocks_compressed_ok++;
    
    dev->bytes_out_total += (u64)out_len;
    {
        u32 pct = (u32)((out_len * 100u) / in_len);
        
        
        pr_info("compblk: blk=%u LZO in=%u out=%zu saved=%u (%u%%)\n",
                bi, in_len, out_len, (unsigned)(in_len - out_len), pct);
    }

    kfree(tmp);
    
    return 0;
}



// load one 4096-byte logical block into out4096[]
// block may be raw or compressed depending on map entry
static int compblk_load_block_4k(struct compblk_dev *dev, u32 bi, u8 out4096[COMPBLK_LOGICAL_BLK_SIZE])
{
    u32 off, len;
    u8 flags, valid;
    u64 end;
    int r;

    if (bi >= COMPBLK_NUM_LBLKS)
        return -EINVAL;

    // read the map entry for this block
    off   = dev->map[bi].off;
    len   = dev->map[bi].len;
    flags = dev->map[bi].flags;
    valid = dev->map[bi].valid;

 // If no entry stored, then read from the original raw disk backing
    if (!valid) 
    {
   
    memcpy(out4096,
           (u8 *)dev->data + ((u64)bi * (u64)COMPBLK_LOGICAL_BLK_SIZE),
           COMPBLK_LOGICAL_BLK_SIZE);
    return 0;
}


     // check for overflows or out-of-range addresses
    end = (u64)off + (u64)len;
    
    if (end < (u64)off ||
        end > (u64)dev->cstore_used ||
        end > (u64)COMPBLK_DISK_SIZE)
        return -EIO;

    if (flags & ~BLKFLAG_RAW)
        return -EINVAL;

    if (flags & BLKFLAG_RAW)
     {
        if (len != COMPBLK_LOGICAL_BLK_SIZE)
            return -EIO;
        
         // simply copy raw bytes into output buffer
        memcpy(out4096, dev->cstore + off, COMPBLK_LOGICAL_BLK_SIZE);
        return 0;
    }

    
    if (len == 0 || len >= COMPBLK_LOGICAL_BLK_SIZE)
        return -EIO;

    {
        size_t out_len = COMPBLK_LOGICAL_BLK_SIZE;
        
        // decompress into output buffer
        r = lzo1x_decompress_safe(dev->cstore + off, (size_t)len, out4096, &out_len);
        
        // decompression must succeed AND output must be exactly 4096 bytes
        if (r != LZO_E_OK || out_len != COMPBLK_LOGICAL_BLK_SIZE)
            return -EIO;
    }

    return 0;
}

// read a range of bytes starting at pos, length = len
// this may span across multiple 4096-byte blocks
static int compblk_read_range(struct compblk_dev *dev, u64 pos, u8 *dst, size_t len)
{
    u8 *tmp;
    
    // temp buffer to hold one full 4096-byte block (raw or decompressed)
    tmp = kmalloc(COMPBLK_LOGICAL_BLK_SIZE, GFP_KERNEL);
    
    if (!tmp)
        return -ENOMEM;


    // keep reading until all requested bytes are copied out
    while (len) 
    {
        u32 bi = (u32)(pos / COMPBLK_LOGICAL_BLK_SIZE);
        u32 in_blk_off = (u32)(pos % COMPBLK_LOGICAL_BLK_SIZE);
        
         // how many bytes to take from this block (until block end or request end)
        size_t chunk = min_t(size_t, len,
                             (size_t)COMPBLK_LOGICAL_BLK_SIZE - in_blk_off);
        int rc;

        if (bi >= COMPBLK_NUM_LBLKS)
         {
            kfree(tmp);
            return -EIO;
        }

        rc = compblk_load_block_4k(dev, bi, tmp);
        if (rc)
         {
            kfree(tmp);
            return rc;
        }

        memcpy(dst, tmp + in_blk_off, chunk);

        dst += chunk;
        pos += chunk;
        len -= chunk;
    }

    kfree(tmp);
    return 0;
}



/*
 * Handles READ requests from the block layer.
 * Uses compblk_read_range() to load raw or decompressed data for each segment
 * Ensures reads never go past disk size and returns the exact bytes requested
 * Completes the request once all BIO segments are processed
 */


static blk_status_t compblk_handle_read(struct compblk_dev *dev, struct request *rq)
{
    struct bio_vec bvec;
    
    struct req_iterator iter;

    sector_t start_sector = blk_rq_pos(rq);

    u64 pos = (u64)start_sector * (u64)COMPBLK_SECTOR_SIZE;
    
    size_t copied = 0;
    
    
     // reject reads that start past the end of virtual disk
    if (pos >= COMPBLK_DISK_SIZE) 
    {
        pr_err("compblk: READ beyond end of disk\n");
        
        blk_mq_end_request(rq, BLK_STS_IOERR);
        return BLK_STS_IOERR;
    }

    mutex_lock(&dev->lock);
    
    
    // iterate over each BIO segment from the block layer
    rq_for_each_segment(bvec, rq, iter)
     {
        void *kaddr = kmap_local_page(bvec.bv_page);
        
        
        
        u8 *dst = (u8 *)kaddr + bvec.bv_offset;
        
        size_t len = bvec.bv_len;  // number of bytes requested in this segment

        if (pos >= COMPBLK_DISK_SIZE)
         {
            kunmap_local(kaddr);
            break;
        }
        
        
        
        if (pos + len > COMPBLK_DISK_SIZE)
            len = (size_t)(COMPBLK_DISK_SIZE - pos);
            
            
            
        //perform the read for this BIO segment
        if (len)
         {
            int rc = compblk_read_range(dev, pos, dst, len);
            
            
            if (rc)
             {
                kunmap_local(kaddr);
                mutex_unlock(&dev->lock);
                pr_err("compblk: READ decompress failed rc=%d\n", rc);
                
                
                blk_mq_end_request(rq, BLK_STS_IOERR);
                return BLK_STS_IOERR;
            }
            
            
            
            pos += len;
            copied += len;
        }

        kunmap_local(kaddr);

        if (len == 0)
            break;
    }

    mutex_unlock(&dev->lock);

    pr_info("compblk: READ complete start_sector=%llu bytes=%zu\n", (unsigned long long)start_sector, copied);
            
            

    blk_mq_end_request(rq, BLK_STS_OK);
    
    
    return BLK_STS_OK;
}

 
 
/*
 * Handles WRITE requests from the block layer.
 * First writes raw data into dev->data
 * Then compresses the touched 4K blocks into dev->cstore and updates map[].
 * records metadata about the write and completes the request
 */

static blk_status_t compblk_handle_write(struct compblk_dev *dev, struct request *rq)
{
    struct bio_vec bvec;
    
    struct req_iterator iter;


    // sector number number is obtained here
    sector_t start_sector = blk_rq_pos(rq);
    
    unsigned long start_off = (unsigned long)start_sector * COMPBLK_SECTOR_SIZE;
    
    unsigned long offset    = start_off;

    size_t total_bytes = 0;
    

    if (start_off >= COMPBLK_DISK_SIZE) 
    {
        pr_err("compblk: invalid start offset beyond disk size\n");
        
        blk_mq_end_request(rq, BLK_STS_IOERR);
        
        return BLK_STS_IOERR;
    }

    mutex_lock(&dev->lock);

    
    rq_for_each_segment(bvec, rq, iter)
     {
        void *kaddr = kmap_local_page(bvec.bv_page);
        
        u8 *src     = (u8 *)kaddr + bvec.bv_offset;
        
        size_t len  = bvec.bv_len;

        if (offset >= COMPBLK_DISK_SIZE)
        {
            kunmap_local(kaddr);
            break;
        }




        if (offset + len > COMPBLK_DISK_SIZE)
            len = COMPBLK_DISK_SIZE - offset;

        if (len)
         {
            memcpy((u8 *)dev->data + offset, src, len);
            
            offset      += len;
            total_bytes += len;
        }

        kunmap_local(kaddr);

        if (len == 0)
            break;
    }

  
    if (total_bytes)
     {
        unsigned long end_off = start_off + total_bytes; 
        
        
        u32 first_bi = (u32)(start_off / COMPBLK_LOGICAL_BLK_SIZE);
        
        u32 last_bi  = (u32)((end_off - 1) / COMPBLK_LOGICAL_BLK_SIZE);
        u32 bi;

        for (bi = first_bi; bi <= last_bi && bi < COMPBLK_NUM_LBLKS; bi++) 
        {
            const u8 *blk = (const u8 *)dev->data +
                            ((unsigned long)bi * COMPBLK_LOGICAL_BLK_SIZE);
                            
            int rc = compblk_store_block_lzo(dev, bi, blk);
            
            
            
            if (rc) 
            {
	    pr_warn("compblk: store failed blk=%u rc=%d -> map INVALID,and it will read as zeros\n",
		    bi, rc);
	    dev->map[bi].valid = 0;  
	   
	    }

            
            
        }
    }

  
    dev->used_space += total_bytes;
    
    if (dev->used_space > COMPBLK_DISK_SIZE)
        dev->used_space = COMPBLK_DISK_SIZE;

    dev->last_write.start_sector  = start_sector;
    dev->last_write.bytes_written = total_bytes;

    if (total_bytes) 
    {
        sector_t end_sector = start_sector + (total_bytes / COMPBLK_SECTOR_SIZE) - 1;
        
        dev->last_write.end_sector = end_sector;

        pr_info("compblk: WRITE complete — start_sector=%llu end_sector=%llu bytes=%zu\n",
                (unsigned long long)start_sector,
                (unsigned long long)end_sector,
                total_bytes);
    } 
    
    else 
    {
        dev->last_write.end_sector = start_sector;
        
        pr_info("compblk: WRITE complete — start_sector=%llu (0 bytes)\n",
                (unsigned long long)start_sector);
    }

    mutex_unlock(&dev->lock);

    blk_mq_end_request(rq, BLK_STS_OK);
    return BLK_STS_OK;
}




static blk_status_t compblk_queue_rq(struct blk_mq_hw_ctx *hctx,
                                     const struct blk_mq_queue_data *bd)
{
    struct request *rq = bd->rq;
    struct compblk_dev *dev = rq->q->queuedata;

    blk_mq_start_request(rq);

   
    switch (req_op(rq))
   {
    case REQ_OP_READ:
        return compblk_handle_read(dev, rq);

    case REQ_OP_WRITE:
        return compblk_handle_write(dev, rq);

    case REQ_OP_FLUSH:
       
        blk_mq_end_request(rq, BLK_STS_OK);
        return BLK_STS_OK;

    case REQ_OP_DISCARD:
    case REQ_OP_WRITE_ZEROES:
       
        blk_mq_end_request(rq, BLK_STS_OK);
        return BLK_STS_OK;

    default:
        pr_warn("compblk: unsupported req_op=%u\n", req_op(rq));
        blk_mq_end_request(rq, BLK_STS_IOERR);
        return BLK_STS_IOERR;
    }
}



/*
 * Called when the block device is opened.
 * 
 */

static int compblk_open(struct block_device *bdev, fmode_t mode)
{
    struct compblk_dev *dev = bdev->bd_disk->private_data;

    if (atomic_inc_return(&dev->open_count) == 1)
        pr_info("compblk: first open\n");

    pr_info("compblk: opened (users = %d, mode = 0x%x)\n",
            atomic_read(&dev->open_count), mode);

    return 0;
}


/*
 * Called when the block device is closed.
 * 
 */
static void compblk_release(struct gendisk *gd, fmode_t mode)
{
    struct compblk_dev *dev = gd->private_data;

    if (atomic_dec_return(&dev->open_count) == 0)
        pr_info("compblk: last close\n");
}




/*
 * Handles all custom IOCTL commands for the compblk driver.
 * Used to get stats, read map info, dump compressed blocks
 * import compressed blocks back into the driver
 *this lets user-space access the compressed data and metadata
 */

static int compblk_ioctl(struct block_device *bdev, fmode_t mode,
                         unsigned int cmd, unsigned long arg)
{
    struct compblk_dev *dev;
    void __user *argp = (void __user *)arg;

    if (!bdev || !bdev->bd_disk)
        return -ENODEV;

    dev = bdev->bd_disk->private_data;
    if (!dev)
        return -ENODEV;

    
    if (_IOC_TYPE(cmd) != COMPBLK_IOCTL_MAGIC)
        return -ENOTTY;

    
    switch (cmd) 
   {
    case COMPBLK_IOCTL_GET_STATS:
        break;

    case COMPBLK_IOCTL_GET_MAP_ENTRY:
    case COMPBLK_IOCTL_DUMP_STORED:
    case COMPBLK_IOCTL_IMPORT_STORED:
        if (!dev->map || !dev->cstore)
            return -ENXIO;
        break;

    default:
        return -ENOTTY;
    }

    switch (cmd)
     {

    case COMPBLK_IOCTL_GET_STATS: {
        struct compblk_ioctl_stats s;

        mutex_lock(&dev->lock);
        s.cstore_used          = (u64)dev->cstore_used;
        s.blocks_compressed_ok = dev->blocks_compressed_ok;
        s.blocks_stored_raw    = dev->blocks_stored_raw;
        s.compress_fail        = dev->compress_fail;
        s.store_enospc         = dev->store_enospc;
        s.bytes_in_total       = dev->bytes_in_total;
        s.bytes_out_total      = dev->bytes_out_total;
        mutex_unlock(&dev->lock);

        return copy_to_user(argp, &s, sizeof(s)) ? -EFAULT : 0;
    }
    
    

    case COMPBLK_IOCTL_GET_MAP_ENTRY:
     {
        struct compblk_ioctl_map_entry e;

        if (copy_from_user(&e, argp, sizeof(e)))
            return -EFAULT;

        if (e.bi >= COMPBLK_NUM_LBLKS)
            return -EINVAL;

        mutex_lock(&dev->lock);
        e.off   = dev->map[e.bi].off;
        e.len   = dev->map[e.bi].len;
        e.flags = dev->map[e.bi].flags;
        e.valid = dev->map[e.bi].valid;
        mutex_unlock(&dev->lock);

        return copy_to_user(argp, &e, sizeof(e)) ? -EFAULT : 0;
    }

   
    case COMPBLK_IOCTL_DUMP_STORED: 
    {
        struct compblk_ioctl_blockdump *d;
        u32 bi, off, len;
        u8 flags, valid;
        u32 copy_len;
        u64 end;

        d = kzalloc(sizeof(*d), GFP_KERNEL);
        if (!d)
            return -ENOMEM;

        if (copy_from_user(d, argp, sizeof(*d))) 
        {
            kfree(d);
            return -EFAULT;
        }

        bi = d->bi;
        if (bi >= COMPBLK_NUM_LBLKS)
         {
            kfree(d);
            return -EINVAL;
        }

        mutex_lock(&dev->lock);

        off   = dev->map[bi].off;
        len   = dev->map[bi].len;
        flags = dev->map[bi].flags;
        valid = dev->map[bi].valid;

        d->flags = flags;
        d->valid = valid;

        if (!valid)
         {
            d->stored_len = 0;
            mutex_unlock(&dev->lock);
        } 
        else 
        {
            
            end = (u64)off + (u64)len;
            if (end < (u64)off || end > (u64)dev->cstore_used ||end > (u64)COMPBLK_DISK_SIZE) 
             {
                mutex_unlock(&dev->lock);
                kfree(d);
                return -EIO;
            }

            copy_len = min_t(u32, len, (u32)COMPBLK_STORED_MAX);

            end = (u64)off + (u64)copy_len;
            
            
            if (end < (u64)off || end > (u64)dev->cstore_used || end > (u64)COMPBLK_DISK_SIZE) 
            {
                mutex_unlock(&dev->lock);
                kfree(d);
                return -EIO;
            }

            memcpy(d->data, dev->cstore + off, copy_len);
            
            d->stored_len = copy_len;

            mutex_unlock(&dev->lock);
        }

        if (copy_to_user(argp, d, sizeof(*d)))
        {
            kfree(d);
            return -EFAULT;
        }

        kfree(d);
        return 0;
    }

   
    case COMPBLK_IOCTL_IMPORT_STORED:
     {
        struct compblk_ioctl_blockdump *d;
        u32 bi, len;
        u8 flags;
        u32 off;
        u64 end;

        d = kzalloc(sizeof(*d), GFP_KERNEL);
        if (!d)
            return -ENOMEM;

        if (copy_from_user(d, argp, sizeof(*d))) 
        {
            kfree(d);
            return -EFAULT;
        }

        bi    = d->bi;
        len   = d->stored_len;
        
        flags = d->flags;

        if (bi >= COMPBLK_NUM_LBLKS)
         {
            kfree(d);
            return -EINVAL;
        }

        if (len == 0 || len > COMPBLK_STORED_MAX)
        {
            kfree(d);
            return -EINVAL;
        }

       
        if (flags & BLKFLAG_RAW)
         {
            if (len != COMPBLK_LOGICAL_BLK_SIZE)
             {
                kfree(d);
                return -EINVAL;
            }
        } 
        else
         {
            if (len >= COMPBLK_LOGICAL_BLK_SIZE) 
            {
                kfree(d);
                return -EINVAL;
            }
        }

        mutex_lock(&dev->lock);

       
        end = (u64)dev->cstore_used + (u64)len;
        
        
        if (end > (u64)COMPBLK_DISK_SIZE)
         {
            dev->store_enospc++;
            mutex_unlock(&dev->lock);
            
            kfree(d);
            return -ENOSPC;
        }

        off = (u32)dev->cstore_used;

        memcpy(dev->cstore + off, d->data, len);
        dev->cstore_used += len;

        dev->map[bi].off   = off;
        dev->map[bi].len   = len;
        dev->map[bi].flags = flags;
        dev->map[bi].valid = 1;

       
        d->valid = 1;

        mutex_unlock(&dev->lock);

        if (copy_to_user(argp, d, sizeof(*d)))
         {
            kfree(d);
            return -EFAULT;
        }

        kfree(d);
        return 0;
    }

    default:
        return -ENOTTY;
    }
}



static const struct blk_mq_ops compblk_mq_ops = {
	.queue_rq = compblk_queue_rq,
};



static void compblk_destroy_disk(struct compblk_dev *dev)
{
    if (dev->gd) 
    {
        del_gendisk(dev->gd);
        put_disk(dev->gd);
        dev->gd = NULL;
    }

   
    if (dev->tag_set.tags) 
    {
        blk_mq_free_tag_set(&dev->tag_set);
        memset(&dev->tag_set, 0, sizeof(dev->tag_set));
    }

    if (dev->lzo_workmem) { vfree(dev->lzo_workmem); dev->lzo_workmem = NULL; }
    if (dev->cstore)      { vfree(dev->cstore);      dev->cstore = NULL; }
    if (dev->map)         { vfree(dev->map);         dev->map = NULL; }
    dev->cstore_used = 0;

    if (dev->data) 
    {
        vfree(dev->data);
        dev->data = NULL;
        pr_info("compblk: freed virtual-disk memory\n");
    }
}


/*
 * Creates and sets up the virtual block device.
 * Allocates memory for the disk, initializes locks and counters,
 * sets up blk-mq tag set, creates the gendisk, and registers it.
 */
static int compblk_create_disk(struct compblk_dev *dev)
{
    int ret;

   
    dev->data = NULL;
    dev->gd   = NULL;
    memset(&dev->tag_set, 0, sizeof(dev->tag_set));

   
    dev->data = vmalloc(COMPBLK_DISK_SIZE);
    
    if (!dev->data) 
    {
        pr_err("compblk: failed to allocate %u bytes for virtual disk\n",
               COMPBLK_DISK_SIZE);
        return -ENOMEM;
    }
    

    memset(dev->data, 0, COMPBLK_DISK_SIZE);
    
    pr_info("compblk: allocated %u bytes virtual disk\n", COMPBLK_DISK_SIZE);
    
    
    dev->map = vzalloc(sizeof(*dev->map) * COMPBLK_NUM_LBLKS);
    
	if (!dev->map) { compblk_destroy_disk(dev); return -ENOMEM; }

	dev->cstore = vmalloc(COMPBLK_DISK_SIZE); 
	
	if (!dev->cstore) { compblk_destroy_disk(dev); return -ENOMEM; }
	dev->cstore_used = 0;

	dev->lzo_workmem = vmalloc(LZO1X_1_MEM_COMPRESS);
	if (!dev->lzo_workmem) { compblk_destroy_disk(dev); return -ENOMEM; }

	
	dev->blocks_compressed_ok = 0;
	dev->blocks_stored_raw    = 0;
	dev->compress_fail        = 0;
	dev->store_enospc         = 0;
	dev->bytes_in_total       = 0;
	dev->bytes_out_total      = 0;



    dev->used_space = 0;
    
    dev->last_write.start_sector  = 0;
    
    dev->last_write.end_sector    = 0;
    dev->last_write.bytes_written = 0;

    mutex_init(&dev->lock);
    
    atomic_set(&dev->open_count, 0);

   
    dev->tag_set.ops          = &compblk_mq_ops;
    dev->tag_set.nr_hw_queues = 1;
    dev->tag_set.queue_depth  = 128;
    dev->tag_set.numa_node    = NUMA_NO_NODE;
    dev->tag_set.flags        = BLK_MQ_F_SHOULD_MERGE;

    ret = blk_mq_alloc_tag_set(&dev->tag_set);
    if (ret)
     {
        pr_err("compblk: blk_mq_alloc_tag_set failed: %d\n", ret);
        
        compblk_destroy_disk(dev);  
        return ret;
    }

    
    dev->gd = blk_mq_alloc_disk(&dev->tag_set, dev);
    
    if (IS_ERR(dev->gd))
     {
        ret = PTR_ERR(dev->gd);
        
        dev->gd = NULL;
        pr_err("compblk: blk_mq_alloc_disk failed: %d\n", ret);
        compblk_destroy_disk(dev);
        return ret;
    }

    dev->gd->major        = dev->major;
    dev->gd->first_minor  = 0;
    dev->gd->minors       = COMPBLK_MINORS;
    dev->gd->fops         = &compblk_fops;
    dev->gd->private_data = dev;
    dev->gd->flags       |= GENHD_FL_NO_PART;
    strscpy(dev->gd->disk_name, COMPBLK_DISK_NAME, DISK_NAME_LEN);

    blk_queue_logical_block_size(dev->gd->queue, COMPBLK_SECTOR_SIZE);
    set_capacity(dev->gd, COMPBLK_NSECTORS);

   
    dev->gd->queue->queuedata = dev;

    add_disk(dev->gd);

    pr_info("compblk: /dev/%s registered (%llu sectors)\n",
            dev->gd->disk_name, (unsigned long long)COMPBLK_NSECTORS);

    return 0;
}






static int init_block_module(void)
{
    int ret;

  
    comp_dev.major = register_blkdev(0, DEVICE_NAME);
    
    if (comp_dev.major <= 0)
    {
        printk("compblk: register_blkdev() failed\n");
        return -EBUSY;
    }
    
    printk("compblk: registered with major %d\n", comp_dev.major);

    ret = compblk_create_disk(&comp_dev);
    
    if (ret) 
    {
        pr_err("compblk: compblk_create_disk() failed: %d\n", ret);
        
        unregister_blkdev(comp_dev.major, DEVICE_NAME);
        return ret;
    }

    printk("compblk: compblk_create_disk() succeeded\n");
    return 0;
}



static void release_block_module(void)
{
   
    compblk_destroy_disk(&comp_dev);
    
    unregister_blkdev(comp_dev.major, DEVICE_NAME);
    
    printk("compblk: unregistered\n");
}


module_init(init_block_module);
module_exit(release_block_module);
