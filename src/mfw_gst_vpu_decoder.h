/*
 * Copyright (C) 2005-2009 Freescale Semiconductor, Inc. All rights reserved.
 *
 */

/*
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/*
 * Module Name:    mfw_gst_vpu_decoder.h
 *
 * Description:    Include File for Hardware (VPU) Decoder Plugin
 *                 for Gstreamer
 *
 * Portability:    This code is written for Linux OS and Gstreamer
 */

#ifndef __MFW_GST_VPU_DECODER_H__
#define __MFW_GST_VPU_DECODER_H__

#include <linux/videodev2.h>

#define NUM_BUFFERS 4

G_BEGIN_DECLS
#define MFW_GST_TYPE_VPU_DEC (mfw_gst_type_vpu_dec_get_type())
#define MFW_GST_VPU_DEC(obj)  \
    (G_TYPE_CHECK_INSTANCE_CAST((obj),MFW_GST_TYPE_VPU_DEC,MfwGstVPU_Dec))
#define MFW_GST_VPU_DEC_CLASS(klass) \
    G_TYPE_CHECK_CLASS_CAST((klass),MFW_GST_TYPE_VPU_DEC,MfwGstVPU_DecClass))
#define MFW_GST_IS_VPU_DEC(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj),MFW_GST_TYPE_VPU_DEC))
#define MFW_GST_IS_VPU_DEC_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass),MFW_GST_TYPE_VPU_DEC))
#define MFW_GST_TYPE_VPU_DEC_CODEC (mfw_gst_vpudec_codec_get_type())
#define MFW_GST_TYPE_VPU_DEC_MIRROR (mfw_gst_vpudec_mirror_get_type())

typedef struct _MfwGstVPU_Dec {
	/* Plug-in specific members */
	GstElement element;	/* instance of base class */
	GstPad *sinkpad;
	GstPad *srcpad;		/* source and sink pad of element */
	GstElementClass *parent_class;
	gboolean init;		/* initialisation flag */
	guint outsize;		/* size of the output image */

	gint codec;		/* codec standard to be selected */
	gint picWidth;		/* Width of the Image obtained through
				   Caps Neogtiation */
	gint picHeight;		/* Height of the Image obtained through
				   Caps Neogtiation */
	GstBuffer *HdrExtData;
	guint HdrExtDataLen;	/* Heafer Extension Data and length
				   obtained through Caps Neogtiation */

	/* Misc members */
	guint64 decoded_frames;	/*number of the decoded frames */
	gfloat frame_rate;	/* Frame rate of display */
	gint32 frame_rate_de;
	gint32 frame_rate_nu;
	gfloat avg_fps_decoding;
	/* average fps of decoding  */
	/* enable direct rendering in case of V4L */
	gboolean first;		/* Flag for inserting the RCV Header
				   fot the first time */
	gboolean rotation_angle;	// rotation angle used for VPU to rotate
	gint mirror_dir;	// VPU mirror direction
	gboolean dbk_enabled;
	gint dbk_offset_a;
	gint dbk_offset_b;

	struct v4l2_buffer buf_v4l2[NUM_BUFFERS];
	unsigned char *buf_data[NUM_BUFFERS];
	unsigned int buf_size[NUM_BUFFERS];
	int vpu_fd;

	int once;
} MfwGstVPU_Dec;

typedef struct _MfwGstVPU_DecClass {
	GstElementClass parent_class;

} MfwGstVPU_DecClass;

GType mfw_gst_type_vpu_dec_get_type(void);
GType mfw_gst_vpudec_codec_get_type(void);

typedef enum {
	STD_MPEG2 = -1,
	STD_VC = -1,
	STD_MPEG4 = 0,
	STD_H263,
	STD_AVC
} CodStd;

typedef enum {
	MIRDIR_NONE,
	MIRDIR_VER,
	MIRDIR_HOR,
	MIRDIR_HOR_VER,
} MirrorDirection;

#define MIRROR_NONE	0
#define MIRROR_VER	4
#define MIRROR_HOR	8
#define MIRROR_HOR_VER	0xc

#define ROTATE_0	0
#define ROTATE_90	1
#define ROTATE_180	2
#define ROTATE_270	3

#define	VPU_IOC_MAGIC		'V'
#define	VPU_IOC_ROTATE_MIRROR	_IO(VPU_IOC_MAGIC, 7)
#define VPU_IOC_DEC_FORMAT	_IO(VPU_IOC_MAGIC, 8)

G_END_DECLS
#endif				/* __MFW_GST_VPU_DECODER_H__ */
