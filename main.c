/*

   Author Name: Vaishnavi Jadhav
   File name: main.c 
   application: This file registers a simple block device module, creates a virtual disk, and defines basic open, 
	        release, and ioctl functions. It sets up the disk structure, initializes it when the module loads, 
	        and removes it when the module is unloaded.
	        
   Note: All Sprint 1 tasks have been completed, including block device registration, disk creation, and basic driver setup.


*/

#include <linux/module.h>       // module_init, module_exit, MODULE_LICENSE
#include <linux/init.h>         // __init, __exit
#include <linux/kernel.h>       // pr_info(), printk()
#include <linux/fs.h>           // register_blkdev, block_device_operations
#include <linux/blkdev.h>       // gendisk, blk_mq interfaces
#include <linux/blk-mq.h>       // blk_mq_tag_set, blk_mq_ops
//#include <linux/genhd.h>        // add_disk(), del_gendisk()
#include <linux/vmalloc.h>      // vmalloc(), vfree()
#include <linux/mutex.h>        // mutex_init, mutex_lock/unlock
#include <linux/atomic.h>       // atomic_t, atomic_set/read
#include <linux/uaccess.h>      // copy_to_user, copy_from_user
#include <linux/version.h>      // optional (kernel version checks)


#include "comp_blkdrv.h"





MODULE_LICENSE("Dual BSD/GPL");

#define DEVICE_NAME        "compblkdev"

#define COMPBLK_MINORS     1

#define COMPBLK_DISK_NAME  "compblk0"

//#define COMPBLK_SECTOR_SIZE   512

//#define COMPBLK_NSECTORS      ((128ULL * 1024 * 1024) / COMPBLK_SECTOR_SIZE)

#define COMPBLK_DISK_SIZE   (256 * 1024 * 1024)   // 256 MB virtual disk
#define COMPBLK_SECTOR_SIZE 512
#define COMPBLK_NSECTORS    (COMPBLK_DISK_SIZE / COMPBLK_SECTOR_SIZE)



struct write_info
{
    sector_t start_sector;
    sector_t end_sector;
    size_t bytes_written;
};


struct compblk_dev 
{
	struct gendisk            *gd;
	
	struct blk_mq_tag_set      tag_set;
	
	int                        major;
	
	void                       *data;
	
	atomic_t                   open_count;  
	
        struct mutex               lock;  
        
        size_t                     used_space;     // total bytes used
        struct write_info          last_write;     // metadata for latest write     
};

static struct compblk_dev comp_dev;


/* Define block device operations */
static const struct block_device_operations compblk_fops = {
    .owner = THIS_MODULE,
    .open = compblk_open,
    .release = compblk_release,
    .ioctl = compblk_ioctl,
};


static blk_status_t compblk_queue_rq(struct blk_mq_hw_ctx *hctx,
                                     const struct blk_mq_queue_data *bd)
{
    struct request *rq = bd->rq;
    struct compblk_dev *dev = rq->q->queuedata;
    struct bio_vec bvec;
    struct req_iterator iter;
    void *buffer;
    sector_t start_sector, end_sector;
    unsigned long offset;
    unsigned int len;
    size_t total_bytes = 0;

    blk_mq_start_request(rq);

    start_sector = blk_rq_pos(rq);
    offset = start_sector * COMPBLK_SECTOR_SIZE;

    if (offset >= COMPBLK_DISK_SIZE) {
        pr_err("compblk: invalid start offset beyond disk size\n");
        blk_mq_end_request(rq, BLK_STS_IOERR);
        return BLK_STS_IOERR;
    }

    mutex_lock(&dev->lock);

    rq_for_each_segment(bvec, rq, iter) {
        buffer = page_address(bvec.bv_page) + bvec.bv_offset;
        len = bvec.bv_len;

        if (offset + len > COMPBLK_DISK_SIZE) {
            pr_warn("compblk: truncated write at disk boundary\n");
            len = COMPBLK_DISK_SIZE - offset;
        }

        memcpy(dev->data + offset, buffer, len);
        offset += len;
        total_bytes += len;
    }

    // calculate sectors written
    end_sector = start_sector + (total_bytes / COMPBLK_SECTOR_SIZE) - 1;

    // update tracking info
    dev->used_space += total_bytes;
    if (dev->used_space > COMPBLK_DISK_SIZE)
        dev->used_space = COMPBLK_DISK_SIZE;

    dev->last_write.start_sector = start_sector;
    dev->last_write.end_sector   = end_sector;
    dev->last_write.bytes_written = total_bytes;

   unsigned int usage_percent = (dev->used_space * 100) / COMPBLK_DISK_SIZE;

	pr_info("compblk: write complete: start=%llu end=%llu bytes=%zu used=%zu/%u (%u%%)\n",
		(unsigned long long)start_sector,
		(unsigned long long)end_sector,
		total_bytes,
		dev->used_space,
		COMPBLK_DISK_SIZE,
		usage_percent);
 

    mutex_unlock(&dev->lock);

    blk_mq_end_request(rq, BLK_STS_OK);
    return BLK_STS_OK;
}

static int compblk_open(struct gendisk *gd, blk_mode_t mode)
{
    struct compblk_dev *dev = gd->private_data;

    if (atomic_inc_return(&dev->open_count) == 1)
        pr_info("compblk: first open\n");

    pr_info("compblk: opened (users = %d, mode = 0x%x)\n",
            atomic_read(&dev->open_count), mode);

    return 0;
}

static void compblk_release(struct gendisk *gd)
{
    struct compblk_dev *dev = gd->private_data;

    if (atomic_dec_return(&dev->open_count) == 0)
        pr_info("compblk: last close\n");
}

static int compblk_ioctl(struct block_device *bdev, fmode_t mode, unsigned int cmd, unsigned long arg)
{
    printk("compblk: ioctl function\n");
    return -ENOTTY;
}





static const struct blk_mq_ops compblk_mq_ops = {
	.queue_rq = compblk_queue_rq,
};


static int compblk_create_disk(struct compblk_dev *dev)
{
	int ret;
	
	   
	dev->data = vmalloc(COMPBLK_DISK_SIZE);
	
	    if (!dev->data) 
	    {
		pr_err("compblk: failed to allocate %u bytes for virtual disk\n",
		       COMPBLK_DISK_SIZE);
		return -ENOMEM;
	    }
	    
	    memset(dev->data, 0, COMPBLK_DISK_SIZE);
	    printk("compblk: allocated %u bytes virtual disk\n", COMPBLK_DISK_SIZE);
	    
	    
	    
        dev->used_space = 0;

	dev->last_write.start_sector  = 0;
	dev->last_write.end_sector    = 0;
	dev->last_write.bytes_written = 0;

	mutex_init(&dev->lock);
	atomic_set(&dev->open_count, 0);

	
	memset(&dev->tag_set, 0, sizeof(dev->tag_set));
	
	dev->tag_set.ops         = &compblk_mq_ops;
	dev->tag_set.nr_hw_queues= 1;
	dev->tag_set.queue_depth = 128;
	dev->tag_set.numa_node   = NUMA_NO_NODE;
	dev->tag_set.flags       = BLK_MQ_F_SHOULD_MERGE;
	



	ret = blk_mq_alloc_tag_set(&dev->tag_set);
	
	if (ret) 
	{
		pr_err("compblk: blk_mq_alloc_tag_set failed: %d\n", ret);
		
		return ret;
	}

	
	dev->gd = blk_mq_alloc_disk(&dev->tag_set, dev);
	if (IS_ERR(dev->gd))
	 {
		ret = PTR_ERR(dev->gd);
		
		dev->gd = NULL;
		
		pr_err("compblk: blk_mq_alloc_disk failed: %d\n", ret);
		
		goto err_free_tagset;
	}

	
	dev->gd->major        = dev->major;
	dev->gd->first_minor  = 0;
	dev->gd->minors       = COMPBLK_MINORS;
	dev->gd->fops         = &compblk_fops;
	dev->gd->private_data = dev;
	dev->gd->flags       |= GENHD_FL_NO_PART; 
	strscpy(dev->gd->disk_name, COMPBLK_DISK_NAME, DISK_NAME_LEN);

	dev->gd->queue->queuedata = dev; 
	blk_queue_logical_block_size(dev->gd->queue, COMPBLK_SECTOR_SIZE);
	
	
	set_capacity(dev->gd, COMPBLK_NSECTORS);

	
	add_disk(dev->gd);
	
	printk("compblk: /dev/%s registered (%llu sectors)\n", dev->gd->disk_name, (unsigned long long)COMPBLK_NSECTORS);
	
	return 0;

err_free_tagset:

	blk_mq_free_tag_set(&dev->tag_set);
	
	return ret;
}



static void compblk_destroy_disk(struct compblk_dev *dev)
{
    if (!dev->gd)
        goto out_tagset;

   
    del_gendisk(dev->gd);

   
    if (dev->gd->queue)
        blk_mq_destroy_queue(dev->gd->queue);

   
    put_disk(dev->gd);
    dev->gd = NULL;
    
    if (dev->data)
    {
        vfree(dev->data);
        dev->data = NULL;
        pr_info("compblk: freed virtual-disk memory\n");
    }

out_tagset:
    blk_mq_free_tag_set(&dev->tag_set);
    
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
