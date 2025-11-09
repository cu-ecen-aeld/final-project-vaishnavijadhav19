/*

    Author: Vaishnavi Jadhav
    File name: main.c
    Application: compression block driver implementation

*/

#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/kernel.h>
//#include <linux/genhd.h>
#include "comp_blkdrv.h"

#include <linux/blkdev.h>




#define DEVICE_NAME "compblkdev"

MODULE_LICENSE("Dual BSD/GPL");

static int major_num;



static int init_block_module()
{
    
    major_num = register_blkdev(0, DEVICE_NAME);
    
    if(major_num <= 0)
    {
       printk(KERN_ERR "compblkdev: unable to get major number\n");
       return -EBUSY;
    }
    
    
    printk(KERN_INFO "compblkdev sucessfully registered with major number %d\n", major_num);
    
    return 0;
    
    
}


static void release_block_module()
{
    
    unregister_blkdev(major_num, DEVICE_NAME);
    
    printk(KERN_INFO "compblkdev unregistered..\n");

}

module_init(init_block_module);
module_exit(release_block_module);

