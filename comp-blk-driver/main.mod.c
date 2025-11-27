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
	{ 0x92997ed8, "_printk" },
	{ 0x8631c9ba, "alt_cb_patch_nops" },
	{ 0x9305c171, "del_gendisk" },
	{ 0x435b4a58, "blk_mq_destroy_queue" },
	{ 0xf6d2973b, "put_disk" },
	{ 0xf46cb4b, "blk_mq_free_tag_set" },
	{ 0xb5a459dc, "unregister_blkdev" },
	{ 0x999e8297, "vfree" },
	{ 0x720a27a7, "__register_blkdev" },
	{ 0xd6ee688f, "vmalloc" },
	{ 0xdcb764ad, "memset" },
	{ 0xcefb0c9f, "__mutex_init" },
	{ 0x6dd9d3f8, "blk_mq_alloc_tag_set" },
	{ 0xe9d266da, "__blk_mq_alloc_disk" },
	{ 0xdd64e639, "strscpy" },
	{ 0x1afb81ef, "set_capacity" },
	{ 0xfa4c0cc8, "device_add_disk" },
	{ 0xc01003e1, "blk_mq_start_request" },
	{ 0x4dfa8d4b, "mutex_lock" },
	{ 0x4829a47e, "memcpy" },
	{ 0x3213f038, "mutex_unlock" },
	{ 0x76b1f176, "blk_mq_end_request" },
	{ 0x8f80e6e5, "module_layout" },
};

MODULE_INFO(depends, "");


MODULE_INFO(srcversion, "9FAE3DA75489FC55E359825");
