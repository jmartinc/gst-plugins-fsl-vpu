#ifndef __MFW_GST_VPU_H
#define __MFW_GST_VPU_H

void mfw_gst_vpu_class_init_common(GObjectClass *klass);

#define VPU_DEVICE "/dev/video/by-name/imx-vpu"

/* properties set on the encoder */
enum {
	MFW_GST_VPU_PROP_0,
	MFW_GST_VPU_CODEC_TYPE,
	MFW_GST_VPU_PROF_ENABLE,
	MFW_GST_VPU_DEVICE,
	/* encoder specific */
	MFW_GST_VPUENC_FRAME_RATE,
	MFW_GST_VPUENC_BITRATE,
	MFW_GST_VPUENC_GOP,
	/* decoder specific */
	MFW_GST_VPU_DBK_ENABLE,
	MFW_GST_VPU_DBK_OFFSETA,
	MFW_GST_VPU_DBK_OFFSETB,
	MFW_GST_VPU_ROTATION,
	MFW_GST_VPU_MIRROR,
};

#endif /* __MFW_GST_VPU_H */
