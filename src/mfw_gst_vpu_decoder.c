/*
 * Copyright 2011 Sascha Hauer, Pengutronix <s.hauer@pengutronix.de>
 * Copyright (C) 2005-2009 Freescale Semiconductor, Inc. All rights reserved.
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

/*
 * Module Name:    mfw_gst_vpu_decoder.c
 *
 * Description:    Implementation of Hardware (VPU) Decoder Plugin for Gstreamer.
 *
 * Portability:    This code is written for Linux OS and Gstreamer
 */

#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <gst/gst.h>
#include <gst/base/gstadapter.h>
#include <linux/videodev2.h>
#include "mfw_gst_vpu.h"
#include "mfw_gst_vpu_decoder.h"

#define MFW_GST_VPUDEC_VIDEO_CAPS \
    "video/mpeg, " \
    "width = (int) [16, 720], " \
    "height = (int) [16, 576], " \
    "mpegversion = (int) 4; " \
    \
    "video/x-divx, " \
    "width = (int) [16, 720], " \
    "height = (int) [16, 576], " \
    "divxversion = (int) [4, 5]; " \
    \
    "video/x-h263, " \
    "width = (int) [16, 720], " \
    "height = (int)[16, 576]; " \
    \
    "video/x-h264, " \
    "width = (int) [16, 720], " \
    "height = (int)[16, 576]"

#define DEFAULT_DBK_OFFSET_VALUE    5

typedef struct _GstVPU_Dec {
	/* Plug-in specific members */
	GstElement element;	/* instance of base class */
	GstPad *sinkpad;
	GstPad *srcpad;		/* source and sink pad of element */
	GstElementClass *parent_class;
	gboolean init;		/* initialisation flag */
	guint outsize;		/* size of the output image */

	gint codec;		/* codec standard to be selected */
	gint width;		/* Width of the Image obtained through
				   Caps Neogtiation */
	gint height;		/* Height of the Image obtained through
				   Caps Neogtiation */
	GstBuffer *hdr_ext_data;
	guint hdr_ext_data_len;	/* Header Extension Data and length
				   obtained through Caps Neogtiation */

	/* Misc members */
	guint64 decoded_frames;	/*number of the decoded frames */
	gfloat frame_rate;	/* Frame rate of display */
	gint32 frame_rate_de;
	gint32 frame_rate_nu;
	/* average fps of decoding  */
	/* enable direct rendering in case of V4L */
	gboolean rotation_angle;	// rotation angle used for VPU to rotate
	gint mirror_dir;	// VPU mirror direction
	gboolean dbk_enabled;
	gint dbk_offset_a;
	gint dbk_offset_b;

	char *device;
	struct v4l2_buffer buf_v4l2[NUM_BUFFERS];
	unsigned char *buf_data[NUM_BUFFERS];
	unsigned int buf_size[NUM_BUFFERS];
	int vpu_fd;

	int once;

	GstState state;
} GstVPU_Dec;

/* get the element details */
static GstElementDetails mfw_gst_vpudec_details =
GST_ELEMENT_DETAILS("Freescale: Hardware (VPU) Decoder",
		    "Codec/Decoder/Video",
		    "Decodes H.264, MPEG4, H263 "
		    "Elementary data into YUV 4:2:0 data",
		    "i.MX series");

/* defines sink pad  properties of the VPU Decoder element */
static GstStaticPadTemplate mfw_gst_vpudec_sink_factory =
GST_STATIC_PAD_TEMPLATE("sink",
			GST_PAD_SINK,
			GST_PAD_ALWAYS,
			GST_STATIC_CAPS(MFW_GST_VPUDEC_VIDEO_CAPS));


#define	GST_CAT_DEFAULT	mfw_gst_vpudec_debug

GST_DEBUG_CATEGORY_STATIC(mfw_gst_vpudec_debug);

static void mfw_gst_vpudec_class_init(GstVPU_DecClass *);
static void mfw_gst_vpudec_base_init(GstVPU_DecClass *);
static void mfw_gst_vpudec_init(GstVPU_Dec *, GstVPU_DecClass *);
static GstFlowReturn mfw_gst_vpudec_chain_stream_mode(GstPad *, GstBuffer *);
static GstStateChangeReturn mfw_gst_vpudec_change_state(GstElement *,
							GstStateChange);
static void mfw_gst_vpudec_set_property(GObject *, guint, const GValue *,
					GParamSpec *);
static void mfw_gst_vpudec_get_property(GObject *, guint, GValue *,
					GParamSpec *);
static gboolean mfw_gst_vpudec_sink_event(GstPad *, GstEvent *);
static gboolean mfw_gst_vpudec_setcaps(GstPad *, GstCaps *);

static void
mfw_gst_vpudec_set_property(GObject * object, guint prop_id,
			    const GValue * value, GParamSpec * pspec)
{
	GstVPU_Dec *vpu_dec = MFW_GST_VPU_DEC(object);
	switch (prop_id) {
	case MFW_GST_VPU_CODEC_TYPE:
		vpu_dec->codec = g_value_get_enum(value);
		GST_DEBUG("codec=%d", vpu_dec->codec);
		break;

	case MFW_GST_VPU_DEVICE:
		g_free(vpu_dec->device);
		vpu_dec->device = g_strdup(g_value_get_string(value));
		GST_DEBUG("device=%s", vpu_dec->device);
		break;

	case MFW_GST_VPU_DBK_ENABLE:
		vpu_dec->dbk_enabled = g_value_get_boolean(value);
		break;

	case MFW_GST_VPU_DBK_OFFSETA:
		vpu_dec->dbk_offset_a = g_value_get_int(value);
		break;

	case MFW_GST_VPU_DBK_OFFSETB:
		vpu_dec->dbk_offset_b = g_value_get_int(value);
		break;

	case MFW_GST_VPU_MIRROR:
		vpu_dec->mirror_dir = g_value_get_enum(value);
		GST_DEBUG("mirror_direction=%d", vpu_dec->mirror_dir);
		break;

	case MFW_GST_VPU_ROTATION:
		vpu_dec->rotation_angle = g_value_get_uint(value);
		switch (vpu_dec->rotation_angle) {
		case 0:
		case 90:
		case 180:
		case 270:
			GST_DEBUG("rotation angle=%d",
				  vpu_dec->rotation_angle);
			break;
		default:
			vpu_dec->rotation_angle = 0;
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
		}
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
	return;
}

static void
mfw_gst_vpudec_get_property(GObject * object, guint prop_id,
			    GValue * value, GParamSpec * pspec)
{

	GstVPU_Dec *vpu_dec = MFW_GST_VPU_DEC(object);
	switch (prop_id) {
	case MFW_GST_VPU_CODEC_TYPE:
		g_value_set_enum(value, vpu_dec->codec);
		break;
	case MFW_GST_VPU_DEVICE:
		g_value_set_string (value, vpu_dec->device);
		break;
	case MFW_GST_VPU_DBK_ENABLE:
		g_value_set_boolean(value, vpu_dec->dbk_enabled);
		break;
	case MFW_GST_VPU_DBK_OFFSETA:
		g_value_set_int(value, vpu_dec->dbk_offset_a);
		break;
	case MFW_GST_VPU_DBK_OFFSETB:
		g_value_set_int(value, vpu_dec->dbk_offset_b);
		break;
	case MFW_GST_VPU_MIRROR:
		g_value_set_enum(value, vpu_dec->mirror_dir);
		break;
	case MFW_GST_VPU_ROTATION:
		g_value_set_uint(value, vpu_dec->rotation_angle);
		break;

	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
	return;
}

static struct v4l2_requestbuffers reqs = {
	.count	= NUM_BUFFERS,
	.type	= V4L2_BUF_TYPE_VIDEO_CAPTURE,
	.memory	= V4L2_MEMORY_MMAP,
};

static GstFlowReturn mfw_gst_vpudec_vpu_init(GstVPU_Dec * vpu_dec)
{
	GstCaps *caps;
	gint crop_top_len, crop_left_len;
	gint crop_right_len, crop_bottom_len;
	gint orgPicW, orgPicH;
	gint width, height;
	gint crop_right_by_pixel, crop_bottom_by_pixel;
	int rotmir;
	int i, retval;
	struct v4l2_format fmt;
	unsigned long type = V4L2_MEMORY_MMAP;

	switch (vpu_dec->mirror_dir) {
	case MIRDIR_NONE:
		rotmir = MIRROR_NONE;
		break;
	case MIRDIR_HOR:
		rotmir = MIRROR_HOR;
		break;
	case MIRDIR_VER:
		rotmir = MIRROR_VER;
		break;
	case MIRDIR_HOR_VER:
		rotmir = MIRROR_HOR_VER;
		break;
	}

	switch (vpu_dec->rotation_angle) {
	case 0:
		rotmir |= ROTATE_0;
		break;
	case 90:
		rotmir |= ROTATE_90;
		break;
	case 180:
		rotmir |= ROTATE_180;
		break;
	case 270:
		rotmir |= ROTATE_270;
		break;
	}

	ioctl(vpu_dec->vpu_fd, VPU_IOC_ROTATE_MIRROR, rotmir);

	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	retval = ioctl(vpu_dec->vpu_fd, VIDIOC_G_FMT, &fmt);
	if (retval && errno == EAGAIN) {
		return -EAGAIN;
	}
	if (retval) {
		GST_ERROR("VIDIOC_G_FMT failed: %s\n", strerror(errno));
		return -errno;
	}

	GST_DEBUG("format: %d x %d\n", fmt.fmt.pix.width, fmt.fmt.pix.height);

	retval = ioctl(vpu_dec->vpu_fd, VIDIOC_REQBUFS, &reqs);
	if (retval) {
		GST_ERROR("VIDIOC_REQBUFS failed: %s\n", strerror(errno));
		return -errno;
	}

	for (i = 0; i < NUM_BUFFERS; i++) {
		struct v4l2_buffer *buf = &vpu_dec->buf_v4l2[i];
		buf->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf->memory = V4L2_MEMORY_MMAP;
		buf->index = i;

		vpu_dec->buf_data[i] = NULL;
		retval = ioctl(vpu_dec->vpu_fd, VIDIOC_QUERYBUF, buf);
		if (retval) {
			GST_ERROR("VIDIOC_QUERYBUF failed: %s\n", strerror(errno));
			return -errno;
		}
		vpu_dec->buf_size[i] = buf->length;
		vpu_dec->buf_data[i] = mmap(NULL, buf->length,
				   PROT_READ | PROT_WRITE, MAP_SHARED,
				   vpu_dec->vpu_fd, vpu_dec->buf_v4l2[i].m.offset);

		if(!vpu_dec->buf_data[i])
			GST_ERROR("MMAP failed: %s\n", strerror(errno));
	}

	for (i = 0; i < NUM_BUFFERS; ++i){
		retval = ioctl(vpu_dec->vpu_fd, VIDIOC_QBUF, &vpu_dec->buf_v4l2[i]);
		if (retval) {
			GST_ERROR("VIDIOC_QBUF failed: %s\n", strerror(errno));
			return -errno;
		}
	}

	gint fourcc = GST_STR_FOURCC("I420");

	vpu_dec->width = fmt.fmt.pix.width;
	vpu_dec->height = fmt.fmt.pix.height;

	GST_DEBUG("Dec InitialInfo => width: %u, height: %u",
			vpu_dec->width,
			vpu_dec->height);

	/* Padding the width and height to 16 */
	orgPicW = vpu_dec->width;
	orgPicH = vpu_dec->height;
	vpu_dec->width = (vpu_dec->width + 15) / 16 * 16;
	vpu_dec->height = (vpu_dec->height + 15) / 16 * 16;

	crop_top_len = 0;
	crop_left_len = 0;
	crop_right_len = vpu_dec->width - orgPicW;
	crop_bottom_len = vpu_dec->height - orgPicH;

	width = vpu_dec->width;
	height = vpu_dec->height;
	crop_right_by_pixel = (crop_bottom_len + 7) / 8 * 8;
	crop_bottom_by_pixel = crop_bottom_len;

	/* set the capabilites on the source pad */
	caps = gst_caps_new_simple("video/x-raw-yuv",
			"format", GST_TYPE_FOURCC, fourcc,
			"width", G_TYPE_INT, width,
			"height", G_TYPE_INT, height,
			"pixel-aspect-ratio", GST_TYPE_FRACTION, 1, 1,
			"crop-top-by-pixel", G_TYPE_INT, crop_top_len,
			"crop-left-by-pixel", G_TYPE_INT, (crop_left_len + 7) / 8 * 8,
			"crop-right-by-pixel", G_TYPE_INT, crop_right_by_pixel,
			"crop-bottom-by-pixel", G_TYPE_INT, crop_bottom_by_pixel,
			"framerate", GST_TYPE_FRACTION, vpu_dec->frame_rate_nu, vpu_dec->frame_rate_de,
			NULL);

	if (!(gst_pad_set_caps(vpu_dec->srcpad, caps)))
		GST_ERROR("Could not set the caps for the VPU decoder's src pad");
	gst_caps_unref(caps);

	vpu_dec->outsize = (vpu_dec->width * vpu_dec->height * 3) / 2;

	retval = ioctl(vpu_dec->vpu_fd, VIDIOC_STREAMON, &type);
	if (retval) {
		GST_ERROR("streamon failed with %d", retval);
		return -errno;
	}

	vpu_dec->init = TRUE;

	return 0;
}

static int vpu_dec_loop (GstVPU_Dec *vpu_dec)
{
	GstBuffer *pushbuff;
	int ret;
	struct v4l2_buffer v4l2_buf = {
		.type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
	};

	ret = ioctl(vpu_dec->vpu_fd, VIDIOC_DQBUF, &v4l2_buf);
	if (ret)
		return -errno;

	ret = gst_pad_alloc_buffer_and_set_caps(vpu_dec->srcpad, 0,
					      vpu_dec->outsize,
					      GST_PAD_CAPS(vpu_dec->srcpad),
					      &pushbuff);
	if (ret != GST_FLOW_OK) {
		GST_DEBUG_OBJECT(vpu_dec, "Allocating the Framebuffer[%d] failed with %d",
		     0, ret);
		goto done;
	}

	memcpy(GST_BUFFER_DATA(pushbuff), vpu_dec->buf_data[v4l2_buf.index], vpu_dec->outsize);
	ret = ioctl(vpu_dec->vpu_fd, VIDIOC_QBUF, &v4l2_buf);
	if (ret) {
		GST_DEBUG_OBJECT(vpu_dec, "Decoder qbuf failed?? error: %d\n", errno);
		return GST_FLOW_ERROR;
	}

	/* Update the time stamp based on the frame-rate */
	GST_BUFFER_SIZE(pushbuff) = vpu_dec->outsize;
//	GST_BUFFER_TIMESTAMP(pushbuff) = GST_TIMEVAL_TO_TIME(v4l2_buf.timestamp);
	GST_BUFFER_TIMESTAMP(pushbuff) = gst_util_uint64_scale(vpu_dec->decoded_frames,
			vpu_dec->frame_rate_de * GST_SECOND,
			vpu_dec->frame_rate_nu);

	vpu_dec->decoded_frames++;

	GST_DEBUG_OBJECT(vpu_dec, "frame decoded : %lld ts = %" GST_TIME_FORMAT,
			vpu_dec->decoded_frames,
			GST_TIME_ARGS(GST_BUFFER_TIMESTAMP(pushbuff)));

	ret = gst_pad_push(vpu_dec->srcpad, pushbuff);
	if (ret != GST_FLOW_OK) {
		GST_ERROR("Pushing the Output onto the Source Pad failed with %d", ret);
	}

	ret = 0;
done:
	return ret;
}

static GstFlowReturn
mfw_gst_vpudec_chain_stream_mode(GstPad * pad, GstBuffer *buffer)
{
	GstVPU_Dec *vpu_dec = MFW_GST_VPU_DEC(GST_PAD_PARENT(pad));
	int ret = 0;
	GstFlowReturn retval = GST_FLOW_OK;
	int remaining, ofs, handled = 0;
	struct pollfd pollfd;

	GST_DEBUG_OBJECT(vpu_dec, "frame input: ts = %" GST_TIME_FORMAT,
			GST_TIME_ARGS(GST_BUFFER_TIMESTAMP(buffer)));

	if (!vpu_dec->once) {
		if (vpu_dec->hdr_ext_data)
			buffer = gst_buffer_join(vpu_dec->hdr_ext_data, buffer);
		vpu_dec->once = 1;
	}

	pollfd.fd = vpu_dec->vpu_fd;
	pollfd.events = POLLIN | POLLOUT;

	remaining = GST_BUFFER_SIZE(buffer);
	ofs = 0;

	while (!handled) {
		ret = poll(&pollfd, 1, -1);
		if (ret < 0) {
			retval = GST_FLOW_ERROR;
			goto done;
		}

		if (pollfd.revents & POLLERR) {
			GST_DEBUG_OBJECT(vpu_dec, "POLLERR\n");
			retval = GST_FLOW_ERROR;
			goto done;
		}

		if ((pollfd.revents & POLLOUT) && buffer) {
			ret = write(vpu_dec->vpu_fd, GST_BUFFER_DATA(buffer) + ofs,
					remaining);
			if (ret == -1) {
				retval = GST_FLOW_ERROR;
				goto done;
			}

			remaining -= ret;
			ofs += ret;

			if (G_UNLIKELY(vpu_dec->init == FALSE)) {
				retval = mfw_gst_vpudec_vpu_init(vpu_dec);
				if (retval == -EAGAIN) {
					retval = GST_FLOW_OK;
					goto done;
				}
				if (retval) {
					GST_ERROR("mfw_gst_vpudec_vpu_init failed initializing VPU");
					retval = GST_FLOW_ERROR;
					goto done;
				}
			}
			if (!remaining)
				handled = 1;
		}

		if (pollfd.revents & POLLIN) {
			while (!vpu_dec_loop(vpu_dec));
			handled = 1;
		}
	}
done:
	gst_buffer_unref(buffer);

	return retval;
}

static gboolean
mfw_gst_vpudec_sink_event(GstPad * pad, GstEvent * event)
{
	GstVPU_Dec *vpu_dec = MFW_GST_VPU_DEC(GST_PAD_PARENT(pad));
	gboolean result = TRUE;
	GstFormat format;
	gint64 start, stop, position;
	gdouble rate;

	switch (GST_EVENT_TYPE(event)) {
	case GST_EVENT_NEWSEGMENT:
		gst_event_parse_new_segment(event, NULL, &rate, &format,
					    &start, &stop, &position);
		GST_DEBUG_OBJECT(vpu_dec, "receiving new seg start = %" GST_TIME_FORMAT
			  " stop = %" GST_TIME_FORMAT
			  " position in mpeg4  =%" GST_TIME_FORMAT,
				GST_TIME_ARGS(start),
				GST_TIME_ARGS(stop),
				GST_TIME_ARGS(position));
		if (GST_FORMAT_TIME == format) {
			result = gst_pad_push_event(vpu_dec->srcpad, event);
			if (TRUE != result) {
				GST_ERROR("Error in pushing the event, result is %d", result);
			}
		}
		break;
	case GST_EVENT_FLUSH_STOP:
		/* The below block of code is used to Flush the buffered input stream data */
		GST_DEBUG_OBJECT(vpu_dec, "GST_EVENT_FLUSH_STOP: not handled\n");

		result = gst_pad_push_event(vpu_dec->srcpad, event);
		if (TRUE != result) {
			GST_DEBUG_OBJECT(vpu_dec, "Error in pushing the event,result is %d", result);
			gst_event_unref(event);
		}
		break;
	case GST_EVENT_EOS:
		write(vpu_dec->vpu_fd, NULL, 0);
		GST_DEBUG_OBJECT(vpu_dec, "GST_EVENT_EOS: handled\n");
		result = gst_pad_push_event(vpu_dec->srcpad, event);
		if (TRUE != result)
			GST_DEBUG_OBJECT(vpu_dec, "Error in pushing the event,result is %d", result);
		break;
	case GST_EVENT_FLUSH_START:
		if (vpu_dec->state == GST_STATE_PLAYING) {
			while (1) {
				result = vpu_dec_loop(vpu_dec);
				if (result && result != -EAGAIN)
					break;
			}
		}

		GST_DEBUG_OBJECT(vpu_dec, "GST_EVENT_FLUSH_START: handled\n");
		result = gst_pad_push_event(vpu_dec->srcpad, event);
		if (TRUE != result) {
			GST_DEBUG_OBJECT(vpu_dec, "Error in pushing the event,result is %d", result);
			gst_event_unref(event);
		}
		break;
	default:
		result = gst_pad_event_default(pad, event);
		break;
	}

	return result;
}

static GstStateChangeReturn
mfw_gst_vpudec_change_state(GstElement * element, GstStateChange transition)
{
	GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
	GstVPU_Dec *vpu_dec = MFW_GST_VPU_DEC(element);
	GstState state, next;
	int retval, i;

	state = (GstState) GST_STATE_TRANSITION_CURRENT (transition);
	next = GST_STATE_TRANSITION_NEXT (transition);

	GST_DEBUG_OBJECT(vpu_dec, "%s: from %s to %s\n", __func__, 
			gst_element_state_get_name (state),
			gst_element_state_get_name (next));

	switch (transition) {
	case GST_STATE_CHANGE_NULL_TO_READY:
		vpu_dec->vpu_fd = open(vpu_dec->device, O_RDWR | O_NONBLOCK);
		if (vpu_dec->vpu_fd < 0) {
			GST_ERROR("opening %s failed", vpu_dec->device);
			return GST_STATE_CHANGE_FAILURE;
		}

#define MFW_GST_VPU_DECODER_PLUGIN VERSION
		PRINT_PLUGIN_VERSION(MFW_GST_VPU_DECODER_PLUGIN);
		break;
	case GST_STATE_CHANGE_READY_TO_PAUSED:
		vpu_dec->init = FALSE;
		break;
	default:
		break;
	}

	ret = vpu_dec->parent_class->change_state(element, transition);

	switch (transition) {
	case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
		break;
	case GST_STATE_CHANGE_PAUSED_TO_READY:
		vpu_dec->decoded_frames=0;
		break;
	case GST_STATE_CHANGE_READY_TO_NULL:
		for (i = 0; i < NUM_BUFFERS; ++i){
			struct v4l2_buffer *buf = &vpu_dec->buf_v4l2[i];
			retval = munmap(vpu_dec->buf_data[i], buf->length);
			if (retval) {
				GST_ERROR("VIDIOC_QBUF munmap failed: %s\n", strerror(errno));
				return -errno;
			}
		}
		retval = close(vpu_dec->vpu_fd);
		if(retval)
			GST_ERROR("closing filedesriptor error: %d\n", errno);
		break;
	default:
		break;
	}

	vpu_dec->state = next;

	return ret;

}

static GstPadTemplate *src_templ(void)
{
	static GstPadTemplate *templ = NULL;
	GstCaps *caps;
	GstStructure *structure;
	GValue list = { 0 }, fmt = {0};

	char *fmts[] = { "YV12", "I420", "Y42B", "NV12", NULL };
	guint n;
	caps = gst_caps_new_simple("video/x-raw-yuv",
				   "format", GST_TYPE_FOURCC,
				   GST_MAKE_FOURCC('I', '4', '2', '0'),
				   "width", GST_TYPE_INT_RANGE, 16, 4096,
				   "height", GST_TYPE_INT_RANGE, 16, 4096,
				   "framerate", GST_TYPE_FRACTION_RANGE, 1, G_MAXINT, G_MAXINT, 1,
				   NULL);

	structure = gst_caps_get_structure(caps, 0);

	g_value_init(&list, GST_TYPE_LIST);
	g_value_init(&fmt, GST_TYPE_FOURCC);
	for (n = 0; fmts[n] != NULL; n++) {
		gst_value_set_fourcc(&fmt, GST_STR_FOURCC(fmts[n]));
		gst_value_list_append_value(&list, &fmt);
	}
	gst_structure_set_value(structure, "format", &list);
	g_value_unset(&list);
	g_value_unset(&fmt);

	templ = gst_pad_template_new("src", GST_PAD_SRC, GST_PAD_ALWAYS, caps);
	return templ;
}

static gboolean
mfw_gst_vpudec_setcaps(GstPad * pad, GstCaps * caps)
{
	GstVPU_Dec *vpu_dec = NULL;
	const gchar *mime;
	GstStructure *structure = gst_caps_get_structure(caps, 0);
	vpu_dec = MFW_GST_VPU_DEC(gst_pad_get_parent(pad));
	mime = gst_structure_get_name(structure);
	GValue *codec_data;
	guint8 *hdrextdata;
	guint i = 0;

	if (strcmp(mime, "video/x-h264") == 0)
		vpu_dec->codec = STD_AVC;
	else if (strcmp(mime, "video/mpeg") == 0)
		vpu_dec->codec = STD_MPEG4;
	else if (strcmp(mime, "video/x-divx") == 0)
		vpu_dec->codec = STD_MPEG4;
	else if (strcmp(mime, "video/x-h263") == 0)
		vpu_dec->codec = STD_H263;
	else {
		GST_ERROR(" Codec Standard not supporded");
		return FALSE;
	}

	ioctl(vpu_dec->vpu_fd, VPU_IOC_CODEC, vpu_dec->codec);

	gst_structure_get_fraction(structure, "framerate",
			&vpu_dec->frame_rate_nu, &vpu_dec->frame_rate_de);

	if (vpu_dec->frame_rate_de != 0)
		vpu_dec->frame_rate =
			((gfloat) vpu_dec->frame_rate_nu /
			 vpu_dec->frame_rate_de);

	gst_structure_get_int(structure, "width", &vpu_dec->width);
	gst_structure_get_int(structure, "height", &vpu_dec->height);

	GST_DEBUG("Frame Rate = %f, Input width = %d, Input height = %d",
			vpu_dec->frame_rate,
			vpu_dec->width,
			vpu_dec->height);

	codec_data = (GValue *) gst_structure_get_value(structure, "codec_data");
	if (codec_data) {
		vpu_dec->hdr_ext_data = gst_value_get_buffer(codec_data);
		vpu_dec->hdr_ext_data_len = GST_BUFFER_SIZE(vpu_dec->hdr_ext_data);
		GST_DEBUG("Codec specific data length is %d", vpu_dec->hdr_ext_data_len);
		GST_DEBUG("Header Extension Data is");
		hdrextdata = GST_BUFFER_DATA(vpu_dec->hdr_ext_data);
		for (i = 0; i < vpu_dec->hdr_ext_data_len; i++)
			GST_DEBUG("%02x ", hdrextdata[i]);
	}

	gst_object_unref(vpu_dec);
	return gst_pad_set_caps(pad, caps);
}

static void
mfw_gst_vpudec_base_init(GstVPU_DecClass * klass)
{

	GstElementClass *element_class = GST_ELEMENT_CLASS(klass);

	gst_element_class_add_pad_template(element_class, src_templ());

	gst_element_class_add_pad_template(element_class,
					   gst_static_pad_template_get
					   (&mfw_gst_vpudec_sink_factory));

	gst_element_class_set_details(element_class, &mfw_gst_vpudec_details);

}

GType
mfw_gst_vpudec_mirror_get_type(void)
{
	static GType vpudec_mirror_type = 0;
	static GEnumValue vpudec_mirror[] = {
		{MIRDIR_NONE, "0", "none"},
		{MIRDIR_VER, "1", "ver"},
		{MIRDIR_HOR, "2", "hor"},
		{MIRDIR_HOR_VER, "3", "hor_ver"},
		{0, NULL, NULL},
	};
	if (!vpudec_mirror_type) {
		vpudec_mirror_type =
		    g_enum_register_static("GstVpuDecMirror", vpudec_mirror);
	}
	return vpudec_mirror_type;
}

static void
mfw_gst_vpudec_class_init(GstVPU_DecClass * klass)
{

	GObjectClass *gobject_class = NULL;
	GstElementClass *gstelement_class = NULL;
	gobject_class = (GObjectClass *) klass;
	gstelement_class = (GstElementClass *) klass;
	gstelement_class->change_state = mfw_gst_vpudec_change_state;
	gobject_class->set_property = mfw_gst_vpudec_set_property;
	gobject_class->get_property = mfw_gst_vpudec_get_property;

	mfw_gst_vpu_class_init_common(gobject_class);

	g_object_class_install_property(gobject_class, MFW_GST_VPU_ROTATION,
					g_param_spec_uint("rotation",
							  "Rotation",
							  "Rotation Angle should be 0, 90, 180 or 270.",
							  0, 270, 0,
							  G_PARAM_READWRITE));

	g_object_class_install_property(gobject_class, MFW_GST_VPU_MIRROR,
					g_param_spec_enum("mirror-dir",
							  "mirror_dir",
							  "specifies mirror direction",
							  MFW_GST_TYPE_VPU_DEC_MIRROR,
							  MIRDIR_NONE,
							  G_PARAM_READWRITE));

	g_object_class_install_property(gobject_class, MFW_GST_VPU_DBK_ENABLE,
					g_param_spec_boolean("dbkenable",
							     "dbkenable",
							     "enables the decoder plug-in deblock",
							     FALSE,
							     G_PARAM_READWRITE));

	g_object_class_install_property(gobject_class, MFW_GST_VPU_DBK_OFFSETA,
					g_param_spec_int("dbk-offseta",
							 "dbk_offseta",
							 "set the deblock offset a",
							 G_MININT, G_MAXINT, 5,
							 G_PARAM_READWRITE));

	g_object_class_install_property(gobject_class, MFW_GST_VPU_DBK_OFFSETB,
					g_param_spec_int("dbk-offsetb",
							 "dbk_offsetb",
							 "set the deblock offset b",
							 G_MININT, G_MAXINT, 5,
							 G_PARAM_READWRITE));

}

static void
mfw_gst_vpudec_init(GstVPU_Dec * vpu_dec, GstVPU_DecClass * gclass)
{

	GstElementClass *klass = GST_ELEMENT_GET_CLASS(vpu_dec);
	/* create the sink and src pads */
	vpu_dec->sinkpad =
	    gst_pad_new_from_template(gst_element_class_get_pad_template
				      (klass, "sink"), "sink");
	vpu_dec->srcpad = gst_pad_new_from_template(src_templ(), "src");
	gst_element_add_pad(GST_ELEMENT(vpu_dec), vpu_dec->sinkpad);
	gst_element_add_pad(GST_ELEMENT(vpu_dec), vpu_dec->srcpad);
	vpu_dec->parent_class = g_type_class_peek_parent(gclass);

	gst_pad_set_chain_function(vpu_dec->sinkpad,
				   mfw_gst_vpudec_chain_stream_mode);
	gst_pad_set_setcaps_function(vpu_dec->sinkpad, mfw_gst_vpudec_setcaps);
	gst_pad_set_event_function(vpu_dec->sinkpad,
				   GST_DEBUG_FUNCPTR
				   (mfw_gst_vpudec_sink_event));

	vpu_dec->rotation_angle = 0;
	vpu_dec->mirror_dir = MIRDIR_NONE;
	vpu_dec->codec = STD_AVC;
	vpu_dec->device = g_strdup(VPU_DEVICE);

	vpu_dec->dbk_enabled = FALSE;
	vpu_dec->dbk_offset_a = vpu_dec->dbk_offset_b = DEFAULT_DBK_OFFSET_VALUE;
}

GType
mfw_gst_type_vpu_dec_get_type(void)
{
	static GType vpu_dec_type = 0;
	if (!vpu_dec_type) {
		static const GTypeInfo vpu_dec_info = {
			sizeof (GstVPU_DecClass),
			(GBaseInitFunc) mfw_gst_vpudec_base_init,
			NULL,
			(GClassInitFunc) mfw_gst_vpudec_class_init,
			NULL,
			NULL,
			sizeof (GstVPU_Dec),
			0,
			(GInstanceInitFunc) mfw_gst_vpudec_init,
		};
		vpu_dec_type = g_type_register_static(GST_TYPE_ELEMENT,
						      "GstVPU_Dec",
						      &vpu_dec_info, 0);
	}
	GST_DEBUG_CATEGORY_INIT(mfw_gst_vpudec_debug,
				"vpudecoder", 0,
				"FreeScale's VPU  Decoder's Log");
	return vpu_dec_type;
}
