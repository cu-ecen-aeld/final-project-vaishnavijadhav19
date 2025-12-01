#ifndef COMP_BLKDRV_H
#define COMP_BLKDRV_H

#include <linux/module.h>   
#include <linux/kernel.h>   
#include <linux/fs.h>    
#include <linux/blkdev.h>
#include <linux/hdreg.h>
#include <linux/blk-mq.h>
#include <linux/version.h>   
#define COMPBLK_IOC_MAGIC  'V'

#define COMPBLK_IOC_READIDX   _IOW(COMPBLK_IOC_MAGIC, 1, int)
#define COMPBLK_IOC_GETCOUNT  _IOR(COMPBLK_IOC_MAGIC, 2, int)


#pragma once
#include <linux/ioctl.h>
#include <linux/types.h>

#pragma once
#include <linux/types.h>


static int init_block_module(void);
static void release_block_module(void);


static int compblk_open(struct block_device *bdev, fmode_t mode);
static void compblk_release(struct gendisk *gd, fmode_t mode);

static int compblk_ioctl(struct block_device *bdev, fmode_t mode,unsigned int cmd, unsigned long arg);


#endif /* COMP_BLKDRV_H */
