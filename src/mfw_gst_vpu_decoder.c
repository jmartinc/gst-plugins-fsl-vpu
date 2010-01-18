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
 * Module Name:    mfw_gst_vpu_decoder.c
 *
 * Description:    Implementation of Hardware (VPU) Decoder Plugin for Gstreamer.
 *
 * Portability:    This code is written for Linux OS and Gstreamer
 */

/*
 * Changelog:
 *
 */

/*======================================================================================
                            INCLUDE FILES
=======================================================================================*/
#include <string.h>
#include <fcntl.h>		/* fcntl */
#include <unistd.h>
#include <sys/mman.h>		/* mmap */
#include <sys/ioctl.h>		/* fopen/fread */
#include <sys/time.h>
#include <gst/gst.h>
#include <gst/base/gstadapter.h>
#include <gst-plugins-fsl_config.h>
#include "vpu_io.h"
#include "vpu_lib.h"
#include "mfw_gst_vpu_decoder.h"
#include "mfw_gst_utils.h"

/*======================================================================================
                                     LOCAL CONSTANTS
=======================================================================================*/

#define BUFF_FILL_SIZE (512 * 1024)
#define PS_SAVE_SIZE		0x028000
#define SLICE_SAVE_SIZE		0x02D800
#define MIN_WIDTH       64
#define MIN_HEIGHT      64

#define PROCESSOR_CLOCK    333

#ifdef VPU_MX51
#define MFW_GST_VPUDEC_VIDEO_CAPS \
    "video/mpeg, " \
    "width = (int) [16, 1280], " \
    "height = (int) [16, 720], " \
    "mpegversion = (int) 4; " \
    \
    "video/x-h263, " \
    "width = (int) [16, 1280], " \
    "height = (int)[16, 720]; " \
    \
    "video/x-h264, " \
    "width = (int) [16, 1280], " \
    "height = (int)[16, 720]; " \
     \
    "video/x-wmv, " \
    "wmvversion = (int) 3, " \
    "width = (int) [16, 1280], " \
    "height = (int)[16, 720]; " \
    \
    "video/mp2v, "  \
    "mpegversion = (int) [1,2] , "  \
    "systemstream = (boolean) false;"\
    \
    "image/jpeg, "  \
    "width = (int) [16, 1280], " \
    "height = (int)[16, 720]"
#elif defined(VPU_MX37)
#define MFW_GST_VPUDEC_VIDEO_CAPS \
    "video/mpeg, " \
    "width = (int) [16, 1280], " \
    "height = (int) [16, 720], " \
    "mpegversion = (int) 4; " \
    \
    "video/x-h263, " \
    "width = (int) [16, 1280], " \
    "height = (int)[16, 720]; " \
    \
    "video/x-h264, " \
    "width = (int) [16, 1280], " \
    "height = (int)[16, 720]; " \
     \
    "video/x-wmv, " \
    "wmvversion = (int) 3, " \
    "width = (int) [16, 1280], " \
    "height = (int)[16, 720]; " \
    \
    "video/mp2v, "  \
    "mpegversion = (int) [1,2] , "  \
    "systemstream = (boolean) false"
#else

#define MFW_GST_VPUDEC_VIDEO_CAPS \
    "video/mpeg, " \
    "width = (int) [16, 576], " \
    "height = (int) [16, 720], " \
    "mpegversion = (int) 4; " \
    \
    "video/x-h263, " \
    "width = (int) [16, 576], " \
    "height = (int)[16, 720]; " \
    \
    "video/x-h264, " \
    "width = (int) [16, 576], " \
    "height = (int)[16, 720]"

#define STD_MPEG2 -1
#define STD_VC1   -1

#endif

#define DEFAULT_DBK_OFFSET_VALUE    5

/*======================================================================================
                          STATIC TYPEDEFS (STRUCTURES, UNIONS, ENUMS)
=======================================================================================*/

enum {
	MFW_GST_VPU_PROP_0,
	MFW_GST_VPU_CODEC_TYPE,
	MFW_GST_VPU_PROF_ENABLE,
	MFW_GST_VPU_DBK_ENABLE,
	MFW_GST_VPU_DBK_OFFSETA,
	MFW_GST_VPU_DBK_OFFSETB,
	MFW_GST_VPU_LOOPBACK,
	MFW_GST_VPU_ROTATION,
	MFW_GST_VPU_MIRROR,
};

/* get the element details */
#ifdef VPU_MX27
static GstElementDetails mfw_gst_vpudec_details =
GST_ELEMENT_DETAILS("Freescale: Hardware (VPU) Decoder",
		    "Codec/Decoder/Video",
		    "Decodes H.264, MPEG4, H263 "
		    "Elementary data into YUV 4:2:0 data",
		    "i.MX series");
#else
static GstElementDetails mfw_gst_vpudec_details =
GST_ELEMENT_DETAILS("Freescale: Hardware (VPU) Decoder",
		    "Codec/Decoder/Video",
		    "Decodes H.264, MPEG4, H263, and VC-1 "
		    "Elementary data into YUV 4:2:0 data",
		    "i.MX series");
#endif

/* defines sink pad  properties of the VPU Decoder element */
static GstStaticPadTemplate mfw_gst_vpudec_sink_factory =
GST_STATIC_PAD_TEMPLATE("sink",
			GST_PAD_SINK,
			GST_PAD_ALWAYS,
			GST_STATIC_CAPS(MFW_GST_VPUDEC_VIDEO_CAPS));

/* table with framerates expressed as fractions */
static const gint fpss[][2] = { {24000, 1001},
{24, 1}, {25, 1}, {30000, 1001},
{30, 1}, {50, 1}, {60000, 1001},
{60, 1}, {0, 1}
};

/*======================================================================================
                                        LOCAL MACROS
=======================================================================================*/

#define	GST_CAT_DEFAULT	mfw_gst_vpudec_debug

/*======================================================================================
                                      STATIC VARIABLES
=======================================================================================*/
extern int vpu_fd;

/*======================================================================================
                                 STATIC FUNCTION PROTOTYPES
=======================================================================================*/

GST_DEBUG_CATEGORY_STATIC(mfw_gst_vpudec_debug);

static void mfw_gst_vpudec_class_init(MfwGstVPU_DecClass *);
static void mfw_gst_vpudec_base_init(MfwGstVPU_DecClass *);
static void mfw_gst_vpudec_init(MfwGstVPU_Dec *, MfwGstVPU_DecClass *);
static GstFlowReturn mfw_gst_vpudec_chain_stream_mode(GstPad *, GstBuffer *);
#ifndef VPU_MX27
static GstFlowReturn mfw_gst_vpudec_chain_file_mode(GstPad *, GstBuffer *);
#endif
static GstStateChangeReturn mfw_gst_vpudec_change_state(GstElement *,
							GstStateChange);
static void mfw_gst_vpudec_set_property(GObject *, guint, const GValue *,
					GParamSpec *);
static void mfw_gst_vpudec_get_property(GObject *, guint, GValue *,
					GParamSpec *);
static gint mfw_gst_vpudec_FrameBufferInit(MfwGstVPU_Dec *, FrameBuffer *,
					   gint);
static gboolean mfw_gst_vpudec_sink_event(GstPad *, GstEvent *);
static gboolean mfw_gst_vpudec_setcaps(GstPad *, GstCaps *);
/*======================================================================================
                                     GLOBAL VARIABLES
=======================================================================================*/
/*======================================================================================
                                     LOCAL FUNCTIONS
=======================================================================================*/

/* helper function for float comaprision with 0.00001 precision */
#define FLOAT_MATCH 1
#define FLOAT_UNMATCH 0
static inline guint
g_compare_float(const gfloat a, const gfloat b)
{
	const gfloat precision = 0.00001;
	if (((a - precision) < b) && (a + precision) > b)
		return FLOAT_MATCH;
	else
		return FLOAT_UNMATCH;
}

/*=============================================================================
FUNCTION:           mfw_gst_vpudec_set_property

DESCRIPTION:        Sets the property of the element

ARGUMENTS PASSED:
        object     - pointer to the elements object
        prop_id    - ID of the property;
        value      - value of the property set by the application
        pspec      - pointer to the attributes of the property

RETURN VALUE:       None
PRE-CONDITIONS:     None
POST-CONDITIONS:    None
IMPORTANT NOTES:    None
=============================================================================*/

static void
mfw_gst_vpudec_set_property(GObject * object, guint prop_id,
			    const GValue * value, GParamSpec * pspec)
{
	MfwGstVPU_Dec *vpu_dec = MFW_GST_VPU_DEC(object);
	switch (prop_id) {
	case MFW_GST_VPU_PROF_ENABLE:
		vpu_dec->profiling = g_value_get_boolean(value);
		GST_DEBUG("profiling=%d\n", vpu_dec->profiling);
		break;

	case MFW_GST_VPU_CODEC_TYPE:
		vpu_dec->codec = g_value_get_enum(value);
		GST_DEBUG("codec=%d\n", vpu_dec->codec);
		break;

	case MFW_GST_VPU_LOOPBACK:
		vpu_dec->loopback = g_value_get_boolean(value);
		GST_DEBUG("loopback=%d\n", vpu_dec->loopback);
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
		GST_DEBUG("mirror_direction=%d\n", vpu_dec->mirror_dir);
		break;

	case MFW_GST_VPU_ROTATION:
		vpu_dec->rotation_angle = g_value_get_uint(value);
		switch (vpu_dec->rotation_angle) {
		case 0:
		case 90:
		case 180:
		case 270:
			GST_DEBUG("rotation angle=%d\n",
				  vpu_dec->rotation_angle);
			break;
		default:
			vpu_dec->rotation_angle = 0;
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id,
							  pspec);
			break;
		}
		break;

	default:		// else rotation will fall through with invalid parameter
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
	return;
}

/*=============================================================================
FUNCTION:           mfw_gst_vpudec_set_property

DESCRIPTION:        Gets the property of the element

ARGUMENTS PASSED:
        object     - pointer to the elements object
        prop_id    - ID of the property;
        value      - value of the property set by the application
        pspec      - pointer to the attributes of the property

RETURN VALUE:       None
PRE-CONDITIONS:     None
POST-CONDITIONS:    None
IMPORTANT NOTES:    None
=============================================================================*/
static void
mfw_gst_vpudec_get_property(GObject * object, guint prop_id,
			    GValue * value, GParamSpec * pspec)
{

	MfwGstVPU_Dec *vpu_dec = MFW_GST_VPU_DEC(object);
	switch (prop_id) {
	case MFW_GST_VPU_PROF_ENABLE:
		g_value_set_boolean(value, vpu_dec->profiling);
		break;
	case MFW_GST_VPU_CODEC_TYPE:
		g_value_set_enum(value, vpu_dec->codec);
		break;
	case MFW_GST_VPU_LOOPBACK:
		g_value_set_boolean(value, vpu_dec->loopback);
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

/*=============================================================================
FUNCTION:           mfw_gst_vpudec_post_fatal_error_msg

DESCRIPTION:        This function is used to post a fatal error message and
                    terminate the pipeline during an unrecoverable error.

ARGUMENTS PASSED:   vpu_dec  - VPU decoder plugins context error_msg message to be posted

RETURN VALUE:       None
PRE-CONDITIONS:     None
POST-CONDITIONS:    None
IMPORTANT NOTES:    None
=============================================================================*/

static void
mfw_gst_vpudec_post_fatal_error_msg(MfwGstVPU_Dec * vpu_dec, gchar * error_msg)
{
	GError *error = NULL;
	GQuark domain;
	domain = g_quark_from_string("mfw_vpudecoder");
	error = g_error_new(domain, 10, "fatal error");
	gst_element_post_message(GST_ELEMENT(vpu_dec),
				 gst_message_new_error(GST_OBJECT
						       (vpu_dec), error,
						       error_msg));
	g_error_free(error);
}

static void
vpu_mutex_lock(GMutex * mutex)
{
	return;
	GST_DEBUG("VPU mutex locked. +++\n");
	g_mutex_lock(mutex);
}

static void
vpu_mutex_unlock(GMutex * mutex)
{
	return;
	GST_DEBUG("VPU mutex unlocked. ---\n");
	g_mutex_unlock(mutex);
}

/*=============================================================================
FUNCTION:           mfw_gst_VC1_Create_RCVheader

DESCRIPTION:        This function is used to create the RCV header
                    for integration with the ASF demuxer using the width,height and the
                    Header Extension data recived through caps negotiation.

ARGUMENTS PASSED:   vpu_dec  - VPU decoder plugins context

RETURN VALUE:       GstBuffer
PRE-CONDITIONS:     None
POST-CONDITIONS:    None
IMPORTANT NOTES:    None
=============================================================================*/

static GstBuffer *
mfw_gst_VC1_Create_RCVheader(MfwGstVPU_Dec * vpu_dec, GstBuffer * inbuffer)
{
	GstBuffer *RCVHeader = NULL;
	unsigned char *RCVHeaderData = NULL;
	unsigned int value = 0;
	int i = 0;

#define RCV_HEADER_LEN  24
	RCVHeader = gst_buffer_new_and_alloc(RCV_HEADER_LEN);
	RCVHeaderData = GST_BUFFER_DATA(RCVHeader);

	//Number of Frames, Header Extension Bit, Codec Version
	value = NUM_FRAMES | SET_HDR_EXT | CODEC_VERSION;
	RCVHeaderData[i++] = (unsigned char) value;
	RCVHeaderData[i++] = (unsigned char) (value >> 8);
	RCVHeaderData[i++] = (unsigned char) (value >> 16);
	RCVHeaderData[i++] = (unsigned char) (value >> 24);
	//Header Extension Size
	//ASF Parser gives 5 bytes whereas the VPU expects only 4 bytes, so limiting it
	if (vpu_dec->HdrExtDataLen > 4)
		vpu_dec->HdrExtDataLen = 4;
	RCVHeaderData[i++] = (unsigned char) vpu_dec->HdrExtDataLen;
	RCVHeaderData[i++] = (unsigned char) (vpu_dec->HdrExtDataLen >> 8);
	RCVHeaderData[i++] = (unsigned char) (vpu_dec->HdrExtDataLen >> 16);
	RCVHeaderData[i++] = (unsigned char) (vpu_dec->HdrExtDataLen >> 24);

	//Header Extension bytes obtained during negotiation
	memcpy(RCVHeaderData + i, GST_BUFFER_DATA(vpu_dec->HdrExtData)
	       , vpu_dec->HdrExtDataLen);
	i += vpu_dec->HdrExtDataLen;
	//Height
	RCVHeaderData[i++] = (unsigned char) vpu_dec->picHeight;
	RCVHeaderData[i++] =
	    (unsigned char) (((vpu_dec->picHeight >> 8) & 0xff));
	RCVHeaderData[i++] =
	    (unsigned char) (((vpu_dec->picHeight >> 16) & 0xff));
	RCVHeaderData[i++] =
	    (unsigned char) (((vpu_dec->picHeight >> 24) & 0xff));
	//Width
	RCVHeaderData[i++] = (unsigned char) vpu_dec->picWidth;
	RCVHeaderData[i++] =
	    (unsigned char) (((vpu_dec->picWidth >> 8) & 0xff));
	RCVHeaderData[i++] =
	    (unsigned char) (((vpu_dec->picWidth >> 16) & 0xff));
	RCVHeaderData[i++] =
	    (unsigned char) (((vpu_dec->picWidth >> 24) & 0xff));
	//Frame Size
	RCVHeaderData[i++] = (unsigned char) GST_BUFFER_SIZE(inbuffer);
	RCVHeaderData[i++] = (unsigned char) (GST_BUFFER_SIZE(inbuffer) >> 8);
	RCVHeaderData[i++] = (unsigned char) (GST_BUFFER_SIZE(inbuffer) >> 16);
	RCVHeaderData[i++] =
	    (unsigned char) ((GST_BUFFER_SIZE(inbuffer) >> 24) | 0x80);

	return RCVHeader;
}

/*=============================================================================
FUNCTION:           mfw_gst_vpudec_FrameBufferClose

DESCRIPTION:        This function frees the allocated frame buffers

ARGUMENTS PASSED:   vpu_dec  - VPU decoder plugins context

RETURN VALUE:       None
PRE-CONDITIONS:     None
POST-CONDITIONS:    None
IMPORTANT NOTES:    None
=============================================================================*/

static void
mfw_gst_vpudec_FrameBufferClose(MfwGstVPU_Dec * vpu_dec)
{
	gint i;

	for (i = 0; i < vpu_dec->numframebufs; i++) {
		if (vpu_dec->frame_mem[i].phy_addr != 0) {
			IOFreePhyMem(&vpu_dec->frame_mem[i]);
			IOFreeVirtMem(&vpu_dec->frame_mem[i]);
			vpu_dec->frame_mem[i].phy_addr = 0;
			vpu_dec->frame_virt[i] = NULL;
		}
	}
}

static gboolean
mfw_gst_get_timestamp(MfwGstVPU_Dec * vpu_dec, GstClockTime * ptimestamp)
{
	gboolean found = FALSE;
	guint i = vpu_dec->ts_tx;
	guint index = 0;
	GstClockTime timestamp = 0;
	while (i != vpu_dec->ts_rx) {
		if (found) {
			if (vpu_dec->timestamp_buffer[i] < timestamp) {
				timestamp = vpu_dec->timestamp_buffer[i];
				index = i;
			}
		} else {
			timestamp = vpu_dec->timestamp_buffer[i];
			index = i;
			found = TRUE;
		}
		i = ((i + 1) % MAX_STREAM_BUF);
	}
	if (found) {
		if (index != vpu_dec->ts_tx) {
			vpu_dec->timestamp_buffer[index] =
			    vpu_dec->timestamp_buffer[vpu_dec->ts_tx];
		}
		vpu_dec->ts_tx = (vpu_dec->ts_tx + 1) % MAX_STREAM_BUF;
		*ptimestamp = timestamp;
		return TRUE;
	} else {
		return FALSE;
	}
}

/*=============================================================================
FUNCTION:           mfw_gst_vpudec_FrameBufferInit

DESCRIPTION:        This function allocates the outbut buffer for the decoder

ARGUMENTS PASSED:   vpu_dec  - VPU decoder plugins context
                    frameBuf - VPU's Output Frame Buffer to be
                                   allocated.

                    num_buffers number of frame buffers to be allocated

RETURN VALUE:       0 (SUCCESS)/ -1 (FAILURE)
PRE-CONDITIONS:     None
POST-CONDITIONS:    None
IMPORTANT NOTES:    None
=============================================================================*/

static gint
mfw_gst_vpudec_FrameBufferInit(MfwGstVPU_Dec * vpu_dec,
			       FrameBuffer * frameBuf, gint num_buffers)
{

	gint i = 0;
	GstFlowReturn retval = GST_FLOW_OK;
	GstBuffer *outbuffer = NULL;
	guint strideY = 0, height = 0;
#if defined (VPU_MX37) || defined (VPU_MX51)
	gint mvsize;
#endif
	strideY = vpu_dec->initialInfo->picWidth;
	height = vpu_dec->initialInfo->picHeight;
#if defined (VPU_MX37) || defined (VPU_MX51)
	mvsize = strideY * height / 4;
#endif

	for (i = 0; i < num_buffers; i++) {
		retval = gst_pad_alloc_buffer_and_set_caps(vpu_dec->srcpad, 0,
							   vpu_dec->outsize,
							   GST_PAD_CAPS
							   (vpu_dec->srcpad),
							   &outbuffer);

		if (retval != GST_FLOW_OK) {
			GST_ERROR("Error in allocating the Framebuffer[%d],"
				  " error is %d", i, retval);
			return -1;
		}

		/* if the buffer allocated is the Hardware Buffer use it as it is */
		if (GST_BUFFER_FLAG_IS_SET(outbuffer, GST_BUFFER_FLAG_LAST) ==
		    TRUE) {
#if defined (VPU_MX37) || defined (VPU_MX51)
			vpu_dec->frame_mem[i].size = mvsize;
			IOGetPhyMem(&vpu_dec->frame_mem[i]);
			frameBuf[i].bufMvCol = vpu_dec->frame_mem[i].phy_addr;
#endif
			vpu_dec->outbuffers[i] = outbuffer;
			GST_BUFFER_SIZE(vpu_dec->outbuffers[i]) = vpu_dec->outsize;
			GST_BUFFER_OFFSET_END(vpu_dec->outbuffers[i]) = i;
			vpu_dec->fb_state_plugin[i] = FB_STATE_ALLOCTED;

			frameBuf[i].bufY = GST_BUFFER_OFFSET(outbuffer);
			frameBuf[i].bufCb = frameBuf[i].bufY + (strideY * height);
			frameBuf[i].bufCr = frameBuf[i].bufCb + ((strideY / 2) * (height / 2));
			vpu_dec->direct_render = TRUE;
		}
		/* else allocate The Hardware buffer through IOGetPhyMem
		   Note this to support writing the output to a file in case of
		   File Sink */
		else {
			if (outbuffer != NULL) {
				gst_buffer_unref(outbuffer);
				outbuffer = NULL;
			}
#if defined (VPU_MX37) || defined (VPU_MX51)
			vpu_dec->frame_mem[i].size = vpu_dec->outsize + mvsize;
#else
			vpu_dec->frame_mem[i].size = vpu_dec->outsize;
#endif
			IOGetPhyMem(&vpu_dec->frame_mem[i]);
			if (vpu_dec->frame_mem[i].phy_addr == 0) {
				gint j;
				for (j = 0; j < i; j++) {
					IOFreeVirtMem(&vpu_dec->frame_mem[i]);
					IOFreePhyMem(&vpu_dec->frame_mem[i]);
				}
				GST_ERROR("No enough mem for framebuffer!\n");
				return -1;
			}
			frameBuf[i].bufY = vpu_dec->frame_mem[i].phy_addr;
			frameBuf[i].bufCb =
			    frameBuf[i].bufY + (strideY * height);
			frameBuf[i].bufCr =
			    frameBuf[i].bufCb + ((strideY / 2) * (height / 2));
#if defined (VPU_MX37) || defined (VPU_MX51)
			frameBuf[i].bufMvCol =
			    frameBuf[i].bufCr + ((strideY / 2) * (height / 2));
#endif
			vpu_dec->frame_virt[i] =
			    (guint8 *) IOGetVirtMem(&vpu_dec->frame_mem[i]);
			vpu_dec->direct_render = FALSE;
		}
	}
	return 0;
}

/*======================================================================================
FUNCTION:           mfw_gst_vpudec_vpu_open

DESCRIPTION:        Open VPU

ARGUMENTS PASSED:   vpu_dec  - VPU decoder plugins context

RETURN VALUE:       GstFlowReturn - Success of Failure.
PRE-CONDITIONS:     None
POST-CONDITIONS:    None
IMPORTANT NOTES:    None
=======================================================================================*/

GstFlowReturn
mfw_gst_vpudec_vpu_open(MfwGstVPU_Dec * vpu_dec)
{
	RetCode vpu_ret = RETCODE_SUCCESS;
	guint8 *virt_bit_stream_buf = NULL;

	GST_DEBUG("codec=%d\n", vpu_dec->codec);
	vpu_dec->bit_stream_buf.size = BUFF_FILL_SIZE;
	IOGetPhyMem(&vpu_dec->bit_stream_buf);
	virt_bit_stream_buf = (guint8 *) IOGetVirtMem(&vpu_dec->bit_stream_buf);
	vpu_dec->start_addr = vpu_dec->base_addr = virt_bit_stream_buf;
	vpu_dec->end_addr = virt_bit_stream_buf + BUFF_FILL_SIZE;

	if (vpu_dec->codec == STD_AVC) {
		vpu_dec->ps_mem_desc.size = PS_SAVE_SIZE;
		IOGetPhyMem(&vpu_dec->ps_mem_desc);
		vpu_dec->decOP->psSaveBuffer = vpu_dec->ps_mem_desc.phy_addr;
		vpu_dec->decOP->psSaveBufferSize = PS_SAVE_SIZE;

		vpu_dec->slice_mem_desc.size = SLICE_SAVE_SIZE;
		IOGetPhyMem(&vpu_dec->slice_mem_desc);
	}

	vpu_dec->decOP->bitstreamBuffer = vpu_dec->bit_stream_buf.phy_addr;
	vpu_dec->decOP->bitstreamBufferSize = BUFF_FILL_SIZE;

#if (defined (VPU_MX37) || defined (VPU_MX51)) && defined (CHROMA_INTERLEAVE)
	g_print("set chromainterleave\n");
	vpu_dec->decOP->chromaInterleave = 1;
	if (vpu_dec->codec == STD_AVC)
		vpu_dec->decOP->reorderEnable = 1;
	if (vpu_dec->codec == STD_MPEG2)
		vpu_dec->decOP->filePlayEnable = 0;
#else
	vpu_dec->decOP->filePlayEnable = 0;
	vpu_dec->decOP->reorderEnable = 0;
#endif

	vpu_dec->decOP->bitstreamFormat = vpu_dec->codec;

	vpu_dec->base_write = vpu_dec->bit_stream_buf.phy_addr;
	vpu_dec->end_write = vpu_dec->bit_stream_buf.phy_addr + BUFF_FILL_SIZE;

	/* open a VPU's decoder instance */
	vpu_ret = vpu_DecOpen(vpu_dec->handle, vpu_dec->decOP);
	if (vpu_ret != RETCODE_SUCCESS) {
		GST_ERROR("vpu_DecOpen failed. Error code is %d \n", vpu_ret);
		return GST_STATE_CHANGE_FAILURE;
	}
	vpu_dec->vpu_opened = TRUE;
	return GST_FLOW_OK;
}

/*======================================================================================
FUNCTION:           mfw_gst_vpudec_stream_file_read

DESCRIPTION:        Update bitstream buffer in streaming mode which means we might not
                    be getting a full frame from upstream as we are in file play mode

ARGUMENTS PASSED:   vpu_dec  - VPU decoder plugins context
                    buffer - pointer to the input buffer which has the video data.

RETURN VALUE:       GstFlowReturn - Success of Failure.
PRE-CONDITIONS:     None
POST-CONDITIONS:    None
IMPORTANT NOTES:    None
=======================================================================================*/
GstFlowReturn
mfw_gst_vpudec_stream_buff_read_init(MfwGstVPU_Dec * vpu_dec,
				     GstBuffer * buffer)
{
	RetCode vpu_ret = RETCODE_SUCCESS;
	PhysicalAddress p1, p2;
	Uint32 space;

	if (G_UNLIKELY(buffer == NULL)) {
		/* now end of stream */
		vpu_dec->eos = TRUE;
		vpu_ret = vpu_DecUpdateBitstreamBuffer(*(vpu_dec->handle), 0);
		if (vpu_ret != RETCODE_SUCCESS) {
			GST_ERROR
			    ("vpu_DecUpdateBitstreamBuffer failed. Error code is %d \n",
			     vpu_ret);
			return GST_FLOW_ERROR;
		}
	}

	/******************************************************************************/
	/********           Fill and update bitstreambuf           ********************/
	/******************************************************************************/

	/*Time stamp Buffer is a circular buffer to store the timestamps which are later
	   used while pushing the decoded frame onto the Sink element */
	if (G_UNLIKELY(vpu_dec->eos == TRUE))
		return GST_FLOW_OK;

	if (vpu_dec->codec != STD_MPEG2 || GST_CLOCK_TIME_IS_VALID(GST_BUFFER_TIMESTAMP(buffer))) {
		/* for mpeg2, we only store valid timestamp */
		vpu_dec->timestamp_buffer[vpu_dec->ts_rx] = GST_BUFFER_TIMESTAMP((buffer));
		vpu_dec->ts_rx = (vpu_dec->ts_rx + 1) % MAX_STREAM_BUF;
	}

	if ((vpu_dec->codec == STD_VC1) && (vpu_dec->picWidth != 0)) {
		/* Creation of RCV Header is done in case of ASF Playback pf VC-1 streams
		   from the parameters like width height and Header Extension Data */
		if (vpu_dec->first == FALSE) {
			GstBuffer *tempBuf;
			tempBuf = mfw_gst_VC1_Create_RCVheader(vpu_dec, buffer);
			buffer = gst_buffer_join(tempBuf, buffer);
			vpu_dec->first = TRUE;
		} else {
			/* The Size of the input stream is appended with the input stream
			   for integration with ASF */

			GstBuffer *SrcFrameSize = NULL;

			SrcFrameSize = gst_buffer_new_and_alloc(4);
			GST_BUFFER_DATA(SrcFrameSize)[0] = (unsigned char) GST_BUFFER_SIZE(buffer);
			GST_BUFFER_DATA(SrcFrameSize)[1] = (unsigned char) (GST_BUFFER_SIZE(buffer) >> 8);
			GST_BUFFER_DATA(SrcFrameSize)[2] = (unsigned char) (GST_BUFFER_SIZE(buffer) >> 16);
			GST_BUFFER_DATA(SrcFrameSize)[3] = (unsigned char) (GST_BUFFER_SIZE(buffer) >> 24);
			buffer = gst_buffer_join(SrcFrameSize, buffer);
		}
	}

	vpu_dec->frame_sizes_buffer[vpu_dec->buffidx_in] = GST_BUFFER_SIZE(buffer);
	vpu_dec->buffidx_in = (vpu_dec->buffidx_in + 1) % MAX_STREAM_BUF;

	vpu_DecGetBitstreamBuffer(*(vpu_dec->handle), &p1, &p2, &space);

	/* Check if there is enough space for input buffer */
	if (space >= GST_BUFFER_SIZE(buffer)) {
		/* The buffer read by the VPU follows a circular buffer approach
		   this block of code handles that */
		if ((vpu_dec->start_addr + GST_BUFFER_SIZE(buffer)) <= vpu_dec->end_addr) {
			memcpy(vpu_dec->start_addr, GST_BUFFER_DATA(buffer), GST_BUFFER_SIZE(buffer));
			vpu_dec->start_addr += GST_BUFFER_SIZE(buffer);
		} else {
			guint residue = vpu_dec->end_addr - vpu_dec->start_addr;
			memcpy(vpu_dec->start_addr, GST_BUFFER_DATA(buffer), residue);
			memcpy(vpu_dec->base_addr, GST_BUFFER_DATA(buffer) + residue, GST_BUFFER_SIZE(buffer) - residue);
			vpu_dec->start_addr = vpu_dec->base_addr + GST_BUFFER_SIZE(buffer) - residue;
		}

		vpu_ret = vpu_DecUpdateBitstreamBuffer(*(vpu_dec->handle), GST_BUFFER_SIZE(buffer));
		if (vpu_ret != RETCODE_SUCCESS) {
			GST_ERROR("vpu_DecUpdateBitstreamBuffer failed. Error code is %d \n", vpu_ret);
			return GST_FLOW_ERROR;
		}

		vpu_dec->buffered_size += GST_BUFFER_SIZE(buffer);
		gst_buffer_unref(buffer);
		vpu_dec->buf_empty = TRUE;
	}

	return GST_FLOW_OK;
}

/*======================================================================================
FUNCTION:           mfw_gst_vpudec_release_buff

DESCRIPTION:        Release buffers that are already displayed

ARGUMENTS PASSED:   vpu_dec  - VPU decoder plugins context

RETURN VALUE:       GstFlowReturn - Success of Failure.
PRE-CONDITIONS:     None
POST-CONDITIONS:    None
IMPORTANT NOTES:    None
=======================================================================================*/
GstFlowReturn
mfw_gst_vpudec_release_buff(MfwGstVPU_Dec * vpu_dec)
{
	RetCode vpu_ret = RETCODE_SUCCESS;

	/* check which buffer has been displayed, then clr it for vpu */
	if (vpu_dec->rotation_angle || vpu_dec->mirror_dir) {
		// In rotation case we only output the rotation buffer so clear it now
		// and below we have to wait for it it be displayed as we do not have a pipeline
		if (vpu_dec->outputInfo->indexFrameDisplay >= 0) {
			vpu_ret =
			    vpu_DecClrDispFlag(*(vpu_dec->handle),
					       vpu_dec->outputInfo->
					       indexFrameDisplay);
			vpu_dec->fb_state_plugin[vpu_dec->outputInfo->
						 indexFrameDisplay] =
			    FB_STATE_ALLOCTED;
			if (vpu_ret != RETCODE_SUCCESS) {
				GST_ERROR("vpu_DecClrDispFlag failed. Error code is %d \n", vpu_ret);
				return GST_FLOW_ERROR;
			}
		}
	} else {
		gint i = 0;
		gint loop_cnt = 5;
		while (loop_cnt) {
			int numFreeBufs = 0;
			int numBusyBufs = 0;
			for (i = 0; i < vpu_dec->numframebufs; i++) {
				GstBuffer *pBuffer = vpu_dec->outbuffers[i];
				if (pBuffer && gst_buffer_is_writable(pBuffer)) {
					if (vpu_dec->fb_state_plugin[i] == FB_STATE_ALLOCTED)
						numFreeBufs++;
					else if (vpu_dec->fb_state_plugin[i] ==
						 FB_STATE_DISPLAY) {
						vpu_dec->fb_state_plugin[i] =
						    FB_STATE_ALLOCTED;
						//g_print (" clearing buffer %d \n", i);
						vpu_ret = vpu_DecClrDispFlag(*vpu_dec->handle, i);
						if (vpu_ret != RETCODE_SUCCESS) {
							GST_ERROR
							    ("vpu_DecClrDispFlag failed. Error code is %d \n",
							     vpu_ret);
							return GST_FLOW_ERROR;
						}
						numFreeBufs++;
					} else if (vpu_dec->
						   fb_state_plugin[i] ==
						   FB_STATE_DECODED)
						numBusyBufs++;
				} else {
					numBusyBufs++;
				}
			}
			// MPEG4 will not be using reference frames like H.264
			if (vpu_dec->codec == STD_MPEG4)
				break;

			//H.264 will have to wait if all the buffers are busy with decode or display
			if (numFreeBufs < 1) {
				// wait for the buffers to be free
				//g_print (" sleeping because - %d buffers are free %d are busy\n", numFreeBufs, numBusyBufs);
				usleep(30);
				loop_cnt--;
			} else {
				//g_print (" *** DEC can decode now - %d buffers are free %d are busy\n", numFreeBufs, numBusyBufs);
				break;	// exit loop - it is safe to start decoding
			}
		}
	}
	return GST_FLOW_OK;
}

/*======================================================================================
FUNCTION:           mfw_gst_vpudec_vpu_init

DESCRIPTION:        Initialize VPU

ARGUMENTS PASSED:   vpu_dec  - VPU decoder plugins context

RETURN VALUE:       GstFlowReturn - Success of Failure.
PRE-CONDITIONS:     None
POST-CONDITIONS:    None
IMPORTANT NOTES:    None
=======================================================================================*/
GstFlowReturn mfw_gst_vpudec_vpu_init(MfwGstVPU_Dec * vpu_dec, int filemode)
{
	RetCode vpu_ret = RETCODE_SUCCESS;
	GstCaps *caps;
	gint crop_top_len, crop_left_len;
	gint crop_right_len, crop_bottom_len;
	gint orgPicW, orgPicH;
	gint width, height;
	gint crop_right_by_pixel, crop_bottom_by_pixel;

#if (defined (VPU_MX37) || defined (VPU_MX51)) && defined (CHROMA_INTERLEAVE)
	gint fourcc = GST_STR_FOURCC("NV12");
#else
	gint fourcc = GST_STR_FOURCC("I420");
#endif
	DecBufInfo bufinfo;
	guint needFrameBufCount = 0;

	vpu_DecSetEscSeqInit(*(vpu_dec->handle), 1);
	vpu_ret = vpu_DecGetInitialInfo(*(vpu_dec->handle), vpu_dec->initialInfo);
	vpu_DecSetEscSeqInit(*(vpu_dec->handle), 0);

	if (vpu_ret == RETCODE_FRAME_NOT_COMPLETE) {
		return GST_FLOW_OK;
	}
	if (vpu_ret != RETCODE_SUCCESS) {
		GST_ERROR("vpu_DecGetInitialInfo failed. Error code is %d \n", vpu_ret);
		mfw_gst_vpudec_post_fatal_error_msg(vpu_dec, "VPU Decoder Initialisation failed ");
		return GST_FLOW_ERROR;
	}
	GST_DEBUG("Dec: min buffer count= %d\n", vpu_dec->initialInfo->minFrameBufferCount);
	GST_DEBUG("Dec InitialInfo =>\npicWidth: %u, picHeight: %u, frameRate: %u\n",
			vpu_dec->initialInfo->picWidth,
			vpu_dec->initialInfo->picHeight,
			(unsigned int) vpu_dec->initialInfo->frameRateInfo);

	/* Check: Minimum resolution limitation */
	if (vpu_dec->initialInfo->picWidth < MIN_WIDTH || vpu_dec->initialInfo->picHeight < MIN_HEIGHT) {
		GstMessage *message = NULL;
		GError *gerror = NULL;
		gchar *text_msg = "unsupported video resolution.";
		gerror = g_error_new_literal(1, 0, text_msg);
		message = gst_message_new_error(GST_OBJECT(GST_ELEMENT(vpu_dec)), gerror, "debug none");
		gst_element_post_message(GST_ELEMENT(vpu_dec), message);
		g_error_free(gerror);

		return GST_FLOW_ERROR;
	}

	if (vpu_dec->initialInfo->minFrameBufferCount > NUM_MAX_VPU_REQUIRED) {
		g_print("vpu required frames number exceed max limitation, required %d.",
		     vpu_dec->initialInfo->minFrameBufferCount);
		return GST_FLOW_ERROR;
	}

	needFrameBufCount = vpu_dec->initialInfo->minFrameBufferCount + 2;

	/* Padding the width and height to 16 */
	orgPicW = vpu_dec->initialInfo->picWidth;
	orgPicH = vpu_dec->initialInfo->picHeight;
	vpu_dec->initialInfo->picWidth = (vpu_dec->initialInfo->picWidth + 15) / 16 * 16;
	vpu_dec->initialInfo->picHeight = (vpu_dec->initialInfo->picHeight + 15) / 16 * 16;
	if (vpu_dec->codec == STD_AVC && (vpu_dec->initialInfo->picCropRect.right > 0
			&& vpu_dec->initialInfo->picCropRect.bottom > 0)) {
		crop_top_len = vpu_dec->initialInfo->picCropRect.top;
		crop_left_len = vpu_dec->initialInfo->picCropRect.left;
		crop_right_len = vpu_dec->initialInfo->picWidth - vpu_dec->initialInfo->picCropRect.right;
		crop_bottom_len = vpu_dec->initialInfo->picHeight - vpu_dec->initialInfo->picCropRect.bottom;
	} else {
		crop_top_len = 0;
		crop_left_len = 0;
		crop_right_len = vpu_dec->initialInfo->picWidth - orgPicW;
		crop_bottom_len = vpu_dec->initialInfo->picHeight - orgPicH;
	}

	if (filemode) {
		width = vpu_dec->initialInfo->picWidth;
		height = vpu_dec->initialInfo->picHeight;
		crop_right_by_pixel = (crop_right_len + 7) / 8 * 8;
		crop_bottom_by_pixel = crop_bottom_len;
	} else {
		if (vpu_dec->rotation_angle == 90 || vpu_dec->rotation_angle == 270) {
			width = vpu_dec->initialInfo->picHeight;
			height = vpu_dec->initialInfo->picWidth;
			crop_right_by_pixel = (crop_right_len + 7) / 8 * 8;
			crop_bottom_by_pixel = crop_right_len;
		} else {
			width = vpu_dec->initialInfo->picWidth;
			height = vpu_dec->initialInfo->picHeight;
			crop_right_by_pixel = (crop_bottom_len + 7) / 8 * 8;
			crop_bottom_by_pixel = crop_bottom_len;
		}
	}

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
			"num-buffers-required", G_TYPE_INT, needFrameBufCount,
			"framerate", GST_TYPE_FRACTION, vpu_dec->frame_rate_nu, vpu_dec->frame_rate_de,
			NULL);

	if (!(gst_pad_set_caps(vpu_dec->srcpad, caps)))
		GST_ERROR("Could not set the caps for the VPU decoder's src pad\n");
	gst_caps_unref(caps);

	vpu_dec->outsize = (vpu_dec->initialInfo->picWidth * vpu_dec->initialInfo->picHeight * 3) / 2;
	vpu_dec->numframebufs = needFrameBufCount;
	/* Allocate the Frame buffers requested by the Decoder */
	if (vpu_dec->framebufinit_done == FALSE) {
		if ((mfw_gst_vpudec_FrameBufferInit(vpu_dec, vpu_dec->frameBuf, needFrameBufCount)) < 0) {
			GST_ERROR("Mem system allocation failed!\n");
			mfw_gst_vpudec_post_fatal_error_msg(vpu_dec,
							    "Allocation of the Frame Buffers Failed");

			return GST_FLOW_ERROR;
		}
		vpu_dec->framebufinit_done = TRUE;
	}

	memset(&bufinfo, 0, sizeof (bufinfo));
	bufinfo.avcSliceBufInfo.sliceSaveBuffer = vpu_dec->slice_mem_desc.phy_addr;
	bufinfo.avcSliceBufInfo.sliceSaveBufferSize = SLICE_SAVE_SIZE;

	/* Register the Allocated Frame buffers with the decoder */
	// for rotation - only register minimum as extra two buffers will be used for display separately

	vpu_ret = vpu_DecRegisterFrameBuffer(*(vpu_dec->handle),
					     vpu_dec->frameBuf,
					     (vpu_dec->rotation_angle || vpu_dec-> mirror_dir) ?
			vpu_dec->initialInfo->minFrameBufferCount : vpu_dec->numframebufs,
			vpu_dec->initialInfo->picWidth, &bufinfo);
	if (vpu_ret != RETCODE_SUCCESS) {
		GST_ERROR("vpu_DecRegisterFrameBuffer failed. Error code is %d \n", vpu_ret);
		mfw_gst_vpudec_post_fatal_error_msg(vpu_dec,
						    "Registration of the Allocated Frame Buffers Failed ");
		return GST_FLOW_ERROR;
	}

	if (filemode) {
		vpu_dec->init = TRUE;
		return GST_FLOW_OK;
	}

	// Setup rotation or mirroring which will be output to separate buffers for display
	if (vpu_dec->rotation_angle || vpu_dec->mirror_dir) {
		int rotStride = width;
		if (vpu_dec->rotation_angle) {
			// must set angle before rotator stride since the stride uses angle for error checking
			vpu_ret = vpu_DecGiveCommand(*(vpu_dec->handle),
					       SET_ROTATION_ANGLE,
					       &vpu_dec->rotation_angle);
		}
		if (vpu_dec->mirror_dir) {
			vpu_ret = vpu_DecGiveCommand(*(vpu_dec->handle),
					       SET_MIRROR_DIRECTION,
					       &vpu_dec->mirror_dir);
		}
		vpu_ret = vpu_DecGiveCommand(*(vpu_dec->handle), SET_ROTATOR_STRIDE, &rotStride);
		if (vpu_ret != RETCODE_SUCCESS) {
			GST_ERROR("vpu_Dec SET_ROTATOR_STRIDE failed. ret=%d \n", vpu_ret);
			mfw_gst_vpudec_post_fatal_error_msg(vpu_dec,
							    "VPU SET_ROTATOR_STRIDE failed ");
			return GST_FLOW_ERROR;
		}
		vpu_dec->rot_buff_idx = vpu_dec->initialInfo->minFrameBufferCount;
		vpu_ret = vpu_DecGiveCommand(*(vpu_dec->handle), SET_ROTATOR_OUTPUT,
				       &vpu_dec->frameBuf[vpu_dec->rot_buff_idx]);
		if (vpu_dec->rotation_angle)
			vpu_ret = vpu_DecGiveCommand(*(vpu_dec->handle), ENABLE_ROTATION, 0);
		if (vpu_dec->mirror_dir)
			vpu_ret = vpu_DecGiveCommand(*(vpu_dec->handle), ENABLE_MIRRORING, 0);
	}

	vpu_dec->decParam->prescanEnable = 1;
#ifndef VPU_MX27
	if (vpu_dec->dbk_enabled) {
		DbkOffset dbkoffset;
		dbkoffset.DbkOffsetEnable = 1;
		dbkoffset.DbkOffsetA = vpu_dec->dbk_offset_a;
		dbkoffset.DbkOffsetB = vpu_dec->dbk_offset_b;

		vpu_DecGiveCommand(*(vpu_dec->handle), SET_DBK_OFFSET,
				   &dbkoffset);
	} else {
		DbkOffset dbkoffset;
		dbkoffset.DbkOffsetEnable = 0;
		dbkoffset.DbkOffsetA = 0;
		dbkoffset.DbkOffsetB = 0;

		vpu_DecGiveCommand(*(vpu_dec->handle), SET_DBK_OFFSET,
				   &dbkoffset);
	}
#endif
	vpu_dec->init = TRUE;
	return GST_FLOW_OK;
}

/*======================================================================================
FUNCTION:           mfw_gst_vpudec_chain_stream_mode

DESCRIPTION:        The main processing function where the data comes in as buffer. This
                    data is decoded, and then pushed onto the next element for further
                    processing.

ARGUMENTS PASSED:   pad - pointer to the sinkpad of this element
                    buffer - pointer to the input buffer which has the H.264 data.

RETURN VALUE:       GstFlowReturn - Success of Failure.
PRE-CONDITIONS:     None
POST-CONDITIONS:    None
IMPORTANT NOTES:    None
=======================================================================================*/
static GstFlowReturn
mfw_gst_vpudec_chain_stream_mode(GstPad * pad, GstBuffer * buffer)
{

	MfwGstVPU_Dec *vpu_dec = MFW_GST_VPU_DEC(GST_PAD_PARENT(pad));
	RetCode vpu_ret = RETCODE_SUCCESS;
	GstFlowReturn retval = GST_FLOW_OK;

	gfloat time_val = 0;
	struct timeval tv_prof, tv_prof1;
	struct timeval tv_prof2, tv_prof3;
	long time_before = 0, time_after = 0;

	// Update Profiling timestamps
	if (G_UNLIKELY(vpu_dec->profiling)) {
		gettimeofday(&tv_prof2, 0);
	}
	// Open VPU is not already opened
	if (G_UNLIKELY(!vpu_dec->vpu_opened)) {
		retval = mfw_gst_vpudec_vpu_open(vpu_dec);
		if (retval != GST_FLOW_OK) {
			GST_ERROR
			    ("mfw_gst_vpudec_stream_buff_read failed. Error code is %d \n",
			     retval);
			goto done;
		}
	}
	vpu_dec->buf_empty = FALSE;

	// Write input bitstream to VPU - special for streaming mode
	retval = mfw_gst_vpudec_stream_buff_read_init(vpu_dec, buffer);
	if (retval != GST_FLOW_OK) {
		GST_ERROR
		    ("mfw_gst_vpudec_stream_buff_read_init failed. Error code is %d \n",
		     retval);
		goto done;
	}
	if (vpu_dec->buf_empty) {
		buffer = NULL;
		if ((vpu_dec->buffered_size <
		     (vpu_dec->frame_sizes_buffer[vpu_dec->buffidx_out] + 1024))
		    || (vpu_dec->buffered_size < 1024)) {
			return GST_FLOW_OK;
		}
	}
	// Initialize VPU
	if (G_UNLIKELY(vpu_dec->init == FALSE)) {
		retval = mfw_gst_vpudec_vpu_init(vpu_dec, 0);
		if (retval != GST_FLOW_OK) {
			GST_ERROR
			    ("mfw_gst_vpudec_vpu_init failed initializing VPU\n");
			goto done;
		}
		// don't exit instead start the first decode
		//return GST_FLOW_OK;
	}
	// Keep looping while there is enough in bitstream to decode
	while (1) {
		if (vpu_dec->flush == TRUE)
			break;

		// read any left over in buffer
		if (G_LIKELY(vpu_dec->eos != TRUE)) {
			if (buffer != NULL) {
				PhysicalAddress p1, p2;
				Uint32 space;

				vpu_DecGetBitstreamBuffer(*(vpu_dec->handle),
							  &p1, &p2, &space);

				if (space >= GST_BUFFER_SIZE(buffer)) {
					if ((vpu_dec->start_addr +
					     GST_BUFFER_SIZE(buffer)) <=
					    vpu_dec->end_addr) {
						memcpy(vpu_dec->start_addr,
						       GST_BUFFER_DATA(buffer),
						       GST_BUFFER_SIZE(buffer));
						vpu_dec->start_addr +=
						    GST_BUFFER_SIZE(buffer);
					} else {
						guint residue =
						    (vpu_dec->end_addr -
						     vpu_dec->start_addr);
						memcpy(vpu_dec->start_addr,
						       GST_BUFFER_DATA(buffer),
						       residue);
						memcpy(vpu_dec->base_addr,
						       GST_BUFFER_DATA(buffer) +
						       residue,
						       GST_BUFFER_SIZE(buffer) -
						       residue);
						vpu_dec->start_addr =
						    vpu_dec->base_addr +
						    GST_BUFFER_SIZE(buffer) -
						    residue;
					}

					vpu_ret =
					    vpu_DecUpdateBitstreamBuffer(*
									 (vpu_dec->
									  handle),
									 GST_BUFFER_SIZE
									 (buffer));
					if (vpu_ret != RETCODE_SUCCESS) {
						GST_ERROR
						    ("vpu_DecUpdateBitstreamBuffer failed. Error code is %d \n",
						     vpu_ret);
						retval = GST_FLOW_ERROR;
						goto done;
					}

					vpu_dec->buffered_size +=
					    GST_BUFFER_SIZE(buffer);
					gst_buffer_unref(buffer);
					buffer = NULL;
				}
			} else {
				if ((vpu_dec->buffered_size <
				     (vpu_dec->
				      frame_sizes_buffer[vpu_dec->buffidx_out] +
				      1024))) {
					break;
				}
				if (vpu_dec->buffered_size < 2048) {
					break;
				}
			}
		}
		// Start Decoding One frame in VPU if not already started
		if (vpu_dec->is_startframe == FALSE) {
			// Release buffers back to VPU before starting next decode
			retval = mfw_gst_vpudec_release_buff(vpu_dec);
			if (retval != GST_FLOW_OK) {
				// Error in clearing VPU buffers
				goto done;
			}

			vpu_mutex_lock(vpu_dec->vpu_mutex);

			vpu_ret =
			    vpu_DecStartOneFrame(*(vpu_dec->handle),
						 vpu_dec->decParam);
			if (vpu_ret == RETCODE_FRAME_NOT_COMPLETE) {
				vpu_mutex_unlock(vpu_dec->vpu_mutex);
				retval = GST_FLOW_OK;
				goto done;
			}

			if (vpu_ret != RETCODE_SUCCESS) {
				vpu_mutex_unlock(vpu_dec->vpu_mutex);
				GST_ERROR
				    ("vpu_DecStartOneFrame failed. Error code is %d \n",
				     vpu_ret);
				retval = GST_FLOW_ERROR;
				goto done;
			}

			vpu_dec->is_startframe = TRUE;
			vpu_mutex_unlock(vpu_dec->vpu_mutex);

			// Start parallelization if not in loopback mode and not doing rotation
			// in those cases we must complete frame after starting decode
			if ((vpu_dec->loopback == FALSE)
			    && (vpu_dec->rotation_angle == 0)
			    && (vpu_dec->mirror_dir == MIRDIR_NONE)) {
				vpu_mutex_lock(vpu_dec->vpu_mutex);
				retval = GST_FLOW_OK;
				goto done;
			}
		}
		// Wait for output from decode
		if (G_UNLIKELY(vpu_dec->profiling)) {
			gettimeofday(&tv_prof, 0);
		}
		while (vpu_IsBusy()) {
			vpu_WaitForInt(1000);
		};
		if (G_UNLIKELY(vpu_dec->profiling)) {
			gettimeofday(&tv_prof1, 0);
			time_before =
			    (tv_prof.tv_sec * 1000000) +
			    tv_prof.tv_usec;
			time_after =
			    (tv_prof1.tv_sec * 1000000) +
			    tv_prof1.tv_usec;
			vpu_dec->decode_wait_time +=
			    time_after - time_before;
		}
		// Get the VPU output from decoding
		vpu_ret =
		    vpu_DecGetOutputInfo(*(vpu_dec->handle),
					 vpu_dec->outputInfo);

		vpu_dec->is_startframe = FALSE;
		vpu_mutex_unlock(vpu_dec->vpu_mutex);

#if 0
		g_print("Get output--\nprescan%d,dec result:%d\n",
			vpu_dec->outputInfo->prescanresult,
			vpu_dec->outputInfo->decodingSuccess);
#endif
		if ((vpu_dec->decParam->prescanEnable == 1)
		    && (vpu_dec->outputInfo->prescanresult == 0)) {
			GST_WARNING
			    ("The prescan result is zero, all the output information have no meaning.\n");
			// Return for more data as this is incomplete - but do not process as an error
			retval = GST_FLOW_OK;
			goto done;
		}

		if (vpu_ret != RETCODE_SUCCESS) {
			GST_ERROR
			    ("vpu_DecGetOutputInfo failed. Error code is %d \n",
			     vpu_ret);
			retval = GST_FLOW_ERROR;
			goto done;
		}
		//g_print (" --DEC disp=%d decode=%d\n",vpu_dec->outputInfo->indexFrameDisplay,vpu_dec->outputInfo->indexFrameDecoded);
		if (vpu_dec->outputInfo->indexFrameDecoded >= 0) {
			if (vpu_dec->
			    fb_state_plugin[vpu_dec->outputInfo->
					    indexFrameDecoded] ==
			    FB_STATE_DISPLAY) {
				//g_print ("***** Decoded returned was in display mode \n");
				vpu_DecClrDispFlag(*(vpu_dec->handle),
						   vpu_dec->outputInfo->
						   indexFrameDecoded);
			}
			vpu_dec->fb_state_plugin[vpu_dec->outputInfo->
						 indexFrameDecoded] =
			    FB_STATE_DECODED;
		}
		if (G_UNLIKELY
		    (vpu_dec->outputInfo->indexFrameDisplay == -1))
			break;	/* decoding done */

		// BIT don't have picture to be displayed
		if (G_UNLIKELY
		    (vpu_dec->outputInfo->indexFrameDisplay == -3)
		    || G_UNLIKELY(vpu_dec->outputInfo->
				  indexFrameDisplay == -2)) {
			GST_DEBUG("Decoded frame not to display!\n");
			continue;
		}

		if (G_LIKELY(vpu_dec->direct_render == TRUE)) {
			if (vpu_dec->rotation_angle
			    || vpu_dec->mirror_dir) {
				vpu_dec->pushbuff = vpu_dec->outbuffers[vpu_dec->rot_buff_idx];
				// switch output buffer for every other frame so we don't overwrite display data in v4lsink
				// this way VPU can still decode while v4l sink is displaying
				vpu_dec->rot_buff_idx =
					(vpu_dec->rot_buff_idx == vpu_dec->initialInfo-> minFrameBufferCount) ?
					vpu_dec->initialInfo->minFrameBufferCount + 1 :
					vpu_dec->initialInfo->minFrameBufferCount;
				vpu_DecGiveCommand(*(vpu_dec->handle),
					SET_ROTATOR_OUTPUT,
					&vpu_dec->
					frameBuf[vpu_dec->rot_buff_idx]);
			} else {
				vpu_dec->pushbuff = vpu_dec->outbuffers[vpu_dec->outputInfo->indexFrameDisplay];
			}
		} else {
			// Incase of the Filesink the output in the hardware buffer is copied onto the
			//  buffer allocated by filesink
			retval =
			    gst_pad_alloc_buffer_and_set_caps(vpu_dec->
							      srcpad, 0,
							      vpu_dec->
							      outsize,
							      GST_PAD_CAPS
							      (vpu_dec->
							       srcpad),
							      &vpu_dec->
							      pushbuff);
			if (retval != GST_FLOW_OK) {
				GST_ERROR
				    ("Error in allocating the Framebuffer[%d],"
				     " error is %d",
				     vpu_dec->prv_use_idx, retval);
				goto done;
			}
			memcpy(GST_BUFFER_DATA(vpu_dec->pushbuff),
			       vpu_dec->frame_virt[vpu_dec->outputInfo->
						   indexFrameDisplay],
			       vpu_dec->outsize);
		}

		// Update the time stamp base on the frame-rate
		GST_BUFFER_SIZE(vpu_dec->pushbuff) = vpu_dec->outsize;
		if (vpu_dec->codec == STD_MPEG2) {
			GstClockTime ts;
			if (!(mfw_gst_get_timestamp(vpu_dec, &ts))) {
				/* no timestamp found */
				vpu_dec->no_ts_frames++;
				if (g_compare_float
					(vpu_dec->frame_rate, 0)
					!= FLOAT_MATCH) {
					/* calculating timestamp for decoded data */
					time_val =
					    ((gfloat) vpu_dec->no_ts_frames /
						      vpu_dec->frame_rate);
					ts = vpu_dec->base_ts +
					    time_val * 1000 * 1000 *
					    1000;
				} else {
					/* calculating timestamp for decoded data at 25.0 fps */
					time_val =
					    ((gfloat) vpu_dec->no_ts_frames /
						      25.0);
					ts = vpu_dec->base_ts +
					    time_val * 1000 * 1000 *
					    1000;
				}
			} else {
				vpu_dec->base_ts = ts;
				vpu_dec->no_ts_frames = 0;
			}
				GST_BUFFER_TIMESTAMP(vpu_dec->pushbuff) = ts;
		} else {
			GST_BUFFER_TIMESTAMP(vpu_dec->pushbuff) =
			    vpu_dec->timestamp_buffer[vpu_dec->ts_tx];
			vpu_dec->ts_tx =
			    (vpu_dec->ts_tx + 1) % MAX_STREAM_BUF;
		}

		vpu_dec->decoded_frames++;
		vpu_dec->fb_state_plugin[vpu_dec->outputInfo->
					 indexFrameDisplay] =
		    FB_STATE_DISPLAY;

		gst_buffer_ref(vpu_dec->pushbuff);
		GST_DEBUG("frame decoded : %lld\n",
			  vpu_dec->decoded_frames);
		retval =
		    gst_pad_push(vpu_dec->srcpad, vpu_dec->pushbuff);
		if (retval != GST_FLOW_OK) {
			GST_ERROR
			    ("Error in Pushing the Output onto the Source Pad,error is %d \n",
			     retval);
			vpu_dec->fb_state_plugin[vpu_dec->outputInfo->
						 indexFrameDisplay] =
			    FB_STATE_ALLOCTED;
			// Make sure we clear and release the buffer since it can't be displayed
			vpu_DecClrDispFlag(*(vpu_dec->handle),
					   vpu_dec->outputInfo->
					   indexFrameDisplay);
		}
		/* get output */
		if (vpu_dec->buffered_size > 0) {
			vpu_dec->buffered_size = vpu_dec->buffered_size -
			    vpu_dec->frame_sizes_buffer[vpu_dec->buffidx_out];
			vpu_dec->buffidx_out =
			    (vpu_dec->buffidx_out + 1) % MAX_STREAM_BUF;
		}
		retval = GST_FLOW_OK;
	}
      done:
	if (G_UNLIKELY(vpu_dec->profiling)) {
		gettimeofday(&tv_prof3, 0);
		time_before = (tv_prof2.tv_sec * 1000000) + tv_prof2.tv_usec;
		time_after = (tv_prof3.tv_sec * 1000000) + tv_prof3.tv_usec;
		vpu_dec->chain_Time += time_after - time_before;
	}
	if (buffer != NULL) {
		gst_buffer_unref(buffer);
		buffer = NULL;
	}
	return retval;
}

#ifndef VPU_MX27
/*======================================================================================
FUNCTION:           mfw_gst_vpudec_chain_file_mode

DESCRIPTION:        The main processing function where the data comes in as buffer. This
                    data is decoded, and then pushed onto the next element for further
                    processing.

ARGUMENTS PASSED:   pad - pointer to the sinkpad of this element
                    buffer - pointer to the input buffer which has the H.264 data.

RETURN VALUE:       GstFlowReturn - Success of Failure.
PRE-CONDITIONS:     None
POST-CONDITIONS:    None
IMPORTANT NOTES:    None
=======================================================================================*/
static GstFlowReturn
mfw_gst_vpudec_chain_file_mode(GstPad * pad, GstBuffer * buffer)
{
	MfwGstVPU_Dec *vpu_dec = MFW_GST_VPU_DEC(GST_PAD_PARENT(pad));
	RetCode vpu_ret = RETCODE_SUCCESS;
	GstFlowReturn retval = GST_FLOW_OK;
	gint i = 0;
	guint needFrameBufCount = 0;
#if (defined (VPU_MX37) || defined (VPU_MX51)) && defined (CHROMA_INTERLEAVE)
	gint fourcc = GST_STR_FOURCC("NV12");
#else
	gint fourcc = GST_STR_FOURCC("I420");
#endif
	GstCaps *caps = NULL;
	struct timeval tv_prof, tv_prof1;
	struct timeval tv_prof2, tv_prof3;
	long time_before = 0, time_after = 0;
	GstBuffer *SrcFrameSize = NULL;
	DecBufInfo bufinfo;
	gint crop_top_len, crop_left_len;
	gint crop_right_len, crop_bottom_len;
	gint orgPicW, orgPicH;

	if (G_UNLIKELY(vpu_dec->profiling)) {
		gettimeofday(&tv_prof2, 0);
	}

	if (G_UNLIKELY(!vpu_dec->vpu_opened)) {
		guint8 *virt_bit_stream_buf = NULL;
		GST_DEBUG("codec=%d\n", vpu_dec->codec);
		vpu_dec->file_play_mode = TRUE;
		vpu_dec->bit_stream_buf.size = BUFF_FILL_SIZE;
		IOGetPhyMem(&vpu_dec->bit_stream_buf);
		virt_bit_stream_buf =
		    (guint8 *) IOGetVirtMem(&vpu_dec->bit_stream_buf);
		vpu_dec->start_addr = vpu_dec->base_addr = virt_bit_stream_buf;
		vpu_dec->end_addr = virt_bit_stream_buf + BUFF_FILL_SIZE;

#if (defined (VPU_MX37) || defined (VPU_MX51)) && defined (CHROMA_INTERLEAVE)
		g_print("set chromainterleave\n");
		vpu_dec->decOP->chromaInterleave = 1;
#endif

		if (vpu_dec->codec == STD_AVC) {
			vpu_dec->ps_mem_desc.size = PS_SAVE_SIZE;
			IOGetPhyMem(&vpu_dec->ps_mem_desc);
			vpu_dec->decOP->psSaveBuffer =
			    vpu_dec->ps_mem_desc.phy_addr;
			vpu_dec->decOP->psSaveBufferSize = PS_SAVE_SIZE;

			vpu_dec->slice_mem_desc.size = SLICE_SAVE_SIZE;
			IOGetPhyMem(&vpu_dec->slice_mem_desc);
		}

		vpu_dec->decOP->bitstreamBuffer =
		    vpu_dec->bit_stream_buf.phy_addr;
		vpu_dec->decOP->bitstreamBufferSize = BUFF_FILL_SIZE;
		if (vpu_dec->codec == STD_AVC)
			vpu_dec->decOP->reorderEnable = 1;

		vpu_dec->decOP->bitstreamFormat = vpu_dec->codec;
#if defined (VPU_MX37) || defined (VPU_MX51)
		vpu_dec->decOP->filePlayEnable = 1;
#else
		vpu_dec->decOP->filePlayEnable = 0;
#endif
		vpu_dec->decOP->picWidth = vpu_dec->picWidth;
		vpu_dec->decOP->picHeight = vpu_dec->picHeight;
		vpu_dec->base_write = vpu_dec->bit_stream_buf.phy_addr;
		vpu_dec->end_write =
		    vpu_dec->bit_stream_buf.phy_addr + BUFF_FILL_SIZE;

		/* open a VPU's decoder instance */
		vpu_ret = vpu_DecOpen(vpu_dec->handle, vpu_dec->decOP);
		if (vpu_ret != RETCODE_SUCCESS) {
			GST_ERROR("vpu_DecOpen failed. Error code is %d \n",
				  vpu_ret);
			retval = GST_STATE_CHANGE_FAILURE;
			goto done;
		}
		vpu_dec->vpu_opened = TRUE;
	}

	/*Time stamp Buffer is a circular buffer to store the timestamps which are later
	   used while pushing the decoded frame onto the Sink element */
	vpu_dec->timestamp_buffer[vpu_dec->ts_rx] = GST_BUFFER_TIMESTAMP(buffer);
	vpu_dec->ts_rx = (vpu_dec->ts_rx + 1) % MAX_STREAM_BUF;
	/******************************************************************************/
	/********           Fill and update bitstreambuf           ********************/
	/******************************************************************************/
	if ((vpu_dec->codec == STD_VC1) && (vpu_dec->picWidth != 0)) {
		/* Creation of RCV Header is done in case of ASF Playback pf VC-1 streams
		   from the parameters like width height and Header Extension Data */
		if (vpu_dec->first == FALSE) {
			GstBuffer *tempBuf;
			tempBuf = mfw_gst_VC1_Create_RCVheader(vpu_dec, buffer);
			buffer = gst_buffer_join(tempBuf, buffer);
			vpu_dec->first = TRUE;
		}

		/* The Size of the input stream is appended with the input stream
		   for integration with ASF */
		else {
			SrcFrameSize = gst_buffer_new_and_alloc(4);
			GST_BUFFER_DATA(SrcFrameSize)[0] = (unsigned char) GST_BUFFER_SIZE(buffer);
			GST_BUFFER_DATA(SrcFrameSize)[1] = (unsigned char) (GST_BUFFER_SIZE(buffer) >> 8);
			GST_BUFFER_DATA(SrcFrameSize)[2] = (unsigned char) (GST_BUFFER_SIZE(buffer) >> 16);
			GST_BUFFER_DATA(SrcFrameSize)[3] = (unsigned char) (GST_BUFFER_SIZE(buffer) >> 24);
			buffer = gst_buffer_join(SrcFrameSize, buffer);
		}
	}

	memcpy(vpu_dec->start_addr, GST_BUFFER_DATA(buffer), GST_BUFFER_SIZE(buffer));
	vpu_dec->decParam->chunkSize = GST_BUFFER_SIZE(buffer);

	/* Initializion of the VPU decoder and the output buffers for the VPU
	   is done here */
	if (G_UNLIKELY(vpu_dec->init == FALSE)) {
		vpu_ret = vpu_DecUpdateBitstreamBuffer(*(vpu_dec->handle), GST_BUFFER_SIZE(buffer));
		if (vpu_ret != RETCODE_SUCCESS) {
			GST_ERROR("vpu_DecUpdateBitstreamBuffer failed. Error code is %d \n", vpu_ret);
			retval = GST_FLOW_ERROR;
			goto done;
		}
		retval = mfw_gst_vpudec_vpu_init(vpu_dec, 1);
		if (retval != GST_FLOW_OK) {
			GST_ERROR
			    ("mfw_gst_vpudec_vpu_init failed initializing VPU\n");
			goto done;
		}
	}

	while (1) {
		gboolean updated = FALSE;

		/* check which buffer has been displayed, then clr it for vpu */
		i = vpu_dec->prv_use_idx;
		while (!updated) {
			GstBuffer *pBuffer = vpu_dec->outbuffers[i];
			if (gst_buffer_is_metadata_writable(pBuffer) &&
			    vpu_dec->fb_state_plugin[i] == FB_STATE_DISPLAY) {
				if (!(vpu_dec->codec == STD_VC1 && i == vpu_dec->outputInfo->indexFrameDisplay)) {
					/* don't clear the frame buffer when it just displayed for vc1 clips */
					vpu_dec->fb_state_plugin[i] = FB_STATE_ALLOCTED;
					vpu_ret = vpu_DecClrDispFlag(*(vpu_dec->handle), i);
					if (vpu_ret != RETCODE_SUCCESS) {
						GST_ERROR("vpu_DecClrDispFlag failed. Error code is %d \n", vpu_ret);
						retval = GST_FLOW_ERROR;
						goto done;
					}
					updated = TRUE;
				}
			}
			i++;
			if ((guint) i == vpu_dec->numframebufs)
				i = 0;
			if (i == vpu_dec->prv_use_idx)
				break;
		}
		vpu_dec->prv_use_idx = i;

		/* Decoder API to decode one Frame at a time */
		vpu_ret =
		    vpu_DecStartOneFrame(*(vpu_dec->handle), vpu_dec->decParam);
		if (vpu_ret == RETCODE_FRAME_NOT_COMPLETE) {
			retval = GST_FLOW_OK;
			break;
		}
		if (vpu_ret != RETCODE_SUCCESS) {
			GST_ERROR
			    ("vpu_DecStartOneFrame failed. Error code is %d \n",
			     vpu_ret);
			retval = GST_FLOW_ERROR;
			break;
		}

		if (G_UNLIKELY(vpu_dec->profiling)) {
			gettimeofday(&tv_prof, 0);
		}

		while (vpu_IsBusy()) {
			vpu_WaitForInt(500);
		};

		if (G_UNLIKELY(vpu_dec->profiling)) {
			gettimeofday(&tv_prof1, 0);
			time_before =
			    (tv_prof.tv_sec * 1000000) + tv_prof.tv_usec;
			time_after =
			    (tv_prof1.tv_sec * 1000000) + tv_prof1.tv_usec;
			vpu_dec->decode_wait_time += time_after - time_before;
		}

		/* get the output information as to which index of the Framebuffers the
		   output is written onto */
		vpu_ret =
		    vpu_DecGetOutputInfo(*(vpu_dec->handle),
					 vpu_dec->outputInfo);
		if (vpu_ret != RETCODE_SUCCESS) {
			GST_ERROR
			    ("vpu_DecGetOutputInfo failed. Error code is %d \n",
			     vpu_ret);
			retval = GST_FLOW_ERROR;
			break;
		}

		if (vpu_dec->outputInfo->indexFrameDecoded >= 0) {
			vpu_dec->fb_state_plugin[vpu_dec->outputInfo->
						 indexFrameDecoded] =
			    FB_STATE_DECODED;
		}
#ifndef VPU_MX27
		if ((vpu_dec->outputInfo->interlacedFrame)
		    && (vpu_dec->lastframedropped)) {
			vpu_dec->ts_tx = (vpu_dec->ts_tx + 1) % MAX_STREAM_BUF;
		}
#endif

		/* BIT don't have picture to be displayed */
		if (G_UNLIKELY(vpu_dec->outputInfo->indexFrameDisplay == -3) ||
		    G_UNLIKELY(vpu_dec->outputInfo->indexFrameDisplay == -2)) {
#ifndef VPU_MX27
			if (vpu_dec->outputInfo->mp4PackedPBframe == 1) {
				continue;
			}
#endif
			GST_DEBUG("Decoded frame not to display!\n");
			vpu_dec->lastframedropped = TRUE;
			retval = GST_FLOW_OK;
			goto done;
		} else {
			vpu_dec->lastframedropped = FALSE;
		}
		// don't try to display any buffers if not valid display index
		if (vpu_dec->outputInfo->indexFrameDisplay <= 0) {
			retval = GST_FLOW_OK;
			goto done;
		}

		if (G_LIKELY(vpu_dec->direct_render == TRUE))
			vpu_dec->pushbuff = vpu_dec->outbuffers[vpu_dec->outputInfo->indexFrameDisplay];

		/* Incase of the Filesink the output in the hardware buffer is copied onto the
		   buffer allocated by filesink */
		else {
			retval =
			    gst_pad_alloc_buffer_and_set_caps(vpu_dec->srcpad,
							      0,
							      vpu_dec->outsize,
							      GST_PAD_CAPS
							      (vpu_dec->srcpad),
							      &vpu_dec->
							      pushbuff);
			if (retval != GST_FLOW_OK) {
				GST_ERROR
				    ("Error in allocating the Framebuffer[%d],"
				     " error is %d", i, retval);
				break;
			}
			memcpy(GST_BUFFER_DATA(vpu_dec->pushbuff),
			       vpu_dec->frame_virt[vpu_dec->outputInfo->
						   indexFrameDisplay],
			       vpu_dec->outsize);
		}
		/* update the time stamp base on the frame-rate */
		GST_BUFFER_SIZE(vpu_dec->pushbuff) = vpu_dec->outsize;
		GST_BUFFER_TIMESTAMP(vpu_dec->pushbuff) =
		    vpu_dec->timestamp_buffer[vpu_dec->ts_tx];
		vpu_dec->ts_tx = (vpu_dec->ts_tx + 1) % MAX_STREAM_BUF;
		vpu_dec->decoded_frames++;
		vpu_dec->fb_state_plugin[vpu_dec->outputInfo->
					 indexFrameDisplay] = FB_STATE_DISPLAY;
		GST_DEBUG("frame decoded : %lld\n", vpu_dec->decoded_frames);

		gst_buffer_ref(vpu_dec->pushbuff);
		retval = gst_pad_push(vpu_dec->srcpad, vpu_dec->pushbuff);
		if (retval != GST_FLOW_OK) {
			GST_ERROR
			    ("Error in Pushing the Output on to the Source Pad,error is %d \n",
			     retval);
		}
#ifndef VPU_MX27
		if (vpu_dec->outputInfo->mp4PackedPBframe == 1) {
			//g_print("mp4PackedPBframe is 1\n");
			continue;
		}
#endif
		break;
	}
      done:
	if (G_UNLIKELY(vpu_dec->profiling)) {
		gettimeofday(&tv_prof3, 0);
		time_before = (tv_prof2.tv_sec * 1000000) + tv_prof2.tv_usec;
		time_after = (tv_prof3.tv_sec * 1000000) + tv_prof3.tv_usec;
		vpu_dec->chain_Time += time_after - time_before;
	}
	if (buffer != NULL) {
		gst_buffer_unref(buffer);
		buffer = NULL;
	}
	return retval;
}

#endif				/* ifndef VPU_MX27 */

/*======================================================================================
FUNCTION:           mfw_gst_vpudec_sink_event

DESCRIPTION:        This function handles the events the occur on the sink pad
                    Like EOS

ARGUMENTS PASSED:   pad - pointer to the sinkpad of this element
                    event - event generated.

RETURN VALUE:       TRUE   event handled success fully.
                    FALSE .event not handled properly

PRE-CONDITIONS:     None
POST-CONDITIONS:    None
IMPORTANT NOTES:    None
=======================================================================================*/

static gboolean
mfw_gst_vpudec_sink_event(GstPad * pad, GstEvent * event)
{
	MfwGstVPU_Dec *vpu_dec = MFW_GST_VPU_DEC(GST_PAD_PARENT(pad));
	gboolean result = TRUE;
	RetCode vpu_ret = RETCODE_SUCCESS;
	gint idx;
	GstFormat format;
	gint64 start, stop, position;
	gdouble rate;

	switch (GST_EVENT_TYPE(event)) {
	case GST_EVENT_NEWSEGMENT:
		gst_event_parse_new_segment(event, NULL, &rate, &format,
					    &start, &stop, &position);
		GST_DEBUG(" receiving new seg \n");
		GST_DEBUG(" start = %" GST_TIME_FORMAT,
			  GST_TIME_ARGS(start));
		GST_DEBUG(" stop = %" GST_TIME_FORMAT,
			  GST_TIME_ARGS(stop));
		GST_DEBUG(" position in mpeg4  =%" GST_TIME_FORMAT,
			  GST_TIME_ARGS(position));
		vpu_dec->flush = FALSE;
		if (GST_FORMAT_TIME == format) {
			result = gst_pad_push_event(vpu_dec->srcpad, event);
			if (TRUE != result) {
				GST_ERROR
				    ("\n Error in pushing the event,result	is %d\n",
				     result);
			}
		}
		break;
	case GST_EVENT_FLUSH_STOP:
		vpu_dec->buffidx_in = 0;
		vpu_dec->buffidx_out = 0;
		vpu_dec->buffered_size = 0;
		memset(&vpu_dec->frame_sizes_buffer[0], 0, MAX_STREAM_BUF);
		memset(&vpu_dec->timestamp_buffer[0], 0, MAX_STREAM_BUF);
		vpu_dec->ts_rx = 0;
		vpu_dec->ts_tx = 0;
		vpu_dec->vpu_wait = FALSE;
		vpu_dec->eos = FALSE;
		vpu_dec->flush = TRUE;
		vpu_dec->no_ts_frames = 0;
		vpu_dec->base_ts = 0;

		if (vpu_dec->is_startframe) {
			vpu_DecGetOutputInfo(*vpu_dec->handle, vpu_dec->outputInfo);
			vpu_dec->is_startframe = FALSE;
			vpu_mutex_unlock(vpu_dec->vpu_mutex);
		}

		if (vpu_dec->file_play_mode == FALSE) {
			/* The below block of code is used to Flush the buffered input stream data */
			if (vpu_dec->codec == STD_VC1 || vpu_dec->codec == STD_AVC) {
				vpu_ret = vpu_DecClose(*vpu_dec->handle);
				if (vpu_ret == RETCODE_FRAME_NOT_COMPLETE) {
					vpu_DecGetOutputInfo(*vpu_dec->handle, vpu_dec->outputInfo);
					vpu_ret = vpu_DecClose(*vpu_dec->handle);
				}

				if (RETCODE_SUCCESS != vpu_ret)
					GST_ERROR("error in vpu_DecClose\n");

				vpu_dec->init = FALSE;

				if (vpu_dec->codec == STD_VC1)
					vpu_dec->first = FALSE;

				vpu_ret = vpu_DecOpen(vpu_dec->handle, vpu_dec->decOP);
				if (vpu_ret != RETCODE_SUCCESS)
					GST_ERROR("vpu_DecOpen failed. Error code is %d \n", vpu_ret);
			} else {
				vpu_ret = vpu_DecBitBufferFlush(*vpu_dec->handle);
				if (vpu_ret == RETCODE_FRAME_NOT_COMPLETE) {
					vpu_DecGetOutputInfo(*vpu_dec->handle, vpu_dec->outputInfo);
					vpu_ret = vpu_DecBitBufferFlush(*vpu_dec->handle);
				}
				if (RETCODE_SUCCESS != vpu_ret)
					GST_ERROR("error in flushing the bitstream buffer\n");
			}
			vpu_dec->start_addr = vpu_dec->base_addr;
		}

		/* clear all the framebuffer which not in display state */
		if (vpu_dec->codec == STD_MPEG2 || vpu_dec->codec == STD_MPEG4) {
			for (idx = 0; idx < vpu_dec->numframebufs; idx++) {
				if (vpu_dec->fb_state_plugin[idx] != FB_STATE_DISPLAY) {
					vpu_ret = vpu_DecClrDispFlag(*(vpu_dec->handle), idx);
					vpu_dec->fb_state_plugin[idx] = FB_STATE_ALLOCTED;
				}
			}
		}

		result = gst_pad_push_event(vpu_dec->srcpad, event);
		if (TRUE != result) {
			GST_ERROR("Error in pushing the event,result is %d\n", result);
			gst_event_unref(event);
		}
		break;
	case GST_EVENT_EOS:
		if (vpu_dec->file_play_mode == FALSE)
			mfw_gst_vpudec_chain_stream_mode(vpu_dec->sinkpad, NULL);

		result = gst_pad_push_event(vpu_dec->srcpad, event);
		if (TRUE != result)
			GST_ERROR("Error in pushing the event,result is %d\n", result);
		break;
	default:
		result = gst_pad_event_default(pad, event);
		break;
	}
	return result;

}

/*======================================================================================
FUNCTION:           mfw_gst_vpudec_change_state

DESCRIPTION:        This function keeps track of different states of pipeline.

ARGUMENTS PASSED:
                element     -   pointer to element
                transition  -   state of the pipeline

RETURN VALUE:
                GST_STATE_CHANGE_FAILURE    - the state change failed
                GST_STATE_CHANGE_SUCCESS    - the state change succeeded
                GST_STATE_CHANGE_ASYNC      - the state change will happen asynchronously
                GST_STATE_CHANGE_NO_PREROLL - the state change cannot be prerolled

PRE-CONDITIONS:     None
POST-CONDITIONS:    None
IMPORTANT NOTES:    None
=======================================================================================*/

static GstStateChangeReturn
mfw_gst_vpudec_change_state(GstElement * element, GstStateChange transition)
{
	GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
	MfwGstVPU_Dec *vpu_dec = MFW_GST_VPU_DEC(element);
	gint vpu_ret = RETCODE_SUCCESS;
	vpu_versioninfo ver;
	gfloat avg_mcps = 0, avg_plugin_time = 0, avg_dec_time = 0;
	gint cnt;
	// gint i=0,err;

	switch (transition) {
	case GST_STATE_CHANGE_NULL_TO_READY:

		GST_DEBUG("VPU State: Null to Ready\n");
		vpu_ret = vpu_Init((PhysicalAddress) (NULL));
		if (vpu_ret < 0) {
			GST_DEBUG("Error in initializing the VPU, error is %d\n",vpu_ret);
			return GST_STATE_CHANGE_FAILURE;
		}

		vpu_ret = vpu_GetVersionInfo(&ver);
		if (vpu_ret) {
			GST_DEBUG("Error in geting the VPU version, error is %d\n", vpu_ret);
			vpu_UnInit();
			return GST_STATE_CHANGE_FAILURE;
		}

		g_print(YELLOW_STR
			("VPU Version: firmware %d.%d.%d; libvpu: %d.%d.%d \n",
			 ver.fw_major, ver.fw_minor, ver.fw_release,
			 ver.lib_major, ver.lib_minor,
			 ver.lib_release));

#define MFW_GST_VPU_DECODER_PLUGIN VERSION
		PRINT_PLUGIN_VERSION(MFW_GST_VPU_DECODER_PLUGIN);
		break;
	case GST_STATE_CHANGE_READY_TO_PAUSED:
		GST_DEBUG("\nVPU State: Ready to Paused\n\n");
		vpu_dec->init = FALSE;
		vpu_dec->vpu_opened = FALSE;
		vpu_dec->start_addr = NULL;
		vpu_dec->end_addr = NULL;
		vpu_dec->base_addr = NULL;
		vpu_dec->outsize = 0;
		vpu_dec->decode_wait_time = 0;
		vpu_dec->chain_Time = 0;
		vpu_dec->decoded_frames = 0;
		vpu_dec->avg_fps_decoding = 0.0;
		vpu_dec->frames_dropped = 0;
		vpu_dec->direct_render = FALSE;
		vpu_dec->vpu_wait = FALSE;
		vpu_dec->buffered_size = 0;
		vpu_dec->first = FALSE;
		vpu_dec->buffidx_in = 0;
		vpu_dec->buffidx_out = 0;
		vpu_dec->ts_rx = 0;
		vpu_dec->ts_tx = 0;
		vpu_dec->framebufinit_done = FALSE;
		vpu_dec->file_play_mode = FALSE;
		vpu_dec->eos = FALSE;
		vpu_dec->no_ts_frames = 0;
		vpu_dec->base_ts = 0;
		vpu_dec->prv_use_idx = 0;

		for (cnt = 0; cnt < NUM_FRAME_BUF; cnt++)
			vpu_dec->outbuffers[cnt] = NULL;

		memset(&vpu_dec->bit_stream_buf, 0,
		       sizeof (vpu_mem_desc));
		memset(&vpu_dec->frame_sizes_buffer[0], 0,
		       MAX_STREAM_BUF * sizeof (guint));
		memset(&vpu_dec->timestamp_buffer[0], 0,
		       MAX_STREAM_BUF * sizeof (GstClockTime));
		memset(&vpu_dec->frameBuf[0], 0,
		       NUM_FRAME_BUF * sizeof (FrameBuffer));
		memset(&vpu_dec->frame_mem[0], 0,
		       NUM_FRAME_BUF * sizeof (vpu_mem_desc));
		/* Handle the decoder Initialization over here. */
		vpu_dec->decOP = g_malloc(sizeof (DecOpenParam));
		vpu_dec->initialInfo =
		    g_malloc(sizeof (DecInitialInfo));
		vpu_dec->decParam = g_malloc(sizeof (DecParam));
		vpu_dec->handle = g_malloc(sizeof (DecHandle));
		vpu_dec->outputInfo = g_malloc(sizeof (DecOutputInfo));
		memset(vpu_dec->decOP, 0, sizeof (DecOpenParam));
		memset(vpu_dec->handle, 0, sizeof (DecHandle));
		memset(vpu_dec->decParam, 0, sizeof (DecParam));
		memset(vpu_dec->outputInfo, 0, sizeof (DecOutputInfo));
		memset(&vpu_dec->ps_mem_desc, 0, sizeof (vpu_mem_desc));
		memset(&vpu_dec->slice_mem_desc, 0, sizeof (vpu_mem_desc));
		memset(vpu_dec->initialInfo, 0, sizeof (DecInitialInfo));
		break;
	case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
		GST_DEBUG("\nVPU State: Paused to Playing\n");
		break;
	default:
		break;
	}

	ret = vpu_dec->parent_class->change_state(element, transition);
	GST_DEBUG("\n State Change for VPU returned %d", ret);

	switch (transition) {
	case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
		GST_DEBUG("\nVPU State: Playing to Paused\n");
		break;
	case GST_STATE_CHANGE_PAUSED_TO_READY:
		GST_DEBUG("\nVPU State: Paused to Ready\n");
		if (vpu_dec->profiling) {
			g_print("PROFILE FIGURES OF VPU DECODER PLUGIN");
			g_print("\nTotal decode wait time is            %fus", (gfloat) vpu_dec->decode_wait_time);
			g_print("\nTotal plugin time is                 %lldus", vpu_dec->chain_Time);
			g_print("\nTotal number of frames decoded is    %lld", vpu_dec->decoded_frames);
			g_print("\nTotal number of frames dropped is    %lld\n",vpu_dec->frames_dropped);
			if (g_compare_float(vpu_dec->frame_rate, 0) != FLOAT_MATCH) {
				avg_mcps =((float) vpu_dec->decode_wait_time * PROCESSOR_CLOCK /
					(1000000 * (vpu_dec->decoded_frames - vpu_dec->frames_dropped)))
					* vpu_dec->frame_rate;
				g_print("\nAverage decode WAIT MCPS is          %f", avg_mcps);
				avg_mcps = ((float) vpu_dec->chain_Time * PROCESSOR_CLOCK /
					(1000000 * (vpu_dec->decoded_frames - vpu_dec->frames_dropped)))
					* vpu_dec->frame_rate;
				g_print("\nAverage plug-in MCPS is              %f", avg_mcps);
			} else {
				g_print("enable the Frame Rate property of the decoder to get the MCPS"
						" ... \n ! mfw_mpeg4decoder framerate=value ! .... "
						"\n Note: value denotes the framerate to be set");
			}
			avg_dec_time = ((float) vpu_dec->decode_wait_time) / vpu_dec->decoded_frames;
			g_print("\nAverage decoding Wait time is        %fus", avg_dec_time);
			avg_plugin_time = ((float) vpu_dec->chain_Time) / vpu_dec->decoded_frames;
			g_print("\nAverage plugin time is               %fus\n", avg_plugin_time);
			vpu_dec->decode_wait_time = 0;
			vpu_dec->chain_Time = 0;
			vpu_dec->decoded_frames = 0;
			vpu_dec->avg_fps_decoding = 0.0;
			vpu_dec->frames_dropped = 0;
		}

		mfw_gst_vpudec_FrameBufferClose(vpu_dec);

		/* release framebuffers hold by vpu */
		for (cnt = 0; cnt < vpu_dec->numframebufs; cnt++) {
			if (vpu_dec->outbuffers[cnt])
				gst_buffer_unref(vpu_dec->outbuffers[cnt]);
		}

		vpu_dec->first = FALSE;
		vpu_dec->buffidx_in = 0;
		vpu_dec->buffidx_out = 0;
		memset(&vpu_dec->timestamp_buffer[0], 0, MAX_STREAM_BUF);
		memset(&vpu_dec->frame_sizes_buffer[0], 0, MAX_STREAM_BUF);
		vpu_dec->start_addr = NULL;
		vpu_dec->end_addr = NULL;
		vpu_dec->base_addr = NULL;
		vpu_dec->outsize = 0;
		vpu_dec->direct_render = FALSE;
		vpu_dec->vpu_wait = FALSE;
		vpu_dec->framebufinit_done = FALSE;

		if (vpu_dec->is_startframe) {
			vpu_DecGetOutputInfo(*vpu_dec->handle, vpu_dec->outputInfo);
			vpu_dec->is_startframe = FALSE;
		}
		if (vpu_dec->vpu_opened) {
			vpu_ret = vpu_DecClose(*vpu_dec->handle);

			if (vpu_ret == RETCODE_FRAME_NOT_COMPLETE) {
				vpu_DecGetOutputInfo(*vpu_dec->handle, vpu_dec->outputInfo);
				vpu_ret = vpu_DecClose(*vpu_dec->handle);
				if (vpu_ret < 0) {
					GST_ERROR("Error in closing the VPU decoder,error is %d\n", vpu_ret);
					return GST_STATE_CHANGE_FAILURE;
				}
			}
			vpu_dec->vpu_opened = FALSE;
		}
		if (vpu_dec->bit_stream_buf.phy_addr) {
			IOFreePhyMem(&(vpu_dec->bit_stream_buf));
			IOFreeVirtMem(&(vpu_dec->bit_stream_buf));
			vpu_dec->bit_stream_buf.phy_addr = 0;
		}
		if (vpu_dec->ps_mem_desc.phy_addr) {
			IOFreePhyMem(&(vpu_dec->ps_mem_desc));
			IOFreeVirtMem(&(vpu_dec->ps_mem_desc));
			vpu_dec->ps_mem_desc.phy_addr = 0;
		}
		if (vpu_dec->slice_mem_desc.phy_addr) {
			IOFreePhyMem(&(vpu_dec->slice_mem_desc));
			IOFreeVirtMem(&(vpu_dec->slice_mem_desc));
			vpu_dec->ps_mem_desc.phy_addr = 0;
		}
		if (vpu_dec->decOP != NULL) {
			g_free(vpu_dec->decOP);
			vpu_dec->decOP = NULL;
		}
		if (vpu_dec->initialInfo != NULL) {
			g_free(vpu_dec->initialInfo);
			vpu_dec->initialInfo = NULL;
		}
		if (vpu_dec->decParam != NULL) {
			g_free(vpu_dec->decParam);
			vpu_dec->decParam = NULL;
		}
		if (vpu_dec->outputInfo != NULL) {
			g_free(vpu_dec->outputInfo);
			vpu_dec->outputInfo = NULL;
		}
		if (vpu_dec->handle != NULL) {
			g_free(vpu_dec->handle);
			vpu_dec->handle = NULL;
		}

		/* Unlock the mutex to free the mutex
		 * in case of date terminated.
		 */
		vpu_mutex_unlock(vpu_dec->vpu_mutex);
		g_mutex_free(vpu_dec->vpu_mutex);
		vpu_dec->vpu_mutex = NULL;
		break;
	case GST_STATE_CHANGE_READY_TO_NULL:
		GST_DEBUG("\nVPU State: Ready to Null\n");

		if (vpu_dec->loopback == FALSE)
			vpu_UnInit();
		break;
	default:
		break;
	}

	return ret;

}

/*=============================================================================
FUNCTION:           src_templ

DESCRIPTION:        Template to create a srcpad for the decoder

ARGUMENTS PASSED:   None

RETURN VALUE:       GstPadTemplate
PRE-CONDITIONS:     None
POST-CONDITIONS:    None
IMPORTANT NOTES:    None
=============================================================================*/
static GstPadTemplate *
src_templ(void)
{
	static GstPadTemplate *templ = NULL;
	GstCaps *caps;
	GstStructure *structure;
	GValue list = { 0 }, fps = {
	0}, fmt = {
	0};

	char *fmts[] = { "YV12", "I420", "Y42B", "NV12", NULL };
	guint n;
	caps = gst_caps_new_simple("video/x-raw-yuv",
				   "format", GST_TYPE_FOURCC,
				   GST_MAKE_FOURCC('I', '4', '2', '0'),
				   "width", GST_TYPE_INT_RANGE, 16, 4096,
				   "height", GST_TYPE_INT_RANGE, 16, 4096,
				   NULL);

	structure = gst_caps_get_structure(caps, 0);

	g_value_init(&list, GST_TYPE_LIST);
	g_value_init(&fps, GST_TYPE_FRACTION);
	for (n = 0; fpss[n][0] != 0; n++) {
		gst_value_set_fraction(&fps, fpss[n][0], fpss[n][1]);
		gst_value_list_append_value(&list, &fps);
	}
	gst_structure_set_value(structure, "framerate", &list);
	g_value_unset(&list);
	g_value_unset(&fps);

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

/*======================================================================================
FUNCTION:           mfw_gst_vpudec_setcaps

DESCRIPTION:        This function negoatiates the caps set on the sink pad

ARGUMENTS PASSED:
                pad   -   pointer to the sinkpad of this element
                caps  -     pointer to the caps set

RETURN VALUE:
               TRUE         negotiation success full
               FALSE        negotiation Failed

PRE-CONDITIONS:     None
POST-CONDITIONS:    None
IMPORTANT NOTES:    None
=======================================================================================*/

static gboolean
mfw_gst_vpudec_setcaps(GstPad * pad, GstCaps * caps)
{
	MfwGstVPU_Dec *vpu_dec = NULL;
	const gchar *mime;
	GstStructure *structure = gst_caps_get_structure(caps, 0);
	vpu_dec = MFW_GST_VPU_DEC(gst_pad_get_parent(pad));
	mime = gst_structure_get_name(structure);
	GValue *codec_data;
	guint8 *hdrextdata;
	guint i = 0;
	gint wmvversion;

	if (strcmp(mime, "video/x-h264") == 0)
		vpu_dec->codec = STD_AVC;
	else if (strcmp(mime, "video/mpeg") == 0)
		vpu_dec->codec = STD_MPEG4;
	else if (strcmp(mime, "video/x-h263") == 0)
		vpu_dec->codec = STD_H263;
	else if (strcmp(mime, "video/x-wmv") == 0)
		vpu_dec->codec = STD_VC1;
	else if (strcmp(mime, "video/mp2v") == 0)
		vpu_dec->codec = STD_MPEG2;
#if defined (VPU_MX51)
	else if (strcmp(mime, "image/jpeg") == 0)
		vpu_dec->codec = STD_MJPG;
#endif
	else {
		GST_ERROR(" Codec Standard not supporded \n");
		return FALSE;
	}

	if (vpu_dec->codec == STD_MPEG2)
		gst_pad_set_chain_function(vpu_dec->sinkpad,
					   mfw_gst_vpudec_chain_stream_mode);

	gst_structure_get_fraction(structure, "framerate",
			&vpu_dec->frame_rate_nu, &vpu_dec->frame_rate_de);

	if (vpu_dec->frame_rate_de != 0)
		vpu_dec->frame_rate =
			((gfloat) vpu_dec->frame_rate_nu /
			 vpu_dec->frame_rate_de);

	GST_DEBUG(" Frame Rate = %f \n", vpu_dec->frame_rate);
	gst_structure_get_int(structure, "width", &vpu_dec->picWidth);
	GST_DEBUG("\nInput Width is %d\n", vpu_dec->picWidth);
	gst_structure_get_int(structure, "height", &vpu_dec->picHeight);
	GST_DEBUG("\nInput Height is %d\n", vpu_dec->picHeight);

	codec_data = (GValue *) gst_structure_get_value(structure, "codec_data");
	if (codec_data) {
		vpu_dec->HdrExtData = gst_value_get_buffer(codec_data);
		vpu_dec->HdrExtDataLen = GST_BUFFER_SIZE(vpu_dec->HdrExtData);
		GST_DEBUG("Codec specific data length is %d\n", vpu_dec->HdrExtDataLen);
		GST_DEBUG("Header Extension Data is \n");
		hdrextdata = GST_BUFFER_DATA(vpu_dec->HdrExtData);
		for (i = 0; i < vpu_dec->HdrExtDataLen; i++)
			GST_DEBUG("%02x ", hdrextdata[i]);
		GST_DEBUG("\n");
	}

	if (vpu_dec->codec == STD_VC1) {
		gst_structure_get_int(structure, "wmvversion", &wmvversion);
		if (wmvversion != 3) {
			mfw_gst_vpudec_post_fatal_error_msg(vpu_dec,
							    "WMV Version error:This is a VC1 decoder supports "
							    "only WMV 9 Simple and Main Profile decode (WMV3)");
			gst_object_unref(vpu_dec);
			return FALSE;
		}

		if (!codec_data) {
			GST_ERROR
			    ("No Header Extension Data found during Caps Negotiation \n");
			mfw_gst_vpudec_post_fatal_error_msg(vpu_dec,
							    "No Extension Header Data Recieved from the Demuxer");
			gst_object_unref(vpu_dec);
			return FALSE;
		}
	}
	gst_object_unref(vpu_dec);
	return gst_pad_set_caps(pad, caps);
}

/*======================================================================================
FUNCTION:           mfw_gst_vpudec_base_init

DESCRIPTION:        Element details are registered with the plugin during
                    _base_init ,This function will initialise the class and child
                    class properties during each new child class creation

ARGUMENTS PASSED:   klass - void pointer

RETURN VALUE:       None
PRE-CONDITIONS:     None
POST-CONDITIONS:    None
IMPORTANT NOTES:    None
=======================================================================================*/
static void
mfw_gst_vpudec_base_init(MfwGstVPU_DecClass * klass)
{

	GstElementClass *element_class = GST_ELEMENT_CLASS(klass);

	gst_element_class_add_pad_template(element_class, src_templ());

	gst_element_class_add_pad_template(element_class,
					   gst_static_pad_template_get
					   (&mfw_gst_vpudec_sink_factory));

	gst_element_class_set_details(element_class, &mfw_gst_vpudec_details);

}

/*======================================================================================
FUNCTION:           mfw_gst_vpudec_codec_get_type

DESCRIPTION:        Gets an enumeration for the different
                    codec standars supported by the decoder

ARGUMENTS PASSED:   None

RETURN VALUE:       enumerated type of the codec standards
                    supported by the decoder

PRE-CONDITIONS:     None
POST-CONDITIONS:    None
IMPORTANT NOTES:    None
========================================================================================*/
GType
mfw_gst_vpudec_codec_get_type(void)
{
	static GType vpudec_codec_type = 0;
#if defined (VPU_MX51)
	static GEnumValue vpudec_codecs[] = {
		{STD_MPEG4, STR(STD_MPEG4), "std_mpeg4"},
		{STD_H263, STR(STD_H263), "std_h263"},
		{STD_AVC, STR(STD_AVC), "std_avc"},
		{STD_VC1, STR(STD_VC1), "std_vc1"},
		{STD_MJPG, STR(STD_MJPG), "std_mjpg"},
		{0, NULL, NULL},
	};
#elif defined (VPU_MX37)
	static GEnumValue vpudec_codecs[] = {
		{STD_MPEG4, STR(STD_MPEG4), "std_mpeg4"},
		{STD_H263, STR(STD_H263), "std_h263"},
		{STD_AVC, STR(STD_AVC), "std_avc"},
		{STD_VC1, STR(STD_VC1), "std_vc1"},
		{0, NULL, NULL},
	};
#else
	static GEnumValue vpudec_codecs[] = {
		{STD_MPEG4, STR(STD_MPEG4), "std_mpeg4"},
		{STD_H263, STR(STD_H263), "std_h263"},
		{STD_AVC, STR(STD_AVC), "std_avc"},
		{0, NULL, NULL},
	};
#endif
	if (!vpudec_codec_type) {
		vpudec_codec_type =
		    g_enum_register_static("MfwGstVpuDecCodecs", vpudec_codecs);
	}
	return vpudec_codec_type;
}

/*======================================================================================
FUNCTION:           mfw_gst_vpudec_mirror_get_type

DESCRIPTION:        Gets an enumeration for mirror directions

ARGUMENTS PASSED:   None

RETURN VALUE:       Enumerated type of the mirror directions supported by VPU

PRE-CONDITIONS:     None
POST-CONDITIONS:    None
IMPORTANT NOTES:    None
========================================================================================*/
GType
mfw_gst_vpudec_mirror_get_type(void)
{
	static GType vpudec_mirror_type = 0;
	static GEnumValue vpudec_mirror[] = {
		{MIRDIR_NONE, STR(MIRDIR_NONE), "none"},
		{MIRDIR_VER, STR(MIRDIR_VER), "ver"},
		{MIRDIR_HOR, STR(MIRDIR_HOR), "hor"},
		{MIRDIR_HOR_VER, STR(MIRDIR_HOR_VER), "hor_ver"},
		{0, NULL, NULL},
	};
	if (!vpudec_mirror_type) {
		vpudec_mirror_type =
		    g_enum_register_static("MfwGstVpuDecMirror", vpudec_mirror);
	}
	return vpudec_mirror_type;
}

/*======================================================================================
FUNCTION:           mfw_gst_vpudec_class_init

DESCRIPTION:        Initialise the class.(specifying what signals,
                    arguments and virtual functions the class has and setting up
                    global states)

ARGUMENTS PASSED:
                klass - pointer to H.264Decoder element class

RETURN VALUE:       None
PRE-CONDITIONS:     None
POST-CONDITIONS:    None
IMPORTANT NOTES:    None
=======================================================================================*/

static void
mfw_gst_vpudec_class_init(MfwGstVPU_DecClass * klass)
{

	GObjectClass *gobject_class = NULL;
	GstElementClass *gstelement_class = NULL;
	gobject_class = (GObjectClass *) klass;
	gstelement_class = (GstElementClass *) klass;
	gstelement_class->change_state = mfw_gst_vpudec_change_state;
	gobject_class->set_property = mfw_gst_vpudec_set_property;
	gobject_class->get_property = mfw_gst_vpudec_get_property;
	g_object_class_install_property(gobject_class, MFW_GST_VPU_PROF_ENABLE,
					g_param_spec_boolean("profiling",
							     "Profiling",
							     "enable time profiling of the vpu decoder plug-in",
							     FALSE,
							     G_PARAM_READWRITE));

	g_object_class_install_property(gobject_class, MFW_GST_VPU_CODEC_TYPE,
					g_param_spec_enum("codec-type",
							  "codec_type",
							  "selects the codec type for decoding",
							  MFW_GST_TYPE_VPU_DEC_CODEC,
							  STD_AVC,
							  G_PARAM_READWRITE));

	g_object_class_install_property(gobject_class, MFW_GST_VPU_LOOPBACK,
					g_param_spec_boolean("loopback",
							     "LoopBack",
							     "enables the decoder plug-in to operate"
							     "in loopback mode with encoder ",
							     FALSE,
							     G_PARAM_READWRITE));

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

/*======================================================================================
FUNCTION:           mfw_gst_vpudec_init

DESCRIPTION:        Create the pad template that has been registered with the
                    element class in the _base_init

ARGUMENTS PASSED:
                vpu_dec -    pointer to vpu_decoder element structure

RETURN VALUE:       None
PRE-CONDITIONS:     None
POST-CONDITIONS:    None
IMPORTANT NOTES:    None
=======================================================================================*/

static void
mfw_gst_vpudec_init(MfwGstVPU_Dec * vpu_dec, MfwGstVPU_DecClass * gclass)
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

#ifdef VPU_MX27
	gst_pad_set_chain_function(vpu_dec->sinkpad,
				   mfw_gst_vpudec_chain_stream_mode);
#else
	gst_pad_set_chain_function(vpu_dec->sinkpad,
				   mfw_gst_vpudec_chain_file_mode);
#endif

	gst_pad_set_setcaps_function(vpu_dec->sinkpad, mfw_gst_vpudec_setcaps);
	gst_pad_set_event_function(vpu_dec->sinkpad,
				   GST_DEBUG_FUNCPTR
				   (mfw_gst_vpudec_sink_event));

	vpu_dec->rotation_angle = 0;
	vpu_dec->mirror_dir = MIRDIR_NONE;
	vpu_dec->codec = STD_AVC;
	vpu_dec->loopback = FALSE;

	vpu_dec->vpu_mutex = g_mutex_new();
	vpu_dec->lastframedropped = FALSE;

	vpu_mutex_lock(vpu_dec->vpu_mutex);
	vpu_dec->is_startframe = FALSE;
	vpu_mutex_unlock(vpu_dec->vpu_mutex);

	vpu_dec->dbk_enabled = FALSE;
	vpu_dec->dbk_offset_a = vpu_dec->dbk_offset_b =
	    DEFAULT_DBK_OFFSET_VALUE;

}

/*======================================================================================
FUNCTION:           mfw_gst_type_vpu_dec_get_type

DESCRIPTION:        Interfaces are initiated in this function.you can register one
                    or more interfaces after having registered the type itself.

ARGUMENTS PASSED:   None

RETURN VALUE:       A numerical value ,which represents the unique identifier
                    of this element.

PRE-CONDITIONS:     None
POST-CONDITIONS:    None
IMPORTANT NOTES:    None
========================================================================================*/
GType
mfw_gst_type_vpu_dec_get_type(void)
{
	static GType vpu_dec_type = 0;
	if (!vpu_dec_type) {
		static const GTypeInfo vpu_dec_info = {
			sizeof (MfwGstVPU_DecClass),
			(GBaseInitFunc) mfw_gst_vpudec_base_init,
			NULL,
			(GClassInitFunc) mfw_gst_vpudec_class_init,
			NULL,
			NULL,
			sizeof (MfwGstVPU_Dec),
			0,
			(GInstanceInitFunc) mfw_gst_vpudec_init,
		};
		vpu_dec_type = g_type_register_static(GST_TYPE_ELEMENT,
						      "MfwGstVPU_Dec",
						      &vpu_dec_info, 0);
	}
	GST_DEBUG_CATEGORY_INIT(mfw_gst_vpudec_debug,
				"mfw_vpudecoder", 0,
				"FreeScale's VPU  Decoder's Log");
	return vpu_dec_type;
}
