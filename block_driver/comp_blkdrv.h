#ifndef COMP_BLKDRV_H
#define COMP_BLKDRV_H

#include <linux/module.h>   
#include <linux/kernel.h>   
#include <linux/fs.h>    
#include <linux/blkdev.h>
#include <linux/hdreg.h>
#include <linux/blk-mq.h>
#include <linux/version.h>   




static int init_block_module(void);
static void release_block_module(void);
static int compblk_open(struct gendisk *gd, blk_mode_t mode);
static void compblk_release(struct gendisk *gd);
static int compblk_ioctl(struct block_device *bdev, fmode_t mode,unsigned int cmd, unsigned long arg);

#endif /* COMP_BLKDRV_H */
