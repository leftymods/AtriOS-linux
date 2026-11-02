// SPDX-License-Identifier: GPL-2.0+
#include <command.h>
#include <console.h>
#include <g_dnl.h>
#include <usb.h>

static int do_aml_burn(struct cmd_tbl *cmdtp, int flag, int argc,
		       char *const argv[])
{
	struct udevice *udc;
	int ret;

	ret = udc_device_get_by_index(0, &udc);
	if (ret) {
		printf("USB init failed: %d\n", ret);
		return CMD_RET_FAILURE;
	}
	g_dnl_clear_detach();
	ret = g_dnl_register("usb_dnl_aml_burn");
	if (ret) {
		udc_device_put(udc);
		return CMD_RET_FAILURE;
	}
	puts("AML burn mode: waiting for USB Burning Tool (Ctrl-C to exit)\n");
	while (!g_dnl_detach() && !ctrlc())
		dm_usb_gadget_handle_interrupts(udc);

	g_dnl_unregister();
	g_dnl_clear_detach();
	udc_device_put(udc);
	return CMD_RET_SUCCESS;
}

U_BOOT_CMD(aml_burn, 1, 0, do_aml_burn, "Amlogic USB Burning Tool mode", "");
U_BOOT_CMD(update, 1, 0, do_aml_burn, "alias for aml_burn", "");
