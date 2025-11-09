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
	{ 0x720a27a7, "__register_blkdev" },
	{ 0x122c3a7e, "_printk" },
	{ 0xb5a459dc, "unregister_blkdev" },
	{ 0x39ff040a, "module_layout" },
};

MODULE_INFO(depends, "");


MODULE_INFO(srcversion, "D6F25D6871E0E9C05BB3915");
