/*
 * Copyright 2006-2007 Freescale Semiconductor, Inc. All Rights Reserved.
 */

/*
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
#include <linux/kfifo.h>
#include <linux/list.h>
#include <linux/stat.h>
#include <linux/wait.h>
#include <linux/clk.h>
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/mm.h>

#include <media/videobuf-dma-contig.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <mach/hardware.h>

#include "vpu_codetable_mx27.h"

#include <linux/slab.h>
#include "imx-vpu.h"

#define VPU_IOC_MAGIC  'V'

#define	BIT_INT_STATUS		0x010

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

#define BITSTREAM_BUF_SIZE	(512 * 1024)
#define PS_SAVE_SIZE            0x028000
#define SLICE_SAVE_SIZE         0x02D800

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
	MP4_DEC = 0,
	MP4_ENC = 1,
	AVC_DEC = 2,
	AVC_ENC = 3
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

#define STD_MPEG4	0
#define STD_H263	1
#define STD_AVC		2

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

	dma_addr_t	vpu_code_table_phys;
	void __iomem	*vpu_code_table;
	dma_addr_t	vpu_work_buf_phys;
	void __iomem	*vpu_work_buf;
};

#define ROUND_UP_2(num)	(((num) + 1) & ~1)
#define ROUND_UP_4(num)	(((num) + 3) & ~3)
#define ROUND_UP_8(num)	(((num) + 7) & ~7)
#define ROUND_UP_16(num)	(((num) + 15) & ~15)

static void *dummybuf;
static dma_addr_t dummydma;

static unsigned int vpu_fifo_len(struct vpu_instance *instance)
{
	return instance->fifo_in - instance->fifo_out;
}

static unsigned int vpu_fifo_avail(struct vpu_instance *instance)
{
	return BITSTREAM_BUF_SIZE - vpu_fifo_len(instance);
}

static int vpu_fifo_out(struct vpu_instance *instance, int len)
{
	instance->fifo_out += len;
	return 0;
}

static int vpu_fifo_in(struct vpu_instance *instance, const char __user *ubuf, size_t len)
{
	unsigned int off, l;
	int ret;

	len = min(vpu_fifo_avail(instance), len);

	off = instance->fifo_in & (BITSTREAM_BUF_SIZE - 1);

	l = min(len, BITSTREAM_BUF_SIZE - off);

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

static struct vpu _vpu_data;
static struct vpu *vpu_data = &_vpu_data;

static void VpuWriteReg(unsigned long addr, unsigned int data)
{
	__raw_writel(data, vpu_data->base + addr);
}

static unsigned long VpuReadReg(unsigned long addr)
{
	unsigned long data = __raw_readl(vpu_data->base + addr);
	return data;
}

static void vpu_reset(void)
{
	unsigned long val;

	VpuWriteReg(BIT_CODE_RUN, 0);
	val = VpuReadReg(BIT_CODE_RESET);
	VpuWriteReg(BIT_CODE_RESET, val | 1);
	udelay(100);
	VpuWriteReg(BIT_CODE_RESET, val & ~1);
	VpuWriteReg(BIT_CODE_RUN, 1);
}

static int vpu_is_busy(void)
{
	return VpuReadReg(BIT_BUSY_FLAG) != 0;
}

static int vpu_wait(void)
{
	int to = 100000;
	while (1) {
		if (!vpu_is_busy())
			return 0;
		if (!to--) {
			printk("%s timed out\n", __func__);
			VpuWriteReg(BIT_BUSY_FLAG, 0x0);
			dump_stack();
			return -ETIMEDOUT;
		}
		udelay(1);
	}
}

static void BitIssueCommand(int instIdx, int cdcMode, int cmd)
{
	VpuWriteReg(BIT_RUN_INDEX, instIdx);
	VpuWriteReg(BIT_RUN_COD_STD, cdcMode);
	VpuWriteReg(BIT_RUN_COMMAND, cmd);
}

static int alloc_fb(struct vpu_instance *instance)
{
	int i, ret = 0;
	int size = (instance->width * instance->height * 3) / 2;
	unsigned long *para_buf = instance->para_buf;

	for (i = 0; i < instance->num_fb; i++ ) {
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
		printk("%s failed with %d\n", __func__, ret);
	return ret;
}

static int encode_header(struct vpu_instance *instance, int headertype)
{
	void *header;
	int headersize;

	VpuWriteReg(CMD_ENC_HEADER_CODE, headertype);

	BitIssueCommand(instance->idx, instance->format, ENCODE_HEADER);

	if (vpu_wait())
		return -EINVAL;

	headersize = VpuReadReg(BIT_WR_PTR(instance->idx)) -
			VpuReadReg(BIT_RD_PTR(instance->idx));

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
	int ret;
	u32 data;
	u32 sliceSizeMode = 0;
	u32 sliceMode = 1;
	u32 bitrate = 32767; /* auto bitrate */
	u32 enableAutoSkip = 0;
	u32 initialDelay = 0;
	u32 sliceReport = 0;
	u32 mbReport = 0;
	u32 rcIntraQp = 0;
	u32 mp4_verid = 1;

	switch (instance->standard) {
	case STD_MPEG4:
	case STD_H263:
		instance->format = MP4_ENC;
		break;
	case STD_AVC:
		instance->format = AVC_ENC;
		break;
	default:
		return -EINVAL;
	};

	VpuWriteReg(BIT_BIT_STREAM_CTRL, 0xc);
	VpuWriteReg(BIT_PARA_BUF_ADDR, instance->para_buf_phys);

	VpuWriteReg(BIT_WR_PTR(instance->idx), instance->bitstream_buf_phys);
	VpuWriteReg(BIT_RD_PTR(instance->idx), instance->bitstream_buf_phys);

	data = (instance->width << 10) | instance->height;
	VpuWriteReg(CMD_ENC_SEQ_SRC_SIZE, data);
	VpuWriteReg(CMD_ENC_SEQ_SRC_F_RATE, 0x03e87530);

	if (instance->standard == STD_MPEG4) {
		u32 mp4_intraDcVlcThr = 0;
		u32 mp4_reversibleVlcEnable = 0;
		u32 mp4_dataPartitionEnable = 0;
		u32 mp4_hecEnable = 0;

		VpuWriteReg(CMD_ENC_SEQ_COD_STD, STD_MPEG4);

		data = mp4_intraDcVlcThr << 2 |
			mp4_reversibleVlcEnable << 1 |
			mp4_dataPartitionEnable |
			mp4_hecEnable << 5 |
			mp4_verid << 6;

		VpuWriteReg(CMD_ENC_SEQ_MP4_PARA, data);
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
			printk("VPU: unsupported size\n");
			return -EINVAL;
		}

		VpuWriteReg(CMD_ENC_SEQ_COD_STD, STD_H263);

		data = h263_annexJEnable << 2 |
			h263_annexKEnable << 1 |
			h263_annexTEnable;

		VpuWriteReg(CMD_ENC_SEQ_263_PARA, data);
	} else if (instance->standard == STD_AVC) {
		u32 avc_deblkFilterOffsetBeta = 0;
		u32 avc_deblkFilterOffsetAlpha = 0;
		u32 avc_disableDeblk = 0;
		u32 avc_constrainedIntraPredFlag = 0;
		u32 avc_chromaQpOffset = 0;

		VpuWriteReg(CMD_ENC_SEQ_COD_STD, STD_AVC);

		data = (avc_deblkFilterOffsetBeta & 15) << 12 |
			(avc_deblkFilterOffsetAlpha & 15) << 8 |
			avc_disableDeblk << 6 |
			avc_constrainedIntraPredFlag << 5 |
			(avc_chromaQpOffset & 31);
		VpuWriteReg(CMD_ENC_SEQ_264_PARA, data);
	}

	data = 4000 << 2 | /* slice size */
		sliceSizeMode << 1 | sliceMode;

	VpuWriteReg(CMD_ENC_SEQ_SLICE_MODE, data);
	VpuWriteReg(CMD_ENC_SEQ_GOP_NUM, 0); /* gop size */

	if (bitrate) {	/* rate control enabled */
		data = (!enableAutoSkip) << 31 | initialDelay << 16 | bitrate << 1 | 1;
		VpuWriteReg(CMD_ENC_SEQ_RC_PARA, data);
	} else {
		VpuWriteReg(CMD_ENC_SEQ_RC_PARA, 0);
	}

	VpuWriteReg(CMD_ENC_SEQ_RC_BUF_SIZE, 0 ); /* vbv buffer size */
	VpuWriteReg(CMD_ENC_SEQ_INTRA_REFRESH, 0);

	VpuWriteReg(CMD_ENC_SEQ_BB_START, instance->bitstream_buf_phys);
	VpuWriteReg(CMD_ENC_SEQ_BB_SIZE, BITSTREAM_BUF_SIZE / 1024);

	data = (sliceReport << 1) | mbReport;

	if (rcIntraQp >= 0)
		data |= (1 << 5);

	VpuWriteReg(CMD_ENC_SEQ_INTRA_QP, rcIntraQp);

//	if (instance->format == AVC_ENC) {
//		data |= (encOP.EncStdParam.avcParam.avc_audEnable << 2);
//		data |= (encOP.EncStdParam.avcParam.avc_fmoEnable << 4);
//	}

	VpuWriteReg(CMD_ENC_SEQ_OPTION, data);

//	if (pCodecInst->codecMode == AVC_ENC) {
//		data = (encOP.EncStdParam.avcParam.avc_fmoType << 4) |
//		    (encOP.EncStdParam.avcParam.avc_fmoSliceNum & 0x0f);
//		data |= (FMO_SLICE_SAVE_BUF_SIZE << 7);
//	}

	VpuWriteReg(CMD_ENC_SEQ_FMO, data);	/* FIXME */

	VpuWriteReg(BIT_BUSY_FLAG, 0x1);

	BitIssueCommand(instance->idx, instance->format, SEQ_INIT);
	if (vpu_wait())
		return -EINVAL;

	if (VpuReadReg(RET_ENC_SEQ_SUCCESS) == 0) {
		printk("%s failed\n", __func__);
		return -EINVAL;
	}

	instance->num_fb = 3; /* FIXME */
	ret = alloc_fb(instance);
	if (ret) {
		printk("alloc fb failed\n");
		goto out;
	}

	/* Tell the codec how much frame buffers we allocated. */
	VpuWriteReg(CMD_SET_FRAME_BUF_NUM, instance->num_fb);
	VpuWriteReg(CMD_SET_FRAME_BUF_STRIDE, ROUND_UP_8(instance->width));

	VpuWriteReg(BIT_BUSY_FLAG, 0x1);

	BitIssueCommand(instance->idx, instance->format, SET_FRAME_BUF);
	if (vpu_wait())
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
	u32 val, val2;
	u64 f;
	int ret;

	switch (instance->standard) {
	case STD_MPEG4:
	case STD_H263:
		instance->format = MP4_DEC;
		break;
	case STD_AVC:
		instance->format = AVC_DEC;
		break;
	default:
		return -EINVAL;
	};

	VpuWriteReg(BIT_PARA_BUF_ADDR, instance->para_buf_phys);

	VpuWriteReg(BIT_WR_PTR(instance->idx), instance->bitstream_buf_phys +
			instance->fifo_in);
	VpuWriteReg(BIT_RD_PTR(instance->idx), instance->bitstream_buf_phys);

	VpuWriteReg(CMD_DEC_SEQ_INIT_ESCAPE, 1);

	VpuWriteReg(CMD_DEC_SEQ_BB_START, instance->bitstream_buf_phys);
	VpuWriteReg(CMD_DEC_SEQ_START_BYTE, instance->bitstream_buf_phys);
	VpuWriteReg(CMD_DEC_SEQ_BB_SIZE, BITSTREAM_BUF_SIZE / 1024);
	VpuWriteReg(CMD_DEC_SEQ_OPTION, 0);
	VpuWriteReg(CMD_DEC_SEQ_PS_BB_START, instance->ps_mem_buf_phys);
	VpuWriteReg(CMD_DEC_SEQ_PS_BB_SIZE, (PS_SAVE_SIZE / 1024));

	VpuWriteReg(BIT_BUSY_FLAG, 0x1);
	BitIssueCommand(instance->idx, instance->format, SEQ_INIT);

	if (vpu_wait()) {
		ret = -EINVAL;
		VpuWriteReg(CMD_DEC_SEQ_INIT_ESCAPE, 0);
		goto out;
	}

	VpuWriteReg(CMD_DEC_SEQ_INIT_ESCAPE, 0);

	if (VpuReadReg(RET_DEC_SEQ_SUCCESS) == 0) {
		ret = -EAGAIN;
		goto out;
	}

	val = VpuReadReg(RET_DEC_SEQ_SRC_SIZE);
	instance->width = ((val >> 10) & 0x3ff);
	instance->height = (val & 0x3ff);

	instance->width = ROUND_UP_16(instance->width);
	instance->height = ROUND_UP_16(instance->height);
	printk("%s instance %d now: %dx%d\n", __func__, instance->idx,
			instance->width, instance->height);

	val = VpuReadReg(RET_DEC_SEQ_SRC_F_RATE);
	printk("%s: Framerate: 0x%08x\n", __func__, val);
	printk("%s: frame delay: %ld\n", __func__,
			VpuReadReg(RET_DEC_SEQ_FRAME_DELAY));
	f = val & 0xffff;
	f *= 1000;
	do_div(f, (val >> 16) + 1);
	instance->frame_duration = ktime_set(0, (u32)f);
	instance->num_fb = VpuReadReg(RET_DEC_SEQ_FRAME_NEED);

	if (instance->format == AVC_DEC) {
		int top, right;
		val = VpuReadReg(RET_DEC_SEQ_CROP_LEFT_RIGHT);
		val2 = VpuReadReg(RET_DEC_SEQ_CROP_TOP_BOTTOM);

		printk("crop left:  %d\n", ((val >> 10) & 0x3FF) * 2);
		printk("crop top:   %d\n", ((val2 >> 10) & 0x3FF) * 2);

		if (val == 0 && val2 == 0) {
			right = 0;
			top = 0;
		} else {
			right = instance->width - ((val & 0x3FF) * 2);
			top = instance->height - ((val2 & 0x3FF) * 2);
		}
		printk("crop right: %d\n", right);
		printk("crop top:   %d\n", top);
	}

	/* access normal registers */
	VpuWriteReg(CMD_DEC_SEQ_INIT_ESCAPE, 0);

	ret = alloc_fb(instance);
	if (ret)
		goto out;

	/* Tell the decoder how many frame buffers we allocated. */
	VpuWriteReg(CMD_SET_FRAME_BUF_NUM, instance->num_fb);
	VpuWriteReg(CMD_SET_FRAME_BUF_STRIDE, instance->width);

	VpuWriteReg(CMD_SET_FRAME_SLICE_BB_START, instance->slice_mem_buf_phys);
	VpuWriteReg(CMD_SET_FRAME_SLICE_BB_SIZE, SLICE_SAVE_SIZE / 1024);

	VpuWriteReg(BIT_BUSY_FLAG, 0x1);
	BitIssueCommand(instance->idx, instance->format, SET_FRAME_BUF);

	if (vpu_wait()) {
		ret = -EINVAL;
		goto out;
	}

	instance->needs_init = 0;

out:
	if (ret)
		printk("%s failed with %d\n", __func__, ret);

	return ret;
}

static void noinline vpu_enc_start_frame(struct vpu_instance *instance, struct videobuf_buffer *vb)
{
	dma_addr_t dma = videobuf_to_dma_contig(vb);
	int height = instance->height;
	int stridey = ROUND_UP_4(instance->width);
	int ustride;
	unsigned long u;

	instance->vpu->active = vb;

	VpuWriteReg(CMD_ENC_PIC_ROT_MODE, 0x10);

	VpuWriteReg(CMD_ENC_PIC_QS, 30);

	VpuWriteReg(CMD_ENC_PIC_SRC_ADDR_Y, dma);
	u = dma + stridey * ROUND_UP_2(height);
	VpuWriteReg(CMD_ENC_PIC_SRC_ADDR_CB, u);
	ustride = ROUND_UP_8(instance->width) / 2;
	VpuWriteReg(CMD_ENC_PIC_SRC_ADDR_CR, u + ustride * ROUND_UP_2(height) / 2);
	VpuWriteReg(CMD_ENC_PIC_OPTION, (0 << 5) | (0 << 1));

	VpuWriteReg(BIT_BUSY_FLAG, 0x1);
	BitIssueCommand(instance->idx, instance->format, PIC_RUN);
}

static void vpu_dec_start_frame(struct vpu_instance *instance, struct videobuf_buffer *vb)
{
	dma_addr_t dma;
	int height, stridey;
	unsigned int readofs;

	readofs = VpuReadReg(BIT_RD_PTR(instance->idx)) -
			instance->bitstream_buf_phys;

	vpu_fifo_out(instance, (readofs - instance->readofs) % BITSTREAM_BUF_SIZE);
	instance->readofs = readofs;

	VpuWriteReg(BIT_WR_PTR(instance->idx),
			instance->bitstream_buf_phys + (instance->fifo_in % BITSTREAM_BUF_SIZE));

	if (instance->rotmir & 0x1) {
		stridey = instance->height;
		height = instance->width;
	} else {
		stridey = instance->width;
		height = instance->height;
	}

	dma = videobuf_to_dma_contig(vb);

	/* Set rotator output */
	VpuWriteReg(CMD_DEC_PIC_ROT_ADDR_Y, dma);
	VpuWriteReg(CMD_DEC_PIC_ROT_ADDR_CB, dma + stridey * height);
	VpuWriteReg(CMD_DEC_PIC_ROT_ADDR_CR,
		dma + stridey * height + (stridey / 2) * (height / 2));
	VpuWriteReg(CMD_DEC_PIC_ROT_STRIDE, stridey);
	VpuWriteReg(CMD_DEC_PIC_ROT_MODE, instance->rotmir);

	VpuWriteReg(CMD_DEC_PIC_OPTION, 1); /* Enable prescan */
	VpuWriteReg(CMD_DEC_PIC_SKIP_NUM, 0);

	VpuWriteReg(BIT_BUSY_FLAG, 0x1);
	BitIssueCommand(instance->idx, instance->format, PIC_RUN);
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
//vpu_reset();
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
	if (!VpuReadReg(RET_DEC_PIC_OPTION)) {
		if (instance->flushing) {
			vb->state = VIDEOBUF_ERROR;
		} else {
			vb->state = VIDEOBUF_QUEUED;
			instance->hold = 1;
			return;
		}
	} else
		vb->state = VIDEOBUF_DONE;

	VpuWriteReg(BIT_FRM_DIS_FLG(instance->idx), 0);

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

	size = VpuReadReg(BIT_WR_PTR(instance->idx)) - VpuReadReg(BIT_RD_PTR(instance->idx));

	if (kfifo_avail(&instance->fifo) < instance->headersize + size) {
		printk("not enough space in fifo\n");
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

	VpuWriteReg(BIT_INT_CLEAR, 1);
	VpuWriteReg(BIT_INT_REASON, 0);

	if (!vb)
		goto out;

	vbuf = container_of(vpu->active, struct vpu_buffer, vb);
	instance = vbuf->instance;

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
	struct vpu_instance *instance;
	int ret = 0, i;

	spin_lock_irq(&vpu_data->lock);

	for (i = 0; i < VPU_NUM_INSTANCE; i++)
		if (!vpu_data->instance[i].in_use)
			break;

	if (i == VPU_NUM_INSTANCE) {
		ret = -EBUSY;
		goto out;
	}

	instance = &vpu_data->instance[i];

	instance->in_use = 1;
	instance->vpu = vpu_data;
	instance->idx = i;
	instance->needs_init = 1;
	instance->headersize = 0;
	instance->header = NULL;
	instance->mode = VPU_MODE_DECODER;
	instance->standard = STD_MPEG4;
	instance->format = AVC_DEC;
	instance->hold = 1;
	instance->flushing = 0;
	instance->readofs = 0;
	instance->fifo_in = 0;
	instance->fifo_out = 0;

	memset(instance->rec, 0, sizeof(instance->rec));

	instance->frametime = ktime_set(0, 0);

	init_waitqueue_head(&instance->waitq);

	file->private_data = instance;

	instance->bitstream_buf = dma_alloc_coherent(NULL, BITSTREAM_BUF_SIZE,
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

	instance->para_buf = dma_alloc_coherent(NULL, PARA_BUF_SIZE,
			&instance->para_buf_phys, GFP_DMA | GFP_KERNEL);
	if (!instance->para_buf) {
		ret = -ENOMEM;
		goto err_alloc4;
	}

	goto out;

err_alloc4:
	dma_free_coherent(NULL, PARA_BUF_SIZE, instance->para_buf,
			instance->para_buf_phys);
err_alloc3:
	dma_free_coherent(NULL, SLICE_SAVE_SIZE, instance->slice_mem_buf,
			instance->slice_mem_buf_phys);
err_alloc2:
	dma_free_coherent(NULL, BITSTREAM_BUF_SIZE, instance->bitstream_buf,
			instance->bitstream_buf_phys);
err_alloc1:
	instance->in_use = 0;
out:
	spin_unlock_irq(&vpu_data->lock);

	return ret;
}

#define STD_MPEG4	0
#define STD_H263	1
#define STD_AVC		2

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
	int i;

	dma_free_coherent(NULL, PARA_BUF_SIZE, instance->para_buf,
			instance->para_buf_phys);
	dma_free_coherent(NULL, SLICE_SAVE_SIZE, instance->slice_mem_buf,
			instance->slice_mem_buf_phys);
	dma_free_coherent(NULL, BITSTREAM_BUF_SIZE, instance->bitstream_buf,
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

	if (instance->header)
		kfree(instance->header);

	if (kfifo_initialized(&instance->fifo)) {
		kfifo_free(&instance->fifo);
	}

	return 0;
}

static int vpu_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct vpu_instance *instance = file->private_data;

	return videobuf_mmap_mapper(&instance->vidq, vma);
}

static ssize_t vpu_write(struct file *file, const char __user *ubuf, size_t len,
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

static ssize_t vpu_read(struct file *file, char __user *ubuf, size_t len, loff_t *off)
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

	VpuWriteReg(RET_VER_NUM, 0);

	VpuWriteReg(BIT_BUSY_FLAG, 0x1);
	BitIssueCommand(0, 0, FIRMWARE_GET);

	if (vpu_wait())
		return -ENODEV;

	ver = VpuReadReg(RET_VER_NUM);
	if (!ver)
		return -ENODEV;

	version = (u16) ver;

	dev_info(vpu->dev, "VPU firmware version %d.%d.%d\n",
			(version >> 12) & 0x0f,
			(version >> 8) & 0x0f,
			version & 0xff);

	return 0;
}

#define IMAGE_ENDIAN                    0
#define STREAM_ENDIAN                   0

static int vpu_program_firmware(struct vpu *vpu)
{
	int i, data;
	u32 *buf;

	vpu->vpu_code_table = dma_alloc_coherent(NULL, CODE_BUF_SIZE,
			&vpu->vpu_code_table_phys, GFP_DMA | GFP_KERNEL);
	if (!vpu->vpu_code_table)
		return -ENOMEM;

	buf = vpu->vpu_code_table;

	for (i = 0; i < sizeof (bit_code2) / sizeof (bit_code2[0]); i += 2) {
		data = (bit_code2[i] << 16) | bit_code2[i + 1];
		buf[i / 2] = data;
	}

	VpuWriteReg(BIT_CODE_BUF_ADDR, vpu->vpu_code_table_phys);
	VpuWriteReg(BIT_CODE_RUN, 0);

	/* Download BIT Microcode to Program Memory */
	for (i = 0; i < 2048; ++i) {
		data = bit_code2[i];
		VpuWriteReg(BIT_CODE_DOWN, (i << 16) | data);
	}

	vpu->vpu_work_buf = dma_alloc_coherent(NULL, WORK_BUF_SIZE,
			&vpu->vpu_work_buf_phys, GFP_DMA | GFP_KERNEL);
	if (!vpu->vpu_work_buf)
		return -ENOMEM;

	VpuWriteReg(BIT_WORK_BUF_ADDR, vpu->vpu_work_buf_phys);

	data = STREAM_ENDIAN | (1 << 2);
	VpuWriteReg(BIT_BIT_STREAM_CTRL, data);
	VpuWriteReg(BIT_FRAME_MEM_CTRL, IMAGE_ENDIAN);

	vpu_reset();
	VpuWriteReg(BIT_INT_ENABLE, 0x8);	/* PIC_RUN irq enable */

	VpuWriteReg(BIT_CODE_RUN, 1);

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
		rdptr = VpuReadReg(BIT_RD_PTR(instance->idx));
		VpuWriteReg(BIT_WR_PTR(instance->idx), rdptr - 1);

		vpu->active = NULL;
	}

	list_for_each_entry(vbtmp, &vpu->queued, queue) {
		if (vbtmp == vb) {
			printk("%s: buffer %p still queued. This should not happen\n",
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
					    sizeof(struct vpu_buffer), instance);

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
	.write		= vpu_write,
	.read		= vpu_read,
	.poll		= vpu_poll,
};

static int vpu_dev_probe(struct platform_device *pdev)
{
	int err = 0;
	struct resource *res;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENODEV;

	vpu_data->vdev = video_device_alloc();
	if (!vpu_data->vdev)
		return -ENOMEM;

	strcpy(vpu_data->vdev->name, "vpu");
	vpu_data->vdev->fops = &vpu_fops;
	vpu_data->vdev->ioctl_ops = &vpu_ioctl_ops;
	vpu_data->vdev->release = video_device_release;
	video_set_drvdata(vpu_data->vdev, vpu_data);
	vpu_data->dev = &pdev->dev;

	vpu_data->clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(vpu_data->clk)) {
		err = -ENOENT;
		goto err_out_clk;
	}

	clk_enable(vpu_data->clk);
	spin_lock_init(&vpu_data->lock);
	INIT_LIST_HEAD(&vpu_data->queued);

	vpu_data->base = ioremap(res->start, resource_size(res));
	if (!vpu_data->base) {
		err = -ENOMEM;
		goto err_out_ioremap;
	}

	vpu_data->irq = platform_get_irq(pdev, 0);
	err = request_irq(vpu_data->irq, vpu_irq_handler, 0,
			dev_name(&pdev->dev), vpu_data);
	if (err)
		goto err_out_irq;

	err = vpu_program_firmware(vpu_data);
	if (err)
		goto err_out_irq;

	err = device_create_file(&pdev->dev, &dev_attr_info);
	if (err)
		goto err_out_irq;

	err = video_register_device(vpu_data->vdev,
				    VFL_TYPE_GRABBER, -1);
	if (err) {
		dev_err(&pdev->dev, "failed to register: %d\n", err);
		goto err_out_register;
	}

	platform_set_drvdata(pdev, vpu_data);

	dummybuf = dma_alloc_coherent(NULL, 400*300,
		&dummydma, GFP_DMA | GFP_KERNEL);
	memset(dummybuf, 0, 320*240);

	dev_info(&pdev->dev, "registered\n");
	return 0;

err_out_register:
	free_irq(vpu_data->irq, vpu_data);
err_out_irq:
	iounmap(vpu_data->base);
err_out_ioremap:
	clk_disable(vpu_data->clk);
	clk_put(vpu_data->clk);
err_out_clk:
	kfree(vpu_data->vdev);

	if (vpu_data->vpu_work_buf)
		dma_free_coherent(NULL, WORK_BUF_SIZE, vpu_data->vpu_work_buf,
				vpu_data->vpu_work_buf_phys);
	if (vpu_data->vpu_code_table)
		dma_free_coherent(NULL, CODE_BUF_SIZE, vpu_data->vpu_code_table,
				vpu_data->vpu_code_table_phys);

	return err;
}

static int vpu_dev_remove(struct platform_device *pdev)
{
	struct vpu *vpu = platform_get_drvdata(pdev);

	free_irq(vpu_data->irq, vpu);

	clk_disable(vpu_data->clk);
	clk_put(vpu->clk);
	iounmap(vpu->base);

	dma_free_coherent(NULL, WORK_BUF_SIZE, vpu->vpu_work_buf,
			vpu->vpu_work_buf_phys);
	dma_free_coherent(NULL, CODE_BUF_SIZE, vpu->vpu_code_table,
			vpu->vpu_code_table_phys);

	video_unregister_device(vpu_data->vdev);

	device_remove_file(&pdev->dev, &dev_attr_info);
	platform_set_drvdata(pdev, NULL);



	return 0;
}

static struct platform_driver mxcvpu_driver = {
	.driver = {
		   .name = "mxc_vpu",
		   },
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

MODULE_AUTHOR("Freescale Semiconductor, Inc.");
MODULE_DESCRIPTION("Linux VPU driver for Freescale i.MX27");
MODULE_LICENSE("GPL");

module_init(vpu_init);
module_exit(vpu_exit);
