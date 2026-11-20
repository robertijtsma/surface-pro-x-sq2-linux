// SPDX-License-Identifier: GPL-2.0-only
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include "overlay.h"

static bool allow;
module_param(allow, bool, 0400);
static int ovcs_id = -1;

static bool check_phandle(const char *path, u32 ph)
{
    struct device_node *n = of_find_node_by_path(path);
    bool ok = n && n->phandle == ph;
    of_node_put(n);
    return ok;
}

static int __init sq2_venus_overlay_init(void)
{
    struct device_node *n;
    struct platform_device *vid_pdev;
    u32 parent = 0;
    int ret;

    if (!allow || !of_machine_is_compatible("microsoft,surface-pro-x"))
        return -EPERM;

    if (!check_phandle("/soc@0/clock-controller@100000", 0x2d) ||
        !check_phandle("/soc@0/iommu@15000000", 0x2e) ||
        !check_phandle("/soc@0/clock-controller@ab00000", 0xf5) ||
        !check_phandle("/soc@0/interconnect@1500000", 0x30) ||
        !check_phandle("/soc@0/interconnect@9680000", 0x5) ||
        !check_phandle("/soc@0/interconnect@1740000", 0x6c) ||
        !check_phandle("/interconnect-mc-virt", 0x6)) {
        pr_err("sq2_venus_overlay: DT phandle mismatch\n");
        return -EINVAL;
    }

    n = of_find_node_by_path("/");
    ret = n ? of_property_read_u32(n, "interrupt-parent", &parent) : -ENODEV;
    of_node_put(n);
    if (ret || parent != 1) {
        pr_err("sq2_venus_overlay: interrupt-parent mismatch (%u)\n", parent);
        return -EINVAL;
    }

    n = of_find_node_by_path("/soc@0/clock-controller@ab00000");
    if (!n)
        return -ENODEV;
    vid_pdev = of_find_device_by_node(n);
    of_node_put(n);
    if (!vid_pdev || !vid_pdev->dev.driver) {
        if (vid_pdev)
            put_device(&vid_pdev->dev);
        pr_err("sq2_venus_overlay: clock-controller@ab00000 has no bound driver (load videocc_sm8150 first)\n");
        return -EPROBE_DEFER;
    }
    put_device(&vid_pdev->dev);

    n = of_find_node_by_path("/soc@0/venus-sq2@aa00000");
    if (n) {
        of_node_put(n);
        return -EEXIST;
    }

    ret = of_overlay_fdt_apply(overlay, sizeof(overlay), &ovcs_id, NULL);
    if (ret) {
        pr_err("sq2_venus_overlay: of_overlay_fdt_apply failed: %d\n", ret);
        return ret;
    }
    __module_get(THIS_MODULE);
    pr_info("sq2_venus_overlay: applied id=%d (venus-sq2@aa00000 resident until reboot)\n", ovcs_id);
    return 0;
}

module_init(sq2_venus_overlay_init);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Surface Pro X SQ2 runtime Venus DT overlay (requires static videocc phandle)");
