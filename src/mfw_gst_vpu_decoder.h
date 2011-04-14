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
#include "mfw_gst_utils.h"

#define NUM_BUFFERS 4

G_BEGIN_DECLS
#define MFW_GST_TYPE_VPU_DEC (mfw_gst_type_vpu_dec_get_type())
#define MFW_GST_VPU_DEC(obj)  \
    (G_TYPE_CHECK_INSTANCE_CAST((obj),MFW_GST_TYPE_VPU_DEC,GstVPU_Dec))
#define MFW_GST_VPU_DEC_CLASS(klass) \
    G_TYPE_CHECK_CLASS_CAST((klass),MFW_GST_TYPE_VPU_DEC,GstVPU_DecClass))
#define MFW_GST_IS_VPU_DEC(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj),MFW_GST_TYPE_VPU_DEC))
#define MFW_GST_IS_VPU_DEC_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass),MFW_GST_TYPE_VPU_DEC))
#define MFW_GST_TYPE_VPU_DEC_CODEC (mfw_gst_vpudec_codec_get_type())
#define MFW_GST_TYPE_VPU_DEC_MIRROR (mfw_gst_vpudec_mirror_get_type())

typedef struct _GstVPU_DecClass {
	GstElementClass parent_class;

} GstVPU_DecClass;

GType mfw_gst_type_vpu_dec_get_type(void);
GType mfw_gst_vpudec_codec_get_type(void);

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
#define VPU_IOC_CODEC		_IO(VPU_IOC_MAGIC, 8)

G_END_DECLS
#endif				/* __MFW_GST_VPU_DECODER_H__ */
