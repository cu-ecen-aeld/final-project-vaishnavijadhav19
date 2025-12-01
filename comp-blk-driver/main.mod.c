#include <linux/module.h>
#define INCLUDE_VERMAGIC
#include <linux/build-salt.h>
#include <linux/elfnote-lto.h>
#include <linux/export-internal.h>
#include <linux/vermagic.h>
#include <linux/compiler.h>

BUILD_SALT;
BUILD_LTO_INFO;

MODULE_INFO(vermagic, VERMAGIC_STRING);
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

#ifdef CONFIG_RETPOLINE
MODULE_INFO(retpoline, "Y");
#endif


static const struct modversion_info ____versions[]
__used __section("__versions") = {
	{ 0x8631c9ba, "alt_cb_patch_nops" },
	{ 0x92997ed8, "_printk" },
	{ 0x9305c171, "del_gendisk" },
	{ 0xf6d2973b, "put_disk" },
	{ 0xf46cb4b, "blk_mq_free_tag_set" },
	{ 0x999e8297, "vfree" },
	{ 0x720a27a7, "__register_blkdev" },
	{ 0xdcb764ad, "memset" },
	{ 0xd6ee688f, "vmalloc" },
	{ 0xb5a459dc, "unregister_blkdev" },
	{ 0x40a9b349, "vzalloc" },
	{ 0xcefb0c9f, "__mutex_init" },
	{ 0x6dd9d3f8, "blk_mq_alloc_tag_set" },
	{ 0xe9d266da, "__blk_mq_alloc_disk" },
	{ 0xdd64e639, "strscpy" },
	{ 0x1d08c5f0, "blk_queue_logical_block_size" },
	{ 0x1afb81ef, "set_capacity" },
	{ 0xfa4c0cc8, "device_add_disk" },
	{ 0xc01003e1, "blk_mq_start_request" },
	{ 0x4dfa8d4b, "mutex_lock" },
	{ 0x4829a47e, "memcpy" },
	{ 0x670ae423, "kmalloc_caches" },
	{ 0x26d9d21b, "kmalloc_trace" },
	{ 0x787c882b, "lzo1x_1_compress" },
	{ 0x37a0cba, "kfree" },
	{ 0x76b1f176, "blk_mq_end_request" },
	{ 0x4df8fbc, "lzo1x_decompress_safe" },
	{ 0x3213f038, "mutex_unlock" },
	{ 0x8da6585d, "__stack_chk_fail" },
	{ 0x6cbbfc54, "__arch_copy_to_user" },
	{ 0x12a4e128, "__arch_copy_from_user" },
	{ 0x8f80e6e5, "module_layout" },
};

MODULE_INFO(depends, "");


MODULE_INFO(srcversion, "521DEC72DD6E03DA05B8EE1");
