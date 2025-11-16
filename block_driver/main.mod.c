#include <linux/module.h>
#define INCLUDE_VERMAGIC
#include <linux/build-salt.h>
#include <linux/elfnote-lto.h>
#include <linux/export-internal.h>
#include <linux/vermagic.h>
#include <linux/compiler.h>

#ifdef CONFIG_UNWINDER_ORC
#include <asm/orc_header.h>
ORC_HEADER;
#endif

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
	{ 0x999e8297, "vfree" },
	{ 0xf282d6ee, "blk_mq_free_tag_set" },
	{ 0xb5a459dc, "unregister_blkdev" },
	{ 0x720a27a7, "__register_blkdev" },
	{ 0xd6ee688f, "vmalloc" },
	{ 0xfb578fc5, "memset" },
	{ 0xcefb0c9f, "__mutex_init" },
	{ 0x308facb2, "blk_mq_alloc_tag_set" },
	{ 0x184e08bf, "__blk_mq_alloc_disk" },
	{ 0x2297d3ad, "blk_queue_logical_block_size" },
	{ 0xfecb8ada, "set_capacity" },
	{ 0xf89e4e9b, "device_add_disk" },
	{ 0xc94900f0, "blk_mq_start_request" },
	{ 0x4dfa8d4b, "mutex_lock" },
	{ 0x97651e6c, "vmemmap_base" },
	{ 0x7cd8d75e, "page_offset_base" },
	{ 0x69acdf38, "memcpy" },
	{ 0x3213f038, "mutex_unlock" },
	{ 0x3762d23, "blk_mq_end_request" },
	{ 0xbdfb6dbb, "__fentry__" },
	{ 0x122c3a7e, "_printk" },
	{ 0x5b8239ca, "__x86_return_thunk" },
	{ 0x7d50b902, "del_gendisk" },
	{ 0xe56c4615, "blk_mq_destroy_queue" },
	{ 0x84b93991, "put_disk" },
	{ 0xe2fd41e5, "module_layout" },
};

MODULE_INFO(depends, "");


MODULE_INFO(srcversion, "C8FDC79F3375C80E80D28D9");
