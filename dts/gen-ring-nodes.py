#!/usr/bin/env python3
"""Generate meson-sm1-yandex-station-max.dts (board DTS + 72 ring LED nodes).

The whole board DTS is templated here so the ring nodes and the rest of the
tree can never drift apart - edit this file and re-run it, never the .dts.

Ring wiring (from the vendor DTB, see docs/ring.md):

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
 *   details      docs/dts-review.md
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
 * RGMII PHY.  No pinctrl here: eth_link_led/eth_act_led are GPIOZ_14/15,
 * which are the same pads as i2c2_sda_z/i2c2_sck_z - the codec bus below.
 * The vendor DT dropped the ethernet pinctrl for exactly this reason
 * ("pinctrl removed to prevent GPIOZ_14/15 collision with i2c2", vendor
 * DTS:380), so the link/activity LEDs are simply not driven on this board.
 */
&ethmac {
\tstatus = "okay";
\tphy-mode = "rmii";
\tphy-handle = <&internal_ephy>;
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
\n&sd_emmc_a {
\tstatus = "okay";
\tpinctrl-0 = <&sdio_pins>;
\tpinctrl-1 = <&sdio_clk_gate_pins>;
\tpinctrl-names = "default", "clk-gate";
\t#address-cells = <1>;
\t#size-cells = <0>;
\tbus-width = <4>;
\tcap-sd-highspeed;
\tmax-frequency = <100000000>;
\tnon-removable;
\tdisable-wp;
\tkeep-power-in-suspend;   /* RTL8822ES firmware survives suspend */
\tvmmc-supply = <&reg_3v3_d>;
};
'''


def dts(text):
    """Turn 4-space logical indentation into DTS tabs.

    The audio block below is kept readable with 4-space steps in this
    source file; the generated DTS must use tabs like everywhere else.
    """
    lines = []
    for line in text.strip("\n").splitlines():
        lead = len(line) - len(line.lstrip(" "))
        depth, extra = divmod(lead, 4)
        lines.append("\t" * depth + " " * extra + line[lead:])
    return "\n".join(lines) + "\n"


AUDIO = dts("""
/*
 * ------------------------------------------------------------------
 * Audio: ES7210 mics, ES8156 headphone DAC, 2x SY6045S amps, TDM-B,
 * PDM array and the AXG DPCM sound card.
 *
 * Everything here is mainline 7.3 (sound/soc/meson/axg-*.c) plus the
 * in-house codec drivers from drivers/audio (patches/linux/
 * 0007..0009).  Vendor reference: sm1_s905x3_yandex.dts -
 * i2c@1d000 codecs :2126-2164, auge_sound :4244, audiobus tdm@1 :2391,
 * PDM pinctrl :1342.  Details and the userspace contract:
 * docs/audio.md
 * ------------------------------------------------------------------
 */

/*
 * Audio clock controller and DDR arbiter.  Both are status = "disabled"
 * in meson-sm1.dtsi while tdmif_b and every FIFO reference them, so
 * without these two nodes the card and the DAIs defer forever.
 */
&clkc_audio {
    status = "okay";
};

&arb {
    status = "okay";
};

/*
 * i2c2 = vendor i2c@1d000 (0xffd1d000), the codec bus; the vendor
 * aliases say so explicitly (i2c2 = "/.../i2c@1d000").
 *
 * Pin conflict: i2c2_sda_z / i2c2_sck_z are GPIOZ_14/15, the same pads
 * as the ethernet link/activity LEDs, which is why &ethmac above no
 * longer claims eth_leds_pins (vendor DTS:380 removed it for the same
 * reason).  The alternative group i2c2_sda_x/sck_x (GPIOX_17/18) is not
 * usable either: GPIOX_17 is the RTL8822CS BT enable line.
 */
&i2c2 {
    status = "okay";
    pinctrl-0 = <&i2c2_sda_z_pins>, <&i2c2_sck_z_pins>;
    pinctrl-names = "default";
    /* vendor: 300 kHz (0x493e0); 100 kHz is the safe bring-up value */
    clock-frequency = <100000>;

    /*
     * ES8156 headphone DAC.  The sound-name-prefix is load-bearing:
     * userspace addresses "Headphone DAC Playback Volume" /
     * "Headphone Headphone Switch" and the card routes through the
     * "Headphone LOUT"/"Headphone ROUT" widgets.
     * hp-det: vendor GPIO 77 -> mainline GPIOX_11 (the vendor periphs
     * bank is one pin higher, see docs/audio.md section 5).
     */
    es8156: audio-codec@8 {
        compatible = "everest,es8156";
        reg = <0x08>;
        #sound-dai-cells = <0>;
        sound-name-prefix = "Headphone";
        hp-det-gpios = <&gpio GPIOX_11 GPIO_ACTIVE_HIGH>;
        status = "okay";
    };

    /* SY6045S tweeter amp (L/R tweeters). */
    sy6045s_tweeters: audio-codec@2a {
        compatible = "silergy,sy6045s";
        reg = <0x2a>;
        #sound-dai-cells = <0>;
        sound-name-prefix = "Tweeters";
        vddio-supply = <&reg_3v3_d>;
        pvdd-supply = <&reg_20v_amp>;
        /* settings file lives in /lib/firmware */
        firmware-name = "sy6045s-tweeters-settings.txt";
        /* vendor "restore-regs": (addr, size, mask) triplets */
        silergy,restore-regs = <0x07 0x01 0xff 0x06 0x01 0x08>;
        status = "okay";
    };

    /* SY6045S woofer amp, PBTL bridged (subwoofer). */
    sy6045s_woofer: audio-codec@2b {
        compatible = "silergy,sy6045s";
        reg = <0x2b>;
        #sound-dai-cells = <0>;
        sound-name-prefix = "Woofer";
        vddio-supply = <&reg_3v3_d>;
        pvdd-supply = <&reg_20v_amp>;
        firmware-name = "sy6045s-woofer-settings.txt";
        silergy,restore-regs = <0x07 0x01 0xff 0x06 0x01 0x08>;
        pbtl-mode;
        status = "okay";
    };

    /*
     * ES7210 mic ADC.  The vendor used "everest,es7210_2ch"; the 4ch
     * generic compatible is a superset and the driver limits itself by
     * the number of slots.  Prefix "Loopback" keeps the
     * "Loopback ADC1_MAX_GAIN" control names used by userspace.
     */
    es7210: audio-codec@40 {
        compatible = "everest,es7210";
        reg = <0x40>;
        #sound-dai-cells = <0>;
        sound-name-prefix = "Loopback";
        status = "okay";
    };
};

/*
 * TDM-B (vendor tdm@1) on the AO pads:
 *   AO_8 = sclk, AO_7 = fs, AO_6 = dout2, AO_4 = din0, AO_9 = mclk0.
 *
 * The pinctrl groups only mux the pads to the audio function; the pad
 * clock muxes in MST_PAD_CTRL0/1 select *which* master drives them and
 * reset to MST_A.  They must be re-parented to MST_B, otherwise the
 * pads stay silent.  This is the real replacement for the vendor
 * /dev/mem poke in /etc/init.d/S03amplifiers and matches what
 * meson-g12a-u200.dts does for TDM-B.  Pad mapping: TDM-B sclk =
 * SCLK pad 1, lrclk = LRCLK pad 1, mclk = MCLK pad 0 (vendor
 * mclk_pad = <0>).
 */
&tdmif_b {
    pinctrl-0 = <&tdm_ao_b_sclk_pins>, <&tdm_ao_b_fs_pins>,
        <&tdm_ao_b_dout2_pins>, <&tdm_ao_b_din0_pins>,
        <&mclk0_ao_pins>;
    pinctrl-names = "default";

    assigned-clocks = <&clkc_audio AUD_CLKID_TDM_MCLK_PAD0>,
        <&clkc_audio AUD_CLKID_TDM_SCLK_PAD1>,
        <&clkc_audio AUD_CLKID_TDM_LRCLK_PAD1>;
    assigned-clock-parents = <&clkc_audio AUD_CLKID_MST_B_MCLK>,
        <&clkc_audio AUD_CLKID_MST_B_SCLK>,
        <&clkc_audio AUD_CLKID_MST_B_LRCLK>;
    assigned-clock-rates = <0>, <0>, <0>;
    status = "okay";
};

&tdmout_b {
    status = "okay";
};

&tdmin_b {
    status = "okay";
};

/*
 * DMA front-ends.  axg-card is a DPCM card, so only frddr/toddr links
 * create PCM devices and they are numbered in this order:
 *   PCM 0 playback -> FRDDR_A -> TDM-B
 *   PCM 1 capture  <- TODDR_A <- TDMIN_B  (es7210)
 *   PCM 2 capture  <- TODDR_B <- PDM
 */
&frddr_a {
    status = "okay";
};

&toddr_a {
    status = "okay";
};

&toddr_b {
    status = "okay";
};

/*
 * PDM array: GPIOC_0..3 = pdm_din0..3_c, GPIOC_4 = pdm_dclk_c, i.e.
 * exactly the vendor "pdmin" pinctrl.  Nothing else in this DTS uses
 * GPIOC_0..4: i2c0 takes GPIOC_5/6, the rotary encoder GPIOC_7, and the
 * only other claimant (the disabled SD node) is never probed.
 */
&pdm {
    pinctrl-0 = <&pdm_din0_c_pins>, <&pdm_din1_c_pins>,
        <&pdm_din2_c_pins>, <&pdm_din3_c_pins>,
        <&pdm_dclk_c_pins>;
    pinctrl-names = "default";
    status = "okay";
};

/ {
    /*
     * Generic DMIC codec for the PDM link (sound/soc/codecs/dmic.c).
     * The prefix must not be "PDM": the controller itself already
     * carries sound-name-prefix = "PDM" in meson-sm1.dtsi and two DAPM
     * widgets cannot share a name.
     */
    pdm_mics: audio-codec-pdm {
        compatible = "dmic-codec";
        #sound-dai-cells = <0>;
        sound-name-prefix = "MIC";
    };

    sound {
        compatible = "amlogic,axg-sound-card";
        /* card short name: amixer -c AMLAUGESOUND, plughw:AMLAUGESOUND,0 */
        model = "AML-AUGESOUND";

        /* formatters have to be aux-devs so DAPM can route through them */
        audio-aux-devs = <&tdmout_b>, <&tdmin_b>;
        audio-widgets = "Headphone", "Headphones";

        /*
         * Interconnect indices are fixed by the audio block: FRDDR_x
         * OUT n -> TDMOUT (0 = TDM-A, 1 = TDM-B, 2 = TDM-C, 3/4 =
         * SPDIF), TODDR_x IN n <- TDMIN_B (n = TDM interface index),
         * TODDR_x IN 4 <- PDM.
         */
        audio-routing = "TDMOUT_B IN 0", "FRDDR_A OUT 1",
            "TDM_B Playback", "TDMOUT_B OUT",
            "TDMIN_B IN 1", "TDM_B Capture",
            "TODDR_A IN 1", "TDMIN_B OUT",
            "TODDR_B IN 4", "PDM Capture",
            "Headphones", "Headphone LOUT",
            "Headphones", "Headphone ROUT";

        /*
         * PLL rates copied from meson-g12a-u200.dts: they give the
         * audio block exact 12.288/24.576 MHz multiples.  Verify with
         * /sys/kernel/debug/clk/clk_summary on the target; if another
         * consumer of MPLL0/1/2 shows up these numbers have to move.
         */
        clocks = <&clkc CLKID_MPLL2>,
            <&clkc CLKID_MPLL0>,
            <&clkc CLKID_MPLL1>;
        assigned-clocks = <&clkc CLKID_MPLL2>,
            <&clkc CLKID_MPLL0>,
            <&clkc CLKID_MPLL1>;
        assigned-clock-rates = <294912000>,
            <270950400>,
            <393216000>;

        dai-link-0 {
            sound-dai = <&frddr_a>;     /* PCM 0 playback */
        };

        dai-link-1 {
            sound-dai = <&toddr_a>;     /* PCM 1 capture */
        };

        dai-link-2 {
            sound-dai = <&toddr_b>;     /* PCM 2 capture */
        };

        /*
         * TDM-B backend: one bus, four codecs.  Slot masks are per
         * lane: vendor dai-tdm-lane-slot-mask-out = <0 0 1 0> becomes
         * tx-mask-2 (lane 2 = AO_6/dout2) and -in = <1 0 0 0> becomes
         * rx-mask-0 (lane 0 = AO_4/din0).
         */
        dai-link-3 {
            sound-dai = <&tdmif_b>;
            dai-format = "i2s";
            mclk-fs = <256>;
            dai-tdm-slot-num = <2>;
            dai-tdm-slot-width = <32>;
            dai-tdm-slot-tx-mask-2 = <1 1>;
            dai-tdm-slot-rx-mask-0 = <1 1>;

            codec-0 {
                sound-dai = <&sy6045s_woofer>;
            };
            codec-1 {
                sound-dai = <&sy6045s_tweeters>;
            };
            codec-2 {
                sound-dai = <&es8156>;
            };
            codec-3 {
                /* capture side of the same I2S bus */
                sound-dai = <&es7210>;
            };
        };

        /*
         * PDM backend.  No mclk-fs here: axg-pdm derives DCLK/SYSCLK
         * from the stream rate in hw_params and the property would only
         * be read for tdm-iface links.
         */
        dai-link-4 {
            sound-dai = <&pdm>;

            codec {
                sound-dai = <&pdm_mics>;
            };
        };
    };
};
""")


def build():
    return HEADER + led_nodes() + FOOTER + "\n" + AUDIO


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
    """Fail hard if the generated DTS lost the ring block or the audio graph."""
    if text.count("{") != text.count("}"):
        raise SystemExit("unbalanced braces in generated DTS")

    for want in ('compatible = "amlogic,axg-sound-card";',
                 'compatible = "everest,es7210";',
                 'compatible = "everest,es8156";',
                 'compatible = "silergy,sy6045s";',
                 "&clkc_audio {", "&arb {", "&tdmif_b {", "&pdm {"):
        if want not in text:
            raise SystemExit(f"missing audio node/property: {want}")

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
