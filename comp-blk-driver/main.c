/*

   Author Name: Vaishnavi Jadhav
   File name: main.c 
   application: This file registers a block device module, allocates a 256 MB virtual disk,
             and implements the main driver operations including open, release, read,
             write, and ioctl. It sets up the gendisk and blk-mq request queue when the
             module loads, and cleans up all allocated resources when the module is
             unloaded.

	        
   Note: All Sprint 2 tasks have been completed, including implementing read and write handling, 
	allocating memory for the 256 MB virtual disk using vmalloc, and setting up the block device logic. 
	The logic for open and release functions were also added in this sprint.



*/

#include <linux/module.h>       
#include <linux/init.h>        
#include <linux/kernel.h>     
#include <linux/fs.h>          
#include <linux/blkdev.h>      
#include <linux/blk-mq.h>       
       
#include <linux/vmalloc.h>      
#include <linux/mutex.h>        
#include <linux/atomic.h>       
#include <linux/uaccess.h>      
#include <linux/version.h>      


#include "comp_blkdrv.h"




#define MAX_WRITES  4096 

MODULE_LICENSE("Dual BSD/GPL");

#define DEVICE_NAME        "compblkdev"

#define COMPBLK_MINORS     1

#define COMPBLK_DISK_NAME  "compblk0"


#define COMPBLK_DISK_SIZE   (256 * 1024 * 1024)   // 256 MB virtual disk
#define COMPBLK_SECTOR_SIZE 512
#define COMPBLK_NSECTORS    (COMPBLK_DISK_SIZE / COMPBLK_SECTOR_SIZE)


// Stores information about the most recent write 
struct write_info
{
    sector_t start_sector;
    sector_t end_sector;
    size_t bytes_written;
};

// One entry in the write table showing where data was written and how large it is
struct write_entry {
    sector_t start_sector;
    sector_t num_sectors;
    size_t byte_len;
    bool valid;
};

// Main device structure 
struct compblk_dev 
{
	struct gendisk            *gd;
	
	struct blk_mq_tag_set      tag_set;
	
	int                        major;
	
	void                       *data;
	
	atomic_t                   open_count;  
	
        struct mutex               lock;  
        
        size_t                     used_space;
             
        struct write_info          last_write;
             
        struct write_entry table[MAX_WRITES];
        
        int write_count;  
        
};

static struct compblk_dev comp_dev;


// Block device operations
static const struct block_device_operations compblk_fops = {
    .owner = THIS_MODULE,
    .open = compblk_open,
    .release = compblk_release,
    .ioctl = compblk_ioctl,
};


/*
 * Handles read requests from the block layer.
 * Copies data from virtual disk buffer into the user’s read buffer.
 */
static blk_status_t compblk_handle_read(struct compblk_dev *dev, struct request *rq)
{
    struct bio_vec bvec;
    struct req_iterator iter;
    
    
    void *dst;
    size_t len;
    
    
    sector_t start_sector = blk_rq_pos(rq);
    
    
    unsigned long offset = start_sector * COMPBLK_SECTOR_SIZE;

    if (offset >= COMPBLK_DISK_SIZE) 
    {
        pr_err("compblk: Reading out of disk\n");
        blk_mq_end_request(rq, BLK_STS_IOERR);
        return BLK_STS_IOERR;
    }

    rq_for_each_segment(bvec, rq, iter) 
    {

        dst = page_address(bvec.bv_page) + bvec.bv_offset;
        
        len = bvec.bv_len;

       
        if (offset + len > COMPBLK_DISK_SIZE)
            len = COMPBLK_DISK_SIZE - offset;

        memcpy(dst, dev->data + offset, len);
        
        offset += len;
    }

    blk_mq_end_request(rq, BLK_STS_OK);
    
    return BLK_STS_OK;
}

/*
 * Handles write requests from the block layer.
 * Copies data from the user’s write buffer into virtual disk.
 * Updates used space and records where the last write happened.
 * Prevents writing past the end of the virtual disk.
 */
static blk_status_t compblk_handle_write(struct compblk_dev *dev, struct request *rq)
{
    struct bio_vec bvec;
    struct req_iterator iter;
    
    
    void *buffer;
    
    sector_t start_sector, end_sector;
    
    unsigned long offset;
    unsigned int len;
    size_t total_bytes = 0;

    start_sector = blk_rq_pos(rq);
    
    offset = start_sector * COMPBLK_SECTOR_SIZE;

    if (offset >= COMPBLK_DISK_SIZE) 
    {
        pr_err("compblk: invalid start offset beyond disk size\n");
        blk_mq_end_request(rq, BLK_STS_IOERR);
        return BLK_STS_IOERR;
    }

    mutex_lock(&dev->lock);

    rq_for_each_segment(bvec, rq, iter)
    {
        buffer = page_address(bvec.bv_page) + bvec.bv_offset;
        
        len = bvec.bv_len;

        if (offset + len > COMPBLK_DISK_SIZE)
            len = COMPBLK_DISK_SIZE - offset;

        memcpy(dev->data + offset, buffer, len);
         
        offset += len;
        total_bytes += len;
    }

    end_sector = start_sector + (total_bytes / COMPBLK_SECTOR_SIZE) - 1;

    dev->used_space += total_bytes;
    
    
    if (dev->used_space > COMPBLK_DISK_SIZE)
        dev->used_space = COMPBLK_DISK_SIZE;

    dev->last_write.start_sector = start_sector;
    dev->last_write.end_sector   = end_sector;
    dev->last_write.bytes_written = total_bytes;

    mutex_unlock(&dev->lock);

    blk_mq_end_request(rq, BLK_STS_OK);
    
    return BLK_STS_OK;
}


/*
 * Main request handler for the block device.
 * Checks whether the request is a read or write
 * and sends it to the correct handler function.
 */
static blk_status_t compblk_queue_rq(struct blk_mq_hw_ctx *hctx,const struct blk_mq_queue_data *bd)
{
    struct request *rq = bd->rq;
    struct compblk_dev *dev = rq->q->queuedata;

    blk_mq_start_request(rq);

    if (rq_data_dir(rq) == WRITE) 
    {
        return compblk_handle_write(dev, rq);
        
     }
     else
     {
        return compblk_handle_read(dev, rq);
        
    }
    
}

/*
 * Called when the block device is opened.
 * Increases open counter 
 */
 
/*
static int compblk_open(struct gendisk *gd, blk_mode_t mode)
{
    struct compblk_dev *dev = gd->private_data;

    if (atomic_inc_return(&dev->open_count) == 1)
        pr_info("compblk: first open\n");

    printk("compblk: opened (users = %d, mode = 0x%x)\n", atomic_read(&dev->open_count), mode);

    return 0;
}
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
 * Decreases open counter and prints when last user closes it.
 */
static void compblk_release(struct gendisk *gd, fmode_t mode)
{
    struct compblk_dev *dev = gd->private_data;

    if (atomic_dec_return(&dev->open_count) == 0)
        pr_info("compblk: last close\n");
}



/*
 * Handles ioctl calls from user space.
 * Currently logic is not written, so returns error.
 */
static int compblk_ioctl(struct block_device *bdev, fmode_t mode, unsigned int cmd, unsigned long arg)
{
    printk("compblk: ioctl function\n");
    return -ENOTTY;
}




/* 
 * blk-mq operations for block driver.
 * We only handle queue_rq to process read/write requests.
 */
static const struct blk_mq_ops compblk_mq_ops = {
	.queue_rq = compblk_queue_rq,
};

/*
 * Creates and sets up the virtual block device.
 * Allocates memory for the disk, initializes locks and counters,
 * sets up blk-mq tag set, creates the gendisk, and registers it.
 */
static int compblk_create_disk(struct compblk_dev *dev)
{
	int ret;
	
	   //memory allocation for disk
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
	//dev->gd = blk_mq_alloc_disk(&dev->tag_set, NULL, dev);

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
	
	set_capacity(dev->gd, COMPBLK_NSECTORS);

	
	add_disk(dev->gd);
	
	printk("compblk: /dev/%s registered (%llu sectors)\n", dev->gd->disk_name, (unsigned long long)COMPBLK_NSECTORS);
	
	return 0;

err_free_tagset:

	blk_mq_free_tag_set(&dev->tag_set);
	
	return ret;
	
}


/*
 * Cleans up and removes the block device.
 * Deletes the disk, destroys the queue, frees memory,
 * and releases the blk-mq tag set.
 */
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


/*
 * Module init function.
 * Registers the block device major number and creates the module.
 */
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


/*
 * Module exit function.
 * Destroys the disk and unregisters the block device.
 */
static void release_block_module(void)
{
   
    compblk_destroy_disk(&comp_dev);
    
    unregister_blkdev(comp_dev.major, DEVICE_NAME);
    
    printk("compblk: unregistered\n");
}


module_init(init_block_module);
module_exit(release_block_module);
