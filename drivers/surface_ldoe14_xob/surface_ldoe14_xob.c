// SPDX-License-Identifier: GPL-2.0
/*
 * Surface Pro X (SQ2) LDO14E XOB Enable Driver
 *
 * On Surface Pro X SQ2, cmd-db configures ldoe14 (0x43900) as an XOB
 * accelerator resource ([02 00 00 00], enable-only at offset +0x4),
 * whereas upstream qcom-rpmh-regulator treats pmc8180 ldo14 as VRM and
 * hangs AMC TCS slots by accessing non-existent VRM_MODE (+0x8) /
 * VRM_VOLTAGE (+0x0) registers.
 *
 * This driver enables ldoe14 cleanly via its XOB ENABLE register (+0x4)
 * for the WSA881x SoundWire speaker amplifiers.
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <soc/qcom/cmd-db.h>
#include <soc/qcom/rpmh.h>
#include <soc/qcom/tcs.h>

#define RPMH_REGULATOR_REG_ENABLE 0x4

static struct device *rsc_child_dev;
static u32 ldoe14_en_addr;

static int __init surface_ldoe14_xob_init(void)
{
	u32 base_addr;
	struct tcs_cmd cmd = { 0 };
	int ret;

	ret = cmd_db_ready();
	if (ret < 0) {
		pr_err("surface_ldoe14_xob: cmd-db not ready (%d)\n", ret);
		return ret;
	}

	base_addr = cmd_db_read_addr("ldoe14");
	if (!base_addr) {
		pr_err("surface_ldoe14_xob: ldoe14 not found in cmd-db\n");
		return -ENODEV;
	}

	rsc_child_dev = bus_find_device_by_name(&platform_bus_type, NULL,
						"18200000.rsc:pmc8180-e-rpmh-regulators");
	if (!rsc_child_dev) {
		pr_err("surface_ldoe14_xob: pmc8180-e-rpmh-regulators device not found\n");
		return -ENODEV;
	}

	ldoe14_en_addr = base_addr + RPMH_REGULATOR_REG_ENABLE;
	cmd.addr = ldoe14_en_addr;
	cmd.data = 1;
	cmd.wait = true;

	/* Vote wake + active (keep 1.8V speaker amp rail active across wake) */
	ret = rpmh_write_async(rsc_child_dev, RPMH_WAKE_ONLY_STATE, &cmd, 1);
	if (ret)
		pr_warn("surface_ldoe14_xob: wake vote returned %d\n", ret);

	ret = rpmh_write(rsc_child_dev, RPMH_ACTIVE_ONLY_STATE, &cmd, 1);
	if (ret) {
		pr_err("surface_ldoe14_xob: active enable @ %#x failed (%d)\n",
		       ldoe14_en_addr, ret);
		put_device(rsc_child_dev);
		rsc_child_dev = NULL;
		return ret;
	}

	pr_info("surface_ldoe14_xob: enabled ldoe14 XOB rail @ %#x\n",
		ldoe14_en_addr);
	return 0;
}

static void __exit surface_ldoe14_xob_exit(void)
{
	if (rsc_child_dev) {
		put_device(rsc_child_dev);
		rsc_child_dev = NULL;
	}
}

module_init(surface_ldoe14_xob_init);
module_exit(surface_ldoe14_xob_exit);

MODULE_DESCRIPTION("Microsoft Surface Pro X SQ2 LDO14E XOB Speaker Rail Enabler");
MODULE_LICENSE("GPL");
