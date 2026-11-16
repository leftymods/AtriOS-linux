"""Host C harnesses execute driver functions with controlled kernel/I2C stubs.

These are regression tests of the C implementation, not hardware or kernel ABI tests.
The separate out-of-tree cross-build checks the real kernel headers and APIs.
"""
import shutil
import subprocess
from pathlib import Path

import pytest

DRIVERS = Path(__file__).resolve().parents[1] / "drivers" / "atrios"


def function(source, name):
    text = (DRIVERS / source).read_text()
    start = text.index("static ", text.index(name + "(") - 40)
    # Some names have forward declarations. Pick the definition's opening brace.
    while text.index(";", start) < text.index("{", start):
        start = text.index("static ", text.index(";", start) + 1)
        if name + "(" not in text[start:text.index("{", start)]:
            continue
    opening = text.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    declaration = text[start:opening].split("#else", 1)[0]
    return declaration + text[opening:end]


def run_c(tmp_path, source):
    cc = shutil.which("cc")
    if not cc:
        pytest.skip("host C compiler unavailable")
    src, exe = tmp_path / "harness.c", tmp_path / "harness"
    src.write_text(source)
    compiled = subprocess.run([cc, "-std=gnu11", "-Wall", "-Wextra", "-Wno-unused-parameter",
                               "-Werror", str(src), "-o", str(exe)], capture_output=True, text=True)
    assert compiled.returncode == 0, compiled.stderr
    subprocess.run([str(exe)], check=True, capture_output=True)


COMMON = r"""
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint8_t u8;
typedef int8_t s8;
#define dev_info(...) ((void)0)
#define dev_err(...) ((void)0)
"""


@pytest.mark.parametrize("driver,prefix,extra", [
    ("snd-soc-sy6045s.c", "sy6045s", r"""
static const char *sy6045s_role_name(void *sy) { (void)sy; return "test"; }
#define crc32_le(seed, data, size) (1U)
#define strscpy(dest, src, size) ((void)snprintf(dest, size, "%s", src))
"""),
    ("snd-soc-tas5825m.c", "tas5825m", "#define msleep(ms) ((void)(ms))\n"),
])
def test_dsp_propagates_i2c_failures(tmp_path, driver, prefix, extra):
    body = function(driver, prefix + "_apply_settings")
    # dev_info is deliberately a no-op in the harness; retain diagnostic-only args.
    if prefix == "sy6045s":
        extra = extra.replace("static const char *", "static __attribute__((unused)) const char *")
    source = COMMON + extra + f"""
struct {prefix}_priv {{ char settings_file[256]; unsigned settings_crc32; }};
static int calls, fail_at;
static int {prefix}_write_block(struct {prefix}_priv *p, u8 reg, u8 len, const u8 *data)
{{ (void)p; (void)reg; (void)len; (void)data; return ++calls == fail_at ? -EIO : 0; }}
""" + body + "\nint main(void) {\n" + f"struct {prefix}_priv p = {{0}};\n" + r"""
const char settings[] = "w 01 02\nw 03 04\n";
""" + (r"""
#define APPLY() sy6045s_apply_settings(&p, settings, sizeof(settings)-1, "profile")
""" if prefix == "sy6045s" else r"""
#define APPLY() tas5825m_apply_settings(&p, settings, sizeof(settings)-1)
""") + r"""
fail_at = 1; assert(APPLY() == -EIO); assert(calls == 1);
calls = 0; fail_at = 2; assert(APPLY() == -EIO); assert(calls == 2);
calls = 0; fail_at = 0; assert(APPLY() == 0); assert(calls == 2);
return 0; }
"""
    # A diagnostic counter is used only in dev_info in the real driver.
    source = source.replace("int blocks_written = 0;", "int blocks_written __attribute__((unused)) = 0;")
    run_c(tmp_path, source)


@pytest.mark.parametrize("driver,name", [
    ("gowin_led_device.c", "lp_fb_mmap"),
    ("quasar_led_ring.c", "ring_fb_mmap"),
    ("quasar_led_screen.c", "qls_fb_mmap"),
])
def test_framebuffer_mmap_rejects_offsets_and_oversize(tmp_path, driver, name):
    run_c(tmp_path, COMMON + r"""
#define PAGE_SHIFT 12
#define PAGE_ALIGN(x) (((x) + 4095UL) & ~4095UL)
struct vm_area_struct { unsigned long vm_start, vm_end, vm_pgoff; };
struct fb_info { unsigned long screen_size; void *screen_buffer; };
static int remaps;
static int remap_vmalloc_range(struct vm_area_struct *v, void *p, unsigned long off)
{ (void)v; (void)p; (void)off; remaps++; return 0; }
""" + function(driver, name) + f"""
int main(void) {{
struct fb_info info = {{ .screen_size = 1200 }};
struct vm_area_struct v = {{ .vm_start = 0x1000, .vm_end = 0x2000 }};
assert({name}(&info, &v) == 0); assert(remaps == 1);
v.vm_end = 0x3000; assert({name}(&info, &v) == -EINVAL);
v.vm_end = 0x2000; v.vm_pgoff = 1; assert({name}(&info, &v) == -EINVAL);
v.vm_pgoff = ~0UL; assert({name}(&info, &v) == -EINVAL);
assert(remaps == 1); return 0; }}
""")


def test_rotary_quadrature_and_bounce(tmp_path):
    text = (DRIVERS / "rotary-volume.c").read_text()
    table = text[text.index("static const s8 rotary_qem"):]
    table = table[:table.index(";") + 1]
    run_c(tmp_path, COMMON + r"""
static bool debug;
static unsigned long jiffies;
#define msecs_to_jiffies(ms) (ms)
#define time_after(a,b) ((long)((b) - (a)) < 0)
struct rotary_volume {
unsigned prev_state; int step_accum; unsigned long last_change_jiffies;
u8 key_codes[2]; unsigned cw_events, ccw_events;
};
static void rotary_volume_report_event(struct rotary_volume *p, unsigned key, const char *dir)
{ (void)dir; if (key == p->key_codes[0]) p->cw_events++; else p->ccw_events++; }
""" + table + function("rotary-volume.c", "rotary_process_state") + r"""
int main(void) {
struct rotary_volume p = {.prev_state = 3, .key_codes = {1,2}};
unsigned cw[] = {1,0,2,3}, ccw[] = {2,0,1,3};
for (unsigned i=0;i<4;i++) rotary_process_state(&p,cw[i],0,0);
assert(p.cw_events == 1 && p.ccw_events == 0 && p.step_accum == 0);
for (unsigned i=0;i<4;i++) rotary_process_state(&p,ccw[i],0,0);
assert(p.cw_events == 1 && p.ccw_events == 1);
rotary_process_state(&p,1,0,0); rotary_process_state(&p,3,0,0);
assert(p.step_accum == 0 && p.cw_events == 1 && p.ccw_events == 1);
return 0; }
""")



def test_keypad_stops_irq_and_timer_before_input_release(tmp_path):
    run_c(tmp_path, COMMON + r"""
#define KERNEL_VERSION(a,b,c) (((a)<<16)|((b)<<8)|(c))
#define LINUX_VERSION_CODE KERNEL_VERSION(7,3,1)
#define WRITE_ONCE(x,y) ((x) = (y))
struct device { int kobj; };
struct platform_device { struct device dev; };
struct pin { int irq_num; };
struct gpio_keypad {
struct pin key[2]; unsigned requested_irqs; bool stopping;
int polling_timer; void *input_dev;
};
static struct gpio_keypad p = {.key = {{10},{11}}, .requested_irqs = 2};
static int gpio_kp_attr_group;
static unsigned disabled, freed, timer_stopped;
static void *platform_get_drvdata(struct platform_device *d) { (void)d; return &p; }
#define sysfs_remove_group(a,b) ((void)(a),(void)(b))
#define device_init_wakeup(a,b) ((void)(a),(void)(b))
static void disable_irq(int n) { assert(p.stopping); assert(n == 10 || n == 11); disabled++; }
static void timer_shutdown_sync(int *timer) { (void)timer; assert(disabled == 2); timer_stopped++; }
static void devm_free_irq(struct device *d, int n, void *arg)
{ (void)d; (void)n; (void)arg; assert(timer_stopped); freed++; }
static void input_unregister_device(void *input)
{ (void)input; assert(timer_stopped == 1 && freed == 2 && p.requested_irqs == 0); }
""" + function("gpio_keypad.c", "meson_gpio_kp_remove") + r"""
int main(void) { struct platform_device d = {0}; meson_gpio_kp_remove(&d); return 0; }
""")


def test_pcba_releases_each_owned_reference_once(tmp_path):
    run_c(tmp_path, COMMON + r"""
struct device { int refs; };
struct i2c_client { struct device dev; };
struct i2c_adapter { int refs; };
struct eeprom_info { struct i2c_client *client; struct i2c_adapter *adap; };
struct pcba_priv { int num_eeproms; struct eeprom_info eeproms[4]; };
static void put_device(struct device *d) { assert(d->refs > 0); d->refs--; }
static void i2c_put_adapter(struct i2c_adapter *a) { assert(a->refs > 0); a->refs--; }
""" + function("quasar_pcba_ids.c", "pcba_release_eeproms") + r"""
int main(void) {
struct i2c_client client = {.dev = {.refs = 1}};
struct i2c_adapter adapter = {.refs = 1};
struct pcba_priv p = {.num_eeproms = 2,
 .eeproms = {{.client=&client, .adap=&adapter}, {.adap=&adapter}}};
pcba_release_eeproms(&p);
assert(client.dev.refs == 0 && adapter.refs == 0 && p.num_eeproms == 0);
pcba_release_eeproms(&p); return 0;
}
""")


def test_ring_destroy_releases_buffer_and_info_together(tmp_path):
    run_c(tmp_path, COMMON + r"""
struct fb_info { void *screen_buffer; };
static int buffers, infos;
static void vfree(void *p) { assert(p); buffers++; }
static void framebuffer_release(struct fb_info *info) { (void)info; assert(buffers == 1); infos++; }
""" + function("quasar_led_ring.c", "ring_fb_destroy") + r"""
int main(void) {
struct fb_info info = {.screen_buffer = (void *)1};
ring_fb_destroy(&info); assert(buffers == 1 && infos == 1); return 0;
}
""")
