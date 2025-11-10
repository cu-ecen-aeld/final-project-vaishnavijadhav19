/*

   Author Name: Vaishnavi Jadhav
   File name: main.c 
   application: This file registers a simple block device module, creates a virtual disk, and defines basic open, 
	        release, and ioctl functions. It sets up the disk structure, initializes it when the module loads, 
	        and removes it when the module is unloaded.
	        
   Note: All Sprint 1 tasks have been completed, including block device registration, disk creation, and basic driver setup.


*/


#include <linux/blkdev.h>
#include <linux/blk-mq.h>
#include <linux/version.h>
#include "comp_blkdrv.h"



MODULE_LICENSE("Dual BSD/GPL");

#define DEVICE_NAME        "compblkdev"

#define COMPBLK_MINORS     1

#define COMPBLK_DISK_NAME  "compblk0"

#define COMPBLK_SECTOR_SIZE   512

#define COMPBLK_NSECTORS      ((128ULL * 1024 * 1024) / COMPBLK_SECTOR_SIZE)


struct compblk_dev 
{
	struct gendisk            *gd;
	
	struct blk_mq_tag_set      tag_set;
	
	int                        major;
};

static struct compblk_dev comp_dev;


static const struct block_device_operations compblk_fops = {
	.owner = THIS_MODULE,
        .open = compblk_open,
        .release = compblk_release,
        .ioctl = compblk_ioctl,
	
};

static int compblk_open(struct gendisk *gd, blk_mode_t mode)
{
    printk("compblk: open function\n");
    return 0;
}

static void compblk_release(struct gendisk *gd)
{
    printk("compblk: release function\n");
}

static int compblk_ioctl(struct block_device *bdev, fmode_t mode, unsigned int cmd, unsigned long arg)
{
    printk("compblk: ioctl function\n");
    return -ENOTTY;
}



static blk_status_t compblk_queue_rq(struct blk_mq_hw_ctx *hctx, const struct blk_mq_queue_data *bd)
{
	
	blk_mq_start_request(bd->rq);
	
	blk_mq_end_request(bd->rq, BLK_STS_IOERR);
	return BLK_STS_OK;
}

static const struct blk_mq_ops compblk_mq_ops = {
	.queue_rq = compblk_queue_rq,
};


static int compblk_create_disk(struct compblk_dev *dev)
{
	int ret;

	
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
