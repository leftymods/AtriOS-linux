#!/usr/bin/env python3
"""Generate meson-sm1-yandex-station-max.dts (board DTS + 72 ring LED nodes).

The whole board DTS is templated here so the ring nodes and the rest of the
tree can never drift apart - edit this file and re-run it, never the .dts.

Ring wiring (from the vendor DTB, see mainline/docs/ring.md):

  * two IS31FL3236A controllers on i2c0, 0x3c (ring LEDs 1-12) and
    0x3f (ring LEDs 13-24);
  * each LED uses three consecutive channels, so
    channel = (led_in_chip - 1) * 3 + c + 1, c in (red, green, blue);
  * 36 children per chip with reg = <1..36> and
    label = "station2:{red,green,blue}:ring1..24" - the exact names the
    vendor userspace (/usr/lib/atrios/light, /dev/fb5) and the led_ring_fb
    trigger rely on;
  * max-brightness-scale = <200> is what the vendor driver used and what
    ledtrig-ringfb.c requires (max_brightness == 200).
"""

import re
import sys
from pathlib import Path

COLOURS = ("red", "green", "blue")

# (i2c address, first ring LED driven by this chip)
CHIPS = ((0x3C, 1), (0x3F, 13))

DTS_NAME = "meson-sm1-yandex-station-max.dts"


def led_nodes():
    out = []
    out.append("\t/*\n"
               "\t * IS31FL3236A LED ring controllers.  The \"gva\" compatible,\n"
               "\t * reg_offset fallback and max-brightness-scale are vendor\n"
               "\t * extensions carried by patches/linux/0002-leds-is31fl32xx-gva.patch.\n"
               "\t */")
    for chip, first in CHIPS:
        out.append(f"\tled-controller@{chip:02x} {{")
        out.append('\t\tcompatible = "issi,is31fl3236a";')
        out.append(f"\t\treg = <0x{chip:02x}>;")
        out.append("\t\t#address-cells = <1>;")
        out.append("\t\t#size-cells = <0>;")
        out.append("\t\tmax-brightness-scale = <200>;")
        out.append("")
        for n in range(first, first + 12):
            for c, colour in enumerate(COLOURS):
                channel = (n - first) * 3 + c + 1
                out.append(f"\t\tled@{channel} {{")
                out.append(f"\t\t\treg = <{channel}>;")
                out.append(f'\t\t\tlabel = "station2:{colour}:ring{n}";')
                out.append('\t\t\tlinux,default-trigger = "led_ring_fb";')
                out.append("\t\t};")
        out.append("\t};")
        out.append("")
    return "\n".join(out)


HEADER = '''// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Yandex Station Max - Amlogic S905X3 (SM1)
 *
 * GENERATED FILE - do not edit.  Change gen-ring-nodes.py and re-run it.
 *
 * Mainline port; the LED ring nodes at the end of &i2c0 are generated.
 * GPIO/pin/data references:
 *   vendor DTB  AtriOS-linux/common/arch/arm64/boot/dts/amlogic/
 *               sm1_s905x3_yandex.dts (decompiled, phandle numbering is
 *               off by one for the periphs bank: vendor = mainline + 1)
 *   details      mainline/docs/dts-review.md
 */

/dts-v1/;

#include <dt-bindings/gpio/gpio.h>
#include <dt-bindings/gpio/meson-g12a-gpio.h>
#include <dt-bindings/input/input.h>
#include <dt-bindings/interrupt-controller/irq.h>
#include "meson-sm1.dtsi"

/ {
\tmodel = "Yandex Station Max";
\tcompatible = "yandex,station-max", "amlogic,sm1";

\taliases {
\t\tserial0 = &uart_AO;
\t\tserial1 = &uart_A;
\t\t/* the board boots from eMMC, keep it as mmcblk0 */
\t\tmmc0 = &sd_emmc_c;
\t\tmmc1 = &sd_emmc_b;
\t\tmmc2 = &sd_emmc_a;
\t\tethernet0 = &ethmac;
\t};

\tchosen {
\t\tstdout-path = "serial0:115200n8";
\t};

\tmemory@0 {
\t\tdevice_type = "memory";
\t\treg = <0x0 0x0 0x0 0x80000000>;
\t};

\t/*
\t * PMIC-less board: every rail is a fixed, always-on regulator and is
\t * only described so that the consumers below have a supply to point at.
\t * Names follow the vendor DT (fixedregulator-3v3d / -1v8d / -20v-amp).
\t */
\treg_3v3_d: regulator-3v3-d {
\t\tcompatible = "regulator-fixed";
\t\tregulator-name = "3V3_D";
\t\tregulator-min-microvolt = <3300000>;
\t\tregulator-max-microvolt = <3300000>;
\t\tregulator-always-on;
\t};

\treg_1v8_d: regulator-1v8-d {
\t\tcompatible = "regulator-fixed";
\t\tregulator-name = "1V8_D";
\t\tregulator-min-microvolt = <1800000>;
\t\tregulator-max-microvolt = <1800000>;
\t\tregulator-always-on;
\t};

\treg_20v_amp: regulator-20v-amp {
\t\tcompatible = "regulator-fixed";
\t\tregulator-name = "20V_AMPL";
\t\tregulator-min-microvolt = <20000000>;
\t\tregulator-max-microvolt = <20000000>;
\t\tgpios = <&gpio GPIOX_9 GPIO_ACTIVE_HIGH>;
\t\tenable-active-high;
\t\tregulator-boot-on;
\t\tregulator-always-on;
\t};

\t/* Vendor emmc node: hw_reset = <&gpio BOOT_12 ...> */
\temmc_pwrseq: emmc-pwrseq {
\t\tcompatible = "mmc-pwrseq-emmc";
\t\treset-gpios = <&gpio BOOT_12 GPIO_ACTIVE_LOW>;
\t};

\t/* drivers/leds/trigger/ledtrig-ringfb.c */
\tled-ring-fb {
\t\tcompatible = "yandex,led-ring-fb";
\t};

\t/*
\t * Vendor DT: compatible = "rotary-volume", gpios = <GPIOC_7>, <GPIOAO_10>,
\t * key-codes = <0x68 0x6d>; the vendor userspace (/usr/lib/atrios/rotaryd)
\t * listens for KEY_PAGEUP/KEY_PAGEDOWN, so the codes must not be changed
\t * to KEY_VOLUMEUP/KEY_VOLUMEDOWN without touching userspace.
\t */
\trotary-volume {
\t\tcompatible = "yandex,rotary-volume";
\t\tgpios = <&gpio GPIOC_7 GPIO_ACTIVE_HIGH>,
\t\t\t<&gpio_ao GPIOAO_10 GPIO_ACTIVE_HIGH>;
\t\tkey-codes = <KEY_PAGEUP KEY_PAGEDOWN>;
\t};

\t/* Vendor gpio_keypad: key_name = "function", "micmute";
\t * key_code = <0x246 0xf8>; key-gpios = GPIOA_15, GPIOA_14 (active low).
\t */
\tkeypad {
\t\tcompatible = "yandex,gpio-keypad";
\t\tkey_num = <2>;
\t\tkey-names = "function", "micmute";
\t\tkey-codes = <KEY_VOICECOMMAND KEY_MICMUTE>;
\t\tkey-gpios = <&gpio GPIOA_15 GPIO_ACTIVE_LOW>,
\t\t\t    <&gpio GPIOA_14 GPIO_ACTIVE_LOW>;
\t\tscan_period = <20>;
\t\tdetect_mode = <0>;
\t};
};

/* AO UART, the 4-pin debug header (GPIOAO_0/GPIOAO_1). */
&uart_AO {
\tstatus = "okay";
\tpinctrl-0 = <&uart_ao_a_pins>;
\tpinctrl-names = "default";
};

/*
 * UART_A drives the RTL8822CS Bluetooth module (vendor: uart_tx_a/rx_a/
 * cts_a/rts_a and bt-dev gpio_reset = GPIOX_17).
 */
&uart_A {
\tstatus = "okay";
\tpinctrl-0 = <&uart_a_pins>, <&uart_a_cts_rts_pins>;
\tpinctrl-names = "default";
\tuart-has-rtscts;

\tbluetooth {
\t\tcompatible = "realtek,rtl8822cs-bt";
\t\tenable-gpios = <&gpio GPIOX_17 GPIO_ACTIVE_HIGH>;
\t};
};

/* eMMC.  Vendor: bus-width 8, 200 MHz, non-removable, MMC_CAP2_HS200. */
&sd_emmc_c {
\tstatus = "okay";
\tpinctrl-0 = <&emmc_ctrl_pins>, <&emmc_data_8b_pins>, <&emmc_ds_pins>;
\tpinctrl-1 = <&emmc_clk_gate_pins>;
\tpinctrl-names = "default", "clk-gate";
\tbus-width = <8>;
\tmax-frequency = <200000000>;
\tnon-removable;
\tdisable-wp;
\tcap-mmc-highspeed;
\tmmc-hs200-1_8v;
\tmmc-pwrseq = <&emmc_pwrseq>;
\tvmmc-supply = <&reg_3v3_d>;
\tvqmmc-supply = <&reg_1v8_d>;
};

/*
 * SD slot.  The vendor DT ships sd@ffe05000 with status = "disabled" and it
 * cannot be enabled here either: sdcard_cmd_c and i2c0_sda_c are the same pad
 * (GPIOC_5) and i2c0 carries the LED ring / EEPROM / light sensor, while
 * i2c0_sck_c (GPIOC_6) would take the card-detect line.  Kept for reference.
 */
&sd_emmc_b {
\tstatus = "disabled";
\tpinctrl-0 = <&sdcard_c_pins>;
\tpinctrl-1 = <&sdcard_clk_gate_c_pins>;
\tpinctrl-names = "default", "clk-gate";
\tbus-width = <4>;
\tcap-sd-highspeed;
\tdisable-wp;
\tmax-frequency = <100000000>;
};

/*
 * 10/100 Ethernet.  Vendor: internal_phy = <1>, i.e. the on-SoC PHY reached
 * through the mdio multiplexer (int_mdio/internal_ephy), not an external
 * RGMII PHY.  GPIOZ_14/15 carry the link/activity LEDs.
 */
&ethmac {
\tstatus = "okay";
\tphy-mode = "rmii";
\tphy-handle = <&internal_ephy>;
\tpinctrl-0 = <&eth_leds_pins>;
\tpinctrl-names = "default";
};

/*
 * Vendor bus: i2c@1f000 on GPIOC_5/GPIOC_6 at 400 kHz.
 */
&i2c0 {
\tstatus = "okay";
\tpinctrl-0 = <&i2c0_sda_c_pins>, <&i2c0_sck_c_pins>;
\tpinctrl-names = "default";
\tclock-frequency = <400000>;

\t/* Vendor: compatible = "24c01", PCBA ids EEPROM. */
\teeprom@56 {
\t\tcompatible = "atmel,24c01";
\t\treg = <0x56>;
\t\tpagesize = <8>;
\t};

\t/*
\t * Vendor: compatible = "lite-on,ltr308als01".  The part is an
\t * LTR-308ALS-01, which upstream binds as "liteon,ltr308"
\t * (drivers/iio/light/ltrf216a.c) - use the upstream id so no driver
\t * patch is needed.
\t */
\tlight-sensor@53 {
\t\tcompatible = "liteon,ltr308";
\t\treg = <0x53>;
\t};

'''

FOOTER = '''};

/*
 * Front matrix panel (25x16 RGB888, Gowin FPGA) on spicc1.
 *
 * NOTE: the panel shares its pads with the FPGA JTAG lines, so those GPIOs
 * cannot simply be requested next to spicc1:
 *   spi1_mosi = GPIOH_4 = jtag-tdi-gpios
 *   spi1_miso = GPIOH_5 = jtag-tdo-gpios
 *   spi1_clk  = GPIOH_7 = jtag-tck-gpios
 *   spi1_ss0  = GPIOH_6 = cs-gpios and jtag-tms-gpios
 * spicc1_pins muxes H_4/H_5/H_7 to the SPI function and the SPI core owns
 * the H_6 chip select, so the panel driver has to do a pinctrl handoff
 * before it can drive the JTAG lines (see docs/dts-review.md).
 */
&spicc1 {
\tstatus = "okay";
\tpinctrl-0 = <&spicc1_pins>;
\tpinctrl-names = "default";
\tcs-gpios = <&gpio GPIOH_6 GPIO_ACTIVE_LOW>;
\tnum-cs = <1>;

\tpanel@0 {
\t\tcompatible = "yandex,led-screen", "atri,led-panel";
\t\treg = <0>;
\t\tspi-max-frequency = <4000000>;
\t\tmax-brightness = <200>;
\t\treset-gpios = <&gpio GPIOX_8 GPIO_ACTIVE_LOW>;
\t\tjtag-tck-gpios = <&gpio GPIOH_7 GPIO_ACTIVE_HIGH>;
\t\tjtag-tdi-gpios = <&gpio GPIOH_4 GPIO_ACTIVE_HIGH>;
\t\tjtag-tdo-gpios = <&gpio GPIOH_5 GPIO_ACTIVE_HIGH>;
\t\tjtag-tms-gpios = <&gpio GPIOH_6 GPIO_ACTIVE_HIGH>;
\t\tjtag-reconfig-gpios = <&gpio GPIOH_8 GPIO_ACTIVE_LOW>;
\t\tjtag-sel-gpios = <&gpio_ao GPIOE_2 GPIO_ACTIVE_HIGH>;
\t};
};

/*
 * IR receiver (vendor: rc@0xff808040, remote_ao_input on GPIOAO_5).
 * The vendor NEC keymap is not upstream yet, so no linux,rc-map-name here.
 */
&ir {
\tstatus = "okay";
\tpinctrl-0 = <&remote_input_ao_pins>;
\tpinctrl-names = "default";
};
'''


def build():
    return HEADER + led_nodes() + FOOTER


def _blocks(text, name):
    """Yield the body of every node whose name matches `name` (incl. @addr)."""
    for m in re.finditer(r"^\t%s(@[0-9a-f]+)? \{" % re.escape(name), text, re.M):
        depth, start = 0, m.start()
        for i in range(start, len(text)):
            if text[i] == "{":
                depth += 1
            elif text[i] == "}":
                depth -= 1
                if depth == 0:
                    yield text[start:i + 1]
                    break


def verify(text):
    """Fail hard if the generated ring block is not exactly 72 channels."""
    if text.count("{") != text.count("}"):
        raise SystemExit("unbalanced braces in generated DTS")

    chips = list(_blocks(text, "led-controller"))
    if len(chips) != len(CHIPS):
        raise SystemExit(f"expected {len(CHIPS)} LED controllers, got {len(chips)}")

    labels = []
    for (chip, first), body in zip(CHIPS, chips):
        if f"reg = <0x{chip:02x}>" not in body:
            raise SystemExit(f"led-controller@{chip:02x} lost its reg property")
        regs = [int(v) for v in re.findall(r"^\t\t\treg = <(\d+)>;", body, re.M)]
        want = list(range(1, 37))
        if regs != want:
            raise SystemExit(f"led-controller@{chip:02x}: regs {regs} != 1..36")
        for n in range(first, first + 12):
            for colour in COLOURS:
                labels.append(f'label = "station2:{colour}:ring{n}";')
        if body.count("led@") != 36:
            raise SystemExit(f"led-controller@{chip:02x}: expected 36 leds")
        if "max-brightness-scale = <200>;" not in body:
            raise SystemExit(f"led-controller@{chip:02x}: missing brightness scale")

    for want in labels:
        if want not in text:
            raise SystemExit(f"missing {want}")
    if len(labels) != 72:
        raise SystemExit(f"expected 72 labels, built {len(labels)}")
    return len(labels)


def main():
    out = Path(__file__).with_name(DTS_NAME)
    text = build()
    n_leds = verify(text)
    out.write_text(text)
    print(f"wrote {out} ({out.stat().st_size} bytes, {n_leds} ring LEDs)")


if __name__ == "__main__":
    main()
