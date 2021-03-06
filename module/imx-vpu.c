/*
 * Copyright 2011 Sascha Hauer, Pengutronix <s.hauer@pengutronix.de>
 * Copyright 2006-2007 Freescale Semiconductor, Inc. All Rights Reserved.
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/videodev2.h>
#include <linux/uaccess.h>
#include <linux/ioport.h>
#include <linux/kdev_t.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/kfifo.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/stat.h>
#include <linux/wait.h>
#include <linux/clk.h>
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/mm.h>

#include <linux/firmware.h>

#include <media/videobuf2-dma-contig.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <mach/hardware.h>
#include <mach/iram.h>

#include "imx-vpu-jpegtable.h"
#include "imx-vpu.h"

#define VPU_IOC_MAGIC  'V'

#define	VPU_IOC_ROTATE_MIRROR	_IO(VPU_IOC_MAGIC, 7)
#define VPU_IOC_CODEC		_IO(VPU_IOC_MAGIC, 8)
#define VPU_IOC_MJPEG_QUALITY	_IO(VPU_IOC_MAGIC, 9)

#define VPU_NUM_INSTANCE	4

#define BIT_WR_PTR(x)		(0x124 + 8 * (x))
#define BIT_RD_PTR(x)		(0x120 + 8 * (x))
#define BIT_FRM_DIS_FLG(x)	(0x150 + 4 * (x))
#define BIT_MSG(x)		(0x1f0 + 4 * (x))

#define VPU_MODE_DECODER	0
#define VPU_MODE_ENCODER	1

#define RET_DEC_SEQ_ERR_REASON          0x1E0

#define PS_SAVE_SIZE            0x028000
#define SLICE_SAVE_SIZE         0x02D800

#define MAX_FW_BINARY_LEN		102400

#define V2_IRAM_SIZE	0x14000

#define VPU_MAX_BITRATE 32767

#define VPU_HUFTABLE_SIZE 432
#define VPU_QMATTABLE_SIZE 192

#if LINUX_VERSION_CODE > KERNEL_VERSION(3, 0, 0)
static inline dma_addr_t
vb2_dma_contig_plane_paddr(struct vb2_buffer *vb, unsigned int plane_no)
{
	return vb2_dma_contig_plane_dma_addr(vb, 0);
}
#endif

static unsigned int vpu_bitrate;
module_param(vpu_bitrate, uint, 0644);
MODULE_PARM_DESC(vpu_bitrate, "bitrate: Specify bitrate for encoder. "
		"0..32767, use 0 for auto bitrate. Default 0");

struct fw_header_info {
	u8 platform[12];
	u32 size;
};

enum {
        VOL_HEADER,	/* video object layer header */
        VOS_HEADER,	/* visual object sequence header */
        VIS_HEADER,	/* video object header */
};

enum {
	SPS_RBSP,
	PPS_RBSP,
};

enum {
	VPU_CODEC_AVC_DEC,
	VPU_CODEC_VC1_DEC,
	VPU_CODEC_MP2_DEC,
	VPU_CODEC_MP4_DEC,
	VPU_CODEC_DV3_DEC,
	VPU_CODEC_RV_DEC,
	VPU_CODEC_MJPG_DEC,
	VPU_CODEC_AVC_ENC,
	VPU_CODEC_MP4_ENC,
	VPU_CODEC_MJPG_ENC,
	VPU_CODEC_MAX,
};

#define STD_MPEG4	0
#define STD_H263	1
#define STD_AVC		2
#define STD_MJPG	3

static int vpu_v1_codecs[VPU_CODEC_MAX] = {
	[VPU_CODEC_AVC_DEC] = 2,
	[VPU_CODEC_VC1_DEC] = -1,
	[VPU_CODEC_MP2_DEC] = -1,
	[VPU_CODEC_DV3_DEC] = -1,
	[VPU_CODEC_RV_DEC] = -1,
	[VPU_CODEC_MJPG_DEC] = 0x82,
	[VPU_CODEC_AVC_ENC] = 3,
	[VPU_CODEC_MP4_ENC] = 1,
	[VPU_CODEC_MJPG_ENC] = 0x83,
};

static int vpu_v2_codecs[VPU_CODEC_MAX] = {
	[VPU_CODEC_AVC_DEC] = 0,
	[VPU_CODEC_VC1_DEC] = 1,
	[VPU_CODEC_MP2_DEC] = 2,
	[VPU_CODEC_MP4_DEC] = 3,
	[VPU_CODEC_DV3_DEC] = 3,
	[VPU_CODEC_RV_DEC] = 4,
	[VPU_CODEC_MJPG_DEC] = 5,
	[VPU_CODEC_AVC_ENC] = 8,
	[VPU_CODEC_MP4_ENC] = 11,
	[VPU_CODEC_MJPG_ENC] = 13,
};

enum {
	SEQ_INIT = 1,
	SEQ_END = 2,
	PIC_RUN = 3,
	SET_FRAME_BUF = 4,
	ENCODE_HEADER = 5,
	ENC_PARA_SET = 6,
	DEC_PARA_SET = 7,
	DEC_BUF_FLUSH = 8,
	RC_CHANGE_PARAMETER = 9,
	FIRMWARE_GET = 0xf,
};

struct vpu_regs {
	u32 bitstream_buf_size;
	u32 bits_streamctrl_mask;
	u32 bit_buf_check_dis;
	u32 bit_enc_dyn_bufalloc_en;
	u32 bit_buf_pic_reset;
	u32 bit_buf_pic_flush;
	u32 bit_pic_width_offset;
	u32 cmd_enc_seq_intra_qp;
	u32 fmo_slice_save_buf_size;
	u32 bit_pic_width_mask;
	u32 ret_dec_pic_option;
	u32 para_buf_size;
	u32 code_buf_size;
	u32 work_buf_size;
};

static struct vpu_regs regs_v1 = {
	.bitstream_buf_size = (512 * 1024),
	.bits_streamctrl_mask = 0x01f,
	.bit_buf_check_dis = 1,
	.bit_enc_dyn_bufalloc_en = 4,
	.bit_buf_pic_reset = 3,
	.bit_buf_pic_flush = 2,
	.bit_pic_width_offset = 10,
	.cmd_enc_seq_intra_qp = 0x1bc,
	.fmo_slice_save_buf_size = 32,
	.bit_pic_width_mask = 0x3ff,
	.ret_dec_pic_option = 0x1d0,
	.para_buf_size = 10 * 1024,
	.code_buf_size = 64 * 1024,
	.work_buf_size = (288 * 1024) + (32 * 1024 * 8),
};

static struct vpu_regs regs_v2 = {
	.bitstream_buf_size = 1024 * 1024,
	.bits_streamctrl_mask = 0x03f,
	.bit_buf_check_dis = 2,
	.bit_enc_dyn_bufalloc_en = 5,
	.bit_buf_pic_reset = 4,
	.bit_buf_pic_flush = 3,
	.bit_pic_width_offset = 16,
	.cmd_enc_seq_intra_qp = 0x1c4,
	.fmo_slice_save_buf_size = 32,
	.bit_pic_width_mask = 0xffff,
	.ret_dec_pic_option = 0x1d4,
	.para_buf_size = 10 * 1024,
	.code_buf_size = 200 * 1024,
	.work_buf_size = (512 * 1024) + (32 * 1024 * 8),
};

struct vpu_instance;

struct vpu_driver_data {
	struct vpu_regs *regs;
	const char *fw_name;
	const int *codecs;
	int (*alloc_fb)(struct vpu_instance *instance);
	int version;
};

static int vpu_alloc_fb_v1(struct vpu_instance *instance);
static int vpu_alloc_fb_v2(struct vpu_instance *instance);

static struct vpu_driver_data drvdata_imx27 = {
	.regs = &regs_v1,
	.codecs = vpu_v1_codecs,
	.fw_name = "vpu_fw_imx27.bin",
	.alloc_fb = vpu_alloc_fb_v1,
	.version = 1,
};

static struct vpu_driver_data drvdata_imx51 = {
	.regs = &regs_v2,
	.codecs = vpu_v2_codecs,
	.fw_name = "vpu_fw_imx51.bin",
	.alloc_fb = vpu_alloc_fb_v2,
	.version = 2,
};

static struct vpu_driver_data drvdata_imx53 = {
	.regs = &regs_v2,
	.codecs = vpu_v2_codecs,
	.fw_name = "vpu_fw_imx53.bin",
	.alloc_fb = vpu_alloc_fb_v2,
	.version = 2,
};

/* buffer for one video frame */
struct vpu_buffer {
	struct vb2_buffer		vb;
	struct list_head		list;
};

struct vpu;

struct memalloc_record {
	u32 size;
	dma_addr_t dma_addr;
	void __iomem *cpu_addr;
};

#define VPU_MAX_FB	10

struct vpu_instance {
	struct vpu *vpu;
	int idx;
	int width, height;
	int num_fb;
	int format;
	struct vb2_queue vidq;
	int in_use;
	int videobuf_init;

	dma_addr_t	bitstream_buf_phys;
	void __iomem	*bitstream_buf;

	dma_addr_t	slice_mem_buf_phys;
	void __iomem	*slice_mem_buf;

	dma_addr_t	para_buf_phys;
	void __iomem	*para_buf;

	dma_addr_t	ps_mem_buf_phys;
	void __iomem	*ps_mem_buf;

	u32 framerate;
	u32 gopsize;
	u32 rotmir;
	int hold;
	int newdata;
	wait_queue_head_t waitq;
	int needs_init;

	ktime_t		frametime, frame_duration;

	struct memalloc_record rec[VPU_MAX_FB];

	int mode;

	struct kfifo	fifo;
	int		headersize;
	void		*header;
	int		buffered_size;
	int		flushing;
	int		standard;
	unsigned int	readofs, fifo_in, fifo_out;

	u32		*mjpg_huf_table;
	u32		*mjpg_q_mat_table;
	int		mjpg_quality;

	/* statistic */
	uint64_t	encoding_time_max;
	uint64_t	encoding_time_total;
	uint64_t	start_time;
	int		num_frames;
};

struct vpu {
	void *dummy;
	struct device		*dev;
	struct fasync_struct	*async_queue;
	struct video_device	*vdev;
	void __iomem		*base;
	struct vpu_instance	instance[VPU_NUM_INSTANCE];
	spinlock_t		lock;
	struct vpu_buffer	*active;
	struct vb2_alloc_ctx	*alloc_ctx;
	enum v4l2_field		field;
	struct list_head	queued;
	struct clk		*clk;
	int			irq;
	struct vpu_regs		*regs;
	struct vpu_driver_data	*drvdata;
	int			sequence;

	dma_addr_t	vpu_code_table_phys;
	void __iomem	*vpu_code_table;
	dma_addr_t	vpu_work_buf_phys;
	void __iomem	*vpu_work_buf;

	void __iomem	*iram_virt;
	unsigned long	iram_phys;
	struct workqueue_struct	*workqueue;
	struct work_struct	work;
	struct completion	complete;
};

static struct vpu_buffer *to_vpu_vb(struct vb2_buffer *vb)
{
	return container_of(vb, struct vpu_buffer, vb);
}

#define ROUND_UP_2(num)	(((num) + 1) & ~1)
#define ROUND_UP_4(num)	(((num) + 3) & ~3)
#define ROUND_UP_8(num)	(((num) + 7) & ~7)
#define ROUND_UP_16(num)	(((num) + 15) & ~15)

static unsigned int vpu_fifo_len(struct vpu_instance *instance)
{
	return instance->fifo_in - instance->fifo_out;
}

static unsigned int vpu_fifo_avail(struct vpu_instance *instance)
{
	struct vpu *vpu = instance->vpu;
	struct vpu_regs *regs = vpu->regs;

	return regs->bitstream_buf_size - vpu_fifo_len(instance);
}

static int vpu_fifo_out(struct vpu_instance *instance, int len)
{
	instance->fifo_out += len;
	return 0;
}

static int vpu_fifo_in(struct vpu_instance *instance, const char __user *ubuf, size_t len)
{
	struct vpu *vpu = instance->vpu;
	struct vpu_regs *regs = vpu->regs;
	unsigned int off, l;
	int ret;

	len = min(vpu_fifo_avail(instance) - 1, len);

	off = instance->fifo_in & (regs->bitstream_buf_size - 1);

	l = min(len, regs->bitstream_buf_size - off);

	ret = copy_from_user(instance->bitstream_buf + off, ubuf, l);
	if (ret)
		return ret;
	ret = copy_from_user(instance->bitstream_buf, ubuf + l, len - l);
	if (ret)
		return ret;

	instance->fifo_in += len;

	return len;
}

static ssize_t show_info(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	struct vpu *vpu = dev_get_drvdata(dev);
	int i, len = 0;
	struct vpu_buffer *vbuf, *tmp;

	spin_lock_irq(&vpu->lock);

	len += sprintf(buf + len, "VPU Info\n"
		"--------\n"
		"active: %p", vpu->active);
	if (vpu->active) {
		len += sprintf(buf + len, " (instance %d)\n", vpu->instance->idx);
	} else
		len += sprintf(buf + len, "\n");


	for (i = 0; i < VPU_NUM_INSTANCE; i++) {
		struct vpu_instance *instance = &vpu->instance[i];

		len += sprintf(buf + len, "instance %d info\n"
			"----------------\n"
			"mode:       %s\n"
			"in_use:     %d\n"
			"needs_init: %d\n"
			"hold:       %d\n"
			"width:      %d\n"
			"height:     %d\n",
			i,
			instance->mode == VPU_MODE_DECODER ? "decoder" : "encoder",
			instance->in_use,
			instance->needs_init,
			instance->hold,
			instance->width,
			instance->height);
		if (instance->mode == VPU_MODE_ENCODER) {
			uint64_t max_time = instance->encoding_time_max;
			uint64_t avg_time = instance->encoding_time_total;

			do_div(max_time, 1000000);
			do_div(avg_time, instance->num_frames);
			do_div(avg_time, 1000000);

			len += sprintf(buf + len,
				"kfifo_avail: %d\n"
				"buffered size: %d\n"
				"header size:   %d\n"
				"avg encoding time: %lldms\n"
				"max encoding time: %lldms\n",
				kfifo_avail(&instance->fifo),
				instance->buffered_size,
				instance->headersize,
				avg_time,
				max_time);
		} else {
			len += sprintf(buf + len, "fifo_avail: %d\n", vpu_fifo_avail(instance));
		}
		len += sprintf(buf + len, "\n");
	}

	len += sprintf(buf + len, "queued buffers\n"
			          "--------------\n");

	list_for_each_entry_safe(vbuf, tmp, &vpu->queued, list) {
		struct vb2_queue *q = vbuf->vb.vb2_queue;
		struct vpu_instance *q_instance = vb2_get_drv_priv(q);
		len += sprintf(buf + len, "%p (instance %d, streaming %d)\n",
				vbuf, q_instance->idx, vb2_is_streaming(q));
	}

	spin_unlock_irq(&vpu->lock);

	return len;
}
static DEVICE_ATTR(info, S_IRUGO, show_info, NULL);

static void vpu_write(struct vpu *vpu, u32 reg, u32 data)
{
	writel(data, vpu->base + reg);
}

static u32 vpu_read(struct vpu *vpu, u32 reg)
{
	return readl(vpu->base + reg);
}

static void vpu_reset(struct vpu *vpu)
{
	unsigned long val;

	vpu_write(vpu, BIT_CODE_RUN, 0);
	val = vpu_read(vpu, BIT_CODE_RESET);
	vpu_write(vpu, BIT_CODE_RESET, val | 1);
	udelay(100);
	vpu_write(vpu, BIT_CODE_RESET, val & ~1);
	vpu_write(vpu, BIT_CODE_RUN, 1);
}

static int vpu_is_busy(struct vpu *vpu)
{
	return vpu_read(vpu, BIT_BUSY_FLAG) != 0;
}

static int vpu_wait(struct vpu *vpu)
{
	int to = 100000;
	while (1) {
		if (!vpu_is_busy(vpu))
			return 0;
		if (!to--) {
			dev_dbg(vpu->dev, "%s timed out\n", __func__);
			vpu_write(vpu, BIT_BUSY_FLAG, 0x0);
			dump_stack();
			return -ETIMEDOUT;
		}
		udelay(1);
	}
}

static void vpu_bit_issue_command(struct vpu_instance *instance, int cmd)
{
	struct vpu *vpu = instance->vpu;

	vpu_write(vpu, BIT_RUN_INDEX, instance->idx);
	vpu_write(vpu, BIT_RUN_COD_STD, vpu->drvdata->codecs[instance->format]);
	vpu_write(vpu, BIT_RUN_COMMAND, cmd);
}

static int vpu_alloc_fb_v1(struct vpu_instance *instance)
{
	struct vpu *vpu = instance->vpu;
	int i, ret = 0;
	int size = (instance->width * instance->height * 3) / 2;
	u32 *para_buf = instance->para_buf;

	for (i = 0; i < instance->num_fb; i++) {
		struct memalloc_record *rec = &instance->rec[i];

		rec->cpu_addr = dma_alloc_coherent(NULL, size, &rec->dma_addr,
					       GFP_DMA | GFP_KERNEL);
		if (!rec->cpu_addr) {
			ret = -ENOMEM;
			goto out;
		}
		rec->size = size;

		/* Let the codec know the addresses of the frame buffers. */
		para_buf[i * 3] = rec->dma_addr;
		para_buf[i * 3 + 1] = rec->dma_addr + instance->width * instance->height;
		para_buf[i * 3 + 2] = para_buf[i * 3 + 1] +
			(instance->width / 2) * (instance->height / 2);
	}
out:
	if (ret)
		dev_dbg(vpu->dev, "%s failed with %d\n", __func__, ret);
	return ret;
}

static int vpu_alloc_fb_v2(struct vpu_instance *instance)
{
	struct vpu *vpu = instance->vpu;
	int i, ret = 0;
	int size = (instance->width * instance->height * 3) / 2;
	unsigned long *para_buf = instance->para_buf;
	int height = instance->height;
	int stridey = instance->width;
	int mvsize = (stridey * height) >> 2;

	size += mvsize;

	for (i = 0; i < instance->num_fb + 1; i++) {
		struct memalloc_record *rec = &instance->rec[i];

		rec->cpu_addr = dma_alloc_coherent(NULL, size, &rec->dma_addr,
						       GFP_DMA | GFP_KERNEL);
		if (!rec->cpu_addr) {
			ret = -ENOMEM;
			goto out;
		}
		rec->size = size;
	}

	for (i = 0; i < instance->num_fb; i+=2) {
		struct memalloc_record *rec = &instance->rec[i];

		para_buf[i * 3] = rec->dma_addr + instance->width * instance->height; /* Cb */
		para_buf[i * 3 + 1] = rec->dma_addr; /* Y */
		para_buf[i * 3 + 3] = para_buf[i * 3] +
			(instance->width / 2) * (instance->height / 2); /* Cr */
		if (instance->standard == STD_AVC)
			para_buf[96 + i + 1] = para_buf[i * 3 + 3] +
				(instance->width / 2) * (instance->height / 2);

		if (i + 1 < instance->num_fb) {
			para_buf[i * 3 + 2] = instance->rec[i + 1].dma_addr; /* Y */
			para_buf[i * 3 + 5] = instance->rec[i + 1].dma_addr +
				instance->width * instance->height ; /* Cb */
			para_buf[i * 3 + 4] = para_buf[i * 3 + 5] +
				(instance->width / 2) * (instance->height / 2); /* Cr */
		}
		if (instance->standard == STD_AVC)
			para_buf[96 + i] = para_buf[i * 3 + 4] + (instance->width / 2) * (instance->height / 2);
	}
	if (instance->standard == STD_MPEG4) {
		para_buf[97] = instance->rec[instance->num_fb].dma_addr;
	}
out:
	if (ret)
		dev_dbg(vpu->dev, "%s failed with %d\n", __func__, ret);
	return ret;
}

static int encode_header(struct vpu_instance *instance, int headertype)
{
	struct vpu *vpu = instance->vpu;
	void *header;
	int headersize;

	vpu_write(vpu, CMD_ENC_HEADER_CODE, headertype);

	vpu_bit_issue_command(instance, ENCODE_HEADER);

	if (vpu_wait(vpu))
		return -EINVAL;

	headersize = vpu_read(vpu, BIT_WR_PTR(instance->idx)) -
			vpu_read(vpu, BIT_RD_PTR(instance->idx));

	header = krealloc(instance->header, headersize + instance->headersize, GFP_KERNEL);
	if (!header)
		return -ENOMEM;

	memcpy(header + instance->headersize, instance->bitstream_buf, headersize);

	print_hex_dump(KERN_INFO, "header: ", DUMP_PREFIX_ADDRESS, 16, 1,
			instance->bitstream_buf, headersize, 0);

	instance->header = header;
	instance->headersize += headersize;

	return 0;
}

static void vpu_calc_mjpeg_quant_tables(struct vpu_instance *instance,
		int quality)
{
	int   i;
	unsigned int temp, new_quality;

	if (quality > 100)
		quality = 100;
	if (quality < 5)
		quality = 5;

	/* re-calculate the Q-matrix */
	if (quality > 50)
		new_quality = 5000 / quality;
	else
		new_quality = 200 - 2 * quality;

	pr_info("quality = %d %d", quality, new_quality);

	/* recalculate  luma Quantification table */
	for (i = 0; i< 64; i++) {
		temp = ((unsigned int)lumaQ2[i] * new_quality + 50) / 100;
		if (temp <= 0)
			temp = 1;
		if (temp > 255)
			temp = 255;
		lumaQ2[i] = (unsigned char)temp;
	}

	pr_info("Luma Quant Table is \n");
	for (i = 0; i < 64; i += 8) {
		pr_info("0x%2x, 0x%2x, 0x%2x, 0x%2x, 0x%2x, 0x%2x, 0x%2x, 0x%2x, \n",
				lumaQ2[i], lumaQ2[i + 1], lumaQ2[i + 2], lumaQ2[i + 3],
				lumaQ2[i + 4], lumaQ2[i + 5], lumaQ2[i + 6], lumaQ2[i + 7]);
	}

	/* chromaB Quantification Table */
	for (i = 0; i< 64; i++) {
		temp = ((unsigned int)chromaBQ2[i] * new_quality + 50) / 100;
		if (temp <= 0)
			temp = 1;
		if (temp > 255)
			temp = 255;
		chromaBQ2[i] = (unsigned char)temp;
	}

	pr_info("chromaB Quantification Table is \n");
	for(i = 0; i < 64; i = i+8) {
		pr_info("0x%2x, 0x%2x, 0x%2x, 0x%2x, 0x%2x, 0x%2x, 0x%2x, 0x%2x, \n",
				chromaBQ2[i], chromaBQ2[i + 1], chromaBQ2[i + 2], chromaBQ2[i + 3],
				chromaBQ2[i + 4], chromaBQ2[i + 5], chromaBQ2[i + 6], chromaBQ2[i + 7]);
	}

	/* chromaR Quantification Table */
	for (i = 0; i< 64; i++) {
		temp = ((unsigned int)chromaRQ2[i] * new_quality + 50) / 100;
		if (temp <= 0)
			temp = 1;
		if (temp > 255)
			temp = 255;
		chromaRQ2[i] = (unsigned char)temp;
	}

	pr_info("chromaR Quantification Table is \n");
	for (i = 0; i < 64; i += 8) {
		pr_info("0x%2x, 0x%2x, 0x%2x, 0x%2x, 0x%2x, 0x%2x, 0x%2x, 0x%2x, \n",
				chromaRQ2[i], chromaRQ2[i + 1], chromaRQ2[i + 2], chromaRQ2[i + 3],
				chromaRQ2[i + 4], chromaRQ2[i + 5], chromaRQ2[i + 6], chromaRQ2[i + 7]);
	}
}

static int vpu_generate_jpeg_tables(struct vpu_instance *instance,
		int quality)
{
	u8 *q_mat_table;
	u8 *huf_table;
	int i;

	huf_table = kzalloc(VPU_HUFTABLE_SIZE, GFP_KERNEL);
	q_mat_table = kzalloc(VPU_QMATTABLE_SIZE, GFP_KERNEL);

	if (!huf_table || !q_mat_table) {
		kfree(huf_table);
		kfree(q_mat_table);
		return -ENOMEM;
	}

	for (i = 0; i < 16; i += 4) {
		huf_table[i] = lumaDcBits[i + 3];
		huf_table[i + 1] = lumaDcBits[i + 2];
		huf_table[i + 2] = lumaDcBits[i + 1];
		huf_table[i + 3] = lumaDcBits[i];
	}

	for (i = 16; i < 32 ; i += 4) {
		huf_table[i] = lumaDcValue[i + 3 - 16];
		huf_table[i + 1] = lumaDcValue[i + 2 - 16];
		huf_table[i + 2] = lumaDcValue[i + 1 - 16];
		huf_table[i + 3] = lumaDcValue[i - 16];
	}

	for (i = 32; i < 48; i += 4) {
		huf_table[i] = lumaAcBits[i + 3 - 32];
		huf_table[i + 1] = lumaAcBits[i + 2 - 32];
		huf_table[i + 2] = lumaAcBits[i + 1 - 32];
		huf_table[i + 3] = lumaAcBits[i - 32];
	}

	for (i = 48; i < 216; i += 4) {
		huf_table[i] = lumaAcValue[i + 3 - 48];
		huf_table[i + 1] = lumaAcValue[i + 2 - 48];
		huf_table[i + 2] = lumaAcValue[i + 1 - 48];
		huf_table[i + 3] = lumaAcValue[i - 48];
	}

	for (i = 216; i < 232; i += 4) {
		huf_table[i] = chromaDcBits[i + 3 - 216];
		huf_table[i + 1] = chromaDcBits[i + 2 - 216];
		huf_table[i + 2] = chromaDcBits[i + 1 - 216];
		huf_table[i + 3] = chromaDcBits[i - 216];
	}

	for (i = 232; i < 248; i += 4) {
		huf_table[i] = chromaDcValue[i + 3 - 232];
		huf_table[i + 1] = chromaDcValue[i + 2 - 232];
		huf_table[i + 2] = chromaDcValue[i + 1 - 232];
		huf_table[i + 3] = chromaDcValue[i - 232];
	}

	for (i = 248; i < 264; i += 4) {
		huf_table[i] = chromaAcBits[i + 3 - 248];
		huf_table[i + 1] = chromaAcBits[i + 2 - 248];
		huf_table[i + 2] = chromaAcBits[i + 1 - 248];
		huf_table[i + 3] = chromaAcBits[i - 248];
	}

	for (i = 264; i < 432; i += 4) {
		huf_table[i] = chromaAcValue[i + 3 - 264];
		huf_table[i + 1] = chromaAcValue[i + 2 - 264];
		huf_table[i + 2] = chromaAcValue[i + 1 - 264];
		huf_table[i + 3] = chromaAcValue[i - 264];
	}

	/* according to the bitrate, recalculate the quant table */
	vpu_calc_mjpeg_quant_tables(instance, quality);

	/* Rearrange and insert pre-defined Q-matrix to deticated variable. */
	for (i = 0; i < 64; i += 4) {
		q_mat_table[i] = lumaQ2[i + 3];
		q_mat_table[i + 1] = lumaQ2[i + 2];
		q_mat_table[i + 2] = lumaQ2[i + 1];
		q_mat_table[i + 3] = lumaQ2[i];
	}

	for (i = 64; i < 128; i += 4) {
		q_mat_table[i] = chromaBQ2[i + 3 - 64];
		q_mat_table[i + 1] = chromaBQ2[i + 2 - 64];
		q_mat_table[i + 2] = chromaBQ2[i + 1 - 64];
		q_mat_table[i + 3] = chromaBQ2[i - 64];
	}

	for (i = 128; i < 192; i += 4) {
		q_mat_table[i] = chromaRQ2[i + 3 - 128];
		q_mat_table[i + 1] = chromaRQ2[i + 2 - 128];
		q_mat_table[i + 2] = chromaRQ2[i + 1 - 128];
		q_mat_table[i + 3] = chromaRQ2[i - 128];
	}

	instance->mjpg_huf_table = (void *)huf_table;
	instance->mjpg_q_mat_table = (void *)q_mat_table;

	return 0;
}

#define VPU_DEFAULT_MPEG4_QP 15
#define VPU_DEFAULT_H264_QP 35

static int noinline vpu_enc_get_initial_info(struct vpu_instance *instance)
{
	struct vpu *vpu = instance->vpu;
	struct vpu_regs *regs = vpu->regs;
	int ret;
	u32 data;
	u32 val;
	u32 sliceSizeMode = 0;
	u32 sliceMode = 0;
	u32 enableAutoSkip = 0;
	u32 initialDelay = 1;
	u32 sliceReport = 0;
	u32 mbReport = 0;
	u32 rcIntraQp = 0;
	u32 *table_buf, *para_buf;
	int i;

	switch (instance->standard) {
	case STD_MPEG4:
	case STD_H263:
		instance->format = VPU_CODEC_MP4_ENC;
		break;
	case STD_AVC:
		instance->format = VPU_CODEC_AVC_ENC;
		break;
	case STD_MJPG:
		instance->format = VPU_CODEC_MJPG_ENC;
		break;
	default:
		return -EINVAL;
	};

	vpu_write(vpu, BIT_BIT_STREAM_CTRL, 1 << regs->bit_buf_pic_reset |
			1 << regs->bit_buf_pic_flush);
	vpu_write(vpu, BIT_PARA_BUF_ADDR, instance->para_buf_phys);

	vpu_write(vpu, BIT_WR_PTR(instance->idx), instance->bitstream_buf_phys);
	vpu_write(vpu, BIT_RD_PTR(instance->idx), instance->bitstream_buf_phys);

	data = (instance->width << regs->bit_pic_width_offset) | instance->height;
	vpu_write(vpu, CMD_ENC_SEQ_SRC_SIZE, data);
	vpu_write(vpu, CMD_ENC_SEQ_SRC_F_RATE, 0x0); /* 0x03e87530 */

	if (instance->standard == STD_MPEG4) {
		u32 mp4_intraDcVlcThr = 7;
		u32 mp4_reversibleVlcEnable = 0;
		u32 mp4_dataPartitionEnable = 0;
		u32 mp4_hecEnable = 0;
		u32 mp4_verid = 1;

		vpu_write(vpu, CMD_ENC_SEQ_COD_STD, STD_MPEG4);

		data = mp4_intraDcVlcThr << 2 |
			mp4_reversibleVlcEnable << 1 |
			mp4_dataPartitionEnable |
			mp4_hecEnable << 5 |
			mp4_verid << 6;

		vpu_write(vpu, CMD_ENC_SEQ_MP4_PARA, data);
		rcIntraQp = VPU_DEFAULT_MPEG4_QP;
	} else if (instance->standard == STD_H263) {
		u32 h263_annexJEnable = 0;
		u32 h263_annexKEnable = 0;
		u32 h263_annexTEnable = 0;
		int w = instance->width;
		int h = instance->height;

		if (!(w == 128 && h == 96) &&
		    !(w == 176 && h == 144) &&
		    !(w == 352 && h == 288) &&
		    !(w == 704 && h == 576)) {
			dev_dbg(vpu->dev, "VPU: unsupported size\n");
			return -EINVAL;
		}

		vpu_write(vpu, CMD_ENC_SEQ_COD_STD, STD_H263);

		data = h263_annexJEnable << 2 |
			h263_annexKEnable << 1 |
			h263_annexTEnable;

		vpu_write(vpu, CMD_ENC_SEQ_263_PARA, data);
		rcIntraQp = VPU_DEFAULT_MPEG4_QP;
	} else if (instance->standard == STD_AVC) {
		u32 avc_deblkFilterOffsetBeta = 0;
		u32 avc_deblkFilterOffsetAlpha = 0;
		u32 avc_disableDeblk = 0;
		u32 avc_constrainedIntraPredFlag = 0;
		u32 avc_chromaQpOffset = 0;

		vpu_write(vpu, CMD_ENC_SEQ_COD_STD, STD_AVC);

		data = (avc_deblkFilterOffsetBeta & 15) << 12 |
			(avc_deblkFilterOffsetAlpha & 15) << 8 |
			avc_disableDeblk << 6 |
			avc_constrainedIntraPredFlag << 5 |
			(avc_chromaQpOffset & 31);
		vpu_write(vpu, CMD_ENC_SEQ_264_PARA, data);
		rcIntraQp = VPU_DEFAULT_H264_QP;
	} else if (instance->standard == STD_MJPG) {
		vpu_write(vpu, CMD_ENC_SEQ_JPG_PARA, 0);
		vpu_write(vpu, CMD_ENC_SEQ_JPG_RST_INTERVAL, 60);
		vpu_write(vpu, CMD_ENC_SEQ_JPG_THUMB_EN, 0);
		vpu_write(vpu, CMD_ENC_SEQ_JPG_THUMB_SIZE, 0);
		vpu_write(vpu, CMD_ENC_SEQ_JPG_THUMB_OFFSET, 0);

		vpu_generate_jpeg_tables(instance, instance->mjpg_quality);

		para_buf = instance->para_buf;
		table_buf = (u32 *)instance->mjpg_huf_table;

		for (i = 0; i < 108; i += 2) {
			para_buf[i + 1] = *table_buf;
			para_buf[i] = *(table_buf + 1);
			table_buf += 2;
		}

		table_buf = (u32 *)instance->mjpg_q_mat_table;

		for (i = 0; i < 48; i += 2) {
			para_buf[i + 129] = *table_buf;
			para_buf[i + 128] = *(table_buf + 1);
			table_buf += 2;
		}
	}

	data = 4000 << 2 | /* slice size */
		sliceSizeMode << 1 | sliceMode;

	vpu_write(vpu, CMD_ENC_SEQ_SLICE_MODE, data);
	vpu_write(vpu, CMD_ENC_SEQ_GOP_NUM, 30); /* gop size */

	if (vpu_bitrate) {	/* rate control enabled */
		data = (!enableAutoSkip) << 31 |
			initialDelay << 16 |
			vpu_bitrate << 1 |
			0;
		vpu_write(vpu, CMD_ENC_SEQ_RC_PARA, data);
	} else {
		vpu_write(vpu, CMD_ENC_SEQ_RC_PARA, 0);
	}

	vpu_write(vpu, CMD_ENC_SEQ_RC_BUF_SIZE, 0); /* vbv buffer size */
	vpu_write(vpu, CMD_ENC_SEQ_INTRA_REFRESH, 0);

	vpu_write(vpu, CMD_ENC_SEQ_BB_START, instance->bitstream_buf_phys);
	vpu_write(vpu, CMD_ENC_SEQ_BB_SIZE, regs->bitstream_buf_size / 1024);

	data = (sliceReport << 1) | mbReport;

	vpu_write(vpu, CMD_ENC_SEQ_RC_QP_MAX, 4096);

	if (rcIntraQp >= 0)
		data |= (1 << 5);

	vpu_write(vpu, regs->cmd_enc_seq_intra_qp, rcIntraQp);

	vpu_write(vpu, CMD_ENC_SEQ_OPTION, data);

	if (vpu->drvdata->version == 1) {
		vpu_write(vpu, V1_CMD_ENC_SEQ_FMO, 0);
		vpu_write(vpu, V1_BIT_SEARCH_RAM_BASE_ADDR, vpu->iram_phys);
	} else {
		vpu_write(vpu, V2_CMD_ENC_SEQ_SEARCH_BASE, vpu->iram_phys);
		vpu_write(vpu, V2_CMD_ENC_SEQ_SEARCH_SIZE, 48 * 1024);
	}

	vpu_write(vpu, BIT_BUSY_FLAG, 0x1);

	vpu_bit_issue_command(instance, SEQ_INIT);
	if (vpu_wait(vpu))
		return -EINVAL;

	if (vpu_read(vpu, RET_ENC_SEQ_SUCCESS) == 0) {
		val = vpu_read(vpu, RET_DEC_SEQ_ERR_REASON);
		dev_dbg(vpu->dev, "%s failed Errorcode: %d\n", __func__, val);
		return -EINVAL;
	}

	instance->num_fb = 2;
	ret = vpu->drvdata->alloc_fb(instance);
	if (ret) {
		dev_dbg(vpu->dev, "alloc fb failed\n");
		goto out;
	}

	/* Tell the codec how much frame buffers we allocated. */
	vpu_write(vpu, CMD_SET_FRAME_BUF_NUM, instance->num_fb);
	vpu_write(vpu, CMD_SET_FRAME_BUF_STRIDE, ROUND_UP_8(instance->width));

	if (vpu->drvdata->version == 2) {
		vpu_write(vpu, CMD_SET_FRAME_SOURCE_BUF_STRIDE,
				ROUND_UP_8(instance->width));
		vpu_write(vpu, V2_CMD_SET_FRAME_AXI_DBKY_ADDR, vpu->iram_phys + 48 * 1024);
		vpu_write(vpu, V2_CMD_SET_FRAME_AXI_DBKC_ADDR, vpu->iram_phys + 53 * 1024);
		vpu_write(vpu, V2_CMD_SET_FRAME_AXI_BIT_ADDR, vpu->iram_phys + 58 * 1024);
		vpu_write(vpu, V2_CMD_SET_FRAME_AXI_IPACDC_ADDR, vpu->iram_phys + 68 * 1024);
		vpu_write(vpu, V2_CMD_SET_FRAME_AXI_OVL_ADDR, 0x0);
	}

	vpu_write(vpu, BIT_BUSY_FLAG, 0x1);

	vpu_bit_issue_command(instance, SET_FRAME_BUF);
	if (vpu_wait(vpu))
		return -EINVAL;

	vpu_write(vpu, BIT_BUSY_FLAG, 0x1);
	vpu_write(vpu, CMD_ENC_SEQ_PARA_CHANGE_ENABLE, 1 << 3);
	vpu_write(vpu, CMD_ENC_SEQ_PARA_RC_FRAME_RATE, 0x0);
	vpu_bit_issue_command(instance, RC_CHANGE_PARAMETER);
	if (vpu_wait(vpu))
		return -EINVAL;

	if (instance->standard == STD_MPEG4) {
		ret = encode_header(instance, VOS_HEADER);
		ret |= encode_header(instance, VIS_HEADER);
		ret |= encode_header(instance, VOL_HEADER);
		if (ret)
			return -ENOMEM;
	}

	if (instance->standard == STD_AVC) {
		ret = encode_header(instance, SPS_RBSP);
		ret |= encode_header(instance, PPS_RBSP);
		if (ret)
			return -ENOMEM;
	}

	ret = kfifo_alloc(&instance->fifo, regs->bitstream_buf_size, GFP_KERNEL);
	if (ret)
		goto out;

	instance->needs_init = 0;
	instance->hold = 0;

	return 0;

out:
	return ret;
}

enum {
    MP4_MPEG4 = 0,
    MP4_DIVX5_HIGHER = 1,
    MP4_XVID = 2,
    MP4_DIVX4 = 5,
};

static int noinline vpu_dec_get_initial_info(struct vpu_instance *instance)
{
	struct vpu *vpu = instance->vpu;
	struct vpu_regs *regs = vpu->regs;
	u32 val, val2;
	u64 f;
	int ret;

	switch (instance->standard) {
	case STD_MPEG4:
	case STD_H263:
		instance->format = VPU_CODEC_MP4_DEC;
		break;
	case STD_AVC:
		instance->format = VPU_CODEC_AVC_DEC;
		break;
	default:
		return -EINVAL;
	};

	vpu_write(vpu, BIT_PARA_BUF_ADDR, instance->para_buf_phys);

	vpu_write(vpu, BIT_WR_PTR(instance->idx), instance->bitstream_buf_phys +
			instance->fifo_in);
	vpu_write(vpu, BIT_RD_PTR(instance->idx), instance->bitstream_buf_phys);

	vpu_write(vpu, CMD_DEC_SEQ_INIT_ESCAPE, 1);

	vpu_write(vpu, CMD_DEC_SEQ_BB_START, instance->bitstream_buf_phys);
	vpu_write(vpu, CMD_DEC_SEQ_START_BYTE, instance->bitstream_buf_phys);
	vpu_write(vpu, CMD_DEC_SEQ_BB_SIZE, regs->bitstream_buf_size / 1024);
	vpu_write(vpu, CMD_DEC_SEQ_OPTION, 0);
	if (instance->format == VPU_CODEC_AVC_DEC) {
		vpu_write(vpu, CMD_DEC_SEQ_PS_BB_START, instance->ps_mem_buf_phys);
		vpu_write(vpu, CMD_DEC_SEQ_PS_BB_SIZE, (PS_SAVE_SIZE / 1024));
	}
	if (instance->format == VPU_CODEC_MP4_DEC) {
		vpu_write(vpu, CMD_DEC_SEQ_MP4_ASP_CLASS, MP4_MPEG4);
	}

	vpu_write(vpu, V2_BIT_RUN_AUX_STD, 0);

	vpu_write(vpu, BIT_BUSY_FLAG, 0x1);
	vpu_bit_issue_command(instance, SEQ_INIT);

	if (vpu_wait(vpu)) {
		ret = -EINVAL;
		vpu_write(vpu, CMD_DEC_SEQ_INIT_ESCAPE, 0);
		goto out;
	}

	vpu_write(vpu, CMD_DEC_SEQ_INIT_ESCAPE, 0);

	if (vpu_read(vpu, RET_DEC_SEQ_SUCCESS) == 0) {
		val = vpu_read(vpu, RET_DEC_SEQ_ERR_REASON);
		dev_dbg(vpu->dev, "%s failed Errorcode: %d\n", __func__, val);
		ret = -EAGAIN;
		goto out;
	}

	val = vpu_read(vpu, RET_DEC_SEQ_SRC_SIZE);
	instance->width = ((val >> regs->bit_pic_width_offset) & regs->bit_pic_width_mask);
	instance->height = (val & regs->bit_pic_width_mask);

	instance->width = ROUND_UP_16(instance->width);
	instance->height = ROUND_UP_16(instance->height);
	dev_dbg(vpu->dev, "%s instance %d now: %dx%d\n", __func__, instance->idx,
			instance->width, instance->height);

	val = vpu_read(vpu, RET_DEC_SEQ_SRC_F_RATE);
	dev_dbg(vpu->dev, "%s: Framerate: 0x%08x\n", __func__, val);
	dev_dbg(vpu->dev, "%s: frame delay: %d\n", __func__,
			vpu_read(vpu, RET_DEC_SEQ_FRAME_DELAY));
	f = val & 0xffff;
	f *= 1000;
	do_div(f, (val >> 16) + 1);
	instance->frame_duration = ktime_set(0, (u32)f);
	instance->num_fb = vpu_read(vpu, RET_DEC_SEQ_FRAME_NEED);
	if (instance->num_fb > VPU_MAX_FB) {
		dev_err(vpu->dev, "num_fb exceeds max fb\n");
		return -EINVAL;
	}

	if (instance->format == VPU_CODEC_AVC_DEC) {
		int top, right;
		val = vpu_read(vpu, RET_DEC_SEQ_CROP_LEFT_RIGHT);
		val2 = vpu_read(vpu, RET_DEC_SEQ_CROP_TOP_BOTTOM);

		dev_dbg(vpu->dev, "crop left:  %d\n", ((val >> 10) & 0x3FF) * 2);
		dev_dbg(vpu->dev, "crop top:   %d\n", ((val2 >> 10) & 0x3FF) * 2);

		if (val == 0 && val2 == 0) {
			right = 0;
			top = 0;
		} else {
			right = instance->width - ((val & 0x3FF) * 2);
			top = instance->height - ((val2 & 0x3FF) * 2);
		}
		dev_dbg(vpu->dev, "crop right: %d\n", right);
		dev_dbg(vpu->dev, "crop top:   %d\n", top);
	}

	/* access normal registers */
	vpu_write(vpu, CMD_DEC_SEQ_INIT_ESCAPE, 0);

	ret = vpu->drvdata->alloc_fb(instance);
	if (ret)
		goto out;

	/* Tell the decoder how many frame buffers we allocated. */
	vpu_write(vpu, CMD_SET_FRAME_BUF_NUM, instance->num_fb);
	vpu_write(vpu, CMD_SET_FRAME_BUF_STRIDE, instance->width);

	vpu_write(vpu, CMD_SET_FRAME_SLICE_BB_START, instance->slice_mem_buf_phys);
	vpu_write(vpu, CMD_SET_FRAME_SLICE_BB_SIZE, SLICE_SAVE_SIZE / 1024);

	vpu_write(vpu, CMD_DEC_PIC_START_BYTE, 0);

	vpu_write(vpu, BIT_BUSY_FLAG, 0x1);
	vpu_bit_issue_command(instance, SET_FRAME_BUF);

	if (vpu_wait(vpu)) {
		ret = -EINVAL;
		goto out;
	}

	instance->needs_init = 0;

out:
	if (ret)
		dev_dbg(vpu->dev, "%s failed with %d\n", __func__, ret);

	return ret;
}

static void noinline vpu_enc_start_frame(struct vpu_instance *instance)
{
	struct vpu *vpu = instance->vpu;
	dma_addr_t dma = vb2_dma_contig_plane_paddr(&vpu->active->vb, 0);
	int height = instance->height;
	int stridey = ROUND_UP_4(instance->width);
	int ustride;
	unsigned long u;

	vpu_write(vpu, CMD_ENC_PIC_ROT_MODE, 0x10);

	vpu_write(vpu, CMD_ENC_PIC_QS, 30);

	vpu_write(vpu, CMD_ENC_PIC_SRC_ADDR_Y, dma);
	u = dma + stridey * ROUND_UP_2(height);
	vpu_write(vpu, CMD_ENC_PIC_SRC_ADDR_CB, u);
	ustride = ROUND_UP_8(instance->width) / 2;
	vpu_write(vpu, CMD_ENC_PIC_SRC_ADDR_CR, u + ustride * ROUND_UP_2(height) / 2);
	vpu_write(vpu, CMD_ENC_PIC_OPTION, (0 << 5) | (0 << 1));

	vpu_write(vpu, V2_BIT_AXI_SRAM_USE, 1 | (1<<7) | (1<<4) | (1<<11));

	vpu_write(vpu, BIT_BUSY_FLAG, 0x1);
	vpu_bit_issue_command(instance, PIC_RUN);
}

static void vpu_dec_start_frame(struct vpu_instance *instance)
{
	struct vpu *vpu = instance->vpu;
	struct vpu_regs *regs = vpu->regs;
	dma_addr_t dma;
	int height, stridey;
	unsigned int readofs;

	readofs = vpu_read(vpu, BIT_RD_PTR(instance->idx)) -
			instance->bitstream_buf_phys;

	vpu_fifo_out(instance, (readofs - instance->readofs) % regs->bitstream_buf_size);
	instance->readofs = readofs;

	vpu_write(vpu, BIT_WR_PTR(instance->idx),
			instance->bitstream_buf_phys + (instance->fifo_in % regs->bitstream_buf_size));

	instance->newdata = 0;

	if (instance->rotmir & 0x1) {
		stridey = instance->height;
		height = instance->width;
	} else {
		stridey = instance->width;
		height = instance->height;
	}

	dma = vb2_dma_contig_plane_paddr(&vpu->active->vb, 0);

	/* Set rotator output */
	vpu_write(vpu, CMD_DEC_PIC_ROT_ADDR_Y, dma);
	vpu_write(vpu, CMD_DEC_PIC_ROT_ADDR_CB, dma + stridey * height);
	vpu_write(vpu, CMD_DEC_PIC_ROT_ADDR_CR,
		dma + stridey * height + (stridey / 2) * (height / 2));
	vpu_write(vpu, CMD_DEC_PIC_ROT_STRIDE, stridey);
	vpu_write(vpu, CMD_DEC_PIC_ROT_MODE, instance->rotmir);

	vpu_write(vpu, CMD_DEC_PIC_OPTION, 1); /* Enable prescan */
	vpu_write(vpu, CMD_DEC_PIC_SKIP_NUM, 0);

	vpu_write(vpu, V2_BIT_AXI_SRAM_USE, 0);

	vpu_write(vpu, BIT_BUSY_FLAG, 0x1);
	vpu_bit_issue_command(instance, PIC_RUN);
}

/*
 * This is the single point of action. Once we start decoding
 * a frame and wait for the corresponding interrupt we are not
 * allowed to touch the VPU. Therefore all accesses to the VPU
 * are serialized here. Caller must hold vpu->lock
 */
static void vpu_work(struct work_struct *work)
{
	struct vpu *vpu = container_of(work, struct vpu, work);
	struct vpu_buffer *vbuf;
	struct vpu_instance *instance = NULL;
	int i, ret;
	struct timespec s;

	while (1) {
		for (i = 0; i < VPU_NUM_INSTANCE; i++) {
			instance = &vpu->instance[i];
			if (instance->in_use && !instance->hold && instance->needs_init) {
				if (instance->mode == VPU_MODE_ENCODER)
					ret = vpu_enc_get_initial_info(instance);
				else
					ret = vpu_dec_get_initial_info(instance);
				if (ret)
					instance->hold = 1;
			}
		}

		vpu->active = NULL;

		list_for_each_entry(vbuf, &vpu->queued, list) {
			struct vb2_queue *q = vbuf->vb.vb2_queue;
			instance = vb2_get_drv_priv(q);
			if (!instance->hold) {
				vpu->active = vbuf;
				break;
			}
		}

		if (!vpu->active)
			return;

		ktime_get_ts(&s);

		instance->start_time = timespec_to_ns(&s);

		if (instance->mode == VPU_MODE_ENCODER)
			vpu_enc_start_frame(instance);
		else
			vpu_dec_start_frame(instance);

		wait_for_completion_interruptible(&vpu->complete);
	}
}

static void vpu_dec_irq_handler(struct vpu *vpu, struct vpu_instance *instance,
		struct vb2_buffer *vb)
{
	struct vpu_regs *regs = vpu->regs;

	struct vpu_buffer *buf = to_vpu_vb(vb);

	if (!vpu_read(vpu, regs->ret_dec_pic_option)) {
		if (instance->flushing) {
			vb2_buffer_done(vb, VB2_BUF_STATE_ERROR);
		} else {
			vb2_buffer_done(vb, VB2_BUF_STATE_QUEUED);

			if (!instance->newdata)
				instance->hold = 1;

			return;
		}
	} else
		vb2_buffer_done(vb, VB2_BUF_STATE_DONE);

	vpu_write(vpu, BIT_FRM_DIS_FLG(instance->idx), 0);
	list_del_init(&buf->list);

	vb->v4l2_buf.timestamp = ktime_to_timeval(instance->frametime);
	instance->frametime = ktime_add(instance->frame_duration,
			instance->frametime);

	vb->v4l2_buf.field = vpu->field;
	vb->v4l2_buf.sequence = vpu->sequence++;

	wake_up_interruptible(&instance->waitq);
}

static void vpu_enc_irq_handler(struct vpu *vpu, struct vpu_instance *instance,
		struct vb2_buffer *vb)
{
	int size;
	int ret;
	struct vpu_buffer *buf = to_vpu_vb(vb);
	s64 time;
	struct timespec e;

	size = vpu_read(vpu, BIT_WR_PTR(instance->idx)) - vpu_read(vpu, BIT_RD_PTR(instance->idx));

	if (kfifo_avail(&instance->fifo) < instance->headersize + size) {
		dev_dbg(vpu->dev, "not enough space in fifo\n");
		instance->hold = 1;
		instance->buffered_size = size;
	} else {
		ret = kfifo_in(&instance->fifo, instance->header, instance->headersize);
		if (ret < instance->headersize)
			BUG();

		ret = kfifo_in(&instance->fifo, instance->bitstream_buf, size);
		if (ret < size)
			BUG();
	}

	ktime_get_ts(&e);
	time = timespec_to_ns(&e) - instance->start_time;
	if (time > instance->encoding_time_max)
		instance->encoding_time_max = time;
	instance->encoding_time_total += time;
	instance->num_frames++;

	list_del_init(&buf->list);
	vb2_buffer_done(vb, VB2_BUF_STATE_DONE);

	vb->v4l2_buf.field = vpu->field;
	vb->v4l2_buf.sequence = vpu->sequence++;

	wake_up_interruptible(&instance->waitq);
}

static irqreturn_t vpu_irq_handler(int irq, void *dev_id)
{
	struct vpu *vpu = dev_id;
	struct vpu_instance *instance;
	struct vpu_buffer *vbuf = vpu->active;
	struct vb2_queue *q;
	unsigned long flags;

	spin_lock_irqsave(&vpu->lock, flags);

	vpu_write(vpu, BIT_INT_CLEAR, 1);
	vpu_write(vpu, BIT_INT_REASON, 0);

	if (!vbuf || !&vbuf->vb)
		goto out;

	q = vbuf->vb.vb2_queue;

	instance = vb2_get_drv_priv(q);

	if (instance->mode == VPU_MODE_DECODER)
		vpu_dec_irq_handler(vpu, instance, &vbuf->vb);
	else
		vpu_enc_irq_handler(vpu, instance, &vbuf->vb);

out:
	complete(&vpu->complete);
	spin_unlock_irqrestore(&vpu->lock, flags);

	return IRQ_HANDLED;
}

static int vpu_open(struct file *file)
{
	struct video_device *dev = video_devdata(file);
	struct vpu *vpu = video_get_drvdata(dev);
	struct vpu_instance *instance;
	struct vpu_regs *regs = vpu->regs;
	int ret = 0, i;

	spin_lock_irq(&vpu->lock);

	for (i = 0; i < VPU_NUM_INSTANCE; i++)
		if (!vpu->instance[i].in_use)
			break;

	if (i == VPU_NUM_INSTANCE) {
		ret = -EBUSY;
		goto out;
	}

	instance = &vpu->instance[i];

	instance->in_use = 1;
	instance->idx = i;
	instance->needs_init = 1;
	instance->headersize = 0;
	instance->header = NULL;
	instance->mode = VPU_MODE_DECODER;
	instance->standard = STD_MPEG4;
	instance->format = VPU_CODEC_AVC_DEC;
	instance->hold = 1;
	instance->flushing = 0;
	instance->readofs = 0;
	instance->fifo_in = 0;
	instance->fifo_out = 0;

	instance->encoding_time_max = 0;
	instance->encoding_time_total = 0;
	instance->num_frames = 0;

	memset(instance->rec, 0, sizeof(instance->rec));

	instance->frametime = ktime_set(0, 0);

	init_waitqueue_head(&instance->waitq);

	file->private_data = instance;

	instance->bitstream_buf = dma_alloc_coherent(NULL, regs->bitstream_buf_size,
			&instance->bitstream_buf_phys, GFP_DMA | GFP_KERNEL);
	if (!instance->bitstream_buf) {
		ret = -ENOMEM;
		goto err_alloc1;
	}

	instance->ps_mem_buf = dma_alloc_coherent(NULL, PS_SAVE_SIZE,
			&instance->ps_mem_buf_phys, GFP_DMA | GFP_KERNEL);
	if (!instance->ps_mem_buf) {
		ret = -ENOMEM;
		goto err_alloc2;
	}

	instance->slice_mem_buf = dma_alloc_coherent(NULL, SLICE_SAVE_SIZE,
			&instance->slice_mem_buf_phys, GFP_DMA | GFP_KERNEL);
	if (!instance->slice_mem_buf) {
		ret = -ENOMEM;
		goto err_alloc3;
	}

	instance->para_buf = dma_alloc_coherent(NULL, regs->para_buf_size,
			&instance->para_buf_phys, GFP_DMA | GFP_KERNEL);
	if (!instance->para_buf) {
		ret = -ENOMEM;
		goto err_alloc4;
	}

	goto out;

err_alloc4:
	dma_free_coherent(NULL, regs->para_buf_size, instance->para_buf,
			instance->para_buf_phys);
err_alloc3:
	dma_free_coherent(NULL, SLICE_SAVE_SIZE, instance->slice_mem_buf,
			instance->slice_mem_buf_phys);
err_alloc2:
	dma_free_coherent(NULL, regs->bitstream_buf_size, instance->bitstream_buf,
			instance->bitstream_buf_phys);
err_alloc1:
	instance->in_use = 0;
out:
	spin_unlock_irq(&vpu->lock);

	return ret;
}

static long vpu_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct vpu_instance *instance = file->private_data;
	int ret = 0;
	u32 std;

	switch (cmd) {
	case VPU_IOC_ROTATE_MIRROR:
		instance->rotmir = (u32)arg | 0x10;
		break;
	case VPU_IOC_CODEC:
		std = (u32)arg;
		switch (std) {
		case STD_MPEG4:
			instance->standard = std;
			break;
		case STD_H263:
			instance->standard = std;
			break;
		case STD_AVC:
			instance->standard = std;
			break;
		case STD_MJPG:
			instance->standard = std;
			instance->mjpg_quality = 50;
			break;
		default:
			ret = -EINVAL;
			break;
		}
		break;
	case VPU_IOC_MJPEG_QUALITY:
		instance->mjpg_quality = (u32)arg;
		break;
	default:
		ret = video_ioctl2(file, cmd, arg);
		break;
	}
	return ret;
}

static int vpu_release(struct file *file)
{
	struct vpu_instance *instance = file->private_data;
	struct vpu *vpu = instance->vpu;
	struct vpu_regs *regs = vpu->regs;
	int i;

	if (instance->videobuf_init) {
		vb2_queue_release(&instance->vidq);
		instance->videobuf_init = 0;
	}

	dma_free_coherent(NULL, regs->para_buf_size, instance->para_buf,
			instance->para_buf_phys);
	dma_free_coherent(NULL, SLICE_SAVE_SIZE, instance->slice_mem_buf,
			instance->slice_mem_buf_phys);
	dma_free_coherent(NULL, regs->bitstream_buf_size, instance->bitstream_buf,
			instance->bitstream_buf_phys);
	dma_free_coherent(NULL, PS_SAVE_SIZE, instance->ps_mem_buf,
			instance->ps_mem_buf_phys);

	for (i = 0; i < VPU_MAX_FB; i++) {
		struct memalloc_record *rec = &instance->rec[i];
		if (rec->cpu_addr)
			dma_free_coherent(NULL, rec->size, rec->cpu_addr,
					rec->dma_addr);
	}

	instance->in_use = 0;
	instance->width = 0;
	kfree(instance->header);
	kfifo_free(&instance->fifo);

	return 0;
}

static int vpu_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct vpu_instance *instance = file->private_data;

	return vb2_mmap(&instance->vidq, vma);
}

static ssize_t vpu_write_stream(struct file *file, const char __user *ubuf, size_t len,
		loff_t *off)
{
	struct vpu_instance *instance = file->private_data;
	struct vpu *vpu = instance->vpu;
	int ret = 0;

	if (instance->mode != VPU_MODE_DECODER)
		return -EINVAL;

	spin_lock_irq(&instance->vpu->lock);

	if (len > 0)
		ret = vpu_fifo_in(instance, ubuf, len);
	else
		instance->flushing = 1;

	instance->hold = 0;
	instance->newdata = 1;

	queue_work(vpu->workqueue, &vpu->work);

	spin_unlock_irq(&instance->vpu->lock);

	return ret;
}

static ssize_t vpu_read_stream(struct file *file, char __user *ubuf, size_t len, loff_t *off)
{
	struct vpu_instance *instance = file->private_data;
	int retlen, ret;

	if (instance->mode != VPU_MODE_ENCODER)
		return -EINVAL;

	ret = kfifo_to_user(&instance->fifo, ubuf, len, &retlen);
	if (ret)
		return ret;

	spin_lock_irq(&instance->vpu->lock);

	if (instance->hold) {
		int size = kfifo_avail(&instance->fifo);
		int ret;

		if (size >= instance->buffered_size + instance->headersize) {
			ret = kfifo_in(&instance->fifo, instance->header, instance->headersize);
			if (ret < instance->headersize)
				BUG();

			ret = kfifo_in(&instance->fifo, instance->bitstream_buf, instance->buffered_size);
			if (ret < instance->buffered_size)
				BUG();
			instance->hold = 0;
			instance->buffered_size = 0;
		}
	}

	spin_unlock_irq(&instance->vpu->lock);

	return retlen;
}

static int vpu_version_info(struct vpu *vpu)
{
	u32 ver;
	u16 version;

	vpu_write(vpu, RET_VER_NUM, 0);

	vpu_write(vpu, BIT_BUSY_FLAG, 0x1);

	vpu_write(vpu, BIT_RUN_INDEX, 0);
	vpu_write(vpu, BIT_RUN_COD_STD, 0);
	vpu_write(vpu, BIT_RUN_COMMAND, FIRMWARE_GET);

	if (vpu_wait(vpu))
		return -ENODEV;

	ver = vpu_read(vpu, RET_VER_NUM);
	if (!ver)
		return -ENODEV;

	version = (u16) ver;

	dev_info(vpu->dev, "VPU firmware version %d.%d.%d\n",
			(version >> 12) & 0x0f,
			(version >> 8) & 0x0f,
			version & 0xff);

	return 0;
}

static int vpu_program_firmware(struct vpu *vpu)
{
	struct vpu_regs *regs = vpu->regs;
	int i, data, ret;
	unsigned short *dp;
	u32 *buf;

	struct fw_header_info info;
	const struct firmware *fw = NULL;

	ret = request_firmware(&fw, vpu->drvdata->fw_name, vpu->dev);
	if (ret) {
		dev_err(vpu->dev, "loading firmware failed with %d\n", ret);
		return -ENOMEM;
	}

	vpu->vpu_code_table = dma_alloc_coherent(NULL, regs->code_buf_size,
			&vpu->vpu_code_table_phys, GFP_DMA | GFP_KERNEL);
	if (!vpu->vpu_code_table)
		return -ENOMEM;

	buf = vpu->vpu_code_table;

	memcpy(&info, fw->data, sizeof(struct fw_header_info));
	dp = (unsigned short *)(fw->data + sizeof(struct fw_header_info));

	if (info.size > MAX_FW_BINARY_LEN) {
		dev_err(vpu->dev, "Size %d in VPU header is too large\n", info.size);
		return -ENOMEM;
	}

	if (vpu->drvdata->version == 2) {
		for (i = 0; i < info.size; i += 4) {
			data = (dp[i + 0] << 16) | dp[i + 1];
			buf[i / 2 + 1] = data;
			data = (dp[i + 2] << 16) | dp[i + 3];
			buf[i / 2] = data;
		}
	} else {
		for (i = 0; i < info.size; i += 2) {
			data = (dp[i] << 16) | dp[i + 1];
			buf[i / 2] = data;
		}
	}

	vpu_write(vpu, BIT_CODE_BUF_ADDR, vpu->vpu_code_table_phys);
	vpu_write(vpu, BIT_CODE_RUN, 0);

	/* Download BIT Microcode to Program Memory */
	for (i = 0; i < 2048 ; ++i) {
		data = dp[i];
		vpu_write(vpu, BIT_CODE_DOWN, (i << 16) | data);
	}

	vpu->vpu_work_buf = dma_alloc_coherent(NULL, regs->work_buf_size,
			&vpu->vpu_work_buf_phys, GFP_DMA | GFP_KERNEL);
	if (!vpu->vpu_work_buf)
		return -ENOMEM;

	vpu_write(vpu, BIT_WORK_BUF_ADDR, vpu->vpu_work_buf_phys);

	data = 1 << regs->bit_buf_pic_flush;
	vpu_write(vpu, BIT_BIT_STREAM_CTRL, data);
	vpu_write(vpu, BIT_FRAME_MEM_CTRL, 0);

	if (vpu->drvdata->version == 2)
		vpu_write(vpu, V2_BIT_AXI_SRAM_USE, 0);

	vpu_reset(vpu);
	vpu_write(vpu, BIT_INT_ENABLE, 0x8);	/* PIC_RUN irq enable */

	vpu_write(vpu, BIT_CODE_RUN, 1);

	vpu_version_info(vpu);

	return 0;
}

static int frame_calc_size(int width, int height)
{
#if 0
	int ystride, ustride, vstride, size;

	ystride = ROUND_UP_4(width);
	ustride = ROUND_UP_8(width) / 2;
	vstride = ROUND_UP_8(ystride) / 2;

	size = ystride * ROUND_UP_2(height);
	size += ustride * ROUND_UP_2(height) / 2;
	size += vstride * ROUND_UP_2(height) / 2;

	return size;
#endif
	return (width * height * 3) / 2;
}
#if LINUX_VERSION_CODE > KERNEL_VERSION(3, 1, 0)
static int vpu_vb2_setup(struct vb2_queue *q, const struct v4l2_format *fmt,
		unsigned int *count, unsigned int *num_planes,
		unsigned int sizes[], void *alloc_ctxs[])
#else
static int vpu_vb2_setup(struct vb2_queue *q,
		unsigned int *count, unsigned int *num_planes,
		unsigned long sizes[], void *alloc_ctxs[])
#endif
{
	struct vpu_instance *instance = vb2_get_drv_priv(q);
	struct vpu *vpu = instance->vpu;

	*num_planes = 1;
	vpu->sequence = 0;
	alloc_ctxs[0] = vpu->alloc_ctx;
	sizes[0] = frame_calc_size(instance->width, instance->height);

	return 0;
}

static int vpu_vb2_prepare(struct vb2_buffer *vb)
{
	struct vb2_queue *q = vb->vb2_queue;
	struct vpu_instance *instance = vb2_get_drv_priv(q);

	size_t new_size = frame_calc_size(instance->width, instance->height);

	if (vb2_plane_size(vb, 0) < new_size) {
		dev_err(instance->vpu->vdev->dev.parent, "Buffer too small (%lu < %zu)\n",
			vb2_plane_size(vb, 0), new_size);
		return -ENOBUFS;
	}

	vb2_set_plane_payload(vb, 0, new_size);
	return 0;
}

static void vpu_vb2_queue(struct vb2_buffer *vb)
{
	struct vb2_queue *q = vb->vb2_queue;
	struct vpu_buffer *buf = to_vpu_vb(vb);
	struct vpu_instance *instance = q->drv_priv;
	struct vpu *vpu = instance->vpu;
	unsigned long flags;

	spin_lock_irqsave(&vpu->lock, flags);
	list_add_tail(&buf->list, &vpu->queued);
	spin_unlock_irqrestore(&vpu->lock, flags);

	queue_work(vpu->workqueue, &vpu->work);
}

static void vpu_vb2_release(struct vb2_buffer *vb)
{
	struct vpu_buffer *buf = to_vpu_vb(vb);
	struct vb2_queue *q = vb->vb2_queue;
	struct vpu_instance *instance = q->drv_priv;
	struct vpu *vpu = instance->vpu;
	struct vpu_buffer *vbuf, *tmp;

	spin_lock_irq(&vpu->lock);

	if (vpu->active == buf) {
		u32 rdptr;

		/*
		 * If this is the active buffer increase make sure
		 * the VPU has enough data to decode the current
		 * frame. I found no sane way to actually cancel
		 * the current frame.
		 */
		rdptr = vpu_read(vpu, BIT_RD_PTR(instance->idx));
		vpu_write(vpu, BIT_WR_PTR(instance->idx), rdptr - 1);

		vpu->active = NULL;
	}

	list_for_each_entry_safe(vbuf, tmp, &vpu->queued, list) {
		if (vbuf == buf) {
			printk("%s: buffer %p still queued. This should not happen\n",
					__func__, vb);
			list_del(&buf->list);
			break;
		}
	}

	spin_unlock_irq(&vpu->lock);
}

static int vpu_vb2_init(struct vb2_buffer *vb)
{

	struct vpu_buffer *buf = to_vpu_vb(vb);
	INIT_LIST_HEAD(&buf->list);

	return 0;
}

static struct vb2_ops vpu_videobuf_ops = {
	.queue_setup	= vpu_vb2_setup,
	.buf_prepare	= vpu_vb2_prepare,
	.buf_queue	= vpu_vb2_queue,
	.buf_cleanup	= vpu_vb2_release,
	.buf_init	= vpu_vb2_init,
};

static int vpu_reqbufs(struct file *file, void *priv,
			struct v4l2_requestbuffers *reqbuf)
{
	struct vpu_instance *instance = file->private_data;
	struct vpu *vpu = instance->vpu;
	struct vb2_queue *q = &instance->vidq;

	int ret = 0;

	vpu->vdev->dev.coherent_dma_mask = DMA_BIT_MASK(32);

	/* Initialize videobuf queue as per the buffer type */
	q->type = reqbuf->type;
	q->io_modes = VB2_MMAP | VB2_USERPTR;
	q->drv_priv = instance;
	q->ops = &vpu_videobuf_ops;
	q->mem_ops = &vb2_dma_contig_memops;

	ret = vb2_queue_init(q);
	instance->videobuf_init = 1;

	/* Allocate buffers */
	ret |= vb2_reqbufs(&instance->vidq, reqbuf);

	return ret;
}

static int vpu_querybuf (struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct vpu_instance *instance = file->private_data;
	return vb2_querybuf(&instance->vidq, p);
}

static int vpu_qbuf (struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct vpu_instance *instance = file->private_data;

	return vb2_qbuf(&instance->vidq, p);
}

static int vpu_dqbuf (struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct vpu_instance *instance = file->private_data;

	return vb2_dqbuf(&instance->vidq, p, file->f_flags & O_NONBLOCK);
}

static int vpu_g_fmt_vid_cap(struct file *file, void *priv,
				struct v4l2_format *fmt)
{
	struct vpu_instance *instance = file->private_data;
	int width, height;

	if (!instance->width)
		return -EAGAIN;

	if (instance->rotmir & 0x1) {
		width = instance->height;
		height = instance->width;
	} else {
		width = instance->width;
		height = instance->height;
	}

	fmt->fmt.pix.width = width;
	fmt->fmt.pix.height = height;
	fmt->fmt.pix.sizeimage = frame_calc_size(width,height);
	fmt->fmt.pix.pixelformat = V4L2_PIX_FMT_YVU420;

	return 0;
}

static int vpu_g_fmt_vid_out(struct file *file, void *priv,
				struct v4l2_format *fmt)
{
	return 0;
}

static int vpu_s_fmt_vid_out(struct file *file, void *priv,
				struct v4l2_format *fmt)
{
	struct vpu_instance *instance = file->private_data;

	fmt->fmt.pix.width &= ~0xf;
	if (fmt->type)
		instance->mode = VPU_MODE_ENCODER;
	else
		instance->mode = VPU_MODE_DECODER;

	instance->width = fmt->fmt.pix.width;
	instance->height = fmt->fmt.pix.height;

	return 0;
}

static int vpu_streamon(struct file *file, void *priv, enum v4l2_buf_type i)
{
	struct vpu_instance *instance = file->private_data;

	if (instance->mode == VPU_MODE_ENCODER)
		instance->hold = 0;

	return vb2_streamon(&instance->vidq, i);
}

static int vpu_streamoff(struct file *file, void *priv, enum v4l2_buf_type i)
{
	struct vpu_instance *instance = file->private_data;

	return vb2_streamoff(&instance->vidq, i);
}

static unsigned int vpu_poll(struct file *file, struct poll_table_struct *wait)
{
	struct vpu_instance *instance = file->private_data;
	int ret = 0;

	if (instance->mode == VPU_MODE_DECODER) {
		poll_wait(file, &instance->waitq, wait);
		if (vpu_fifo_avail(instance) > 0)
			ret |= POLLOUT | POLLWRNORM;

		if (instance->vidq.streaming)
			ret |= vb2_poll(&instance->vidq, file, wait);
	} else {
		if (instance->vidq.streaming)
			ret |= vb2_poll(&instance->vidq, file, wait);
	}

	return ret;
}

static const struct v4l2_ioctl_ops vpu_ioctl_ops = {
	.vidioc_g_fmt_vid_cap 	     = vpu_g_fmt_vid_cap,
	.vidioc_g_fmt_vid_out	     = vpu_g_fmt_vid_out,
	.vidioc_s_fmt_vid_out        = vpu_s_fmt_vid_out,
	.vidioc_reqbufs              = vpu_reqbufs,
	.vidioc_querybuf             = vpu_querybuf,
	.vidioc_qbuf                 = vpu_qbuf,
	.vidioc_dqbuf                = vpu_dqbuf,
	.vidioc_streamon             = vpu_streamon,
	.vidioc_streamoff            = vpu_streamoff,
};

static const struct v4l2_file_operations vpu_fops = {
	.owner		= THIS_MODULE,
	.open		= vpu_open,
	.release	= vpu_release,
	.ioctl		= vpu_ioctl,
	.mmap		= vpu_mmap,
	.write		= vpu_write_stream,
	.read		= vpu_read_stream,
	.poll		= vpu_poll,
};

static struct platform_device_id vpu_devtype[] = {
	{
		.name = "imx27-vpu",
		.driver_data = (kernel_ulong_t)&drvdata_imx27,
	}, {
		.name = "imx51-vpu",
		.driver_data = (kernel_ulong_t)&drvdata_imx51,
	}, {
		.name = "imx53-vpu",
		.driver_data = (kernel_ulong_t)&drvdata_imx53,
	},
};

static int vpu_dev_probe(struct platform_device *pdev)
{
	struct vpu *vpu;
	int err = 0, i;
	struct resource *res;
	struct vpu_driver_data *drvdata;
	struct vpu_regs *regs;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENODEV;

	vpu = devm_kzalloc(&pdev->dev, sizeof(*vpu), GFP_KERNEL);
	if (!vpu)
		return -ENOMEM;

	vpu->vdev = video_device_alloc();
	if (!vpu->vdev)
		return -ENOMEM;

	vpu->workqueue = create_singlethread_workqueue(
			dev_name(&pdev->dev));
	if (!vpu->workqueue) {
		err = -EBUSY;
		goto err_out_work;
	}

	if (vpu_bitrate > VPU_MAX_BITRATE) {
		vpu_bitrate = VPU_MAX_BITRATE;
		dev_warn(&pdev->dev, "specified bitrate too high. Limiting to %d\n",
				VPU_MAX_BITRATE);
	}

	INIT_WORK(&vpu->work, vpu_work);
	init_completion(&vpu->complete);
	strcpy(vpu->vdev->name, "imx-vpu");
	vpu->vdev->fops = &vpu_fops;
	vpu->vdev->ioctl_ops = &vpu_ioctl_ops;
	vpu->vdev->release = video_device_release;
	video_set_drvdata(vpu->vdev, vpu);
	vpu->dev = &pdev->dev;

	for (i = 0; i < VPU_NUM_INSTANCE; i++)
		vpu->instance[i].vpu = vpu;

	drvdata = (void *)pdev->id_entry->driver_data;

	regs = drvdata->regs;
	vpu->regs = regs;
	vpu->drvdata = drvdata;

	vpu->clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(vpu->clk)) {
		err = -ENOENT;
		goto err_out_clk;
	}

	clk_prepare_enable(vpu->clk);
	spin_lock_init(&vpu->lock);
	INIT_LIST_HEAD(&vpu->queued);

	vpu->base = ioremap(res->start, resource_size(res));
	if (!vpu->base) {
		err = -ENOMEM;
		goto err_out_ioremap;
	}

	vpu->alloc_ctx = vb2_dma_contig_init_ctx(&pdev->dev);
	if (IS_ERR(vpu->alloc_ctx)) {
		err = PTR_ERR(vpu->alloc_ctx);
		goto err_alloc_ctx;
	}

	pdev->dev.coherent_dma_mask = DMA_BIT_MASK(32);
	vpu->irq = platform_get_irq(pdev, 0);
	err = request_irq(vpu->irq, vpu_irq_handler, 0,
			dev_name(&pdev->dev), vpu);
	if (err)
		goto err_out_irq;

	err = vpu_program_firmware(vpu);
	if (err)
		goto err_out_irq;

	err = device_create_file(&pdev->dev, &dev_attr_info);
	if (err)
		goto err_out_irq;

	vpu->iram_virt = iram_alloc(V2_IRAM_SIZE, &vpu->iram_phys);
	if (!vpu->iram_virt) {
		dev_err(&pdev->dev, "unable to alloc iram\n");
		goto err_out_irq;
	}

	err = video_register_device(vpu->vdev,
				    VFL_TYPE_GRABBER, -1);
	if (err) {
		dev_err(&pdev->dev, "failed to register: %d\n", err);
		goto err_out_register;
	}

	platform_set_drvdata(pdev, vpu);

	dev_info(&pdev->dev, "registered\n");
	return 0;

err_out_register:
	free_irq(vpu->irq, vpu);
err_out_irq:
	vb2_dma_contig_cleanup_ctx(vpu->alloc_ctx);
err_alloc_ctx:
	iounmap(vpu->base);
err_out_ioremap:
	clk_disable_unprepare(vpu->clk);
	clk_put(vpu->clk);
err_out_clk:
	destroy_workqueue(vpu->workqueue);

	if (vpu->vpu_work_buf)
		dma_free_coherent(NULL, regs->work_buf_size, vpu->vpu_work_buf,
				vpu->vpu_work_buf_phys);
	if (vpu->vpu_code_table)
		dma_free_coherent(NULL, regs->code_buf_size, vpu->vpu_code_table,
				vpu->vpu_code_table_phys);

err_out_work:
	kfree(vpu->vdev);

	return err;
}

static int vpu_dev_remove(struct platform_device *pdev)
{
	struct vpu *vpu = platform_get_drvdata(pdev);
	struct vpu_regs *regs = vpu->regs;

	free_irq(vpu->irq, vpu);

	clk_disable_unprepare(vpu->clk);
	clk_put(vpu->clk);
	iounmap(vpu->base);

	dma_free_coherent(NULL, regs->work_buf_size, vpu->vpu_work_buf,
			vpu->vpu_work_buf_phys);
	dma_free_coherent(NULL, regs->code_buf_size, vpu->vpu_code_table,
			vpu->vpu_code_table_phys);

	vb2_dma_contig_cleanup_ctx(vpu->alloc_ctx);

	if (vpu->iram_virt)
		iram_free(vpu->iram_phys, V2_IRAM_SIZE);

	video_unregister_device(vpu->vdev);

	destroy_workqueue(vpu->workqueue);

	device_remove_file(&pdev->dev, &dev_attr_info);
	platform_set_drvdata(pdev, NULL);

	return 0;
}

static struct platform_driver mxcvpu_driver = {
	.driver = {
		   .name = "imx-vpu",
		   },
	.id_table = vpu_devtype,
	.probe = vpu_dev_probe,
	.remove = vpu_dev_remove,
};

static int __init vpu_init(void)
{
	int ret = platform_driver_register(&mxcvpu_driver);

	return ret;
}

static void __exit vpu_exit(void)
{
	platform_driver_unregister(&mxcvpu_driver);
}

MODULE_AUTHOR("Sascha Hauer <s.hauer@pengutronix.de>");
MODULE_DESCRIPTION("Linux VPU driver for Freescale i.MX27");
MODULE_LICENSE("GPL");

module_init(vpu_init);
module_exit(vpu_exit);
