/*
 * Copyright 2011 Sascha Hauer, Pengutronix <s.hauer@pengutronix.de>
 * Copyright 2005-2007 Freescale Semiconductor, Inc. All Rights Reserved.
 *
 * Module Name:            mfw_gst_vpu_encoder.c
 *
 * General Description:    Implementation of Hardware (VPU) Encoder Plugin for
 *                         Gstreamer.
 *
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
#include <linux/videodev2.h>
#include <unistd.h>
#include <stdio.h>

#include "mfw_gst_vpu.h"
#include "mfw_gst_vpu_encoder.h"
#include "mfw_gst_utils.h"

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
} FRAME_BUF;

#define NUM_BUFFERS 3

typedef struct _GstVPU_Enc
{
	GstElement	element;	/* instance of base class */
	GstPad		*sinkpad;
	GstPad		*srcpad;	/* source and sink pad of element */
	GstElementClass	*parent_class;

	gboolean	init;		/* initialisation flag */
	guint8		*start_addr;	/* start addres of the Hardware input buffer */
	guint64 	encoded_frames;		/* number of the decoded frames */
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
	FRAME_BUF	FrameBufPool[NUM_INPUT_BUF];
	gint		bitrate;
	gint		gopsize;
	gboolean 	codecTypeProvided; 	/* Set when the user provides the compression format on the command line */

	int		once;
	enum v4l2_memory	memory;

	int vpu_fd;
	struct v4l2_buffer buf_v4l2[NUM_BUFFERS];
	unsigned char *buf_data[NUM_BUFFERS];
	unsigned int buf_size[NUM_BUFFERS];
	unsigned int queued;
	char *device;
}GstVPU_Enc;

/* Default frame rate */
#define DEFAULT_FRAME_RATE	30

#define MFW_GST_VPUENC_VIDEO_CAPS \
    "video/mpeg, " \
    "width = (int) [16, 1280], " \
    "height = (int) [16, 720]; " \
    \
    "video/x-h263, " \
    "width = (int) [16, 1280], " \
    "height = (int) [16, 720]; " \
    \
    "video/x-h264, " \
    "width = (int) [16, 1280], " \
    "height = (int) [16, 720]; " \
    \
    "image/jpeg, " \
    "width = (int) [16, 1280], " \
    "height = (int) [16, 720]; "

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
					"width = (int) [16, 1280], "
					"height = (int) [16, 720], "
					"framerate = (fraction) [0/1, 60/1]")
    );

#define	GST_CAT_DEFAULT	mfw_gst_vpuenc_debug

GST_DEBUG_CATEGORY_STATIC(mfw_gst_vpuenc_debug);

static void mfw_gst_vpuenc_set_property(GObject * object, guint prop_id,
			    const GValue * value, GParamSpec * pspec)
{
	GST_DEBUG("mfw_gst_vpuenc_set_property");

	GstVPU_Enc *vpu_enc = MFW_GST_VPU_ENC(object);
	switch (prop_id) {
	case MFW_GST_VPU_PROF_ENABLE:
		vpu_enc->profile = g_value_get_boolean(value);
		break;

	case MFW_GST_VPU_DEVICE:
		g_free(vpu_enc->device);
		vpu_enc->device = g_strdup(g_value_get_string(value));
		break;

	case MFW_GST_VPU_CODEC_TYPE:
		vpu_enc->codec = g_value_get_enum(value);
		vpu_enc->codecTypeProvided = TRUE;
		break;

	case MFW_GST_VPUENC_BITRATE:
		vpu_enc->bitrate = g_value_get_int(value);
		break;

	case MFW_GST_VPUENC_FRAME_RATE:
		vpu_enc->framerate = g_value_get_float(value);
		break;

	case MFW_GST_VPUENC_GOP:
		vpu_enc->gopsize = g_value_get_int(value);
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
	GST_DEBUG("mfw_gst_vpuenc_get_property");
	GstVPU_Enc *vpu_enc = MFW_GST_VPU_ENC(object);
	switch (prop_id) {
	case MFW_GST_VPU_PROF_ENABLE:
		g_value_set_boolean(value, vpu_enc->profile);
		break;

	case MFW_GST_VPU_DEVICE:
		g_value_set_string (value, vpu_enc->device);
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

static struct v4l2_requestbuffers reqs = {
	.count	= NUM_BUFFERS,
	.type	= V4L2_BUF_TYPE_VIDEO_OUTPUT,
	.memory	= V4L2_MEMORY_MMAP,
};

static int mfw_gst_vpuenc_init_encoder(GstPad *pad, enum v4l2_memory memory)
{
	GstVPU_Enc *vpu_enc = MFW_GST_VPU_ENC(GST_PAD_PARENT(pad));
	gchar *mime = "undef";
	gint ret;
	GstCaps *caps = NULL;
	struct v4l2_format fmt;
	int retval, i;

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

	fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	fmt.fmt.pix.width = vpu_enc->width;
	fmt.fmt.pix.height = vpu_enc->height;
	fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YVU420;

	retval = ioctl(vpu_enc->vpu_fd, VIDIOC_S_FMT, &fmt);
	if (retval) {
		printf("VIDIOC_S_FMT failed: %s\n", strerror(errno));
		return GST_FLOW_ERROR;
	}

	reqs.memory = memory;
	retval = ioctl(vpu_enc->vpu_fd, VIDIOC_REQBUFS, &reqs);
	if (retval) {
		perror("VIDIOC_REQBUFS");
		return GST_FLOW_ERROR;
	}

	retval = ioctl(vpu_enc->vpu_fd, VPU_IOC_CODEC, vpu_enc->codec);
	if (retval) {
		perror("VPU_IOC_CODEC");
		return GST_FLOW_ERROR;
	}

	for (i = 0; i < NUM_BUFFERS; i++) {
		struct v4l2_buffer *buf = &vpu_enc->buf_v4l2[i];
		buf->type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
		buf->memory = memory;
		buf->index = i;

		if (memory == V4L2_MEMORY_MMAP) {
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
	}

	switch (vpu_enc->codec) {
	case  STD_MPEG4:
		mime = "video/mpeg";
		break;
	case STD_AVC:
		mime = "video/x-h264";
		break;
	case STD_H263:
		mime = "video/x-h263";
		break;
	case STD_MJPG:
		mime = "image/jpeg";
		break;
	default:
		return GST_FLOW_ERROR;
	}

	caps = gst_caps_new_simple(mime,
			   "mpegversion", G_TYPE_INT, 4,
			   "systemstream", G_TYPE_BOOLEAN, FALSE,
			   "height", G_TYPE_INT, vpu_enc->height,
			   "width", G_TYPE_INT, vpu_enc->width,
			   "framerate", GST_TYPE_FRACTION, (gint32) (vpu_enc->framerate * 1000),
			   1000, NULL);

	gst_pad_set_caps(vpu_enc->srcpad, caps);

	vpu_enc->init = TRUE;

	return GST_FLOW_OK;
}

static GstFlowReturn mfw_gst_vpuenc_chain(GstPad * pad, GstBuffer * buffer)
{
	GstVPU_Enc *vpu_enc = NULL;
	GstFlowReturn retval = GST_FLOW_OK;
	GstCaps *src_caps;
	GstBuffer *outbuffer;
	gint i = 0;
	int ret;
	struct pollfd pollfd;
	unsigned long type = V4L2_BUF_TYPE_VIDEO_OUTPUT;

	GST_DEBUG(__func__);

	vpu_enc = MFW_GST_VPU_ENC(GST_PAD_PARENT(pad));

	if (vpu_enc->init == FALSE) {
		retval = mfw_gst_vpuenc_init_encoder(pad, vpu_enc->memory);
		if (retval != GST_FLOW_OK)
			return retval;
		printf("VPU ENC initialised\n");
	}

	i = 0;
	if (vpu_enc->memory == V4L2_MEMORY_USERPTR) {
		for (i = 0; i < NUM_BUFFERS; i++) {
			if (vpu_enc->buf_v4l2[i].m.userptr == (long int)GST_BUFFER_DATA (buffer))
				break;
		}
		if (i == NUM_BUFFERS) {
			for (i = 0; i < NUM_BUFFERS; i++) {
				if (!vpu_enc->buf_v4l2[i].m.userptr)
					break;
			}
		}
		i = i % NUM_BUFFERS;
	}

	if (i == NUM_BUFFERS) {
		printf("NO BUFFER AVAILABLE\n");
		return GST_FLOW_ERROR;
	}

	if (!buffer)
		return GST_FLOW_OK;

	if (vpu_enc->memory == V4L2_MEMORY_MMAP) {
		/* copy the input Frame into the allocated buffer */
		memcpy(vpu_enc->buf_data[i], GST_BUFFER_DATA(buffer), GST_BUFFER_SIZE(buffer));
		gst_buffer_unref(buffer);
	} else {
		vpu_enc->buf_v4l2[i].m.userptr = (long int)GST_BUFFER_DATA (buffer);
		vpu_enc->buf_v4l2[i].length = GST_BUFFER_SIZE (buffer);
	}

	pollfd.fd = vpu_enc->vpu_fd;
	pollfd.events = POLLIN | POLLOUT;

	ret = ioctl(vpu_enc->vpu_fd, VIDIOC_QBUF, &vpu_enc->buf_v4l2[i]);
	if (ret) {
		if (vpu_enc->memory == V4L2_MEMORY_USERPTR) {
			/* fallback to mmap */
			vpu_enc->init = FALSE;
			vpu_enc->memory = V4L2_MEMORY_MMAP;
			GST_WARNING("mfw_gst_vpuenc_chain: fallback to mmap");
			return mfw_gst_vpuenc_chain(pad, buffer);
		}
		GST_ERROR("VIDIOC_QBUF failed: %s\n", strerror(errno));
		return GST_FLOW_ERROR;
	}

	if (!vpu_enc->once) {
		retval = ioctl(vpu_enc->vpu_fd, VIDIOC_STREAMON, &type);
		if (retval) {
			printf("streamon failed with %d", retval);
			return GST_FLOW_ERROR;
		}
		vpu_enc->once = 1;
	}

	ret = ioctl(vpu_enc->vpu_fd, VIDIOC_DQBUF, &vpu_enc->buf_v4l2[0]);
	if (ret) {
		GST_ERROR("VIDIOC_DQBUF failed: %s\n", strerror(errno));
		return GST_FLOW_ERROR;
	}

	if (vpu_enc->memory == V4L2_MEMORY_USERPTR) {
		gst_buffer_unref(buffer);
	}

	src_caps = GST_PAD_CAPS(vpu_enc->srcpad);

	retval = gst_pad_alloc_buffer_and_set_caps(vpu_enc->srcpad,
			0, 1024 * 1024, src_caps, &outbuffer);
	if (retval != GST_FLOW_OK) {
		GST_ERROR("Allocating buffer failed with %d", ret);
		return retval;
	}

	ret = read(vpu_enc->vpu_fd, GST_BUFFER_DATA(outbuffer), 1024 * 1024);
	if (ret < 0) {
		printf("read failed: %s\n", strerror(errno));
		return GST_FLOW_ERROR;
	}
	GST_BUFFER_SIZE(outbuffer) = ret;
	GST_BUFFER_TIMESTAMP(outbuffer) = gst_util_uint64_scale(vpu_enc->encoded_frames,
		1 * GST_SECOND,
		vpu_enc->framerate);

	vpu_enc->encoded_frames++;

	GST_DEBUG_OBJECT(vpu_enc, "frame encoded : %lld ts = %" GST_TIME_FORMAT,
			vpu_enc->encoded_frames,
			GST_TIME_ARGS(GST_BUFFER_TIMESTAMP(outbuffer)));

	retval = gst_pad_push(vpu_enc->srcpad, outbuffer);
	if (retval != GST_FLOW_OK) {
		GST_ERROR("Pushing Output onto the source pad failed with %d \n",
			  retval);
	}

	return retval;
}

static GstStateChangeReturn mfw_gst_vpuenc_change_state
    (GstElement * element, GstStateChange transition)
{
	GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
	GstVPU_Enc *vpu_enc = NULL;
	vpu_enc = MFW_GST_VPU_ENC(element);
	CodStd mode;

	switch (transition) {
	case GST_STATE_CHANGE_NULL_TO_READY:
		GST_DEBUG("VPU State: Null to Ready");
		vpu_enc->vpu_fd = open(vpu_enc->device, O_RDWR);
		if (vpu_enc->vpu_fd < 0) {
			GST_ERROR("opening %s failed", vpu_enc->device);
			return GST_STATE_CHANGE_FAILURE;
		}

		printf("Enc opened. res: %dx%d\n", vpu_enc->width, vpu_enc->height);
		break;
	case GST_STATE_CHANGE_READY_TO_PAUSED:
		printf("VPU State: Ready to Paused");
		printf("Enc opened. res: %dx%d\n", vpu_enc->width, vpu_enc->height);

		GST_DEBUG("codec=%d", vpu_enc->codec);
		mode = vpu_enc->codec;

		vpu_enc->init = FALSE;
		vpu_enc->wait = FALSE;
		vpu_enc->numframebufs = 0;

		switch (mode) {
		case STD_MPEG4:
			break;
		case STD_H263:
			break;
		case STD_AVC:
			break;
		case STD_MJPG:
			break;
		default:
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
		vpu_enc->encoded_frames = 0;
		GST_DEBUG("VPU State: Paused to Ready");
		break;
	case GST_STATE_CHANGE_READY_TO_NULL:
		GST_DEBUG("VPU State: Ready to Null");
		break;
	default:
		break;
	}

	return ret;

}

static gboolean mfw_gst_vpuenc_sink_event(GstPad * pad, GstEvent * event)
{
	GstVPU_Enc *vpu_enc = NULL;
	gboolean ret = FALSE;
	vpu_enc = MFW_GST_VPU_ENC(GST_PAD_PARENT(pad));
	GstFormat format;
	gint64 start, stop, position;
	gdouble rate;

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
	GstVPU_Enc *vpu_enc = NULL;
	gint32 frame_rate_de = 0;
	gint32 frame_rate_nu = 0;
	gint width = 0;
	gint height = 0;

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

static void mfw_gst_vpuenc_base_init(GstVPU_EncClass * klass)
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

static void
mfw_gst_vpuenc_class_init(GstVPU_EncClass * klass)
{

	GObjectClass *gobject_class = NULL;
	GstElementClass *gstelement_class = NULL;

	gobject_class = (GObjectClass *) klass;
	gstelement_class = (GstElementClass *) klass;
	gstelement_class->change_state = mfw_gst_vpuenc_change_state;
	gobject_class->set_property = mfw_gst_vpuenc_set_property;
	gobject_class->get_property = mfw_gst_vpuenc_get_property;

	mfw_gst_vpu_class_init_common(gobject_class);

	g_object_class_install_property(gobject_class, MFW_GST_VPU_PROF_ENABLE,
			g_param_spec_boolean("profile",
					     "Profile",
					     "enable time profile of the vpu encoder plug-in",
					     FALSE,
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
mfw_gst_vpuenc_init(GstVPU_Enc * vpu_enc, GstVPU_EncClass * gclass)
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
	vpu_enc->device = g_strdup(VPU_DEVICE);
	vpu_enc->framerate = DEFAULT_FRAME_RATE;
	vpu_enc->bitrate = 0;
	vpu_enc->gopsize = 0;
	vpu_enc->codecTypeProvided = FALSE;
	vpu_enc->memory = V4L2_MEMORY_USERPTR;
}

GType mfw_gst_type_vpu_enc_get_type(void)
{
	static GType vpu_enc_type = 0;

	if (!vpu_enc_type) {
		static const GTypeInfo vpu_enc_info = {
			sizeof (GstVPU_EncClass),
			(GBaseInitFunc) mfw_gst_vpuenc_base_init,
			NULL,
			(GClassInitFunc) mfw_gst_vpuenc_class_init,
			NULL,
			NULL,
			sizeof (GstVPU_Enc),
			0,
			(GInstanceInitFunc) mfw_gst_vpuenc_init,
		};
		vpu_enc_type = g_type_register_static(GST_TYPE_ELEMENT,
						      "GstVPU_Enc",
						      &vpu_enc_info, 0);
	}

	GST_DEBUG_CATEGORY_INIT(mfw_gst_vpuenc_debug,
				"vpuencoder", 0,
				"FreeScale's VPU  Encoder's Log");

	return vpu_enc_type;
}
