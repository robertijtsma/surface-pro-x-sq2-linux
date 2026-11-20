// SPDX-License-Identifier: GPL-2.0
/*
 * Live V4L2 S_FMT / TRY_FMT clamping hook for /dev/video12 (msm_vfe3_rdi0 IR camera)
 * Ensures OpenCV / Howdy V4L2 auto-negotiation receives V4L2_PIX_FMT_Y10 (640x480)
 * on the current boot without requiring a reload of qcom-camss.
 */

#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/videodev2.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-ioctl.h>

static struct video_device *ir_vdev;
static const struct v4l2_ioctl_ops *orig_ops;
static struct v4l2_ioctl_ops hooked_ops;

static int ir_try_fmt_mplane(struct file *file, void *fh, struct v4l2_format *f)
{
	f->fmt.pix_mp.pixelformat = V4L2_PIX_FMT_Y10;
	f->fmt.pix_mp.width = 640;
	f->fmt.pix_mp.height = 480;
	return orig_ops->vidioc_try_fmt_vid_cap_mplane(file, fh, f);
}

static int ir_s_fmt_mplane(struct file *file, void *fh, struct v4l2_format *f)
{
	f->fmt.pix_mp.pixelformat = V4L2_PIX_FMT_Y10;
	f->fmt.pix_mp.width = 640;
	f->fmt.pix_mp.height = 480;
	return orig_ops->vidioc_s_fmt_vid_cap_mplane(file, fh, f);
}

static int __init surface_ir_sfmt_init(void)
{
	struct file *filp;

	filp = filp_open("/dev/video12", O_RDONLY, 0);
	if (IS_ERR(filp)) {
		pr_err("surface_ir_sfmt: failed to open /dev/video12: %ld\n",
		       PTR_ERR(filp));
		return PTR_ERR(filp);
	}

	ir_vdev = video_devdata(filp);
	if (!ir_vdev || !ir_vdev->ioctl_ops) {
		filp_close(filp, NULL);
		return -ENODEV;
	}

	orig_ops = ir_vdev->ioctl_ops;
	hooked_ops = *orig_ops;
	hooked_ops.vidioc_try_fmt_vid_cap_mplane = ir_try_fmt_mplane;
	hooked_ops.vidioc_s_fmt_vid_cap_mplane = ir_s_fmt_mplane;
	ir_vdev->ioctl_ops = &hooked_ops;

	filp_close(filp, NULL);
	pr_info("surface_ir_sfmt: hooked /dev/video12 (%s) for Y10 format negotiation\n",
		ir_vdev->name);
	return 0;
}

static void __exit surface_ir_sfmt_exit(void)
{
	if (ir_vdev && orig_ops) {
		ir_vdev->ioctl_ops = orig_ops;
		pr_info("surface_ir_sfmt: restored original ioctl_ops on /dev/video12\n");
	}
}

module_init(surface_ir_sfmt_init);
module_exit(surface_ir_sfmt_exit);

MODULE_DESCRIPTION("Surface Pro X IR V4L2 Y10 format negotiation hook");
MODULE_LICENSE("GPL");
