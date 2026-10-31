#include "mfg.h"
#include "zbhci.h"
#include "zb_task_queue.h"
#include "tl_common.h"

int main() {
    drv_platform_init();

    ev_buf_init();

    mfgInit();

    zb_sched_init();

    zbhciInit();

    drv_enable_irq();

    while (true) {
        tl_zbTaskProcedure();
    }
}
