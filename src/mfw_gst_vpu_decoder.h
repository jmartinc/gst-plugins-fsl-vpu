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

/*
 * Changelog:
 *
 */

/*=============================================================================
                            INCLUDE FILES
=============================================================================*/
#ifndef __MFW_GST_VPU_DECODER_H__
#define __MFW_GST_VPU_DECODER_H__
/*=============================================================================
                                           CONSTANTS
=============================================================================*/
#define NUM_MAX_VPU_REQUIRED 20
#define NUM_FRAME_BUF	(NUM_MAX_VPU_REQUIRED+2)

#define MAX_STREAM_BUF  512
//For Composing the RCV format for VC1

//VPU Supports only FOURCC_WMV3_WMV format (i.e. WMV9 only)
#define CODEC_VERSION	(0x5 << 24)	//FOURCC_WMV3_WMV
#define NUM_FRAMES		0xFFFFFF

#define SET_HDR_EXT			0x80000000
#define RCV_VERSION_2		0x40000000

/* None. */

/*=============================================================================
                                             ENUMS
=============================================================================*/

/* None. */

/*=============================================================================
                                            MACROS
=============================================================================*/

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
/*=============================================================================
                                 STRUCTURES AND OTHER TYPEDEFS
=============================================================================*/
typedef enum {
	FB_STATE_ILLEGAL,
	FB_STATE_ALLOCTED,	/* buffer is in allocated */
	FB_STATE_DISPLAY,	/* buffer is in display */
	FB_STATE_DECODED,	/* buffer is in decoded */
	FB_STATE_FREE,		/* buffer need free */
} FB_STATE;

typedef struct _MfwGstVPU_Dec {
	/* Plug-in specific members */
	GstElement element;	/* instance of base class */
	GstPad *sinkpad;
	GstPad *srcpad;		/* source and sink pad of element */
	GstElementClass *parent_class;
	gboolean init;		/* initialisation flag */
	gboolean vpu_opened;
	guint outsize;		/* size of the output image */
	GstBuffer *outbuffers[NUM_FRAME_BUF];
	/*output buffers allocated */
	GstBuffer *pushbuff;	/* out put buffer to be pushed */
	GstClockTime timestamp_buffer[MAX_STREAM_BUF];
	guint ts_rx;
	guint ts_tx;		/* members to handle timestamp */

	guint buffered_size;
	guint frame_sizes_buffer[MAX_STREAM_BUF];
	guint buffidx_in;
	guint buffidx_out;	/* members to handle input
				   buffer management */
	gboolean buf_empty;

	/* VPU specific Members */
	DecHandle *handle;
	DecOpenParam *decOP;
	DecInitialInfo *initialInfo;
	DecOutputInfo *outputInfo;
	DecParam *decParam;	/* Data Structures associated with
				   VPU API */
	vpu_mem_desc bit_stream_buf;	/* Structure for Bitstream buffer parameters */
	guint8 *start_addr;	/* start addres of the Hardware input buffer */
	guint8 *end_addr;	/* end addres of the Hardware input buffer */
	guint8 *base_addr;	/* base addres of the Hardware input buffer */
	FrameBuffer frameBuf[NUM_FRAME_BUF];
	/* Hardware output buffer structure */
	guint8 *frame_virt[NUM_FRAME_BUF];
	/* Hardware output buffer virtual adresses */
	FB_STATE fb_state_plugin[NUM_FRAME_BUF];
	CodStd codec;		/* codec standard to be selected */
	gboolean vpu_wait;	/* Flag for the VPU wait call */
	gint picWidth;		/* Width of the Image obtained through
				   Caps Neogtiation */
	gint picHeight;		/* Height of the Image obtained through
				   Caps Neogtiation */
	GstBuffer *HdrExtData;
	guint HdrExtDataLen;	/* Heafer Extension Data and length
				   obtained through Caps Neogtiation */
	vpu_mem_desc frame_mem[NUM_FRAME_BUF];
	/* Structure for Frame buffer parameters
	   if not used with V4LSink */
	gint numframebufs;	/* Number of Frame buffers */
	gboolean file_play_mode;	/* Flag for file play mode */
	gboolean eos;		/* Flag for end of stream */
	vpu_mem_desc ps_mem_desc;	/* ps save buffer */
	vpu_mem_desc slice_mem_desc;	/* slice save buffer */

	/* Misc members */
	guint64 decoded_frames;	/*number of the decoded frames */
	guint64 no_ts_frames;	/*number of no timestamp frames, for mpeg2 */
	GstClockTime base_ts;	/*the latest valid timestamp */
	gfloat frame_rate;	/* Frame rate of display */
	gint32 frame_rate_de;
	gint32 frame_rate_nu;
	gboolean profiling;	/* enable profiling */
	guint64 chain_Time;	/* time spent in the chain function */
	guint64 decode_wait_time;
	/* time for decode of one frame */
	guint64 frames_dropped;	/* number of frames dropped due to error */
	gfloat avg_fps_decoding;
	/* average fps of decoding  */
	gboolean direct_render;
	/* enable direct rendering in case of V4L */
	gboolean first;		/* Flag for inserting the RCV Header
				   fot the first time */
	gboolean loopback;	/* Flag to turn of parallelism in case of
				   loop back */
	gboolean framebufinit_done;
	/* Flag to initialise the Frame buffers */
	gint prv_use_idx;
	gboolean is_startframe;

	GMutex *vpu_mutex;
	gboolean lastframedropped;
	gboolean flush;		// Flag to indicate the flush event
	gboolean rotation_angle;	// rotation angle used for VPU to rotate
	gint rot_buff_idx;	// rotator buf index
	MirrorDirection mirror_dir;	// VPU mirror direction
	gboolean dbk_enabled;
	gint dbk_offset_a;
	gint dbk_offset_b;

	int once;
} MfwGstVPU_Dec;

typedef struct _MfwGstVPU_DecClass {
	GstElementClass parent_class;

} MfwGstVPU_DecClass;

/*=============================================================================
                                 GLOBAL VARIABLE DECLARATIONS
=============================================================================*/

/* None. */

/*=============================================================================
                                     FUNCTION PROTOTYPES
=============================================================================*/

GType mfw_gst_type_vpu_dec_get_type(void);
GType mfw_gst_vpudec_codec_get_type(void);

G_END_DECLS
#endif				/* __MFW_GST_VPU_DECODER_H__ */
