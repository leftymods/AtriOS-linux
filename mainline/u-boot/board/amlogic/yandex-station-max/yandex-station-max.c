// SPDX-License-Identifier: GPL-2.0+
/*
 * Yandex Station Max (Amlogic S905X3 / SM1)
 *
 * Board glue for mainline U-Boot.  Largely modelled on
 * board/amlogic/odroid-n2/odroid-n2.c (Copyright (C) 2020 BayLibre, SAS),
 * minus the Odroid ADC board-variant detection.
 */

#include <dm.h>
#include <env.h>
#include <init.h>
#include <net.h>
#include <vsprintf.h>
#include <asm/arch/sm.h>
#include <asm/arch/eth.h>

#define EFUSE_MAC_OFFSET	20
#define EFUSE_MAC_SIZE		12
#define MAC_ADDR_LEN		6

int misc_init_r(void)
{
	u8 mac_addr[MAC_ADDR_LEN + 1];
	char efuse_mac_addr[EFUSE_MAC_SIZE], tmp[3];
	ssize_t len;

	if (IS_ENABLED(CONFIG_ENV_VARS_UBOOT_RUNTIME_CONFIG) &&
	    meson_get_soc_rev(tmp, sizeof(tmp)) > 0)
		env_set("soc_rev", tmp);

	if (!eth_env_get_enetaddr("ethaddr", mac_addr)) {
		len = meson_sm_read_efuse(EFUSE_MAC_OFFSET,
					  efuse_mac_addr, EFUSE_MAC_SIZE);
		if (len != EFUSE_MAC_SIZE)
			return 0;

		/* MAC is stored in ASCII format, 1 byte = 2 characters */
		for (int i = 0; i < 6; i++) {
			tmp[0] = efuse_mac_addr[i * 2];
			tmp[1] = efuse_mac_addr[i * 2 + 1];
			tmp[2] = '\0';
			mac_addr[i] = hextoul(tmp, NULL);
		}
		mac_addr[MAC_ADDR_LEN] = '\0';

		if (is_valid_ethaddr(mac_addr))
			eth_env_set_enetaddr("ethaddr", mac_addr);
		else
			meson_generate_serial_ethaddr();
	}

	/* Tell the distro/bootstd which kernel DT to load */
	env_set("fdtfile", "amlogic/meson-sm1-yandex-station-max.dtb");

	return 0;
}
