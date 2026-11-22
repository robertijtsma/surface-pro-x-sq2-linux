// SPDX-License-Identifier: GPL-2.0-only
/*
 * Microsoft Surface Pro X SQ2 (Qualcomm SC8180X) RPMh S2idle Deep Sleep Driver
 *
 * 1. Detaches the unused /mmcx-reg dummy fixed-domain regulator (which only
 *    exists for the disabled dsi@ae94000 node in sc8180x.dtsi and otherwise
 *    locks SC8180X_MMCX at NOM=256 in RPMH_SLEEP_STATE 24/7).
 * 2. Switches non-display interconnect requests (qcom-camss, pcie-qcom,
 *    dwc3-qcom, qcom-geni) from QCOM_ICC_TAG_ALWAYS (0/7) to
 *    QCOM_ICC_TAG_ACTIVE_ONLY (3), preserving 100% active/wake bus bandwidth
 *    while zeroing their QCOM_ICC_BUCKET_SLEEP votes.
 * 3. During s2idle (suspend_noirq -> resume_noirq), switches the suspended
 *    display-subsystem interconnect requests to QCOM_ICC_TAG_ACTIVE_ONLY (3)
 *    and lowers APSS 18200000.rsc RPMH_SLEEP_STATE votes for cx.lvl, mx.lvl,
 *    and mmcx.lvl to RETENTION (hlvl=1, vlvl=0x10) and xo.lvl / lnbclka2 /
 *    rfclkd2 to 0 while keeping RPMH_WAKE_ONLY_STATE at full active levels.
 */

#include <linux/device.h>
#include <linux/init.h>
#include <linux/interconnect-provider.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/spinlock.h>
#include <soc/qcom/tcs.h>

static int target_lvl = 1; /* 1 = RETENTION (vlvl 0x10), 0 = OFF (vlvl 0x0) */
module_param(target_lvl, int, 0644);
MODULE_PARM_DESC(target_lvl, "ARC sleep level for cx/mx/mmcx during s2idle (1=RETENTION, 0=OFF)");

struct sq2_icc_req {
	struct hlist_node req_node;
	struct icc_node *node;
	struct device *dev;
	bool enabled;
	u32 tag;
	u32 avg_bw;
	u32 peak_bw;
};

struct sq2_cache_req {
	u32 addr;
	u32 sleep_val;
	u32 wake_val;
	u32 pad;
	struct list_head list;
};

static const char * const icc_pdev_names[] = {
	"1500000.interconnect",
	"1620000.interconnect",
	"16e0000.interconnect",
	"1700000.interconnect",
	"1720000.interconnect",
	"1740000.interconnect",
	"9680000.interconnect",
	"interconnect-camnoc-virt",
	"interconnect-mc-virt",
	"interconnect-qup-virt",
};

static struct device *g_rsc_dev;
static struct platform_device *g_pdev;

static int sq2_set_icc_tags(bool include_display)
{
	int i, updated = 0;

	for (i = 0; i < ARRAY_SIZE(icc_pdev_names); i++) {
		struct device *dev = bus_find_device_by_name(&platform_bus_type,
							     NULL, icc_pdev_names[i]);
		struct icc_provider *p;
		struct icc_node *n, *first_n = NULL;

		if (!dev)
			continue;
		p = dev_get_drvdata(dev);
		if (!p || !p->pre_aggregate || !p->aggregate || !p->set) {
			put_device(dev);
			continue;
		}

		list_for_each_entry(n, &p->nodes, node_list) {
			struct sq2_icc_req *req;

			if (!first_n)
				first_n = n;
			hlist_for_each_entry(req, &n->req_list, req_node) {
				const char *dname = req->dev ? dev_name(req->dev) : "";
				u32 new_tag;

				if (strcmp(dname, "ae00000.display-subsystem") == 0)
					new_tag = include_display ? 3 : 7;
				else
					new_tag = 3; /* QCOM_ICC_TAG_ACTIVE_ONLY */

				if (req->tag != new_tag) {
					req->tag = new_tag;
					updated++;
				}
			}
		}

		if (first_n) {
			list_for_each_entry(n, &p->nodes, node_list) {
				struct sq2_icc_req *req;

				n->avg_bw = 0;
				n->peak_bw = 0;
				p->pre_aggregate(n);
				hlist_for_each_entry(req, &n->req_list, req_node) {
					u32 avg = req->enabled ? req->avg_bw : 0;
					u32 peak = req->enabled ? req->peak_bw : 0;

					p->aggregate(n, req->tag, avg, peak,
						     &n->avg_bw, &n->peak_bw);
				}
			}
			p->set(first_n, first_n);
		}
		put_device(dev);
	}

	return updated;
}

static void sq2_update_rsc_sleep_vals(struct device *rsc_dev,
				      bool enable_deep_sleep, u32 arc_lvl)
{
	void *drv;
	struct list_head *cache_head, *pos;
	spinlock_t *cache_lock;
	u8 *dirty;
	unsigned long flags;

	if (!rsc_dev)
		return;
	drv = dev_get_drvdata(rsc_dev);
	if (!drv)
		return;

	cache_head = (struct list_head *)((u8 *)drv + 0x180);
	cache_lock = (spinlock_t *)((u8 *)drv + 0x190);
	dirty = (u8 *)drv + 0x194;

	spin_lock_irqsave(cache_lock, flags);
	list_for_each(pos, cache_head) {
		struct sq2_cache_req *req = container_of(pos, struct sq2_cache_req, list);

		if (req->sleep_val == UINT_MAX)
			continue;
		if (enable_deep_sleep) {
			switch (req->addr) {
			case 0x30000: /* cx.lvl */
			case 0x30010: /* mx.lvl */
			case 0x30080: /* mmcx.lvl */
				req->sleep_val = arc_lvl;
				break;
			case 0x30090: /* xo.lvl */
			case 0x42f04: /* lnbclka2+4 */
			case 0x43504: /* rfclkd2+4 */
				req->sleep_val = 0;
				break;
			}
		} else {
			req->sleep_val = req->wake_val;
		}
	}
	*dirty = 1;
	spin_unlock_irqrestore(cache_lock, flags);
}

static void sq2_release_unused_mmcx_reg(void)
{
	struct device *mmcx_dev = bus_find_device_by_name(&platform_bus_type,
							  NULL, "mmcx-reg");
	if (!mmcx_dev)
		return;
	if (mmcx_dev->driver) {
		device_release_driver(mmcx_dev);
		pr_info("sq2_s2idle_pm: detached unused mmcx-reg fixed-domain regulator\n");
	}
	put_device(mmcx_dev);
}

static int sq2_pm_suspend_noirq(struct device *dev)
{
	sq2_set_icc_tags(true);
	sq2_update_rsc_sleep_vals(g_rsc_dev, true, target_lvl);
	pr_info("sq2_s2idle_pm: armed RPMh deep sleep votes (arc_lvl=%d)\n",
		target_lvl);
	return 0;
}

static int sq2_pm_resume_noirq(struct device *dev)
{
	sq2_update_rsc_sleep_vals(g_rsc_dev, false, 0);
	sq2_set_icc_tags(false);
	pr_info("sq2_s2idle_pm: restored active RPMh votes\n");
	return 0;
}

static const struct dev_pm_ops sq2_pm_ops = {
	.suspend_noirq = sq2_pm_suspend_noirq,
	.resume_noirq = sq2_pm_resume_noirq,
};

static struct platform_driver sq2_pm_driver = {
	.driver = {
		.name = "sq2_s2idle_pm",
		.pm = &sq2_pm_ops,
	},
};

static int __init sq2_s2idle_pm_init(void)
{
	int ret, updated;

	g_rsc_dev = bus_find_device_by_name(&platform_bus_type, NULL, "18200000.rsc");
	if (!g_rsc_dev)
		return -ENODEV;

	sq2_release_unused_mmcx_reg();
	updated = sq2_set_icc_tags(false);
	pr_info("sq2_s2idle_pm: set %d non-display interconnect requests to ACTIVE_ONLY\n",
		updated);

	ret = platform_driver_register(&sq2_pm_driver);
	if (ret) {
		put_device(g_rsc_dev);
		return ret;
	}

	g_pdev = platform_device_register_simple("sq2_s2idle_pm", -1, NULL, 0);
	if (IS_ERR(g_pdev)) {
		ret = PTR_ERR(g_pdev);
		platform_driver_unregister(&sq2_pm_driver);
		put_device(g_rsc_dev);
		return ret;
	}

	return 0;
}

static void __exit sq2_s2idle_pm_exit(void)
{
	platform_device_unregister(g_pdev);
	platform_driver_unregister(&sq2_pm_driver);
	sq2_update_rsc_sleep_vals(g_rsc_dev, false, 0);
	sq2_set_icc_tags(false);
	put_device(g_rsc_dev);
}

module_init(sq2_s2idle_pm_init);
module_exit(sq2_s2idle_pm_exit);

MODULE_AUTHOR("Surface Pro X SQ2 Linux Contributors");
MODULE_DESCRIPTION("Surface Pro X SQ2 RPMh S2idle Deep Sleep PM Driver");
MODULE_LICENSE("GPL");
