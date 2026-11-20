// SPDX-License-Identifier: GPL-2.0
/*
 * Surface Pro X (SQ2 / SC8180X) Camera SPMI LDO Rail Controller
 *
 * Controls PMC8180-A (SPMI SID 1, /soc@0/spmi@c440000/pmic@1) camera LDO rails:
 *   - LDO14_A @ 0x4d00 : 1.800V camera I/O + CCI pull-up rail
 *   - LDO17_A @ 0x5000 : 2.856V camera analog rail
 *   - LDO1_A  @ 0x4000 : 1.200V camera digital core rail
 *
 * On module load: programs voltages and activates rails.
 * On module unload: safely powers down rails.
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/spmi.h>

#define LDO_STATUS1	0x08
#define LDO_VSET_LB	0x40
#define LDO_MODE_CTL1	0x44
#define LDO_EN_CTL	0x46
#define LDO_EN_BIT	0x80
#define LDO_NPM_MODE	0x07

struct cam_ldo {
	const char *name;
	u16 base;
	u16 mv;
};

static const struct cam_ldo cam_ldos[] = {
	{ "LDO14_A (1.8V IO)",     0x4d00, 1800 },
	{ "LDO17_A (2.856V AVDD)", 0x5000, 2856 },
	{ "LDO1_A  (1.2V DVDD)",   0x4000, 1200 },
};

static struct spmi_device *pmic_sdev;
static struct regmap *pmic_regmap;
static bool rails_enabled;

static void surface_cam_ldos_set(bool on)
{
	int i;

	if (!pmic_regmap)
		return;

	if (on) {
		if (rails_enabled)
			return;
		for (i = 0; i < ARRAY_SIZE(cam_ldos); i++) {
			u16 b = cam_ldos[i].base;

			regmap_write(pmic_regmap, b + LDO_MODE_CTL1, LDO_NPM_MODE);
			regmap_write(pmic_regmap, b + LDO_EN_CTL, LDO_EN_BIT);
		}
		udelay(1500);
		rails_enabled = true;
		pr_info("surface_cam_spmi: camera LDO rails ON (1.8V, 2.85V, 1.2V)\n");
	} else {
		if (!rails_enabled)
			return;
		usleep_range(2000, 5000);
		for (i = ARRAY_SIZE(cam_ldos) - 1; i >= 0; i--)
			regmap_write(pmic_regmap, cam_ldos[i].base + LDO_EN_CTL, 0);
		rails_enabled = false;
		pr_info("surface_cam_spmi: camera LDO rails OFF\n");
	}
}

static int __init surface_cam_spmi_init(void)
{
	struct device_node *np;
	int i;

	np = of_find_node_by_path("/soc@0/spmi@c440000/pmic@1");
	if (!np) {
		pr_err("surface_cam_spmi: pmic@1 node not found\n");
		return -ENODEV;
	}

	pmic_sdev = spmi_find_device_by_of_node(np);
	of_node_put(np);
	if (!pmic_sdev) {
		pr_err("surface_cam_spmi: spmi device 0-01 not found\n");
		return -EPROBE_DEFER;
	}

	pmic_regmap = dev_get_regmap(&pmic_sdev->dev, NULL);
	if (!pmic_regmap) {
		spmi_device_put(pmic_sdev);
		pmic_sdev = NULL;
		pr_err("surface_cam_spmi: regmap on 0-01 not found\n");
		return -ENODEV;
	}

	/* Pre-program target voltages */
	for (i = 0; i < ARRAY_SIZE(cam_ldos); i++) {
		u16 b = cam_ldos[i].base;
		u16 mv = cam_ldos[i].mv;
		unsigned int lb = 0, ub = 0;

		regmap_read(pmic_regmap, b + LDO_VSET_LB, &lb);
		regmap_read(pmic_regmap, b + LDO_VSET_LB + 1, &ub);
		if (((ub << 8) | lb) != mv) {
			regmap_write(pmic_regmap, b + LDO_VSET_LB, mv & 0xff);
			regmap_write(pmic_regmap, b + LDO_VSET_LB + 1, (mv >> 8) & 0xff);
		}
		regmap_write(pmic_regmap, b + LDO_MODE_CTL1, LDO_NPM_MODE);
	}

	/* Turn on rails */
	surface_cam_ldos_set(true);

	return 0;
}

static void __exit surface_cam_spmi_exit(void)
{
	surface_cam_ldos_set(false);
	if (pmic_sdev) {
		spmi_device_put(pmic_sdev);
		pmic_sdev = NULL;
	}
	pmic_regmap = NULL;
}

module_init(surface_cam_spmi_init);
module_exit(surface_cam_spmi_exit);

MODULE_DESCRIPTION("Microsoft Surface Pro X SQ2 Camera SPMI Rail Controller");
MODULE_LICENSE("GPL");
