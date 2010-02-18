/*
 * Copyright 2005-2007 Freescale Semiconductor, Inc. All Rights Reserved.
 * Module Name:            mfw_gst_vpu_encoder.c
 *
 * General Description:    Implementation of Hardware (VPU) Encoder Plugin for Gstreamer.
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

#include <gst/gst.h>
#include <string.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <gst-plugins-fsl_config.h>
#include <linux/videodev2.h>
#include <unistd.h>

#include "vpu_io.h"
#include "vpu_lib.h"
#include "mfw_gst_vpu_encoder.h"

typedef struct {
	gint Index;
	gint AddrY;
	gint AddrCb;
	gint AddrCr;
	gint StrideY;
	gint StrideC;		/* Stride Cb/Cr */

	gint DispY;		/* DispY is the page aligned AddrY */
	gint DispCb;		/* DispCb is the page aligned AddrCb */
	gint DispCr;
	vpu_mem_desc CurrImage;	/* Current memory descriptor for user space */
} FRAME_BUF;

#define NUM_BUFFERS 3

typedef struct _MfwGstVPU_Enc
{
	GstElement	element;	/* instance of base class */
	GstPad		*sinkpad;
	GstPad		*srcpad;	/* source and sink pad of element */
	GstElementClass	*parent_class;

	/* VPU Specific defined in vpu_lib.h */
    	EncHandle	handle;
	EncOpenParam	*encOP;
	EncInitialInfo	*initialInfo;
	EncOutputInfo	*outputInfo;
	EncParam	*encParam;
    	vpu_mem_desc	bit_stream_buf;

	gboolean	init;		/* initialisation flag */
	guint8		*start_addr;	/* start addres of the Hardware input buffer */
	gfloat		frame_rate;	/* Frame rate of display */
	gboolean	profile;
	CodStd		codec;		/* codec standard to be selected */
	guint		width;
	guint		height;
	gfloat		framerate;
	gboolean	wait;
	gint		numframebufs;
	guint8*		header[NUM_INPUT_BUF];
	guint		headersize[NUM_INPUT_BUF];
	gint		headercount;
	gint		frameIdx;
	FrameBuffer	frameBuf[NUM_INPUT_BUF];
	FRAME_BUF	FrameBufPool[NUM_INPUT_BUF];
	gint		bitrate;
	gint		gopsize;
	gboolean 	codecTypeProvided; 	/* Set when the user provides the compression format on the command line */
	int vpu_fd;
	struct v4l2_buffer buf_v4l2[NUM_BUFFERS];
	unsigned char *buf_data[NUM_BUFFERS];
	unsigned int buf_size[NUM_BUFFERS];
	unsigned int queued;
}MfwGstVPU_Enc;

#define VPU_DEVICE "/dev/video0"

/*maximum limit of the output buffer */
#define BUFF_FILL_SIZE (200 * 1024)

/* Maximum width and height - D1*/
#define MAX_WIDTH		720
#define MAX_HEIGHT		576

/* Default frame rate */
#define DEFAULT_FRAME_RATE	30

/* The processor clock is 333 MHz for  MX27
to be chnaged for other platforms */

#define MFW_GST_VPUENC_VIDEO_CAPS \
    "video/mpeg, " \
    "width = (int) [16,  720], " \
    "height = (int) [16, 576]; " \
    \
    "video/x-h263, " \
    "width = (int) [16, 720], " \
    "height = (int)[16, 576]; " \
    \
    "video/x-h264, " \
    "width = (int) [16, 720], " \
    "height = (int)[16, 576] "

/* 	Chroma Subsampling ratio - assuming 4:2:0. */
/*	Not providing ability to set this on the command line because I'm not sure if VPU supports 4:2:2 - r58604 */
#define CHROMA_SAMPLING_MULTIPLE	1.5

/* properties set on the encoder */
enum {
	MFW_GST_VPU_PROP_0,
	MFW_GST_VPU_CODEC_TYPE,
	MFW_GST_VPU_PROF_ENABLE,
	MFW_GST_VPUENC_FRAME_RATE,
	MFW_GST_VPUENC_BITRATE,
	MFW_GST_VPUENC_GOP
};

/* get the element details */
static GstElementDetails mfw_gst_vpuenc_details =
GST_ELEMENT_DETAILS("Freescale: Hardware (VPU) Encoder",
		    "Codec/Encoder/Video",
		    "Encodes Raw YUV Data to MPEG4 SP,or H.264 BP, or H.263 Format",
		    "Multimedia Team <mmsw@freescale.com>");

static GstStaticPadTemplate mfw_gst_vpuenc_src_factory =
GST_STATIC_PAD_TEMPLATE("src",
			GST_PAD_SRC,
			GST_PAD_ALWAYS,
			GST_STATIC_CAPS(MFW_GST_VPUENC_VIDEO_CAPS));

/* defines the source pad  properties of VPU Encoder element */
static GstStaticPadTemplate mfw_gst_vpuenc_sink_factory =
GST_STATIC_PAD_TEMPLATE("sink",
			GST_PAD_SINK,
			GST_PAD_ALWAYS,
			GST_STATIC_CAPS("video/x-raw-yuv, "
					"format = (fourcc) {I420}, "
					"width = (int) [ 16, 720 ], "
					"height = (int) [ 16, 576 ], "
					"framerate = (fraction) [ 0/1, 60/1 ]")
    );

#define	GST_CAT_DEFAULT	mfw_gst_vpuenc_debug

GST_DEBUG_CATEGORY_STATIC(mfw_gst_vpuenc_debug);

static void mfw_gst_vpuenc_set_property(GObject * object, guint prop_id,
			    const GValue * value, GParamSpec * pspec)
{
	GST_DEBUG("mfw_gst_vpuenc_set_property");
printf("%s\n", __func__);
	MfwGstVPU_Enc *vpu_enc = MFW_GST_VPU_ENC(object);
	switch (prop_id) {
	case MFW_GST_VPU_PROF_ENABLE:
		vpu_enc->profile = g_value_get_boolean(value);
		GST_DEBUG("profile=%d", vpu_enc->profile);
		break;

	case MFW_GST_VPU_CODEC_TYPE:
		vpu_enc->codec = g_value_get_enum(value);
		GST_DEBUG("codec=%d", vpu_enc->codec);
		vpu_enc->codecTypeProvided = TRUE;
		break;

	case MFW_GST_VPUENC_BITRATE:
		vpu_enc->bitrate = g_value_get_int(value);
		GST_DEBUG("bitrate=%u", vpu_enc->bitrate);
		break;

	case MFW_GST_VPUENC_FRAME_RATE:
		vpu_enc->framerate = g_value_get_float(value);
		GST_DEBUG("framerate=%u", vpu_enc->framerate);
		break;

	case MFW_GST_VPUENC_GOP:
		vpu_enc->gopsize = g_value_get_int(value);
		GST_DEBUG("gopsize=%u", vpu_enc->gopsize);
		break;

	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
	return;
}

static void mfw_gst_vpuenc_get_property(GObject * object, guint prop_id,
			    GValue * value, GParamSpec * pspec)
{
printf("%s\n", __func__);
	GST_DEBUG("mfw_gst_vpuenc_get_property");
	MfwGstVPU_Enc *vpu_enc = MFW_GST_VPU_ENC(object);
	switch (prop_id) {
	case MFW_GST_VPU_PROF_ENABLE:
		g_value_set_boolean(value, vpu_enc->profile);
		break;

	case MFW_GST_VPU_CODEC_TYPE:
		g_value_set_enum(value, vpu_enc->codec);
		break;

	case MFW_GST_VPUENC_BITRATE:
		g_value_set_int(value, vpu_enc->bitrate);
		break;

	case MFW_GST_VPUENC_FRAME_RATE:
		g_value_set_float(value, vpu_enc->framerate);
		break;

	case MFW_GST_VPUENC_GOP:
		g_value_set_int(value, vpu_enc->gopsize);
		break;

	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
	return;

}

static int mfw_gst_encoder_fill_headers(MfwGstVPU_Enc * vpu_enc)
{
	EncHeaderParam enchdr_param = { 0 };
printf("%s\n", __func__);
	guint8 *ptr;
	/* Must put encode header before encoding */
	if (vpu_enc->codec == STD_MPEG4) {

		vpu_enc->headercount = 3;
		enchdr_param.headerType = VOS_HEADER;
		vpu_EncGiveCommand(vpu_enc->handle, ENC_PUT_MP4_HEADER,
				   &enchdr_param);
		vpu_enc->headersize[0] = enchdr_param.size;
		vpu_enc->header[0] = g_malloc(enchdr_param.size);

		if (vpu_enc->header[0] == NULL) {
			GST_ERROR
			    ("Error in Allocating memory for VOS_HEADER\n");
			return -1;
		}
		ptr =
		    vpu_enc->start_addr + enchdr_param.buf -
		    vpu_enc->bit_stream_buf.phy_addr;
		memcpy(vpu_enc->header[0], ptr, enchdr_param.size);

		enchdr_param.headerType = VIS_HEADER;
		vpu_EncGiveCommand(vpu_enc->handle, ENC_PUT_MP4_HEADER,
				   &enchdr_param);
		vpu_enc->headersize[1] = enchdr_param.size;

		vpu_enc->header[1] = g_malloc(enchdr_param.size);
		if (vpu_enc->header[1] == NULL) {
			GST_ERROR
			    ("Error in Allocating memory for VIS_HEADER\n");
			return -1;
		}

		ptr = vpu_enc->start_addr + enchdr_param.buf -
			vpu_enc->bit_stream_buf.phy_addr;
		memcpy(vpu_enc->header[1], ptr, enchdr_param.size);

		enchdr_param.headerType = VOL_HEADER;
		vpu_EncGiveCommand(vpu_enc->handle, ENC_PUT_MP4_HEADER,
				   &enchdr_param);
		vpu_enc->headersize[2] = enchdr_param.size;

		vpu_enc->header[2] = g_malloc(enchdr_param.size);
		if (vpu_enc->header[2] == NULL) {
			GST_ERROR
			    ("Error in Allocating memory for VOL_HEADER\n");
			return -1;
		}
		ptr =
		    vpu_enc->start_addr + enchdr_param.buf -
		    vpu_enc->bit_stream_buf.phy_addr;
		memcpy(vpu_enc->header[2], ptr, enchdr_param.size);

	}

	else if (vpu_enc->codec == STD_AVC) {

		vpu_enc->headercount = 2;
		enchdr_param.headerType = SPS_RBSP;
		vpu_EncGiveCommand(vpu_enc->handle, ENC_PUT_AVC_HEADER,
				   &enchdr_param);
		vpu_enc->headersize[0] = enchdr_param.size;
		vpu_enc->header[0] = g_malloc(enchdr_param.size);
		if (vpu_enc->header[0] == NULL) {
			GST_ERROR
			    ("Error in Allocating memory for SPS_RBSP Header \n");
			return -1;
		}

		ptr =
		    vpu_enc->start_addr + enchdr_param.buf -
		    vpu_enc->bit_stream_buf.phy_addr;
		memcpy(vpu_enc->header[0], ptr, enchdr_param.size);
		enchdr_param.headerType = PPS_RBSP;
		vpu_EncGiveCommand(vpu_enc->handle, ENC_PUT_AVC_HEADER,
				   &enchdr_param);
		vpu_enc->headersize[1] = enchdr_param.size;
		vpu_enc->header[1] = g_malloc(enchdr_param.size);
		if (vpu_enc->header[1] == NULL) {
			GST_ERROR
			    ("Error in Allocating memory for PPS_RBSP Header \n");
			return -1;
		}

		ptr =
		    vpu_enc->start_addr + enchdr_param.buf -
		    vpu_enc->bit_stream_buf.phy_addr;
		memcpy(vpu_enc->header[1], ptr, enchdr_param.size);

		return 0;

	}

	return 0;
}

static struct v4l2_requestbuffers reqs = {
	.count	= NUM_BUFFERS,
	.type	= V4L2_BUF_TYPE_VIDEO_OUTPUT,
	.memory	= V4L2_MEMORY_MMAP,
};

static int mfw_gst_vpuenc_init_encoder(GstPad *pad, GstBuffer *buffer)
{
	MfwGstVPU_Enc *vpu_enc = MFW_GST_VPU_ENC(GST_PAD_PARENT(pad));
	gchar *mime = "undef";
	gint ret;
	GstCaps *caps = NULL;
	struct v4l2_format fmt;
	int retval, i;
	unsigned long type = V4L2_MEMORY_MMAP;

printf("%s\n", __func__);
	if (!vpu_enc->codecTypeProvided) {
		GST_ERROR("Incomplete command line.\n");
		GError *error = NULL;
		GQuark domain = g_quark_from_string("mfw_vpuencoder");
		error = g_error_new(domain, 10, "fatal error");
		gst_element_post_message(GST_ELEMENT(vpu_enc),
					 gst_message_new_error
					 (GST_OBJECT(vpu_enc), error,
					  "Incomplete command line - codec type was not provided."));
		return GST_FLOW_ERROR;
	}
#if 0
	/* The Frame Rate Value is set only in case of MPEG4 and H.264
	   not set for H.263 */
	if (vpu_enc->encOP->frameRateInfo != 0x3E87530)
		vpu_enc->encOP->frameRateInfo =
		    (gint) (vpu_enc->framerate + 0.5);
#endif

//	vpu_enc->encParam->quantParam = 30;
//	vpu_enc->encParam->forceIPicture = 0;
//	vpu_enc->encParam->skipPicture = 0;

	fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	fmt.fmt.pix.width = vpu_enc->width;
	fmt.fmt.pix.height = vpu_enc->height;
	fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YVU420;

	retval = ioctl(vpu_enc->vpu_fd, VIDIOC_S_FMT, &fmt);
	if (retval) {
		printf("VIDIOC_S_FMT failed: %s\n", strerror(errno));
		return GST_FLOW_ERROR;
	}

	retval = ioctl(vpu_enc->vpu_fd, VIDIOC_REQBUFS, &reqs);
	if (retval) {
		printf("VIDIOC_REQBUFS failed: %s\n", strerror(errno));
		return GST_FLOW_ERROR;
	}

	for (i = 0; i < NUM_BUFFERS; i++) {
		struct v4l2_buffer *buf = &vpu_enc->buf_v4l2[i];
		buf->type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
		buf->memory = V4L2_MEMORY_MMAP;
		buf->index = i;

		retval = ioctl(vpu_enc->vpu_fd, VIDIOC_QUERYBUF, buf);
		if (retval) {
			GST_ERROR("VIDIOC_QUERYBUF failed: %s\n", strerror(errno));
			return GST_FLOW_ERROR;
		}
		vpu_enc->buf_size[i] = buf->length;
		vpu_enc->buf_data[i] = mmap(NULL, buf->length,
				   PROT_READ | PROT_WRITE, MAP_SHARED,
				   vpu_enc->vpu_fd, vpu_enc->buf_v4l2[i].m.offset);
	}

	if (0) {
	ret = mfw_gst_encoder_fill_headers(vpu_enc);
	if (ret < 0) {
		GError *error = NULL;
		GQuark domain;
		domain = g_quark_from_string("mfw_vpuencoder");
		error = g_error_new(domain, 10, "fatal error");
		gst_element_post_message(GST_ELEMENT(vpu_enc),
					 gst_message_new_error
					 (GST_OBJECT(vpu_enc), error,
					  "Allocation for Headers failed "));
		return GST_FLOW_ERROR;
	}
	}

	if (vpu_enc->codec == STD_MPEG4)
		mime = "video/mpeg";
	else if (vpu_enc->codec == STD_AVC)
		mime = "video/x-h264";
	else if (vpu_enc->codec == STD_H263)
		mime = "video/x-h263";

	caps = gst_caps_new_simple(mime,
			   "mpegversion", G_TYPE_INT, 4,
			   "systemstream", G_TYPE_BOOLEAN, FALSE,
			   "height", G_TYPE_INT, vpu_enc->height,
			   "width", G_TYPE_INT, vpu_enc->width,
			   "framerate", GST_TYPE_FRACTION, (gint32) (vpu_enc->framerate * 1000),
			   1000, NULL);

	gst_pad_set_caps(vpu_enc->srcpad, caps);

	retval = ioctl(vpu_enc->vpu_fd, VIDIOC_STREAMON, &type);
	if (retval) {
		printf("streamon failed with %d", retval);
		return GST_FLOW_ERROR;
	}

	vpu_enc->init = TRUE;

	return GST_FLOW_OK;
}

static GstFlowReturn mfw_gst_vpuenc_chain(GstPad * pad, GstBuffer * buffer)
{
	MfwGstVPU_Enc *vpu_enc = NULL;
//	RetCode vpu_ret = RETCODE_SUCCESS;
	GstFlowReturn retval = GST_FLOW_OK;
	GstCaps *src_caps;
	GstBuffer *outbuffer;
	gint i = 0;
//	gint totalsize = 0;
//	gint offset = 0;
//	int handled = 0;
	int ret;
	struct pollfd pollfd;

	GST_DEBUG("mfw_gst_vpuenc_chain");

	vpu_enc = MFW_GST_VPU_ENC(GST_PAD_PARENT(pad));
printf("%s: %dx%d\n", __func__, vpu_enc->width, vpu_enc->height);

	if (vpu_enc->init == FALSE) {
		retval = mfw_gst_vpuenc_init_encoder(pad, buffer);
		if (retval != GST_FLOW_OK)
			return retval;
		printf("VPU ENC initialised\n");
	}

	for (i = 0; i < NUM_BUFFERS; i++) {
		if (!(vpu_enc->queued & (1 << i)))
			break;
	}

	if (i == NUM_BUFFERS) {
		printf("NO BUFFER AVAILABLE\n");
		return GST_FLOW_ERROR;
	}

	/* copy the input Frame into the allocated buffer */
	memcpy(vpu_enc->buf_data[i], GST_BUFFER_DATA(buffer), GST_BUFFER_SIZE(buffer));
	gst_buffer_unref(buffer);

	pollfd.fd = vpu_enc->vpu_fd;
	pollfd.events = POLLIN | POLLOUT;

//	vpu_enc->queued |= 1 << i;
	ret = ioctl(vpu_enc->vpu_fd, VIDIOC_QBUF, &vpu_enc->buf_v4l2[0]);
	if (ret) {
		GST_ERROR("VIDIOC_QBUF failed: %s\n", strerror(errno));
		return GST_FLOW_ERROR;
	}
printf("queued\n");
	ret = ioctl(vpu_enc->vpu_fd, VIDIOC_DQBUF, &vpu_enc->buf_v4l2[0]);
	if (ret) {
		GST_ERROR("VIDIOC_DQBUF failed: %s\n", strerror(errno));
		return GST_FLOW_ERROR;
	}
printf("dequeued\n");

	src_caps = GST_PAD_CAPS(vpu_enc->srcpad);

	retval = gst_pad_alloc_buffer_and_set_caps(vpu_enc->srcpad,
			0, 32768, src_caps, &outbuffer);
	if (retval != GST_FLOW_OK) {
		GST_ERROR("Allocating buffer failed with %d", ret);
		return retval;
	}

	ret = read(vpu_enc->vpu_fd, GST_BUFFER_DATA(outbuffer), 32768);
	if (ret < 0) {
		printf("read failed: %s\n", strerror(errno));
		return GST_FLOW_ERROR;
	}
	GST_BUFFER_SIZE(outbuffer) = ret;

	retval = gst_pad_push(vpu_enc->srcpad, outbuffer);
	if (retval != GST_FLOW_OK) {
		GST_ERROR("Error in Pushing the Output ont to "
			  "the Source Pad,error is %d \n",
			  retval);
	}

//done:
	return retval;
}

static GstStateChangeReturn mfw_gst_vpuenc_change_state
    (GstElement * element, GstStateChange transition)
{
	GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
	MfwGstVPU_Enc *vpu_enc = NULL;
	vpu_enc = MFW_GST_VPU_ENC(element);
//	gint vpu_ret = 0;
	CodStd mode;

	switch (transition) {
	case GST_STATE_CHANGE_NULL_TO_READY:
		GST_DEBUG("VPU State: Null to Ready");
		vpu_enc->vpu_fd = open(VPU_DEVICE, O_RDWR); // | O_NONBLOCK);
		if (vpu_enc->vpu_fd < 0) {
			GST_ERROR("opening %s failed", VPU_DEVICE);
			return GST_STATE_CHANGE_FAILURE;
		}

		ioctl(vpu_enc->vpu_fd, VPU_IOC_SET_ENCODER, 0);

		printf("Enc opened. res: %dx%d\n", vpu_enc->width, vpu_enc->height);
		break;
	case GST_STATE_CHANGE_READY_TO_PAUSED:
		printf("VPU State: Ready to Paused");
		printf("Enc opened. res: %dx%d\n", vpu_enc->width, vpu_enc->height);

		GST_DEBUG("codec=%d", vpu_enc->codec);
		mode = vpu_enc->codec;

		vpu_enc->init = FALSE;
		vpu_enc->wait = FALSE;
//		vpu_enc->encOP->bitRate = vpu_enc->bitrate;
//		vpu_enc->encOP->gopSize = vpu_enc->gopsize;
//		vpu_enc->encOP->slicemode.sliceMode = 1;	/* 1 slice per picture */
//		vpu_enc->encOP->slicemode.sliceSize = 4000;	/* not used if sliceMode is 0 */
		vpu_enc->numframebufs = 0;

		if (mode == STD_MPEG4) {
		} else if (mode == STD_H263) {
//			vpu_enc->encOP->frameRateInfo = 0x3E87530;
		} else if (mode == STD_AVC) {
		} else {
			GST_ERROR("Encoder: Invalid codec standard mode");
			return GST_STATE_CHANGE_FAILURE;
		}

		break;
	case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
		GST_DEBUG("VPU State: Paused to Playing");
		break;
	default:
		break;
	}

	ret = vpu_enc->parent_class->change_state(element, transition);
	GST_DEBUG("State Change for VPU returned %d", ret);

	switch (transition) {
	case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
		GST_DEBUG("VPU State: Playing to Paused");
		break;
	case GST_STATE_CHANGE_PAUSED_TO_READY:
		GST_DEBUG("VPU State: Paused to Ready");
		break;
	case GST_STATE_CHANGE_READY_TO_NULL:
		GST_DEBUG("VPU State: Ready to Null");
		IOSystemShutdown();
		break;
	default:
		break;
	}

	return ret;

}

static gboolean mfw_gst_vpuenc_sink_event(GstPad * pad, GstEvent * event)
{
	MfwGstVPU_Enc *vpu_enc = NULL;
	gboolean ret = FALSE;
	vpu_enc = MFW_GST_VPU_ENC(GST_PAD_PARENT(pad));
	GstFormat format;
	gint64 start, stop, position;
	gdouble rate;
printf("%s\n", __func__);
	switch (GST_EVENT_TYPE(event)) {
	case GST_EVENT_NEWSEGMENT:
		gst_event_parse_new_segment(event, NULL, &rate, &format,
					    &start, &stop, &position);

		if (format == GST_FORMAT_BYTES) {
			ret = gst_pad_push_event(vpu_enc->srcpad,
						 gst_event_new_new_segment
						 (FALSE, 1.0,
						  GST_FORMAT_TIME, 0,
						  GST_CLOCK_TIME_NONE,
						  0));
		}
		break;
	case GST_EVENT_EOS:
		ret = gst_pad_push_event(vpu_enc->srcpad, event);

		if (TRUE != ret) {
			GST_ERROR("Error in pushing the event, result is %d\n",
			     ret);
			gst_event_unref(event);
		}
		break;
	default:
		ret = gst_pad_event_default(pad, event);
		break;
	}
	return ret;
}

static gboolean mfw_gst_vpuenc_setcaps(GstPad * pad, GstCaps * caps)
{
	MfwGstVPU_Enc *vpu_enc = NULL;
	gint32 frame_rate_de = 0;
	gint32 frame_rate_nu = 0;
	gint width = 0;
	gint height = 0;
printf("%s\n", __func__);
	GST_DEBUG("mfw_gst_vpuenc_setcaps");
	vpu_enc = MFW_GST_VPU_ENC(gst_pad_get_parent(pad));

	GstStructure *structure = gst_caps_get_structure(caps, 0);

	gst_structure_get_int(structure, "width", &width);
	vpu_enc->width = width;

	gst_structure_get_int(structure, "height", &height);
	vpu_enc->height = height;

	gst_structure_get_fraction(structure, "framerate",
				   &frame_rate_nu, &frame_rate_de);

	if ((frame_rate_nu != 0) && (frame_rate_de != 0)) {
		vpu_enc->framerate = (gfloat) frame_rate_nu / frame_rate_de;

	}
	GST_DEBUG("framerate=%f", vpu_enc->framerate);
	gst_object_unref(vpu_enc);
	return gst_pad_set_caps(pad, caps);

}

static void mfw_gst_vpuenc_base_init(MfwGstVPU_EncClass * klass)
{

	GstElementClass *element_class = GST_ELEMENT_CLASS(klass);

	gst_element_class_add_pad_template(element_class,
					   gst_static_pad_template_get
					   (&mfw_gst_vpuenc_src_factory));

	gst_element_class_add_pad_template(element_class,
					   gst_static_pad_template_get
					   (&mfw_gst_vpuenc_sink_factory));

	gst_element_class_set_details(element_class, &mfw_gst_vpuenc_details);

}

GType mfw_gst_vpuenc_codec_get_type(void)
{
	static GType vpuenc_codec_type = 0;
	static GEnumValue vpuenc_codecs[] = {
		{STD_MPEG4, "0", "std_mpeg4"},
		{STD_H263, "1", "std_h263"},
		{STD_AVC, "2", "std_avc"},
		{0, NULL, NULL},
	};
	if (!vpuenc_codec_type) {
		vpuenc_codec_type =
		    g_enum_register_static("MfwGstVpuEncCodecs", vpuenc_codecs);
	}
	return vpuenc_codec_type;
}

static void
mfw_gst_vpuenc_class_init(MfwGstVPU_EncClass * klass)
{

	GObjectClass *gobject_class = NULL;
	GstElementClass *gstelement_class = NULL;

	gobject_class = (GObjectClass *) klass;
	gstelement_class = (GstElementClass *) klass;
	gstelement_class->change_state = mfw_gst_vpuenc_change_state;
	gobject_class->set_property = mfw_gst_vpuenc_set_property;
	gobject_class->get_property = mfw_gst_vpuenc_get_property;

	g_object_class_install_property(gobject_class, MFW_GST_VPU_PROF_ENABLE,
					g_param_spec_boolean("profile",
							     "Profile",
							     "enable time profile of the vpu encoder plug-in",
							     FALSE,
							     G_PARAM_READWRITE));

	g_object_class_install_property(gobject_class, MFW_GST_VPU_CODEC_TYPE,
					g_param_spec_enum("codec-type",
							  "codec_type",
							  "selects the codec type for encoding",
							  MFW_GST_TYPE_VPU_ENC_CODEC,
							  STD_AVC,
							  G_PARAM_READWRITE));

	g_object_class_install_property(gobject_class,
					MFW_GST_VPUENC_FRAME_RATE,
					g_param_spec_float("framerate",
							   "FrameRate",
							   "gets the framerate at which the input stream is to be encoded",
							   0, 60.0, 30.0,
							   G_PARAM_READWRITE));

	g_object_class_install_property(gobject_class, MFW_GST_VPUENC_BITRATE,
					g_param_spec_int("bitrate", "Bitrate",
							 "gets the bitrate (in kbps) at which stream is to be encoded",
							 0, 32767, 0,
							 G_PARAM_READWRITE));

	g_object_class_install_property(gobject_class, MFW_GST_VPUENC_GOP,
					g_param_spec_int("gopsize", "Gopsize",
							 "gets the GOP size at which stream is to be encoded",
							 0, 60, 0,
							 G_PARAM_READWRITE));

}

static void
mfw_gst_vpuenc_init(MfwGstVPU_Enc * vpu_enc, MfwGstVPU_EncClass * gclass)
{

	GST_DEBUG("mfw_gst_vpuenc_init");

	GstElementClass *klass = GST_ELEMENT_GET_CLASS(vpu_enc);

	/* create the sink and src pads */
	vpu_enc->sinkpad =
	    gst_pad_new_from_template(gst_element_class_get_pad_template
				      (klass, "sink"), "sink");
	vpu_enc->srcpad =
	    gst_pad_new_from_template(gst_element_class_get_pad_template
				      (klass, "src"), "src");
	gst_element_add_pad(GST_ELEMENT(vpu_enc), vpu_enc->sinkpad);
	gst_element_add_pad(GST_ELEMENT(vpu_enc), vpu_enc->srcpad);
	vpu_enc->parent_class = g_type_class_peek_parent(gclass);
	gst_pad_set_chain_function(vpu_enc->sinkpad, mfw_gst_vpuenc_chain);
	gst_pad_set_event_function(vpu_enc->sinkpad,
				   GST_DEBUG_FUNCPTR
				   (mfw_gst_vpuenc_sink_event));

	gst_pad_set_setcaps_function(vpu_enc->sinkpad, mfw_gst_vpuenc_setcaps);

	vpu_enc->codec = STD_AVC;
	vpu_enc->framerate = DEFAULT_FRAME_RATE;
	vpu_enc->bitrate = 0;
	vpu_enc->gopsize = 0;
	vpu_enc->codecTypeProvided = FALSE;
}

GType mfw_gst_type_vpu_enc_get_type(void)
{
	static GType vpu_enc_type = 0;

	if (!vpu_enc_type) {
		static const GTypeInfo vpu_enc_info = {
			sizeof (MfwGstVPU_EncClass),
			(GBaseInitFunc) mfw_gst_vpuenc_base_init,
			NULL,
			(GClassInitFunc) mfw_gst_vpuenc_class_init,
			NULL,
			NULL,
			sizeof (MfwGstVPU_Enc),
			0,
			(GInstanceInitFunc) mfw_gst_vpuenc_init,
		};
		vpu_enc_type = g_type_register_static(GST_TYPE_ELEMENT,
						      "MfwGstVPU_Enc",
						      &vpu_enc_info, 0);
	}

	GST_DEBUG_CATEGORY_INIT(mfw_gst_vpuenc_debug,
				"mfw_vpuencoder", 0,
				"FreeScale's VPU  Encoder's Log");

	return vpu_enc_type;
}
