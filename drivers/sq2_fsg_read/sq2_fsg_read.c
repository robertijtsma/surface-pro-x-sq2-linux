// SPDX-License-Identifier: GPL-2.0-only
/*
 * Read-only Surface Pro X (SC8180X) own-device modem_fsg extractor via
 * Qualcomm TrustZone SCM storage service (SMC 0x02001a01, GET_INFO + READ).
 *
 * Performs strictly read-only GET_INFO (op=4) and bounded 4 KiB READ (op=1)
 * calls against the device's primary GPT to locate the modem 'fsg' partition
 * (type GUID 638ff8e2-22c9-e33b-8f5d-0e81686a68cb) and expose it temporarily
 * at /sys/kernel/debug/sq2_fsg (mode 0400) so the operator can copy their own
 * factory RF calibration / NV image to /var/lib/rmtfs/modem_fsg.
 *
 * Never writes or erases storage; wipes the buffer on module unload.
 * Protocol ABI derived from lhf1a/googlebookos_surface_pro_x (QcSOCPartition).
 */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/kprobes.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/arm-smccc.h>
#include <linux/firmware/qcom/qcom_scm.h>
#include <linux/firmware/qcom/qcom_tzmem.h>
#include <linux/debugfs.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/io.h>

#define SPX_STORAGE_SVC        0x1au
#define SPX_STORAGE_CMD        0x01u
#define SPX_STORAGE_ARGINFO    0x214u
#define SPX_STORAGE_READ       1u
#define SPX_STORAGE_GET_INFO   4u
#define SPX_STORAGE_INFO_BYTES 105u
#define SPX_STORAGE_PAGE       4096u
#define SPX_STORAGE_MAX_BLOCKS 262144u
#define SPX_FSG_MAX_BYTES      (2u * 1024u * 1024u)

#define MAX_QCOM_SCM_ARGS      10
#define MAX_QCOM_SCM_RETS      3

/* Minimal internal ABI matching drivers/firmware/qcom/qcom_scm.h */
struct sq2_scm_desc {
	u32 svc;
	u32 cmd;
	u32 arginfo;
	u64 args[MAX_QCOM_SCM_ARGS];
	u32 owner;
};

struct sq2_scm_res {
	u64 result[MAX_QCOM_SCM_RETS];
};

typedef int (*sq2_scm_call_fn)(struct device *, const struct sq2_scm_desc *,
			       struct sq2_scm_res *);

static bool allow;
module_param(allow, bool, 0400);
MODULE_PARM_DESC(allow, "Must be set to 1 to perform read-only SCM storage FSG read");

static struct dentry *dbg_file;
static u8 *fsg_buf;
static size_t fsg_len;

static u32 rd_u32(const u8 *p)
{
	return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static u64 rd_u64(const u8 *p)
{
	return (u64)rd_u32(p) | ((u64)rd_u32(p + 4) << 32);
}

static void wr_u64(u8 *p, u64 v)
{
	int i;

	for (i = 0; i < 8; i++)
		p[i] = (u8)(v >> (8 * i));
}

static u32 gpt_crc(const u8 *p, size_t n, bool header)
{
	u32 c = ~0u;
	size_t i;

	for (i = 0; i < n; i++) {
		u8 b = (header && i >= 16 && i < 20) ? 0 : p[i];
		int j;

		c ^= b;
		for (j = 0; j < 8; j++)
			c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1u)));
	}
	return ~c;
}

static bool is_zero(const u8 *p, size_t n)
{
	size_t i;

	for (i = 0; i < n; i++)
		if (p[i])
			return false;
	return true;
}

static int scm_storage_op(struct device *scm_dev, sq2_scm_call_fn call,
			  u8 *pages, phys_addr_t base,
			  u32 op, u64 lba, u64 length,
			  struct sq2_scm_res *res)
{
	struct sq2_scm_desc desc = {
		.svc = SPX_STORAGE_SVC,
		.cmd = SPX_STORAGE_CMD,
		.arginfo = SPX_STORAGE_ARGINFO,
		.args = { base, 40, base + 40, 32 },
		.owner = ARM_SMCCC_OWNER_SIP,
	};

	memset(pages, 0xa5, 2 * SPX_STORAGE_PAGE);
	memset(pages, 0, 72);
	wr_u64(pages + 0, 1);
	wr_u64(pages + 32, op);
	wr_u64(pages + 40, lba);
	wr_u64(pages + 48, length);
	wr_u64(pages + 56, base + SPX_STORAGE_PAGE);
	dma_wmb();

	return call(scm_dev, &desc, res);
}

static ssize_t fsg_dbg_read(struct file *f, char __user *ubuf,
			    size_t count, loff_t *ppos)
{
	if (!fsg_buf || !fsg_len)
		return 0;
	return simple_read_from_buffer(ubuf, count, ppos, fsg_buf, fsg_len);
}

static const struct file_operations fsg_dbg_fops = {
	.owner = THIS_MODULE,
	.read = fsg_dbg_read,
	.llseek = default_llseek,
};

static int __init sq2_fsg_read_init(void)
{
	static const u8 fsg_type_guid[16] = {
		0xe2, 0xf8, 0x8f, 0x63, 0xc9, 0x22, 0x3b, 0xe3,
		0x8f, 0x5d, 0x0e, 0x81, 0x68, 0x6a, 0x68, 0xcb
	};
	struct qcom_tzmem_pool_config cfg = {
		.initial_size = 16384,
		.policy = QCOM_TZMEM_POLICY_STATIC,
	};
	struct kprobe kp = { .symbol_name = "qcom_scm_call" };
	struct device_node *node;
	struct platform_device *pdev;
	struct qcom_tzmem_pool *pool = NULL;
	sq2_scm_call_fn call;
	struct sq2_scm_res res = {};
	u8 *pages = NULL, *gpt_tbl = NULL;
	phys_addr_t base;
	u64 blocks, entries_lba, first_lba, last_lba;
	u64 fsg_first = 0, fsg_blocks = 0;
	u32 block_bytes, page_bytes, count, entry_size, crc_hdr, crc_tbl, tbl_blocks, i;
	int ret;

	if (!allow || !of_machine_is_compatible("microsoft,surface-pro-x"))
		return -EPERM;

	node = of_find_compatible_node(NULL, NULL, "qcom,scm");
	if (!node)
		return -ENODEV;
	pdev = of_find_device_by_node(node);
	of_node_put(node);
	if (!pdev)
		return -ENODEV;

	ret = register_kprobe(&kp);
	if (ret) {
		put_device(&pdev->dev);
		return ret;
	}
	call = (sq2_scm_call_fn)kp.addr;
	unregister_kprobe(&kp);
	if (!call) {
		put_device(&pdev->dev);
		return -ENOENT;
	}

	pool = qcom_tzmem_pool_new(&cfg);
	if (IS_ERR(pool)) {
		ret = PTR_ERR(pool);
		put_device(&pdev->dev);
		return ret;
	}

	pages = qcom_tzmem_alloc(pool, 2 * SPX_STORAGE_PAGE, GFP_KERNEL);
	if (!pages) {
		ret = -ENOMEM;
		goto out_pool;
	}
	base = qcom_tzmem_to_phys(pages);
	if (!base || (base & 4095) || base > 0xffffe000ULL) {
		ret = -EINVAL;
		goto out_mem;
	}

	ret = scm_storage_op(&pdev->dev, call, pages, base,
			     SPX_STORAGE_GET_INFO, 0, SPX_STORAGE_INFO_BYTES, &res);
	dma_rmb();
	if (ret || res.result[0] != 0) {
		ret = ret ? ret : -EIO;
		goto out_mem;
	}

	blocks = rd_u64(pages + SPX_STORAGE_PAGE + 0);
	block_bytes = rd_u32(pages + SPX_STORAGE_PAGE + 8);
	page_bytes = rd_u32(pages + SPX_STORAGE_PAGE + 12);
	pr_info("sq2_fsg_read: storage geometry blocks=%llu block_bytes=%u page_bytes=%u\n",
		blocks, block_bytes, page_bytes);
	if (block_bytes != SPX_STORAGE_PAGE || blocks < 7 || blocks > SPX_STORAGE_MAX_BLOCKS) {
		ret = -EINVAL;
		goto out_mem;
	}

	/* Read and CRC-validate primary GPT header at LBA 1 */
	ret = scm_storage_op(&pdev->dev, call, pages, base,
			     SPX_STORAGE_READ, 1, SPX_STORAGE_PAGE, &res);
	dma_rmb();
	if (ret || res.result[0] != 0) {
		ret = ret ? ret : -EIO;
		goto out_mem;
	}
	if (memcmp(pages + SPX_STORAGE_PAGE, "EFI PART", 8)) {
		ret = -EINVAL;
		goto out_mem;
	}
	crc_hdr = rd_u32(pages + SPX_STORAGE_PAGE + 16);
	if (gpt_crc(pages + SPX_STORAGE_PAGE, 92, true) != crc_hdr) {
		ret = -EINVAL;
		goto out_mem;
	}
	first_lba = rd_u64(pages + SPX_STORAGE_PAGE + 40);
	last_lba = rd_u64(pages + SPX_STORAGE_PAGE + 48);
	entries_lba = rd_u64(pages + SPX_STORAGE_PAGE + 72);
	count = rd_u32(pages + SPX_STORAGE_PAGE + 80);
	entry_size = rd_u32(pages + SPX_STORAGE_PAGE + 84);
	crc_tbl = rd_u32(pages + SPX_STORAGE_PAGE + 88);
	if (!count || count > 128 || entry_size != 128 ||
	    entries_lba < 2 || first_lba <= entries_lba || last_lba >= blocks) {
		ret = -EINVAL;
		goto out_mem;
	}

	tbl_blocks = (count * 128 + 4095) / 4096;
	gpt_tbl = kzalloc(tbl_blocks * 4096, GFP_KERNEL);
	if (!gpt_tbl) {
		ret = -ENOMEM;
		goto out_mem;
	}

	for (i = 0; i < tbl_blocks; i++) {
		ret = scm_storage_op(&pdev->dev, call, pages, base,
				     SPX_STORAGE_READ, entries_lba + i, SPX_STORAGE_PAGE, &res);
		dma_rmb();
		if (ret || res.result[0] != 0) {
			ret = ret ? ret : -EIO;
			goto out_tbl;
		}
		memcpy(gpt_tbl + i * 4096, pages + SPX_STORAGE_PAGE, 4096);
	}
	if (gpt_crc(gpt_tbl, count * 128, false) != crc_tbl) {
		ret = -EINVAL;
		goto out_tbl;
	}

	for (i = 0; i < count; i++) {
		const u8 *e = gpt_tbl + i * 128;

		if (is_zero(e, 16))
			continue;
		if (!memcmp(e, fsg_type_guid, 16)) {
			u64 a = rd_u64(e + 32), b = rd_u64(e + 40);

			if (a < first_lba || b < a || b > last_lba) {
				ret = -EINVAL;
				goto out_tbl;
			}
			fsg_first = a;
			fsg_blocks = b - a + 1;
			break;
		}
	}

	if (!fsg_blocks || fsg_blocks * 4096ULL > SPX_FSG_MAX_BYTES) {
		pr_err("sq2_fsg_read: fsg partition missing or oversized (%llu blocks)\n",
		       fsg_blocks);
		ret = -ENOENT;
		goto out_tbl;
	}

	fsg_len = fsg_blocks * 4096;
	fsg_buf = vmalloc(fsg_len);
	if (!fsg_buf) {
		ret = -ENOMEM;
		goto out_tbl;
	}

	for (i = 0; i < fsg_blocks; i++) {
		ret = scm_storage_op(&pdev->dev, call, pages, base,
				     SPX_STORAGE_READ, fsg_first + i, SPX_STORAGE_PAGE, &res);
		dma_rmb();
		if (ret || res.result[0] != 0) {
			memzero_explicit(fsg_buf, fsg_len);
			vfree(fsg_buf);
			fsg_buf = NULL;
			fsg_len = 0;
			ret = ret ? ret : -EIO;
			goto out_tbl;
		}
		memcpy(fsg_buf + i * 4096, pages + SPX_STORAGE_PAGE, 4096);
	}

	dbg_file = debugfs_create_file("sq2_fsg", 0400, NULL, NULL, &fsg_dbg_fops);
	pr_info("sq2_fsg_read: exported own-device fsg (%zu bytes, LBA %llu..%llu) to /sys/kernel/debug/sq2_fsg\n",
		fsg_len, fsg_first, fsg_first + fsg_blocks - 1);
	ret = 0;

out_tbl:
	memzero_explicit(gpt_tbl, tbl_blocks * 4096);
	kfree(gpt_tbl);
out_mem:
	memzero_explicit(pages, 2 * SPX_STORAGE_PAGE);
	qcom_tzmem_free(pages);
out_pool:
	qcom_tzmem_pool_free(pool);
	put_device(&pdev->dev);
	return ret;
}

static void __exit sq2_fsg_read_exit(void)
{
	debugfs_remove(dbg_file);
	if (fsg_buf) {
		memzero_explicit(fsg_buf, fsg_len);
		vfree(fsg_buf);
		fsg_buf = NULL;
		fsg_len = 0;
	}
}

module_init(sq2_fsg_read_init);
module_exit(sq2_fsg_read_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Read-only Surface Pro X SQ2 own-device modem_fsg SCM storage reader");
