#include <linux/module.h>
#include <linux/export-internal.h>
#include <linux/compiler.h>

MODULE_INFO(name, KBUILD_MODNAME);

__visible struct module __this_module
__section(".gnu.linkonce.this_module") = {
	.name = KBUILD_MODNAME,
	.init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
	.exit = cleanup_module,
#endif
	.arch = MODULE_ARCH_INIT,
};



static const struct modversion_info ____versions[]
__used __section("__versions") = {
	{ 0x122c3a7e, "_printk" },
	{ 0xa65c6def, "alt_cb_patch_nops" },
	{ 0xab7819e9, "del_gendisk" },
	{ 0x57a1e482, "blk_mq_destroy_queue" },
	{ 0x340f1542, "put_disk" },
	{ 0x999e8297, "vfree" },
	{ 0xa5c065b2, "blk_mq_free_tag_set" },
	{ 0xb5a459dc, "unregister_blkdev" },
	{ 0x720a27a7, "__register_blkdev" },
	{ 0x1b2fa1a9, "vmalloc_noprof" },
	{ 0xdcb764ad, "memset" },
	{ 0xcefb0c9f, "__mutex_init" },
	{ 0x82f86bee, "blk_mq_alloc_tag_set" },
	{ 0xc9e27b0d, "__blk_mq_alloc_disk" },
	{ 0x476b165a, "sized_strscpy" },
	{ 0x3df229cf, "set_capacity" },
	{ 0x356f2a7f, "device_add_disk" },
	{ 0xc1a4457d, "blk_mq_start_request" },
	{ 0x4dfa8d4b, "mutex_lock" },
	{ 0x4829a47e, "memcpy" },
	{ 0x3213f038, "mutex_unlock" },
	{ 0x1ef59ee5, "blk_mq_end_request" },
	{ 0x39ff040a, "module_layout" },
};

MODULE_INFO(depends, "");


MODULE_INFO(srcversion, "D4F8EF19993388B1E71BDF5");
