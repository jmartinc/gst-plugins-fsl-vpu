/*
 * Copyright 2005-2007 Freescale Semiconductor, Inc. All Rights Reserved.
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

/*======================================================================================

    Module Name:            mfw_gst_vpu_encoder.c

    General Description:    Implementation of Hardware (VPU) Encoder Plugin for Gstreamer.

========================================================================================
Portability:    compatable with Linux OS and Gstreamer 10.11 and below

========================================================================================
                            INCLUDE FILES
=======================================================================================*/
#include <gst/gst.h>
#include <string.h>
#include <fcntl.h>		/* fcntl */
#include <sys/mman.h>		/* mmap */
#include <sys/ioctl.h>		/* fopen/fread */
#include <gst-plugins-fsl-vpu_config.h>
#include "vpu_io.h"
#include "vpu_lib.h"
#include "mfw_gst_vpu_encoder.h"

/*======================================================================================
                                     LOCAL CONSTANTS
=======================================================================================*/

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

/*======================================================================================
                          STATIC TYPEDEFS (STRUCTURES, UNIONS, ENUMS)
=======================================================================================*/

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

/*======================================================================================
                                        LOCAL MACROS
=======================================================================================*/

#define	GST_CAT_DEFAULT	mfw_gst_vpuenc_debug

/*======================================================================================
                                      STATIC VARIABLES
=======================================================================================*/

/*======================================================================================
                                 STATIC FUNCTION PROTOTYPES
=======================================================================================*/

GST_DEBUG_CATEGORY_STATIC(mfw_gst_vpuenc_debug);

static void mfw_gst_vpuenc_class_init(MfwGstVPU_EncClass *);
static void mfw_gst_vpuenc_base_init(MfwGstVPU_EncClass *);
static void mfw_gst_vpuenc_init(MfwGstVPU_Enc *, MfwGstVPU_EncClass *);
static GstFlowReturn mfw_gst_vpuenc_chain(GstPad *, GstBuffer *);
static GstStateChangeReturn mfw_gst_vpuenc_change_state
    (GstElement *, GstStateChange);
static void mfw_gst_vpuenc_set_property(GObject *, guint, const GValue *,
					GParamSpec *);
static void mfw_gst_vpuenc_get_property(GObject *, guint, GValue *,
					GParamSpec *);
static int mfw_gst_vpuenc_FrameBuffer_alloc(int strideY, int height,
					    FRAME_BUF * FrameBuf,
					    int FrameNumber);
static gboolean mfw_gst_vpuenc_setcaps(GstPad *, GstCaps *);

/*======================================================================================
                                     GLOBAL VARIABLES
=======================================================================================*/

/*======================================================================================
                                     LOCAL FUNCTIONS
=======================================================================================*/

/*=============================================================================
FUNCTION: mfw_gst_vpuenc_set_property

DESCRIPTION: sets the property of the element

ARGUMENTS PASSED:
        object     - pointer to the elements object
        prop_id    - ID of the property;
        value      - value of the property set by the application
        pspec      - pointer to the attributes of the property

RETURN VALUE:
        None

PRE-CONDITIONS:
        None

POST-CONDITIONS:

IMPORTANT NOTES:
        None
=============================================================================*/

static void
mfw_gst_vpuenc_set_property(GObject * object, guint prop_id,
			    const GValue * value, GParamSpec * pspec)
{
	GST_DEBUG("mfw_gst_vpuenc_set_property");

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

/*=============================================================================
FUNCTION: mfw_gst_vpuenc_set_property

DESCRIPTION: gets the property of the element

ARGUMENTS PASSED:
        object     - pointer to the elements object
        prop_id    - ID of the property;
        value      - value of the property set by the application
        pspec      - pointer to the attributes of the property

RETURN VALUE:
        None

PRE-CONDITIONS:
        None

POST-CONDITIONS:

IMPORTANT NOTES:
        None
=============================================================================*/
static void
mfw_gst_vpuenc_get_property(GObject * object, guint prop_id,
			    GValue * value, GParamSpec * pspec)
{

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

/*=============================================================================
FUNCTION:       mfw_gst_vpuenc_FrameBuffer_alloc

DESCRIPTION:    Allocates the Framebuffers required to store the
                input frames for the encoder.

ARGUMENTS PASSED:
        strideY         - width of the image
        height          - height of the image.
        FrameBuf        - pointer to structure used for allocating the
                          frame buffers.
        FrameNumber     - Number of Frame Buffers to be allocated.

RETURN VALUE:
        0               - Succesful Allocation
       -1               - Error in Allocation.

PRE-CONDITIONS:
        None

POST-CONDITIONS:

IMPORTANT NOTES:
        None
=============================================================================*/
static int
mfw_gst_vpuenc_FrameBuffer_alloc(int strideY, int height,
				 FRAME_BUF * FrameBuf, int FrameNumber)
{
	int i;

	for (i = 0; i < FrameNumber; i++) {
		memset(&(FrameBuf[i].CurrImage), 0, sizeof (vpu_mem_desc));
		FrameBuf[i].CurrImage.size =
		    (strideY * height * CHROMA_SAMPLING_MULTIPLE);

		IOGetPhyMem(&(FrameBuf[i].CurrImage));

		if (FrameBuf[i].CurrImage.phy_addr == 0) {
			int j;
			for (j = 0; j < i; j++) {
				IOFreeVirtMem(&(FrameBuf[j].CurrImage));
				IOFreePhyMem(&(FrameBuf[j].CurrImage));
			}
			GST_ERROR("Not enough mem for framebuffer!\n");
			return -1;
		}
		FrameBuf[i].Index = i;
		FrameBuf[i].AddrY = FrameBuf[i].CurrImage.phy_addr;
		FrameBuf[i].AddrCb = FrameBuf[i].AddrY + strideY * height;
		FrameBuf[i].AddrCr =
		    FrameBuf[i].AddrCb + strideY / 2 * height / 2;
		FrameBuf[i].StrideY = strideY;
		FrameBuf[i].StrideC = strideY / 2;
		FrameBuf[i].DispY = FrameBuf[i].AddrY;
		FrameBuf[i].DispCb = FrameBuf[i].AddrCb;
		FrameBuf[i].DispCr = FrameBuf[i].AddrCr;
		FrameBuf[i].CurrImage.virt_uaddr =
		    IOGetVirtMem(&(FrameBuf[i].CurrImage));
	}
	return 0;
}

/*=============================================================================
FUNCTION:       mfw_gst_vpuenc_cleanup

DESCRIPTION:    Closes the Encoder and frees all the memory allocated

ARGUMENTS PASSED:
        vpu_enc         - Plug-in context.

RETURN VALUE:
       None

PRE-CONDITIONS:
        None

POST-CONDITIONS:

IMPORTANT NOTES:
        None
=============================================================================*/

static void
mfw_gst_vpuenc_cleanup(MfwGstVPU_Enc * vpu_enc)
{

	int i = 0;
	RetCode vpu_ret = RETCODE_SUCCESS;
	int ret = 0;
	GST_DEBUG("mfw_gst_vpuenc_cleanup");

	if (vpu_enc->directrender == FALSE) {

		for (i = 0; i < NUM_INPUT_BUF; i++) {
			IOFreeVirtMem(&(vpu_enc->FrameBufPool[i].CurrImage));
			IOFreePhyMem(&(vpu_enc->FrameBufPool[i].CurrImage));
		}
	}

	if (vpu_enc->handle > 0) {
		vpu_ret = vpu_EncClose(vpu_enc->handle);
		if (vpu_ret == RETCODE_FRAME_NOT_COMPLETE) {
			vpu_EncGetOutputInfo(vpu_enc->handle,
					     vpu_enc->outputInfo);
			vpu_ret = vpu_EncClose(vpu_enc->handle);
			if (ret < 0) {
				GST_ERROR("Error in closing the VPU encoder"
					  ",error is %d\n", vpu_ret);
			}
		}
	}

	if (vpu_enc->encOP != NULL) {
		g_free(vpu_enc->encOP);
		vpu_enc->encOP = NULL;
	}
	if (vpu_enc->initialInfo != NULL) {
		g_free(vpu_enc->initialInfo);
		vpu_enc->initialInfo = NULL;
	}
	if (vpu_enc->encParam != NULL) {
		g_free(vpu_enc->encParam);
		vpu_enc->encParam = NULL;
	}
	if (vpu_enc->outputInfo != NULL) {
		g_free(vpu_enc->outputInfo);
		vpu_enc->outputInfo = NULL;
	}

	for (i = 0; i < vpu_enc->headercount; i++) {
		g_free(vpu_enc->header[i]);
	}
	vpu_enc->headercount = 0;

	IOFreePhyMem(&(vpu_enc->bit_stream_buf));
	IOFreeVirtMem(&(vpu_enc->bit_stream_buf));

}

/*=============================================================================
FUNCTION:       mfw_gst_encoder_fill_headers

DESCRIPTION:    Writes the Headers incase of MPEG4 and H.264 before encoding the
                Frame.

ARGUMENTS PASSED:
        vpu_enc         - Plug-in context.

RETURN VALUE:
       None

PRE-CONDITIONS:
        None

POST-CONDITIONS:

IMPORTANT NOTES:
        None
=============================================================================*/

static int
mfw_gst_encoder_fill_headers(MfwGstVPU_Enc * vpu_enc)
{
	EncHeaderParam enchdr_param = { 0 };

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

		ptr =
		    vpu_enc->start_addr + enchdr_param.buf -
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

/*======================================================================================

FUNCTION:          mfw_gst_vpuenc_chain

DESCRIPTION:       The main processing function where the data comes in as buffer. This
                    data is encoded, and then pushed onto the next element for further
                    processing.

ARGUMENTS PASSED:  pad - pointer to the sinkpad of this element
                   buffer - pointer to the input buffer which has the H.264 data.

RETURN VALUE:
                    GstFlowReturn - Success of Failure.

PRE-CONDITIONS:
                    None

POST-CONDITIONS:
                    None

IMPORTANT NOTES:
                    None

=======================================================================================*/
static GstFlowReturn
mfw_gst_vpuenc_chain(GstPad * pad, GstBuffer * buffer)
{
	MfwGstVPU_Enc *vpu_enc = NULL;
	RetCode vpu_ret = RETCODE_SUCCESS;
	GstFlowReturn retval = GST_FLOW_OK;
	GstCaps *src_caps = NULL;;
	GstCaps *caps = NULL;;
	GstBuffer *outbuffer = NULL;
	gint i = 0;
	EncInitialInfo initialInfo = { 0 };
	gint totalsize = 0;
	gint offset = 0;
	FRAME_BUF *pFrame[NUM_INPUT_BUF] = { 0 };
	gint ret;
	gchar *mime = "undef";

	GST_DEBUG("mfw_gst_vpuenc_chain");
	vpu_enc = MFW_GST_VPU_ENC(GST_PAD_PARENT(pad));
	if (vpu_enc->init == FALSE) {

		/* store the physical addresses of the buffers used by the source
		   to register them in the encoder */
		if (GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_LAST)) {

			i = vpu_enc->numframebufs;
			vpu_enc->frameBuf[i].bufY = GST_BUFFER_OFFSET(buffer);
			vpu_enc->frameBuf[i].bufCb = GST_BUFFER_OFFSET(buffer) +
			    (vpu_enc->width * vpu_enc->height);
			vpu_enc->frameBuf[i].bufCr =
			    vpu_enc->frameBuf[i].bufCb +
			    (vpu_enc->width * vpu_enc->height) / 4;

			vpu_enc->directrender = TRUE;
			vpu_enc->numframebufs++;
			if (vpu_enc->numframebufs < NUM_INPUT_BUF) {
				return GST_FLOW_OK;
			}
		}

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

		/* */
		vpu_enc->encOP->picWidth = vpu_enc->width;
		vpu_enc->encOP->picHeight = vpu_enc->height;

		/* The Frame Rate Value is set only in case of MPEG4 and H.264
		   not set for H.263 */
		if (vpu_enc->encOP->frameRateInfo != 0x3E87530)
			vpu_enc->encOP->frameRateInfo =
			    (gint) (vpu_enc->framerate + 0.5);

		/* open a VPU's encoder instance */
		vpu_ret = vpu_EncOpen(&vpu_enc->handle, vpu_enc->encOP);
		if (vpu_ret != RETCODE_SUCCESS) {
			GST_ERROR("vpu_EncOpen failed. Error code is %d \n",
				  vpu_ret);
			GError *error = NULL;
			GQuark domain;
			domain = g_quark_from_string("mfw_vpuencoder");
			error = g_error_new(domain, 10, "fatal error");
			gst_element_post_message(GST_ELEMENT(vpu_enc),
						 gst_message_new_error
						 (GST_OBJECT(vpu_enc), error,
						  "vpu_EncOpen failed"));
			return GST_FLOW_ERROR;
		}

		/* get the minum number of framebuffers to be allocated  */
		vpu_ret = vpu_EncGetInitialInfo(vpu_enc->handle, &initialInfo);
		if (vpu_ret != RETCODE_SUCCESS) {
			GST_ERROR
			    ("vpu_EncGetInitialInfo failed. Error code is %d \n",
			     vpu_ret);
			GError *error = NULL;
			GQuark domain;
			domain = g_quark_from_string("mfw_vpuencoder");
			error = g_error_new(domain, 10, "fatal error");
			gst_element_post_message(GST_ELEMENT(vpu_enc),
						 gst_message_new_error
						 (GST_OBJECT(vpu_enc), error,
						  "vpu_EncGetInitialInfo failed"));
			return GST_FLOW_ERROR;

		}

		GST_DEBUG("Enc: min buffer count= %d",
			  initialInfo.minFrameBufferCount);

		/* allocate the frame buffers if the buffers cannot be shared with the
		   source element */
		if (vpu_enc->directrender == FALSE) {
			ret = mfw_gst_vpuenc_FrameBuffer_alloc(vpu_enc->width,
							       vpu_enc->height,
							       vpu_enc->
							       FrameBufPool,
							       NUM_INPUT_BUF);

			if (ret < 0) {
				GError *error = NULL;
				GQuark domain;
				domain = g_quark_from_string("mfw_vpuencoder");
				error = g_error_new(domain, 10, "fatal error");
				gst_element_post_message(GST_ELEMENT(vpu_enc),
							 gst_message_new_error
							 (GST_OBJECT(vpu_enc),
							  error,
							  "Allocation for frame buffers failed "));
				return GST_FLOW_ERROR;
			}
			for (i = 0; i < NUM_INPUT_BUF; ++i) {
				pFrame[i] = &vpu_enc->FrameBufPool[i];
				vpu_enc->frameBuf[i].bufY = pFrame[i]->AddrY;
				vpu_enc->frameBuf[i].bufCb = pFrame[i]->AddrCb;
				vpu_enc->frameBuf[i].bufCr = pFrame[i]->AddrCr;
			}
		}

		/* register the framebuffers with the encoder */
		vpu_ret = vpu_EncRegisterFrameBuffer(vpu_enc->handle,
						     vpu_enc->frameBuf,
						     NUM_INPUT_BUF - 1,
						     vpu_enc->width);
		if (vpu_ret != RETCODE_SUCCESS) {
			GST_ERROR
			    ("vpu_EncRegisterFrameBuffer failed.Error code is %d \n",
			     vpu_ret);
			GError *error = NULL;
			GQuark domain;
			domain = g_quark_from_string("mfw_vpuencoder");
			error = g_error_new(domain, 10, "fatal error");
			gst_element_post_message(GST_ELEMENT(vpu_enc),
						 gst_message_new_error
						 (GST_OBJECT(vpu_enc), error,
						  "vpu_EncRegisterFrameBuffer failed "));
			return GST_FLOW_ERROR;
		}

		vpu_enc->encParam->quantParam = 30;
		vpu_enc->encParam->forceIPicture = 0;
		vpu_enc->encParam->skipPicture = 0;
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

		vpu_enc->init = TRUE;
	}

	if (vpu_enc->directrender == FALSE) {

		/* copy the input Frame into the allocated buffer */
		i = vpu_enc->numframebufs;
		vpu_enc->encParam->sourceFrame = &vpu_enc->frameBuf[i];
		memcpy((guint8 *) vpu_enc->FrameBufPool[i].CurrImage.virt_uaddr,
		       GST_BUFFER_DATA(buffer), GST_BUFFER_SIZE(buffer));
		vpu_enc->numframebufs = (vpu_enc->numframebufs + 1) % 3;
		gst_buffer_unref(buffer);
	} else {

		/* search the exact Frame buffer in which the incoming frame is
		   present */
		for (i = 0; i < NUM_INPUT_BUF; i++) {
			if (vpu_enc->frameBuf[i].bufY ==
			    GST_BUFFER_OFFSET(buffer))
				break;
		}
		if (i == NUM_INPUT_BUF) {
			GST_ERROR("vpuenc:could not find the right frame\n");
			return GST_FLOW_ERROR;
		}
		vpu_enc->encParam->sourceFrame = &vpu_enc->frameBuf[i];
	}

	/* Wait for the VPU to complete the Processing */
	if (vpu_enc->wait == TRUE) {
		while (vpu_IsBusy());

		vpu_ret = vpu_EncGetOutputInfo(vpu_enc->handle,
					       vpu_enc->outputInfo);
		if (vpu_ret != RETCODE_SUCCESS) {
			GST_ERROR
			    ("vpu_EncGetOutputInfo failed. Error code is %d \n",
			     vpu_ret);
			return GST_FLOW_ERROR;
		}

		src_caps = GST_PAD_CAPS(vpu_enc->srcpad);

		for (i = 0; i < vpu_enc->headercount; i++)
			totalsize += vpu_enc->headersize[i];

		retval = gst_pad_alloc_buffer_and_set_caps(vpu_enc->srcpad,
			0, vpu_enc->outputInfo->bitstreamSize + totalsize,
			src_caps, &outbuffer);

		if (retval != GST_FLOW_OK) {
			GST_ERROR
			    ("Error in allocating the Framebuffer[%d],"
			     " error is %d", i, retval);
			return retval;
		}

		for (i = 0; i < vpu_enc->headercount; i++) {
			memcpy(GST_BUFFER_DATA(outbuffer) + offset,
			       vpu_enc->header[i], vpu_enc->headersize[i]);
			offset += vpu_enc->headersize[i];
		}

		memcpy(GST_BUFFER_DATA(outbuffer) + offset,
		       vpu_enc->start_addr, vpu_enc->outputInfo->bitstreamSize);

		GST_BUFFER_SIZE(outbuffer) =
				vpu_enc->outputInfo->bitstreamSize + totalsize;

		retval = gst_pad_push(vpu_enc->srcpad, outbuffer);
		if (retval != GST_FLOW_OK) {
			GST_ERROR("Error in Pushing the Output ont to "
				  "the Source Pad,error is %d \n",
				  retval);
		}
	}

	GST_DEBUG("bitsream size=%d", vpu_enc->outputInfo->bitstreamSize);
	GST_DEBUG("fram=%d", vpu_enc->frameIdx);

	vpu_ret = vpu_EncStartOneFrame(vpu_enc->handle, vpu_enc->encParam);
	if (vpu_ret != RETCODE_SUCCESS) {
		GST_ERROR("vpu_EncStartOneFrame failed. Error code is %d \n",
			  vpu_ret);
		return GST_FLOW_ERROR;
	}

	vpu_enc->frameIdx++;
	vpu_enc->wait = TRUE;
	return retval;
}

/*======================================================================================

FUNCTION:       mfw_gst_vpuenc_change_state

DESCRIPTION:    This function keeps track of different states of pipeline.

ARGUMENTS PASSED:
                element     -   pointer to element
                transition  -   state of the pipeline

RETURN VALUE:
                GST_STATE_CHANGE_FAILURE    - the state change failed
                GST_STATE_CHANGE_SUCCESS    - the state change succeeded
                GST_STATE_CHANGE_ASYNC      - the state change will happen
                                                asynchronously
                GST_STATE_CHANGE_NO_PREROLL - the state change cannot be prerolled

PRE-CONDITIONS:
                None

POST-CONDITIONS:
                None

IMPORTANT NOTES:
                None

=======================================================================================*/

static GstStateChangeReturn mfw_gst_vpuenc_change_state
    (GstElement * element, GstStateChange transition) {
	GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
	MfwGstVPU_Enc *vpu_enc = NULL;
	vpu_enc = MFW_GST_VPU_ENC(element);
	gint vpu_ret = 0;
	guint8 *virt_bit_stream_buf = NULL;
	CodStd mode;

	switch (transition) {
	case GST_STATE_CHANGE_NULL_TO_READY:
		{
			GST_DEBUG("VPU State: Null to Ready");
			vpu_ret = IOSystemInit(NULL);
			if (vpu_ret < 0) {
				GST_DEBUG("Error in initializing the VPU: error is %d", vpu_ret);
				return GST_STATE_CHANGE_FAILURE;
			}

			break;
		}
	case GST_STATE_CHANGE_READY_TO_PAUSED:
		{
			GST_DEBUG("VPU State: Ready to Paused");

			vpu_enc->encOP = g_malloc(sizeof (EncOpenParam));
			if (vpu_enc->encOP == NULL) {
				GST_DEBUG("Error in allocating encoder"
					  "open parameter structure");
				mfw_gst_vpuenc_cleanup(vpu_enc);
				return GST_STATE_CHANGE_FAILURE;
			}

			vpu_enc->initialInfo =
			    g_malloc(sizeof (EncInitialInfo));
			if (vpu_enc->initialInfo == NULL) {
				GST_DEBUG("Error in allocating encoder"
					  "initial info structure");
				mfw_gst_vpuenc_cleanup(vpu_enc);
				return GST_STATE_CHANGE_FAILURE;
			}

			vpu_enc->encParam = g_malloc(sizeof (EncParam));
			if (vpu_enc->encParam == NULL) {
				GST_DEBUG("Error in allocating encoder"
					  "parameter structure");
				mfw_gst_vpuenc_cleanup(vpu_enc);
				return GST_STATE_CHANGE_FAILURE;
			}

			vpu_enc->outputInfo = g_malloc(sizeof (EncOutputInfo));
			if (vpu_enc->outputInfo == NULL) {
				GST_DEBUG("Error in allocating encoder"
					  "output structure");
				mfw_gst_vpuenc_cleanup(vpu_enc);
				return GST_STATE_CHANGE_FAILURE;
			}

			memset(vpu_enc->initialInfo, 0,
			       sizeof (EncInitialInfo));
			memset(vpu_enc->encParam, 0, sizeof (EncParam));
			memset(vpu_enc->encOP, 0, sizeof (EncOpenParam));
			memset(vpu_enc->outputInfo, 0, sizeof (EncOutputInfo));
			memset(&vpu_enc->bit_stream_buf, 0,
			       sizeof (vpu_mem_desc));

			vpu_enc->bit_stream_buf.size = BUFF_FILL_SIZE;
			IOGetPhyMem(&vpu_enc->bit_stream_buf);
			virt_bit_stream_buf =
			    (guint8 *) IOGetVirtMem(&vpu_enc->bit_stream_buf);
			vpu_enc->start_addr = virt_bit_stream_buf;
			vpu_enc->encOP->bitstreamBuffer =
			    vpu_enc->bit_stream_buf.phy_addr;
			vpu_enc->encOP->bitstreamBufferSize = BUFF_FILL_SIZE;

			GST_DEBUG("codec=%d", vpu_enc->codec);
			mode = vpu_enc->encOP->bitstreamFormat = vpu_enc->codec;

			vpu_enc->init = FALSE;
			vpu_enc->wait = FALSE;
			vpu_enc->frameIdx = 0;
			vpu_enc->headercount = 0;
			vpu_enc->handle = 0;
			vpu_enc->encOP->bitRate = vpu_enc->bitrate;
			vpu_enc->encOP->initialDelay = 0;
			vpu_enc->encOP->vbvBufferSize = 0;	/* 0 = ignore 8 */
			vpu_enc->encOP->enableAutoSkip = 0;
			vpu_enc->encOP->gopSize = vpu_enc->gopsize;
			vpu_enc->encOP->slicemode.sliceMode = 1;	/* 1 slice per picture */
			vpu_enc->encOP->slicemode.sliceSizeMode = 0;
			vpu_enc->encOP->slicemode.sliceSize = 4000;	/* not used if sliceMode is 0 */
			vpu_enc->numframebufs = 0;

			if (mode == STD_MPEG4) {
				vpu_enc->encOP->EncStdParam.mp4Param.
				    mp4_dataPartitionEnable = 0;
				vpu_enc->encOP->EncStdParam.mp4Param.
				    mp4_reversibleVlcEnable = 0;
				vpu_enc->encOP->EncStdParam.mp4Param.
				    mp4_intraDcVlcThr = 0;
			} else if (mode == STD_H263) {

				vpu_enc->encOP->EncStdParam.h263Param.
				    h263_annexJEnable = 0;
				vpu_enc->encOP->EncStdParam.h263Param.
				    h263_annexKEnable = 0;
				vpu_enc->encOP->EncStdParam.h263Param.
				    h263_annexTEnable = 0;

				if (vpu_enc->encOP->EncStdParam.h263Param.
				    h263_annexJEnable == 0
				    && vpu_enc->encOP->EncStdParam.h263Param.
				    h263_annexKEnable == 0
				    && vpu_enc->encOP->EncStdParam.h263Param.
				    h263_annexTEnable == 0) {
					vpu_enc->encOP->frameRateInfo =
					    0x3E87530;
				}
			} else if (mode == STD_AVC) {
				vpu_enc->encOP->EncStdParam.avcParam.
				    avc_constrainedIntraPredFlag = 0;
				vpu_enc->encOP->EncStdParam.avcParam.
				    avc_disableDeblk = 0;
				vpu_enc->encOP->EncStdParam.avcParam.
				    avc_deblkFilterOffsetAlpha = 0;
				vpu_enc->encOP->EncStdParam.avcParam.
				    avc_deblkFilterOffsetBeta = 0;
				vpu_enc->encOP->EncStdParam.avcParam.
				    avc_chromaQpOffset = 0;
				vpu_enc->encOP->EncStdParam.avcParam.
				    avc_audEnable = 0;
				vpu_enc->encOP->EncStdParam.avcParam.
				    avc_fmoEnable = 0;
				vpu_enc->encOP->EncStdParam.avcParam.
				    avc_fmoType = 0;
				vpu_enc->encOP->EncStdParam.avcParam.
				    avc_fmoSliceNum = 0;
			} else {
				GST_ERROR
				    ("Encoder: Invalid codec standard mode");
				mfw_gst_vpuenc_cleanup(vpu_enc);
				return GST_STATE_CHANGE_FAILURE;
			}

			break;
		}
	case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
		{
			GST_DEBUG("VPU State: Paused to Playing");
			break;
		}
	default:
		break;
	}

	ret = vpu_enc->parent_class->change_state(element, transition);
	GST_DEBUG("State Change for VPU returned %d", ret);

	switch (transition) {
	case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
		{
			GST_DEBUG("VPU State: Playing to Paused");
			break;
		}
	case GST_STATE_CHANGE_PAUSED_TO_READY:
		{
			GST_DEBUG("VPU State: Paused to Ready");
			mfw_gst_vpuenc_cleanup(vpu_enc);
			break;
		}
	case GST_STATE_CHANGE_READY_TO_NULL:
		{
			GST_DEBUG("VPU State: Ready to Null");
			IOSystemShutdown();
			break;
		}
	default:
		break;
	}

	return ret;

}

/*==================================================================================================

FUNCTION:       mfw_gst_vpuenc_sink_event

DESCRIPTION:    send an event to sink  pad of mp3decoder element

ARGUMENTS PASSED:
        pad        -    pointer to pad
        event      -    pointer to event
RETURN VALUE:
        TRUE       -    event is handled properly
        FALSE      -	event is not handled properly

PRE-CONDITIONS:
        None

POST-CONDITIONS:
        None

IMPORTANT NOTES:
        None

==================================================================================================*/

static gboolean
mfw_gst_vpuenc_sink_event(GstPad * pad, GstEvent * event)
{
	MfwGstVPU_Enc *vpu_enc = NULL;
	gboolean ret = FALSE;
	vpu_enc = MFW_GST_VPU_ENC(GST_PAD_PARENT(pad));

	switch (GST_EVENT_TYPE(event)) {
	case GST_EVENT_NEWSEGMENT:
		{
			GstFormat format;
			gint64 start, stop, position;
			gdouble rate;

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
		}

	case GST_EVENT_EOS:
		{

			ret = gst_pad_push_event(vpu_enc->srcpad, event);

			if (TRUE != ret) {
				GST_ERROR
				    ("\n Error in pushing the event,result	is %d\n",
				     ret);
				gst_event_unref(event);
			}
			break;
		}
	default:
		{
			ret = gst_pad_event_default(pad, event);
			break;
		}
	}
	return ret;
}

/*======================================================================================

FUNCTION:       mfw_gst_vpuenc_setcaps

DESCRIPTION:    this function negoatiates the caps set on the sink pad

ARGUMENTS PASSED:
                pad     -   pointer to the sinkpad of this element
                caps  -     pointer to the caps set

RETURN VALUE:
               TRUE         negotiation success full
               FALSE        negotiation Failed

PRE-CONDITIONS:
                None

POST-CONDITIONS:
                None

IMPORTANT NOTES:
                None

=======================================================================================*/

static gboolean
mfw_gst_vpuenc_setcaps(GstPad * pad, GstCaps * caps)
{
	MfwGstVPU_Enc *vpu_enc = NULL;
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

/*======================================================================================

FUNCTION:          mfw_gst_vpuenc_base_init

DESCRIPTION:       Element details are registered with the plugin during
                   _base_init ,This function will initialise the class and child
                   class properties during each new child class creation

ARGUMENTS PASSED:  klass - void pointer

RETURN VALUE:
                    None

PRE-CONDITIONS:
                    None

POST-CONDITIONS:
                    None

IMPORTANT NOTES:
                    None

=======================================================================================*/
static void
mfw_gst_vpuenc_base_init(MfwGstVPU_EncClass * klass)
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

/*======================================================================================

FUNCTION:       mfw_gst_vpuenc_codec_get_type

DESCRIPTION:    Gets an enumeration for the different
                codec standars supported by the encoder
ARGUMENTS PASSED:
                None

RETURN VALUE:
                enumerated type of the codec standatds
                supported by the encoder

PRE-CONDITIONS:
                None

POST-CONDITIONS:
                None

IMPORTANT NOTES:
                None

========================================================================================*/

GType
mfw_gst_vpuenc_codec_get_type(void)
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

/*======================================================================================

FUNCTION:       mfw_gst_vpuenc_class_init

DESCRIPTION:    Initialise the class.(specifying what signals,
                 arguments and virtual functions the class has and setting up
                 global states)

ARGUMENTS PASSED:
                klass - pointer to H.264Encoder element class

RETURN VALUE:
                None

PRE-CONDITIONS:
                None

POST-CONDITIONS:
                None

IMPORTANT NOTES:
                None

=======================================================================================*/

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

/*======================================================================================
FUNCTION:       mfw_gst_vpuenc_init

DESCRIPTION:    create the pad template that has been registered with the
                element class in the _base_init

ARGUMENTS PASSED:
                vpu_enc -    pointer to vpu_encoder element structure

RETURN VALUE:
                None

PRE-CONDITIONS:
                None

POST-CONDITIONS:
                None

IMPORTANT NOTES:
                None

=======================================================================================*/

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

/*======================================================================================

FUNCTION:       plugin_init

DESCRIPTION:    special function , which is called as soon as the plugin or
                element is loaded and information returned by this function
                will be cached in central registry

ARGUMENTS PASSED:
		        plugin - pointer to container that contains features loaded
                        from shared object module

RETURN VALUE:
		        return TRUE or FALSE depending on whether it loaded initialized any
                dependency correctly

PRE-CONDITIONS:
		        None

POST-CONDITIONS:
		        None

IMPORTANT NOTES:
		        None

=======================================================================================*/

static gboolean
plugin_init(GstPlugin * plugin)
{

	return gst_element_register(plugin, "mfw_vpuencoder",
				    GST_RANK_PRIMARY, MFW_GST_TYPE_VPU_ENC);
}

GST_PLUGIN_DEFINE(GST_VERSION_MAJOR,	/* major version of gstreamer */
		  GST_VERSION_MINOR,	/* minor version of gstreamer */
		  "mfw_vpuencoder",	/* name of our  plugin */
		  "Encodes Raw YUV Data to MPEG4 SP," "or H.264 BP, or H.263 Format" "data to Raw YUV Data ",	/* what our plugin actually does */
		  plugin_init,	/* first function to be called */
		  VERSION,
		  GST_LICENSE_UNKNOWN,
		  "freescale semiconductor", "www.freescale.com")

/*======================================================================================

FUNCTION:       mfw_gst_type_vpu_enc_get_type

DESCRIPTION:    Intefaces are initiated in this function.you can register one
                or more interfaces after having registered the type itself.

ARGUMENTS PASSED:
                None

RETURN VALUE:
                A numerical value ,which represents the unique identifier of this
                elment.

PRE-CONDITIONS:
                None

POST-CONDITIONS:
                None

IMPORTANT NOTES:
                None

========================================================================================*/
GType
mfw_gst_type_vpu_enc_get_type(void)
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
