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

#include <media/videobuf-dma-contig.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <mach/hardware.h>

#include "imx-vpu.h"

#define VPU_IOC_MAGIC  'V'

#define	VPU_IOC_SET_ENCODER	_IO(VPU_IOC_MAGIC, 5)
#define VPU_IOC_SET_DECODER	_IO(VPU_IOC_MAGIC, 6)
#define	VPU_IOC_ROTATE_MIRROR	_IO(VPU_IOC_MAGIC, 7)
#define VPU_IOC_CODEC		_IO(VPU_IOC_MAGIC, 8)

#define VPU_NUM_INSTANCE	4

#define BIT_WR_PTR(x)		(0x124 + 8 * (x))
#define BIT_RD_PTR(x)		(0x120 + 8 * (x))
#define BIT_FRM_DIS_FLG(x)	(0x150 + 4 * (x))
#define BIT_MSG(x)		(0x1f0 + 4 * (x))

#define VPU_MODE_DECODER	0
#define VPU_MODE_ENCODER	1

#define RET_DEC_SEQ_ERR_REASON          0x1E0

#define BITSTREAM_BUF_SIZE	(512 * 1024)
#define PS_SAVE_SIZE            0x028000
#define SLICE_SAVE_SIZE         0x02D800

#define IMAGE_ENDIAN                    0
#define MAX_FW_BINARY_LEN		102400

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

static int vpu_v1_codecs[VPU_CODEC_MAX] = {
	[VPU_CODEC_AVC_DEC] = 2,
	[VPU_CODEC_VC1_DEC] = -1,
	[VPU_CODEC_MP2_DEC] = -1,
	[VPU_CODEC_MP4_DEC] = 0,
	[VPU_CODEC_DV3_DEC] = -1,
	[VPU_CODEC_RV_DEC] = -1,
	[VPU_CODEC_MJPG_DEC] = -1,
	[VPU_CODEC_AVC_ENC] = 3,
	[VPU_CODEC_MP4_ENC] = 1,
	[VPU_CODEC_MJPG_ENC] = -1,
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
	/* common v4l buffer stuff -- must be first */
	struct videobuf_buffer		vb;
	struct vpu_instance		*instance;
};

struct vpu;

struct memalloc_record {
	u32 size;
	dma_addr_t dma_addr;
	void __iomem *cpu_addr;
};

#define VPU_MAX_FB	3

struct vpu_instance {
	struct vpu *vpu;
	int idx;
	int width, height;
	int num_fb;
	int format;
	struct videobuf_queue vidq;
	int in_use;

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
};

struct vpu {
	void *dummy;
	struct device		*dev;
	struct fasync_struct	*async_queue;
	struct video_device	*vdev;
	void __iomem		*base;
	struct vpu_instance	instance[VPU_NUM_INSTANCE];
	spinlock_t		lock;
	struct videobuf_buffer	*active;
	struct list_head	queued;
	struct clk		*clk;
	int			irq;
	struct vpu_regs		*regs;
	struct vpu_driver_data	*drvdata;

	dma_addr_t	vpu_code_table_phys;
	void __iomem	*vpu_code_table;
	dma_addr_t	vpu_work_buf_phys;
	void __iomem	*vpu_work_buf;
};

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

	len = min(vpu_fifo_avail(instance), len);

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
	struct vpu_buffer *vbuf;
	struct videobuf_buffer *vb;
	int i, len = 0;

	spin_lock_irq(&vpu->lock);

	vb = vpu->active;
	len += sprintf(buf + len, "VPU Info\n"
		"--------\n"
		"active: %p", vpu->active);
	if (vb) {
		vbuf = container_of(vb, struct vpu_buffer, vb);
		len += sprintf(buf + len, " (instance %d)\n", vbuf->instance->idx);
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
		if (instance->mode == VPU_MODE_ENCODER)
			len += sprintf(buf + len, "kfifo_avail: %d\n"
					          "buffered size: %d\n"
						  "header size:   %d\n",
						  kfifo_avail(&instance->fifo),
						  instance->buffered_size,
						  instance->headersize);
		else
			len += sprintf(buf + len, "fifo_avail: %d\n", vpu_fifo_avail(instance));
	}

	len += sprintf(buf + len, "queued buffers\n"
			          "--------------\n");

	list_for_each_entry(vb, &vpu->queued, queue) {
		vbuf = container_of(vb, struct vpu_buffer, vb);
		len += sprintf(buf + len, "%p (instance %d, state %d)\n", vb, vbuf->instance->idx, vb->state);
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
	unsigned long *para_buf = instance->para_buf;

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

	for (i = 0; i < instance->num_fb; i++) {
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
		para_buf[i * 3 + 3] = para_buf[i * 3] + (instance->width / 2) * (instance->height / 2); /* Cr */
		if (instance->standard == STD_AVC)
			para_buf[96 + i + 1] = para_buf[i * 3 + 3] + (instance->width / 2) * (instance->height / 2);

		if (i + 1 < instance->num_fb) {
			para_buf[i * 3 + 2] = instance->rec[i + 1].dma_addr; /* Y */
			para_buf[i * 3 + 5] = instance->rec[i + 1].dma_addr + instance->width * instance->height ; /* Cb */
			para_buf[i * 3 + 4] = para_buf[i * 3 + 5] + (instance->width / 2) * (instance->height / 2); /* Cr */
		}
		if (instance->standard == STD_AVC)
			para_buf[96 + i] = para_buf[i * 3 + 4] + (instance->width / 2) * (instance->height / 2);
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

	print_hex_dump(KERN_INFO, "header: ", DUMP_PREFIX_ADDRESS, 16, 1, instance->bitstream_buf, headersize, 0);

	instance->header = header;
	instance->headersize += headersize;

	return 0;
}

static int noinline vpu_enc_get_initial_info(struct vpu_instance *instance)
{
	struct vpu *vpu = instance->vpu;
	struct vpu_regs *regs = vpu->regs;
	int ret;
	u32 data;
	u32 val;
	u32 sliceSizeMode = 1;
	u32 sliceMode = 1;
	u32 bitrate = 0; /* auto bitrate */
	u32 enableAutoSkip = 0;
	u32 initialDelay = 1;
	u32 sliceReport = 0;
	u32 mbReport = 0;
	u32 rcIntraQp = 0;

	switch (instance->standard) {
	case STD_MPEG4:
	case STD_H263:
		instance->format = VPU_CODEC_MP4_ENC;
		break;
	case STD_AVC:
		instance->format = VPU_CODEC_AVC_ENC;
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
	vpu_write(vpu, CMD_ENC_SEQ_SRC_F_RATE, 0x03e87530); /* 0x03e87530 */

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
	} else if (instance->standard == STD_AVC) {
		u32 avc_deblkFilterOffsetBeta = 0;
		u32 avc_deblkFilterOffsetAlpha = 0;
		u32 avc_disableDeblk = 1;
		u32 avc_constrainedIntraPredFlag = 0;
		u32 avc_chromaQpOffset = 0;

		vpu_write(vpu, CMD_ENC_SEQ_COD_STD, STD_AVC);

		data = (avc_deblkFilterOffsetBeta & 15) << 12 |
			(avc_deblkFilterOffsetAlpha & 15) << 8 |
			avc_disableDeblk << 6 |
			avc_constrainedIntraPredFlag << 5 |
			(avc_chromaQpOffset & 31);
		vpu_write(vpu, CMD_ENC_SEQ_264_PARA, data);
	}

	data = 8000 << 2 | /* slice size */
		sliceSizeMode << 1 | sliceMode;

	vpu_write(vpu, CMD_ENC_SEQ_SLICE_MODE, data);
	vpu_write(vpu, CMD_ENC_SEQ_GOP_NUM, 1); /* gop size */

	if (bitrate) {	/* rate control enabled */
		data = (!enableAutoSkip) << 31 |
			initialDelay << 16 |
			bitrate << 1 |
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

	vpu_write(vpu, CMD_ENC_SEQ_OPTION, 0);
	vpu_write(vpu, CMD_ENC_SEQ_FMO, 0);

	vpu_write(vpu, BIT_BUSY_FLAG, 0x1);

	vpu_bit_issue_command(instance, SEQ_INIT);
	if (vpu_wait(vpu))
		return -EINVAL;

	if (vpu_read(vpu, RET_ENC_SEQ_SUCCESS) == 0) {
		val = vpu_read(vpu, RET_DEC_SEQ_ERR_REASON);
		dev_dbg(vpu->dev, "%s failed Errorcode: %d\n", __func__, val);
		return -EINVAL;
	}

	instance->num_fb = 3; /* FIXME */
	ret = vpu->drvdata->alloc_fb(instance);
	if (ret) {
		dev_dbg(vpu->dev, "alloc fb failed\n");
		goto out;
	}

	/* Tell the codec how much frame buffers we allocated. */
	vpu_write(vpu, CMD_SET_FRAME_BUF_NUM, instance->num_fb);
	vpu_write(vpu, CMD_SET_FRAME_BUF_STRIDE, ROUND_UP_8(instance->width));

	if (vpu->drvdata->version == 2) {
		vpu_write(vpu, V2_CMD_SET_FRAME_AXI_BIT_ADDR, 0x0);
		vpu_write(vpu, V2_CMD_SET_FRAME_AXI_IPACDC_ADDR, 0x0);
		vpu_write(vpu, V2_CMD_SET_FRAME_AXI_DBKY_ADDR, 0x0);
		vpu_write(vpu, V2_CMD_SET_FRAME_AXI_DBKC_ADDR, 0x0);
		vpu_write(vpu, V2_CMD_SET_FRAME_AXI_OVL_ADDR, 0x0);
	}

	vpu_write(vpu, BIT_BUSY_FLAG, 0x1);

	vpu_bit_issue_command(instance, SET_FRAME_BUF);
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

	ret = kfifo_alloc(&instance->fifo, 128 * 1024, GFP_KERNEL);
	if (ret)
		goto out;

	instance->needs_init = 0;
	instance->hold = 0;

	return 0;

out:
	return ret;
}

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
	vpu_write(vpu, CMD_DEC_SEQ_PS_BB_START, instance->ps_mem_buf_phys);
	vpu_write(vpu, CMD_DEC_SEQ_PS_BB_SIZE, (PS_SAVE_SIZE / 1024));

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

static void noinline vpu_enc_start_frame(struct vpu_instance *instance, struct videobuf_buffer *vb)
{
	struct vpu *vpu = instance->vpu;
	dma_addr_t dma = videobuf_to_dma_contig(vb);
	int height = instance->height;
	int stridey = ROUND_UP_4(instance->width);
	int ustride;
	unsigned long u;

	vpu->active = vb;

	vpu_write(vpu, CMD_ENC_PIC_ROT_MODE, 0x10);

	vpu_write(vpu, CMD_ENC_PIC_QS, 30);

	vpu_write(vpu, CMD_ENC_PIC_SRC_ADDR_Y, dma);
	u = dma + stridey * ROUND_UP_2(height);
	vpu_write(vpu, CMD_ENC_PIC_SRC_ADDR_CB, u);
	ustride = ROUND_UP_8(instance->width) / 2;
	vpu_write(vpu, CMD_ENC_PIC_SRC_ADDR_CR, u + ustride * ROUND_UP_2(height) / 2);
	vpu_write(vpu, CMD_ENC_PIC_OPTION, (0 << 5) | (0 << 1));

	vpu_write(vpu, BIT_BUSY_FLAG, 0x1);
	vpu_bit_issue_command(instance, PIC_RUN);
}

static void vpu_dec_start_frame(struct vpu_instance *instance, struct videobuf_buffer *vb)
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

	if (instance->rotmir & 0x1) {
		stridey = instance->height;
		height = instance->width;
	} else {
		stridey = instance->width;
		height = instance->height;
	}

	dma = videobuf_to_dma_contig(vb);

	/* Set rotator output */
	vpu_write(vpu, CMD_DEC_PIC_ROT_ADDR_Y, dma);
	vpu_write(vpu, CMD_DEC_PIC_ROT_ADDR_CB, dma + stridey * height);
	vpu_write(vpu, CMD_DEC_PIC_ROT_ADDR_CR,
		dma + stridey * height + (stridey / 2) * (height / 2));
	vpu_write(vpu, CMD_DEC_PIC_ROT_STRIDE, stridey);
	vpu_write(vpu, CMD_DEC_PIC_ROT_MODE, instance->rotmir);

	vpu_write(vpu, CMD_DEC_PIC_OPTION, 1); /* Enable prescan */
	vpu_write(vpu, CMD_DEC_PIC_SKIP_NUM, 0);

	vpu_write(vpu, BIT_BUSY_FLAG, 0x1);
	vpu_bit_issue_command(instance, PIC_RUN);
}

/*
 * This is the single point of action. Once we start decoding
 * a frame and wait for the corresponding interrupt we are not
 * allowed to touch the VPU. Therefore all accesses to the VPU
 * are serialized here. Caller must hold vpu->lock
 */
static int vpu_start_frame(struct vpu *vpu)
{
	struct videobuf_buffer *vb;
	struct vpu_buffer *vbuf;
	struct vpu_instance *instance;
	int i, ret;

	vpu->active = NULL;

	for (i = 0; i < VPU_NUM_INSTANCE; i++) {
		instance = &vpu->instance[i];
		if (instance->in_use &&
		    instance->needs_init &&
		    instance->mode == VPU_MODE_DECODER &&
		    !instance->hold) {
			ret = vpu_dec_get_initial_info(instance);
			if (ret)
				instance->hold = 1;
		}
	}

	list_for_each_entry(vb, &vpu->queued, queue) {
		vbuf = container_of(vb, struct vpu_buffer, vb);
		instance = vbuf->instance;
		if (!instance->hold) {
			vpu->active = vb;
			break;
		}
	}

	if (!vpu->active)
		return 0;

	if (instance->mode == VPU_MODE_ENCODER && instance->needs_init) {
		ret = vpu_enc_get_initial_info(instance);
		if (ret)
			instance->hold = 1;
	}

	vb = vpu->active;

	vb->state = VIDEOBUF_ACTIVE;

	if (instance->mode == VPU_MODE_ENCODER)
		vpu_enc_start_frame(instance, vb);
	else
		vpu_dec_start_frame(instance, vb);

	return 0;
}

static void vpu_dec_irq_handler(struct vpu *vpu, struct vpu_instance *instance,
		struct videobuf_buffer *vb)
{
	struct vpu_regs *regs = vpu->regs;

	if (!vpu_read(vpu, regs->ret_dec_pic_option)) {
		if (instance->flushing) {
			vb->state = VIDEOBUF_ERROR;
		} else {
			vb->state = VIDEOBUF_QUEUED;
			instance->hold = 1;
			return;
		}
	} else
		vb->state = VIDEOBUF_DONE;

	vpu_write(vpu, BIT_FRM_DIS_FLG(instance->idx), 0);

	list_del_init(&vb->queue);

	vb->ts = ktime_to_timeval(instance->frametime);
	instance->frametime = ktime_add(instance->frame_duration,
			instance->frametime);

	vb->field_count++;
	wake_up(&vb->done);
	wake_up_interruptible(&instance->waitq);
}

static void vpu_enc_irq_handler(struct vpu *vpu, struct vpu_instance *instance,
		struct videobuf_buffer *vb)
{
	int size;
	int ret;

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

	list_del_init(&vb->queue);
	vb->state = VIDEOBUF_DONE;

	vb->field_count++;
	wake_up(&vb->done);
	wake_up_interruptible(&instance->waitq);
}

static irqreturn_t vpu_irq_handler(int irq, void *dev_id)
{
	struct vpu *vpu = dev_id;
	struct vpu_instance *instance;
	struct videobuf_buffer *vb = vpu->active;
	struct vpu_buffer *vbuf;
	unsigned long flags;

	spin_lock_irqsave(&vpu->lock, flags);

	if (!vb)
		goto out;

	vbuf = container_of(vpu->active, struct vpu_buffer, vb);
	instance = vbuf->instance;

	vpu_write(vpu, BIT_INT_CLEAR, 1);
	vpu_write(vpu, BIT_INT_REASON, 0);

	if (instance->mode == VPU_MODE_DECODER)
		vpu_dec_irq_handler(vpu, instance, vb);
	else
		vpu_enc_irq_handler(vpu, instance, vb);

	vpu_start_frame(vpu);

out:
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
	case VPU_IOC_SET_ENCODER:
		instance->mode = VPU_MODE_ENCODER;
		break;
	case VPU_IOC_SET_DECODER:
		instance->mode = VPU_MODE_DECODER;
		break;
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
		default:
			ret = -EINVAL;
			break;
		}
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

	return videobuf_mmap_mapper(&instance->vidq, vma);
}

static ssize_t vpu_write_stream(struct file *file, const char __user *ubuf, size_t len,
		loff_t *off)
{
	struct vpu_instance *instance = file->private_data;
	int ret = 0;

	if (instance->mode != VPU_MODE_DECODER)
		return -EINVAL;

	spin_lock_irq(&instance->vpu->lock);

	if (len > 0)
		ret = vpu_fifo_in(instance, ubuf, len);
	else
		instance->flushing = 1;

	instance->hold = 0;

	if (!instance->vpu->active)
		vpu_start_frame(instance->vpu);

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
	vpu_write(vpu, BIT_FRAME_MEM_CTRL, IMAGE_ENDIAN);

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

static int vpu_videobuf_setup(struct videobuf_queue *q,
		unsigned int *count, unsigned int *size)
{
	struct vpu_instance *instance = q->priv_data;

	*size = frame_calc_size(instance->width, instance->height);

	return 0;
}

static int vpu_videobuf_prepare(struct videobuf_queue *q,
		struct videobuf_buffer *vb,
		enum v4l2_field field)
{
	struct vpu_instance *instance = q->priv_data;
	struct vpu_buffer *vbuf = container_of(vb, struct vpu_buffer, vb);
	int ret = 0;

	vbuf->instance = instance;
	vb->width = instance->width;
	vb->height = instance->height;
	vb->size = frame_calc_size(vb->width, vb->height);

	if (vb->state == VIDEOBUF_NEEDS_INIT) {
		ret = videobuf_iolock(&instance->vidq, vb, NULL);
		if (ret)
			goto fail;

		vb->state = VIDEOBUF_PREPARED;
	}

fail:
	return ret;
}

static void vpu_videobuf_queue(struct videobuf_queue *q,
		struct videobuf_buffer *vb)
{
	struct vpu_instance *instance = q->priv_data;
	struct vpu *vpu = instance->vpu;

	list_add_tail(&vb->queue, &vpu->queued);

	vb->state = VIDEOBUF_QUEUED;

	if (!vpu->active)
		vpu_start_frame(vpu);
}

static void vpu_videobuf_release(struct videobuf_queue *q,
		struct videobuf_buffer *vb)
{
	struct vpu_instance *instance = q->priv_data;
	struct vpu *vpu = instance->vpu;
	struct videobuf_buffer *vbtmp;

	spin_lock_irq(&vpu->lock);

	if (vb->state == VIDEOBUF_ACTIVE) {
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

	list_for_each_entry(vbtmp, &vpu->queued, queue) {
		if (vbtmp == vb) {
			dev_dbg(vpu->dev, "%s: buffer %p still queued. This should not happen\n",
					__func__, vb);
			list_del(&vb->queue);
			break;
		}
	}

	spin_unlock_irq(&vpu->lock);

	videobuf_dma_contig_free(q, vb);

	vb->state = VIDEOBUF_NEEDS_INIT;
}

static struct videobuf_queue_ops vpu_videobuf_ops = {
	.buf_setup	= vpu_videobuf_setup,
	.buf_prepare	= vpu_videobuf_prepare,
	.buf_queue	= vpu_videobuf_queue,
	.buf_release	= vpu_videobuf_release,
};

static int vpu_reqbufs(struct file *file, void *priv,
			struct v4l2_requestbuffers *reqbuf)
{
	struct vpu_instance *instance = file->private_data;
	struct vpu *vpu = instance->vpu;

	int ret = 0;

	vpu->vdev->dev.coherent_dma_mask = DMA_BIT_MASK(32);

	/* Initialize videobuf queue as per the buffer type */
	videobuf_queue_dma_contig_init(&instance->vidq,
					    &vpu_videobuf_ops, &vpu->vdev->dev,
					    &vpu->lock,
					    reqbuf->type,
					    V4L2_FIELD_NONE,
					    sizeof(struct vpu_buffer),
					    instance,
					    NULL);

	/* Allocate buffers */
	ret = videobuf_reqbufs(&instance->vidq, reqbuf);

	return ret;
}

static int vpu_querybuf (struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct vpu_instance *instance = file->private_data;

	return videobuf_querybuf(&instance->vidq, p);
}

static int vpu_qbuf (struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct vpu_instance *instance = file->private_data;

	return videobuf_qbuf(&instance->vidq, p);
}

static int vpu_dqbuf (struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct vpu_instance *instance = file->private_data;

	return videobuf_dqbuf(&instance->vidq, p, file->f_flags & O_NONBLOCK);
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

	instance->width = fmt->fmt.pix.width;
	instance->height = fmt->fmt.pix.height;

	return 0;
}

static int vpu_streamon(struct file *file, void *priv, enum v4l2_buf_type i)
{
	struct vpu_instance *instance = file->private_data;

	if (instance->mode == VPU_MODE_ENCODER)
		instance->hold = 0;

	return videobuf_streamon(&instance->vidq);
}

static int vpu_streamoff(struct file *file, void *priv, enum v4l2_buf_type i)
{
	struct vpu_instance *instance = file->private_data;

	return videobuf_streamoff(&instance->vidq);
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
			ret |= videobuf_poll_stream(file, &instance->vidq, wait);
	} else {
		if (instance->vidq.streaming)
			ret |= videobuf_poll_stream(file, &instance->vidq, wait);
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

	strcpy(vpu->vdev->name, "vpu");
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

	clk_enable(vpu->clk);
	spin_lock_init(&vpu->lock);
	INIT_LIST_HEAD(&vpu->queued);

	vpu->base = ioremap(res->start, resource_size(res));
	if (!vpu->base) {
		err = -ENOMEM;
		goto err_out_ioremap;
	}

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
	iounmap(vpu->base);
err_out_ioremap:
	clk_disable(vpu->clk);
	clk_put(vpu->clk);
err_out_clk:
	kfree(vpu->vdev);

	if (vpu->vpu_work_buf)
		dma_free_coherent(NULL, regs->work_buf_size, vpu->vpu_work_buf,
				vpu->vpu_work_buf_phys);
	if (vpu->vpu_code_table)
		dma_free_coherent(NULL, regs->code_buf_size, vpu->vpu_code_table,
				vpu->vpu_code_table_phys);

	return err;
}

static int vpu_dev_remove(struct platform_device *pdev)
{
	struct vpu *vpu = platform_get_drvdata(pdev);
	struct vpu_regs *regs = vpu->regs;

	free_irq(vpu->irq, vpu);

	clk_disable(vpu->clk);
	clk_put(vpu->clk);
	iounmap(vpu->base);

	dma_free_coherent(NULL, regs->work_buf_size, vpu->vpu_work_buf,
			vpu->vpu_work_buf_phys);
	dma_free_coherent(NULL, regs->code_buf_size, vpu->vpu_code_table,
			vpu->vpu_code_table_phys);

	video_unregister_device(vpu->vdev);

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
