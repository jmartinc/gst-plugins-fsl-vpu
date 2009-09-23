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

    Module Name:            mfw_gst_vpu_decoder.c

    General Description:    Implementation of Hardware (VPU) Decoder Plugin for Gstreamer.

========================================================================================
Portability:    compatable with Linux OS and Gstreamer 10.11 and above

========================================================================================
                            INCLUDE FILES
=======================================================================================*/
#include <gst/gst.h>
#include <gst/base/gstadapter.h>
#include <string.h>
#include <fcntl.h>		/* fcntl */
#include <sys/mman.h>		/* mmap */
#include <sys/ioctl.h>		/* fopen/fread */		
#include <sys/time.h>
#include <gst-plugins-fsl-vpu_config.h>
#include "vpu_io.h"
#include "vpu_lib.h"
#include "mfw_gst_vpu_decoder.h"

/*======================================================================================
                                     LOCAL CONSTANTS
=======================================================================================*/
#define BUFF_FILL_SIZE (200 * 1024)

/* The processor clock is 333 MHz for  MX27 
to be chnaged for other platforms */
#define PROCESSOR_CLOCK    333

#define MFW_GST_VPUDEC_VIDEO_CAPS \
    "video/mpeg, " \
    "width = (int) [16, 1280], " \
    "height = (int) [16, 720]; " \
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
    "height = (int)[16, 720] "



/*======================================================================================
                          STATIC TYPEDEFS (STRUCTURES, UNIONS, ENUMS)
=======================================================================================*/

enum {
    MFW_GST_VPU_PROP_0,
    MFW_GST_VPU_CODEC_TYPE,
    MFW_GST_VPU_PROF_ENABLE,
    MFW_GST_VPU_LOOPBACK
};

/* get the element details */
static GstElementDetails mfw_gst_vpudec_details =
    GST_ELEMENT_DETAILS("Freescale: Hardware (VPU) Decoder",
		    "Codec/Decoder/Video",
		    "Decodes H.264, MPEG4, H263 and VC-1" 
            "(VC-1 Supported only in i.MX32 )"  
            "Elementary data into YUV 4:2:0 data",
		    "Multimedia Team <mmsw@freescale.com>");

/* defines sink pad  properties of the VPU Decoder element */
static GstStaticPadTemplate mfw_gst_vpudec_sink_factory =
    GST_STATIC_PAD_TEMPLATE("sink",
			GST_PAD_SINK,
			GST_PAD_ALWAYS,
			GST_STATIC_CAPS(MFW_GST_VPUDEC_VIDEO_CAPS));


/* defines the source pad  properties of VPU Decoder element */
static GstStaticPadTemplate mfw_gst_vpudec_src_factory =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw-yuv, "
        "format = (fourcc) {I420}, "
        "width = (int) [ 16, 4096 ], "
        "height = (int) [ 16, 4096 ], "
        "framerate = (fraction) [ 0/1, 2147483647/1 ]")
    );

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

static void mfw_gst_vpudec_class_init   (MfwGstVPU_DecClass *);
static void mfw_gst_vpudec_base_init    (MfwGstVPU_DecClass *);
static void mfw_gst_vpudec_init         (MfwGstVPU_Dec *, MfwGstVPU_DecClass *);
static GstFlowReturn mfw_gst_vpudec_chain              
                                        (GstPad *, GstBuffer *);
static GstStateChangeReturn mfw_gst_vpudec_change_state    
                                        (GstElement *, GstStateChange);
static void mfw_gst_vpudec_set_property (GObject *,guint,const GValue *,GParamSpec *);
static void mfw_gst_vpudec_get_property (GObject *,guint,GValue *,GParamSpec *);
static gint mfw_gst_vpudec_FrameBufferInit
                                        (MfwGstVPU_Dec *,FrameBuffer *,gint);
static gboolean mfw_gst_vpudec_sink_event
                                        (GstPad *, GstEvent *);
static gboolean mfw_gst_vpudec_setcaps  (GstPad * , GstCaps *);
/*======================================================================================
                                     GLOBAL VARIABLES
=======================================================================================*/
/*======================================================================================
                                     LOCAL FUNCTIONS
=======================================================================================*/

/* helper function for float comaprision with 0.00001 precision */
#define FLOAT_MATCH 1
#define FLOAT_UNMATCH 0
static inline guint g_compare_float(const gfloat a, const gfloat b)
{
	const gfloat precision = 0.00001;
	if (((a - precision) < b) && (a + precision) > b)
		return FLOAT_MATCH;
	else
		return FLOAT_UNMATCH;
}

/*=============================================================================
FUNCTION: mfw_gst_vpudec_set_property

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

static void mfw_gst_vpudec_set_property(GObject * object, guint prop_id,
					const GValue * value,
					GParamSpec * pspec)
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
    default:
	G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
	break;
    }
    return;
}
/*=============================================================================
FUNCTION: mfw_gst_vpudec_set_property

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
static void mfw_gst_vpudec_get_property(GObject * object, guint prop_id,
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
    default:
	G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
	break;
    }
    return;
}


/*=============================================================================
FUNCTION:               mfw_gst_vpudec_post_fatal_error_msg

DESCRIPTION:            This function is used to post a fatal error message and 
                         terminate the pipeline during an unrecoverable error.
                        

ARGUMENTS PASSED:
                        vpu_dec  - VPU decoder plugins context
                        error_msg message to be posted 
        

RETURN VALUE:
        None

PRE-CONDITIONS:
        None

POST-CONDITIONS:


IMPORTANT NOTES:
        None
=============================================================================*/

static void mfw_gst_vpudec_post_fatal_error_msg(MfwGstVPU_Dec *vpu_dec,gchar *error_msg)
{
    GError *error = NULL;
    GQuark domain;
    domain = g_quark_from_string("mfw_vpudecoder");
    error = g_error_new(domain, 10, "fatal error");
    gst_element_post_message(GST_ELEMENT(vpu_dec),
        gst_message_new_error(GST_OBJECT
        (vpu_dec),error,error_msg));
    g_error_free(error);
}


/*=============================================================================
FUNCTION:               mfw_gst_VC1_Create_RCVheader

DESCRIPTION:            This function is used to create the RCV header 
                        for integration with the ASF demuxer using the width,height and the 
                        Header Extension data recived through caps negotiation.
                        

ARGUMENTS PASSED:
                        vpu_dec  - VPU decoder plugins context
        

RETURN VALUE:
        None

PRE-CONDITIONS:
        None

POST-CONDITIONS:


IMPORTANT NOTES:
        None
=============================================================================*/
static gint mfw_gst_VC1_Create_RCVheader(MfwGstVPU_Dec *vpu_dec,GstBuffer *inbuffer)
{
    unsigned char *RCVHeaderData=NULL;
    unsigned int value=0;
    int i =0;
    RetCode vpu_ret=RETCODE_SUCCESS;
    RCVHeaderData = vpu_dec->start_addr;

    //Number of Frames, Header Extension Bit, Codec Version
    value = NUM_FRAMES | SET_HDR_EXT | CODEC_VERSION;
    RCVHeaderData[i++] = (unsigned char)value;
    RCVHeaderData[i++] = (unsigned char)(value >> 8);
    RCVHeaderData[i++] = (unsigned char)(value >> 16);
    RCVHeaderData[i++] = (unsigned char)(value >> 24);
    //Header Extension Size
    //ASF Parser gives 5 bytes whereas the VPU expects only 4 bytes, so limiting it
    if(vpu_dec->HdrExtDataLen > 4)
        vpu_dec->HdrExtDataLen = 4;
    RCVHeaderData[i++] = (unsigned char)vpu_dec->HdrExtDataLen;
    RCVHeaderData[i++] = (unsigned char)(vpu_dec->HdrExtDataLen >> 8);
    RCVHeaderData[i++] = (unsigned char)(vpu_dec->HdrExtDataLen >> 16);
    RCVHeaderData[i++] = (unsigned char)(vpu_dec->HdrExtDataLen >> 24);
 
    //Header Extension bytes obtained during negotiation
    memcpy(RCVHeaderData+i,GST_BUFFER_DATA(vpu_dec->HdrExtData)
        ,vpu_dec->HdrExtDataLen);
    i += vpu_dec->HdrExtDataLen;
    //Height
    RCVHeaderData[i++] = (unsigned char)vpu_dec->picHeight;
    RCVHeaderData[i++] = (unsigned char)(((vpu_dec->picHeight >> 8) & 0xff));
    RCVHeaderData[i++] = (unsigned char)(((vpu_dec->picHeight >> 16) & 0xff));
    RCVHeaderData[i++] = (unsigned char)(((vpu_dec->picHeight >> 24) & 0xff));
    //Width
    RCVHeaderData[i++] = (unsigned char)vpu_dec->picWidth;
    RCVHeaderData[i++] = (unsigned char)(((vpu_dec->picWidth >> 8) & 0xff));
    RCVHeaderData[i++] = (unsigned char)(((vpu_dec->picWidth >> 16) & 0xff)); 
    RCVHeaderData[i++] = (unsigned char)(((vpu_dec->picWidth >> 24) & 0xff));
    //Frame Size
    RCVHeaderData[i++] = (unsigned char)GST_BUFFER_SIZE(inbuffer);
    RCVHeaderData[i++] = (unsigned char)(GST_BUFFER_SIZE(inbuffer) >> 8);
    RCVHeaderData[i++] = (unsigned char)(GST_BUFFER_SIZE(inbuffer) >> 16);
    RCVHeaderData[i++] = (unsigned char)((GST_BUFFER_SIZE(inbuffer) >> 24) | 0x80);
    vpu_dec->start_addr += i;
    vpu_dec->frame_sizes_buffer[vpu_dec->buffidx_in]=GST_BUFFER_SIZE(inbuffer)+i;
    vpu_dec->buffidx_in = (vpu_dec->buffidx_in+1)%MAX_STREAM_BUF;
    vpu_ret = vpu_DecUpdateBitstreamBuffer(*(vpu_dec->handle),i);
    if (vpu_ret != RETCODE_SUCCESS) {
        GST_ERROR("vpu_DecUpdateBitstreamBuffer failed. Error code is %d \n",
            vpu_ret);
        return -1;
    }
    return 0;


}

/*=============================================================================
FUNCTION:               mfw_gst_vpudec_FrameBufferClose

DESCRIPTION:            This function frees the allocated frame buffers
                        

ARGUMENTS PASSED:
                        vpu_dec  - VPU decoder plugins context
        

RETURN VALUE:
        None

PRE-CONDITIONS:
        None

POST-CONDITIONS:


IMPORTANT NOTES:
        None
=============================================================================*/

static void mfw_gst_vpudec_FrameBufferClose(MfwGstVPU_Dec *vpu_dec)
{
    guint i;

    for(i=0;i<vpu_dec->numframebufs;i++)
    {
        if (vpu_dec->frame_mem[i].phy_addr != 0)
        {
            IOFreePhyMem(&vpu_dec->frame_mem[i]);
            IOFreeVirtMem(&vpu_dec->frame_mem[i]);
            vpu_dec->frame_mem[i].phy_addr=0;
            vpu_dec->frame_virt[i]=NULL;
        }
    }
}

/*=============================================================================
FUNCTION:               mfw_gst_vpudec_FrameBufferInit

DESCRIPTION:            This function allocates the outbut buffer for the 
                        decoder

ARGUMENTS PASSED:
                        vpu_dec  - VPU decoder plugins context
                        frameBuf - VPU's Output Frame Buffer to be 
                                   allocated.
                        
                        num_buffers number of frame buffers 
                        to be allocated
        

RETURN VALUE:
        None

PRE-CONDITIONS:
        None

POST-CONDITIONS:


IMPORTANT NOTES:
        None
=============================================================================*/

static gint mfw_gst_vpudec_FrameBufferInit(MfwGstVPU_Dec *vpu_dec,FrameBuffer *frameBuf,
                    gint num_buffers)
{
    
    gint i=0;
    GstCaps *src_caps=NULL;;
    GstFlowReturn retval=GST_FLOW_OK;
    GstBuffer *outbuffer=NULL;
    guint strideY=0,height=0;
    strideY = vpu_dec->initialInfo->picWidth;
    height = vpu_dec->initialInfo->picHeight;
    src_caps = GST_PAD_CAPS(vpu_dec->srcpad);
    for(i=0;i<num_buffers;i++){
        retval = gst_pad_alloc_buffer_and_set_caps(vpu_dec->srcpad, 0,
            vpu_dec->outsize,
            src_caps, &outbuffer);
        if(retval != GST_FLOW_OK){
            GST_ERROR("Error in allocating the Framebuffer[%d],"
                " error is %d",i,retval);
            return -1;
        }
        /* if the buffer allocated is the Hardware Buffer use it as it is */
        if(GST_BUFFER_FLAG_IS_SET(outbuffer,GST_BUFFER_FLAG_LAST)==TRUE)
        {
            vpu_dec->outbuffers[i] = outbuffer;
            GST_BUFFER_SIZE(vpu_dec->outbuffers[i]) = 
                vpu_dec->outsize;
            frameBuf[i].bufY = GST_BUFFER_OFFSET(outbuffer);
            frameBuf[i].bufCb = frameBuf[i].bufY + (strideY * height);
            frameBuf[i].bufCr = frameBuf[i].bufCb + ((strideY/2) * (height/2));
            vpu_dec->direct_render=TRUE;
        }
        /* else allocate The Harware buffer through IOGetPhyMem
           Note this to support writing the output to a file in case of 
           File Sink */
        else
        {
            if(outbuffer!=NULL){
                gst_buffer_unref(outbuffer);
                outbuffer=NULL;
            }
            memset(&vpu_dec->frame_mem[i], 0, sizeof(vpu_mem_desc));
            vpu_dec->frame_mem[i].size = vpu_dec->outsize;
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
            frameBuf[i].bufCb = frameBuf[i].bufY + (strideY * height);
            frameBuf[i].bufCr = frameBuf[i].bufCb + ((strideY/2) * (height/2));
            vpu_dec->frame_virt[i] = (guint8 *)IOGetVirtMem(&vpu_dec->frame_mem[i]);
            vpu_dec->direct_render=FALSE;
        }
    }
    return 0;
}


/*======================================================================================

FUNCTION:          mfw_gst_vpudec_chain

DESCRIPTION:       The main processing function where the data comes in as buffer. This 
                    data is decoded, and then pushed onto the next element for further
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
static GstFlowReturn mfw_gst_vpudec_chain(GstPad *pad, GstBuffer *buffer)
{
    MfwGstVPU_Dec *vpu_dec  = NULL;
    RetCode vpu_ret=RETCODE_SUCCESS;
    GstFlowReturn retval=GST_FLOW_OK;
    gint i=0;
    gint valret=0;
    guint needFrameBufCount=0;
    guint residue=0;
    gint fourcc = GST_STR_FOURCC("I420");
    GstCaps *src_caps = NULL;
    GstCaps *caps=NULL;
    struct timeval tv_prof, tv_prof1;
    struct timeval tv_prof2, tv_prof3;
    long time_before = 0, time_after = 0;
    GstBuffer *SrcFrameSize=NULL;
     vpu_dec = MFW_GST_VPU_DEC(GST_PAD_PARENT(pad));
    if (vpu_dec->profiling) {
        gettimeofday(&tv_prof2, 0);
    }
    /*Time stamp Buffer is a circular buffer to store the timestamps which are later 
      used while pushing the decoded frame onto the Sink element */
    vpu_dec->timestamp_buffer[vpu_dec->ts_rx] = GST_BUFFER_TIMESTAMP(buffer);
    vpu_dec->ts_rx = (vpu_dec->ts_rx+1)%MAX_STREAM_BUF;
    if((vpu_dec->codec==STD_VC1)&&(vpu_dec->picWidth!=0))
    {
        /* Creation of RCV Header is done in case of ASF Playback pf VC-1 streams
        from the parameters like width height and Header Extension Data */
        if(vpu_dec->first==FALSE)
        {
            valret = mfw_gst_VC1_Create_RCVheader(vpu_dec,buffer);
            if(valret!=0)
            {
                mfw_gst_vpudec_post_fatal_error_msg(vpu_dec,
                    "Error in Creating the RCV Header");
                retval = GST_FLOW_ERROR;
                goto done;
            }
            vpu_dec->first=TRUE;
        }

        /* The Size of the input stream is appended with the input stream 
        for integration with ASF */
        else
        {
            SrcFrameSize = gst_buffer_new_and_alloc(4);
            GST_BUFFER_DATA(SrcFrameSize)[0] = 
                (unsigned char)GST_BUFFER_SIZE(buffer);
            GST_BUFFER_DATA(SrcFrameSize)[1] = 
                (unsigned char)(GST_BUFFER_SIZE(buffer) >> 8);
            GST_BUFFER_DATA(SrcFrameSize)[2] = 
                (unsigned char)(GST_BUFFER_SIZE(buffer) >> 16);
            GST_BUFFER_DATA(SrcFrameSize)[3] = 
                (unsigned char)(GST_BUFFER_SIZE(buffer) >> 24);
            buffer = gst_buffer_join(SrcFrameSize,buffer);
            vpu_dec->frame_sizes_buffer[vpu_dec->buffidx_in]=GST_BUFFER_SIZE(buffer);
            vpu_dec->buffidx_in = (vpu_dec->buffidx_in+1)%MAX_STREAM_BUF;
        }
    }
    else
    {
        vpu_dec->frame_sizes_buffer[vpu_dec->buffidx_in]=GST_BUFFER_SIZE(buffer);
            vpu_dec->buffidx_in = (vpu_dec->buffidx_in+1)%MAX_STREAM_BUF;
    }
    /* The buffer read by the VPU follows a circular buffer approach 
       this block of code handles that */
    if((vpu_dec->start_addr + GST_BUFFER_SIZE(buffer)) <= vpu_dec->end_addr){
        memcpy(vpu_dec->start_addr,GST_BUFFER_DATA(buffer),
            GST_BUFFER_SIZE(buffer));
        vpu_dec->start_addr += GST_BUFFER_SIZE(buffer);
    }
    else{
        residue = (vpu_dec->end_addr - vpu_dec->start_addr);
        memcpy(vpu_dec->start_addr,GST_BUFFER_DATA(buffer),residue);
        memcpy(vpu_dec->base_addr,GST_BUFFER_DATA(buffer)+residue,
            GST_BUFFER_SIZE(buffer)-residue);
        vpu_dec->start_addr = vpu_dec->base_addr + GST_BUFFER_SIZE(buffer)-residue;
    }
    vpu_ret = vpu_DecUpdateBitstreamBuffer(*(vpu_dec->handle), 
        GST_BUFFER_SIZE(buffer));
    if (vpu_ret != RETCODE_SUCCESS) {
        GST_ERROR("vpu_DecUpdateBitstreamBuffer failed. Error code is %d \n",
            vpu_ret);
        retval = GST_FLOW_ERROR;
        goto done;
    }
    vpu_dec->buffered_size += GST_BUFFER_SIZE(buffer);
    if(buffer != NULL)
    {
        gst_buffer_unref(buffer);
        buffer = NULL;
    }
    if(vpu_dec->buffered_size > 200 * 1024){
        GST_ERROR("buffered size=%d\n",vpu_dec->buffered_size);
        GST_ERROR("buffer space is not sufficent to fill incoming" 
            "buffer\n");
    }
    if((vpu_dec->buffered_size < 
        (vpu_dec->frame_sizes_buffer[vpu_dec->buffidx_out] + 1024)) ||
        (vpu_dec->buffered_size<1024)) {
        return GST_FLOW_OK;
    }
    /* Initializion of the VPU decoder and the output buffers for the VPU
        is done here */
    if(vpu_dec->init==FALSE){
        vpu_DecSetEscSeqInit(*(vpu_dec->handle), 1);
        /* Decoder API to parse the input and get some initial parameters like height
        width and number of output buffers to be allocated */
        vpu_ret = vpu_DecGetInitialInfo(*(vpu_dec->handle), 
            vpu_dec->initialInfo);
        if (vpu_ret == RETCODE_FRAME_NOT_COMPLETE) {
                return GST_FLOW_OK;
            }
        if (vpu_ret != RETCODE_SUCCESS) {
            GST_ERROR("vpu_DecGetInitialInfo failed. Error code is %d \n",
                vpu_ret);
            mfw_gst_vpudec_post_fatal_error_msg(vpu_dec,
                "VPU Decoder Initialisation failed ");
            retval = GST_FLOW_ERROR;
            goto done;
        }
        GST_DEBUG("Dec: min buffer count= %d\n", vpu_dec->initialInfo->minFrameBufferCount);
        GST_DEBUG("Dec InitialInfo =>\npicWidth: %u, picHeight: %u, frameRate: %u\n",
            vpu_dec->initialInfo->picWidth, vpu_dec->initialInfo->picHeight,
            (unsigned int)vpu_dec->initialInfo->frameRateInfo);
        
        needFrameBufCount = vpu_dec->initialInfo->minFrameBufferCount + 1;
        /* set the capabilites on the source pad */
        caps = gst_caps_new_simple("video/x-raw-yuv",
            "format", GST_TYPE_FOURCC, fourcc,
            "width", G_TYPE_INT,
            vpu_dec->initialInfo->picWidth, 
            "height", G_TYPE_INT,
            vpu_dec->initialInfo->picHeight, 
            "pixel-aspect-ratio",GST_TYPE_FRACTION,1,1,
            "num-buffers-required",G_TYPE_INT,needFrameBufCount,NULL);
        if (!(gst_pad_set_caps(vpu_dec->srcpad, caps))) {
            GST_ERROR ("\nCould not set the caps" 
                "for the VPU decoder's src pad\n");
        }
        gst_caps_unref(caps);
        caps = NULL;
        vpu_DecSetEscSeqInit(*(vpu_dec->handle), 0);
        vpu_dec->outsize = (vpu_dec->initialInfo->picWidth * 
            vpu_dec->initialInfo->picHeight * 3)/2;
         vpu_dec->numframebufs = needFrameBufCount;
        /* Allocate the Frame buffers requested by the Decoder */
         if(vpu_dec->framebufinit_done==FALSE)
         {
             if((mfw_gst_vpudec_FrameBufferInit(vpu_dec,vpu_dec->frameBuf,
                 needFrameBufCount))< 0){
                 GST_ERROR("Mem system allocation failed!\n");
                 mfw_gst_vpudec_post_fatal_error_msg(vpu_dec,
                     "Allocation of the Frame Buffers Failed");
                 
                 retval = GST_FLOW_ERROR;
                 goto done;
             }
             vpu_dec->framebufinit_done=TRUE;
         }
        /* Register the Allocated Frame buffers wit the decoder*/
        vpu_ret = vpu_DecRegisterFrameBuffer(*(vpu_dec->handle),
            vpu_dec->frameBuf,
            vpu_dec->initialInfo->minFrameBufferCount,
            vpu_dec->initialInfo->picWidth, 
	    /*FIXME: the decoder we have may need another vpu lib version, which
	     * doesn't have pBufInfo in parameter. We add a NULL here
	     * temporarily to get it compile, this may crash though , we need to
	     * make clear how this comes */
	    NULL);
        if (vpu_ret != RETCODE_SUCCESS) {
            GST_ERROR("vpu_DecRegisterFrameBuffer failed. Error code is %d \n",
                vpu_ret);
            mfw_gst_vpudec_post_fatal_error_msg(vpu_dec,
                "Registration of the Allocated Frame Buffers Failed ");
            retval = GST_FLOW_ERROR;
            goto done;
        }

        vpu_dec->decParam->prescanEnable = 1;
        vpu_dec->init=TRUE;
        return GST_FLOW_OK;
    }
 
    while(1)
    {
        
        if(vpu_dec->flush==TRUE)
            break;
        if((vpu_dec->buffered_size < 
            (vpu_dec->frame_sizes_buffer[vpu_dec->buffidx_out] + 1024))
            ) {
            break;
        }
        if(vpu_dec->buffered_size<2048){
            break;
        }
        if(vpu_dec->loopback==FALSE){
            if(vpu_dec->vpu_wait == TRUE){
                if (vpu_dec->profiling) {
                    gettimeofday(&tv_prof, 0);
                }
                while (vpu_IsBusy()){
                    vpu_WaitForInt(500);
                };
                if (vpu_dec->profiling) {
                    gettimeofday(&tv_prof1, 0);
                    time_before = (tv_prof.tv_sec * 1000000) + tv_prof.tv_usec;
                    time_after = (tv_prof1.tv_sec * 1000000) + tv_prof1.tv_usec;
                    vpu_dec->decode_wait_time += time_after - time_before;
                }
               /* get the output information as to which index of the Framebuffers the 
                output is written onto */
                vpu_ret = vpu_DecGetOutputInfo(*(vpu_dec->handle),vpu_dec->outputInfo);
                if (vpu_ret != RETCODE_SUCCESS) {
                    GST_ERROR ("vpu_DecGetOutputInfo failed. Error code is %d \n",
                        vpu_ret);
                    retval = GST_FLOW_ERROR;
                    goto done;
                }
            }
        }
        /* Decoder API to decode one Frame at a time */ 
        vpu_ret = vpu_DecStartOneFrame(*(vpu_dec->handle), vpu_dec->decParam);
        if (vpu_ret == RETCODE_FRAME_NOT_COMPLETE) {
            return GST_FLOW_OK;
        }
        if (vpu_ret != RETCODE_SUCCESS) {
            GST_ERROR("vpu_DecStartOneFrame failed. Error code is %d \n",
                vpu_ret);
            retval = GST_FLOW_ERROR;
            goto done;
        }
        vpu_dec->buffered_size=vpu_dec->buffered_size - 
            vpu_dec->frame_sizes_buffer[vpu_dec->buffidx_out];
        vpu_dec->buffidx_out = (vpu_dec->buffidx_out+1)%MAX_STREAM_BUF;
        if(vpu_dec->loopback==TRUE){
            if (vpu_dec->profiling) {
                gettimeofday(&tv_prof, 0);
            }
            while (vpu_IsBusy()){
                vpu_WaitForInt(500);
            };
            if (vpu_dec->profiling) {
                gettimeofday(&tv_prof1, 0);
                time_before = (tv_prof.tv_sec * 1000000) + tv_prof.tv_usec;
                time_after = (tv_prof1.tv_sec * 1000000) + tv_prof1.tv_usec;
                vpu_dec->decode_wait_time += time_after - time_before;
            }
            /* get the output information as to which index of the Framebuffers the 
            output is written onto */
            vpu_ret = vpu_DecGetOutputInfo(*(vpu_dec->handle),vpu_dec->outputInfo);
            if (vpu_ret != RETCODE_SUCCESS) {
                GST_ERROR ("vpu_DecGetOutputInfo failed. Error code is %d \n",
                    vpu_ret);
                retval = GST_FLOW_ERROR;
                goto done;
            }
        }
        else{
            if(vpu_dec->vpu_wait == FALSE){
                vpu_dec->vpu_wait = TRUE;
                continue;
            }
        }
        if(vpu_dec->direct_render==TRUE){
            vpu_dec->pushbuff = vpu_dec->outbuffers[
                vpu_dec->outputInfo->indexFrameDisplay];
        }
        /* Incase of the Filesink the output in the hardware buffer is copied onto the 
        buffer allocated by filesink */
        else
        {
            retval = gst_pad_alloc_buffer_and_set_caps(vpu_dec->srcpad, 0,
                vpu_dec->outsize,
                src_caps, &vpu_dec->pushbuff);
            if(retval != GST_FLOW_OK){
                GST_ERROR("Error in allocating the Framebuffer[%d],"
                    " error is %d",i,retval);
                goto done;
            }
            memcpy(GST_BUFFER_DATA(vpu_dec->pushbuff),
                vpu_dec->frame_virt[vpu_dec->outputInfo->indexFrameDisplay],
                vpu_dec->outsize);
        }
        /* update the time stamp base on the frame-rate */
        GST_BUFFER_SIZE(vpu_dec->pushbuff) = vpu_dec->outsize;
        GST_BUFFER_TIMESTAMP(vpu_dec->pushbuff)=vpu_dec->timestamp_buffer[vpu_dec->ts_tx];
           vpu_dec->ts_tx = (vpu_dec->ts_tx+1)%MAX_STREAM_BUF;
        vpu_dec->decoded_frames++;
        GST_DEBUG("frame decoded : %lld\n",vpu_dec->decoded_frames);
        retval = gst_pad_push(vpu_dec->srcpad,vpu_dec->pushbuff);
        if(retval != GST_FLOW_OK){
            GST_ERROR("Error in Pushing the Output ont to the Source Pad,error is %d \n",retval);
        }
        retval = GST_FLOW_OK;
 }
done:
    if (vpu_dec->profiling) {
        gettimeofday(&tv_prof3, 0);
        time_before = (tv_prof2.tv_sec * 1000000) + tv_prof2.tv_usec;
        time_after = (tv_prof3.tv_sec * 1000000) + tv_prof3.tv_usec;
        vpu_dec->chain_Time += time_after - time_before;
    }
    if(buffer != NULL)
    {
        gst_buffer_unref(buffer);
        buffer = NULL;
    }
    return retval;
}

/*======================================================================================

FUNCTION:          mfw_gst_vpudec_sink_event

DESCRIPTION:       This function handles the events the occur on the sink pad
                   Like EOS

ARGUMENTS PASSED:  pad - pointer to the sinkpad of this element
                   event - event generated.

RETURN VALUE:
                    TRUE   event handled success fully.
                    FALSE .event not handled properly

PRE-CONDITIONS:
                    None

POST-CONDITIONS:
                    None

IMPORTANT NOTES:
                    None

=======================================================================================*/

static gboolean mfw_gst_vpudec_sink_event(GstPad * pad, GstEvent * event)
{
    MfwGstVPU_Dec *vpu_dec  = NULL;
    gboolean result = TRUE;
    vpu_dec                     = MFW_GST_VPU_DEC(GST_PAD_PARENT(pad));
    guint height=0,width=0;
    RetCode vpu_ret=RETCODE_SUCCESS;

    width = vpu_dec->initialInfo->picWidth;
    height = vpu_dec->initialInfo->picHeight;

    switch (GST_EVENT_TYPE(event)) {

    case GST_EVENT_NEWSEGMENT:
        {
            GstFormat format;
            gint64 start, stop, position;
            gdouble rate;
            gst_event_parse_new_segment(event, NULL, &rate, &format,
                &start, &stop, &position);
            GST_DEBUG(" receiving new seg \n");
            GST_DEBUG(" start = %" GST_TIME_FORMAT, GST_TIME_ARGS(start));
            GST_DEBUG(" stop = %" GST_TIME_FORMAT, GST_TIME_ARGS(stop));
            GST_DEBUG(" position in mpeg4  =%" GST_TIME_FORMAT,
                GST_TIME_ARGS(position));
            vpu_dec->flush=FALSE;
             if (GST_FORMAT_TIME == format) {
                 result = gst_pad_push_event(vpu_dec->srcpad, event);
                if (TRUE != result) {
                    GST_ERROR("\n Error in pushing the event,result	is %d\n",
                        result);
                }
             } 
            break;
        }
    case GST_EVENT_FLUSH_STOP:
	{
       
        vpu_dec->buffidx_in = 0;
        vpu_dec->buffidx_out = 0;
        vpu_dec->buffered_size=0;
        memset(&vpu_dec->frame_sizes_buffer[0],0,MAX_STREAM_BUF);
        memset(&vpu_dec->timestamp_buffer[0],0,MAX_STREAM_BUF);
        vpu_dec->ts_rx=0;
        vpu_dec->ts_tx=0;
        vpu_dec->vpu_wait=FALSE;
        vpu_dec->flush=TRUE;
        /* The below block of code is used to Flush the buffered input stream data*/
        if(vpu_dec->codec==STD_VC1)
        {
            vpu_ret = vpu_DecClose(*vpu_dec->handle);
            if (vpu_ret == RETCODE_FRAME_NOT_COMPLETE) {
                vpu_DecGetOutputInfo(*vpu_dec->handle, vpu_dec->outputInfo);
                vpu_ret = vpu_DecClose(*vpu_dec->handle);
            }
            if(RETCODE_SUCCESS != vpu_ret){
                GST_ERROR(" error in vpu_DecClose ");
            }
            vpu_dec->first=FALSE;
            vpu_dec->init=FALSE;
            vpu_ret = vpu_DecOpen(vpu_dec->handle,vpu_dec->decOP);
            if (vpu_ret != RETCODE_SUCCESS) {
                GST_ERROR("vpu_DecOpen failed. Error code is %d \n", vpu_ret);
                
            }
            
        }
        else
        {
            vpu_ret = vpu_DecBitBufferFlush(*vpu_dec->handle);
            if (vpu_ret == RETCODE_FRAME_NOT_COMPLETE) {
                vpu_DecGetOutputInfo(*vpu_dec->handle, vpu_dec->outputInfo);
                vpu_ret = vpu_DecBitBufferFlush(*vpu_dec->handle);
            }
            if(RETCODE_SUCCESS != vpu_ret){
                GST_ERROR(" error in flushing the bitstream buffer");
            }
           
        }
        vpu_dec->start_addr = vpu_dec->base_addr;
        result = gst_pad_push_event(vpu_dec->srcpad, event);
        if (TRUE != result) {
            GST_ERROR("\n Error in pushing the event,result	is %d\n",
                result);
            gst_event_unref(event);
        }
         break;
	}
    case GST_EVENT_EOS:
        {
            result = gst_pad_push_event(vpu_dec->srcpad, event);
            if (TRUE != result) {
                GST_ERROR("\n Error in pushing the event,result	is %d\n",
                    result);
            }
            break;
        }
    default:
        {
            result = gst_pad_event_default(pad, event);
            break;
        }

    }
    return result;
        
}

/*======================================================================================

FUNCTION:       mfw_gst_vpudec_change_state

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

static GstStateChangeReturn mfw_gst_vpudec_change_state
                            (GstElement *element, GstStateChange transition)
{
    GstStateChangeReturn ret        = GST_STATE_CHANGE_SUCCESS;
    MfwGstVPU_Dec *vpu_dec  = NULL;
    vpu_dec                     = MFW_GST_VPU_DEC(element);
    gint vpu_ret=0;
    gfloat avg_mcps = 0, avg_plugin_time = 0, avg_dec_time = 0;
    guint8 *virt_bit_stream_buf=NULL;

    switch (transition) 
    {
        case GST_STATE_CHANGE_NULL_TO_READY:
	    {
 
            GST_DEBUG("\nVPU State: Null to Ready\n");
            vpu_ret = IOSystemInit(NULL);
            if(vpu_ret < 0){
                GST_DEBUG("Error in initializing the VPU\n,error is %d",vpu_ret);
                return GST_STATE_CHANGE_FAILURE;
            }
            break;
	    }
        case GST_STATE_CHANGE_READY_TO_PAUSED:
	    {
            GST_DEBUG("\nVPU State: Ready to Paused\n\n");
            vpu_dec->init=FALSE;
            vpu_dec->start_addr=NULL;
            vpu_dec->end_addr=NULL;
            vpu_dec->base_addr=NULL;
            vpu_dec->outsize=0;
            vpu_dec->decode_wait_time = 0;
            vpu_dec->chain_Time = 0;
            vpu_dec->decoded_frames = 0;
            vpu_dec->avg_fps_decoding = 0.0;
            vpu_dec->frames_dropped = 0;
            vpu_dec->direct_render=FALSE;
            vpu_dec->vpu_wait=FALSE;
            vpu_dec->buffered_size = 0;
            vpu_dec->first=FALSE;
            vpu_dec->buffidx_in = 0;
            vpu_dec->buffidx_out = 0;
            vpu_dec->ts_rx=0;
            vpu_dec->ts_tx=0;
            vpu_dec->framebufinit_done=FALSE;
            memset(&vpu_dec->frame_sizes_buffer[0],0,MAX_STREAM_BUF);
            memset(&vpu_dec->timestamp_buffer[0],0,MAX_STREAM_BUF);
            memset(&vpu_dec->frameBuf[0],0,NUM_FRAME_BUF);
            /* Handle the decoder Initialization over here. */
            vpu_dec->decOP          = g_malloc(sizeof(DecOpenParam));
            vpu_dec->initialInfo    = g_malloc(sizeof(DecInitialInfo));
            vpu_dec->decParam       = g_malloc(sizeof(DecParam));
            vpu_dec->handle         = g_malloc(sizeof(DecHandle));
            vpu_dec->outputInfo     = g_malloc(sizeof(DecOutputInfo));
            memset(vpu_dec->decOP,0,sizeof(DecOpenParam));
            memset(vpu_dec->handle ,0,sizeof(DecHandle));
            memset(vpu_dec->decParam,0,sizeof(DecParam));
            memset(vpu_dec->outputInfo,0,sizeof(DecOutputInfo));

            memset(&vpu_dec->bit_stream_buf, 0, sizeof(vpu_mem_desc));
            vpu_dec->bit_stream_buf.size = BUFF_FILL_SIZE;
            IOGetPhyMem(&vpu_dec->bit_stream_buf);
            virt_bit_stream_buf = (guint8 *)IOGetVirtMem(&vpu_dec->bit_stream_buf);
            vpu_dec->start_addr = vpu_dec->base_addr = virt_bit_stream_buf;
            vpu_dec->decOP->bitstreamBuffer = vpu_dec->bit_stream_buf.phy_addr;
            vpu_dec->decOP->bitstreamBufferSize = BUFF_FILL_SIZE;
            vpu_dec->end_addr = virt_bit_stream_buf + BUFF_FILL_SIZE;
            vpu_dec->decOP->reorderEnable = 0;
            vpu_dec->base_write = vpu_dec->bit_stream_buf.phy_addr;
            vpu_dec->end_write = vpu_dec->bit_stream_buf.phy_addr + BUFF_FILL_SIZE;
            GST_DEBUG("codec=%d\n",vpu_dec->codec);
            vpu_dec->decOP->bitstreamFormat = vpu_dec->codec;
            /* open a VPU's decoder instance */
            vpu_ret = vpu_DecOpen(vpu_dec->handle,vpu_dec->decOP);
            if (vpu_ret != RETCODE_SUCCESS) {
                GST_ERROR("vpu_DecOpen failed. Error code is %d \n", vpu_ret);
                return GST_STATE_CHANGE_FAILURE;
            }
            break;
	    }
        case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
        {
            GST_DEBUG("\nVPU State: Paused to Playing\n");
	        break;
        }
        default:
	        break;
    }

    ret = vpu_dec->parent_class->change_state(element, transition);
    GST_DEBUG("\n State Change for VPU returned %d", ret);

    switch (transition) 
    {
        case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
        {
            GST_DEBUG("\nVPU State: Playing to Paused\n");
	        break;
        }
        case GST_STATE_CHANGE_PAUSED_TO_READY:
	    {
            GST_DEBUG("\nVPU State: Paused to Ready\n");
            if (vpu_dec->profiling) {
                g_print("PROFILE FIGURES OF VPU DECODER PLUGIN");
                g_print("\nTotal decode wait time is            %fus",
                    (gfloat)vpu_dec->decode_wait_time);
                g_print("\nTotal plugin time is                 %lluus",
                    vpu_dec->chain_Time);
                g_print("\nTotal number of frames decoded is    %llu",
                    vpu_dec->decoded_frames);
                g_print("\nTotal number of frames dropped is    %llu\n",
                    vpu_dec->frames_dropped);
                if(g_compare_float(vpu_dec->frame_rate, 0) != FLOAT_MATCH){
                    avg_mcps = ((float) vpu_dec->decode_wait_time * PROCESSOR_CLOCK /
                        (1000000 *
                        (vpu_dec->decoded_frames -
                        vpu_dec->frames_dropped)))
                        * vpu_dec->frame_rate;
                    g_print("\nAverage decode WAIT MCPS is          %f", avg_mcps);
                    
                    avg_mcps = ((float) vpu_dec->chain_Time * PROCESSOR_CLOCK /
                        (1000000 *
                        (vpu_dec->decoded_frames -
                        vpu_dec->frames_dropped)))
                        * vpu_dec->frame_rate;
                    g_print("\nAverage plug-in MCPS is              %f",
                        avg_mcps);
                }
                else
                {
                g_print("enable the Frame Rate property of the decoder to get the MCPS \
                        ... \n ! mfw_mpeg4decoder framerate=value ! .... \
                        \n Note: value denotes the framerate to be set");
                }
               avg_dec_time =
                    ((float) vpu_dec->decode_wait_time) / vpu_dec->decoded_frames;
                g_print("\nAverage decoding Wait time is        %fus",
                    avg_dec_time);
                avg_plugin_time =
                    ((float) vpu_dec->chain_Time) / vpu_dec->decoded_frames;
                g_print("\nAverage plugin time is               %fus\n",
                    avg_plugin_time);
                
                vpu_dec->decode_wait_time = 0;
                vpu_dec->chain_Time = 0;
                vpu_dec->decoded_frames = 0;
                vpu_dec->avg_fps_decoding = 0.0;
                vpu_dec->frames_dropped = 0;
            }
           if(vpu_dec->direct_render==FALSE)
           mfw_gst_vpudec_FrameBufferClose(vpu_dec);

           vpu_dec->first=FALSE;
           vpu_dec->buffidx_in = 0;
           vpu_dec->buffidx_out = 0;
           memset(&vpu_dec->timestamp_buffer[0],0,MAX_STREAM_BUF);
           memset(&vpu_dec->frame_sizes_buffer[0],0,MAX_STREAM_BUF);
           vpu_dec->start_addr=NULL;
           vpu_dec->end_addr=NULL;
           vpu_dec->base_addr=NULL;
           vpu_dec->outsize=0;
           vpu_dec->direct_render=FALSE;
           vpu_dec->vpu_wait=FALSE;
           vpu_dec->framebufinit_done=FALSE;
           vpu_ret = vpu_DecClose(*vpu_dec->handle);
           if (vpu_ret == RETCODE_FRAME_NOT_COMPLETE) {
               vpu_DecGetOutputInfo(*vpu_dec->handle, vpu_dec->outputInfo);
               vpu_ret = vpu_DecClose(*vpu_dec->handle);
               if(vpu_ret < 0){
                   GST_ERROR("Error in closing the VPU decoder,error is %d\n",vpu_ret);
                   return GST_STATE_CHANGE_FAILURE;
               }
           }
           IOFreePhyMem(&(vpu_dec->bit_stream_buf));
           IOFreeVirtMem(&(vpu_dec->bit_stream_buf));
           if(vpu_dec->decOP!=NULL)
           {
               g_free(vpu_dec->decOP);  
               vpu_dec->decOP = NULL;
           }
           if(vpu_dec->initialInfo!=NULL)
           {
               
               g_free(vpu_dec->initialInfo);
               vpu_dec->initialInfo = NULL;
               
           }
           if(vpu_dec->decParam!=NULL)
           {
               g_free(vpu_dec->decParam);
               vpu_dec->decParam = NULL;
           }
           if(vpu_dec->outputInfo!=NULL)
           {
               g_free(vpu_dec->outputInfo);
               vpu_dec->outputInfo = NULL;
           }
           if(vpu_dec->handle!=NULL)
           {
               g_free(vpu_dec->handle);
               vpu_dec->handle = NULL;
           }
            break;
        }   
        case GST_STATE_CHANGE_READY_TO_NULL:
	    {
            GST_DEBUG("\nVPU State: Ready to Null\n");

            if(vpu_dec->loopback==FALSE)
            IOSystemShutdown();
	        break;
        }
	    default:
	        break;
    }

    return ret;

}



/*=============================================================================
FUNCTION:               src_templ

DESCRIPTION:            Template to create a srcpad for the decoder.

ARGUMENTS PASSED:       None.


RETURN VALUE:           a GstPadTemplate


PRE-CONDITIONS:  	    None

POST-CONDITIONS:   	    None

IMPORTANT NOTES:   	    None
=============================================================================*/
static GstPadTemplate *src_templ(void)
{
    static GstPadTemplate *templ = NULL;

    if (!templ) {
	GstCaps *caps;
	GstStructure *structure;
	GValue list = { 0 }
	, fps = {
	0}
	, fmt = {
	0};
	char *fmts[] = { "YV12", "I420", "Y42B", NULL };
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

	templ =
	    gst_pad_template_new("src", GST_PAD_SRC, GST_PAD_ALWAYS, caps);
    }
    return templ;
}

/*======================================================================================

FUNCTION:       mfw_gst_vpudec_setcaps

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
mfw_gst_vpudec_setcaps(GstPad * pad, GstCaps *caps)
{
   MfwGstVPU_Dec *vpu_dec  = NULL;
   const gchar *mime;
    gint32 frame_rate_de = 0;
    gint32 frame_rate_nu = 0;
    GstStructure *structure = gst_caps_get_structure(caps, 0);
    vpu_dec = MFW_GST_VPU_DEC(gst_pad_get_parent(pad));
    mime = gst_structure_get_name(structure);
    GValue *codec_data = NULL;
    guint8 *hdrextdata;
    guint i=0;
    gint wmvversion;
    if(mime!=NULL)
    {
        switch(vpu_dec->codec) {
        case STD_AVC:
            {
                if(strcmp(mime,"video/x-h264") != 0){
                    GST_ERROR("VPU : mime type %s not negotiated by the plugin",
                        vpu_dec->codec);
                    return FALSE;
                }
                break;
            }
        case STD_MPEG4:
            {
                if(strcmp(mime,"video/mpeg") != 0){
                    GST_ERROR("VPU : mime type %s not negotiated by the plugin",
                        vpu_dec->codec);
                    return FALSE;
                }
                break;
            }
        case STD_H263:
            {
                if(strcmp(mime,"video/x-h263") != 0){
                    GST_ERROR("VPU : mime type %s not negotiated by the plugin",
                        vpu_dec->codec);
                    return FALSE;
                }
                break;
            }
             case STD_VC1:
            {
                if(strcmp(mime,"video/x-wmv") != 0){
                    GST_ERROR("VPU : mime type %s not negotiated by the plugin",
                        vpu_dec->codec);
                    return FALSE;
                }
                break;
            }

        default:
            {
                GST_ERROR(" Codec Standard not supporded \n");
                return FALSE;
                break;
            }
        }
    }
    gst_structure_get_fraction(structure, "framerate", &frame_rate_nu,
			       &frame_rate_de);
    if (frame_rate_de != 0) {
	vpu_dec->frame_rate = (gfloat) (frame_rate_nu) / frame_rate_de;
    }
    GST_DEBUG(" Frame Rate = %f \n", vpu_dec->frame_rate);
    gst_structure_get_uint(structure, "width", &vpu_dec->picWidth);
    GST_DEBUG("\nInput Width is %d\n", vpu_dec->picWidth);
    gst_structure_get_uint(structure, "height", &vpu_dec->picHeight);
    GST_DEBUG("\nInput Height is %d\n", vpu_dec->picHeight);
    if(vpu_dec->codec==STD_VC1)
    {
        gst_structure_get_int(structure, "wmvversion", &wmvversion);
        if(wmvversion !=3)
        {
            mfw_gst_vpudec_post_fatal_error_msg(vpu_dec,
                "WMV Version error:This is a VC1 decoder supports "
                "only WMV 9 Simple and Main Profile decode (WMV3)");
            return FALSE;
        }
  
        codec_data =
            (GValue *) gst_structure_get_value(structure, "codec_data");
        
        if (NULL != codec_data) {
            vpu_dec->HdrExtData = gst_value_get_buffer(codec_data);
            vpu_dec->HdrExtDataLen = GST_BUFFER_SIZE(vpu_dec->HdrExtData);
            GST_DEBUG("\nCodec specific data length is %d\n",vpu_dec->HdrExtDataLen);
            GST_DEBUG("Header Extension Data is \n");
            hdrextdata = GST_BUFFER_DATA(vpu_dec->HdrExtData);
            for(i=0;i<vpu_dec->HdrExtDataLen;i++)
                GST_DEBUG("0X%x",hdrextdata[i]);
            GST_DEBUG("\n");
            
        }
        else
        {
            GST_ERROR("No Header Extension Data found during Caps Negotiation \n");
            mfw_gst_vpudec_post_fatal_error_msg(vpu_dec,
                "No Extension Header Data Recieved from the Demuxer");
            return FALSE;
        }
    }
    gst_object_unref(vpu_dec);
    return gst_pad_set_caps(pad, caps);
}

/*======================================================================================

FUNCTION:          mfw_gst_vpudec_base_init

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
static void mfw_gst_vpudec_base_init(MfwGstVPU_DecClass *klass)
{

    GstElementClass *element_class = GST_ELEMENT_CLASS(klass);

    gst_element_class_add_pad_template(element_class,
				       src_templ());

    gst_element_class_add_pad_template(element_class,
				       gst_static_pad_template_get
				       (&mfw_gst_vpudec_sink_factory));
    
    gst_element_class_add_pad_template(element_class,
				       gst_static_pad_template_get
				       (&mfw_gst_vpudec_src_factory));
    
    gst_element_class_set_details(element_class, &mfw_gst_vpudec_details);

}

/*======================================================================================

FUNCTION:       mfw_gst_vpudec_codec_get_type

DESCRIPTION:    Gets an enumeration for the different 
                codec standars supported by the decoder
ARGUMENTS PASSED:
                None

RETURN VALUE:
                enumerated type of the codec standatds 
                supported by the decoder

PRE-CONDITIONS:
                None

POST-CONDITIONS:
                None

IMPORTANT NOTES:
                None

========================================================================================*/

GType
mfw_gst_vpudec_codec_get_type(void)
{
  static GType vpudec_codec_type = 0;
  static GEnumValue vpudec_codecs[] = {
    {STD_MPEG4, "0", "std_mpeg4"},
    {STD_H263,  "1", "std_h263"},
    {STD_AVC,   "2", "std_avc" },
    {STD_VC1,   "3", "std_vc1"},
    {0, NULL, NULL},
  };
  if (!vpudec_codec_type) {
    vpudec_codec_type =
        g_enum_register_static ("MfwGstVpuDecCodecs", vpudec_codecs);
  }
  return vpudec_codec_type;
}


/*======================================================================================

FUNCTION:       mfw_gst_vpudec_class_init

DESCRIPTION:    Initialise the class.(specifying what signals,
                 arguments and virtual functions the class has and setting up
                 global states)

ARGUMENTS PASSED:
                klass - pointer to H.264Decoder element class

RETURN VALUE:
                None

PRE-CONDITIONS:
                None

POST-CONDITIONS:
                None

IMPORTANT NOTES:
                None

=======================================================================================*/


static void mfw_gst_vpudec_class_init(MfwGstVPU_DecClass *klass)
{
   
   
    GObjectClass *gobject_class         = NULL;
    GstElementClass *gstelement_class   = NULL;
    gobject_class                       = (GObjectClass *) klass;
    gstelement_class                    = (GstElementClass *) klass;
    gstelement_class->change_state      = mfw_gst_vpudec_change_state;
    gobject_class->set_property = mfw_gst_vpudec_set_property;
    gobject_class->get_property = mfw_gst_vpudec_get_property;
    g_object_class_install_property(gobject_class, MFW_GST_VPU_PROF_ENABLE,
				    g_param_spec_boolean("profiling", "Profiling", 
                    "enable time profiling of the vpu decoder plug-in",
                    FALSE, G_PARAM_READWRITE));
    g_object_class_install_property(gobject_class, MFW_GST_VPU_CODEC_TYPE,
				    g_param_spec_enum("codec-type", "codec_type", 
                    "selects the codec type for decoding",
                    MFW_GST_TYPE_VPU_DEC_CODEC, STD_AVC, G_PARAM_READWRITE));

    g_object_class_install_property(gobject_class, MFW_GST_VPU_LOOPBACK,
				    g_param_spec_boolean("loopback", "LoopBack", 
                    "enables the decoder plug-in to operate"
                    "in lopback mode with encoder ",
                    FALSE, G_PARAM_READWRITE));
}


/*======================================================================================
FUNCTION:       mfw_gst_vpudec_init

DESCRIPTION:    create the pad template that has been registered with the
                element class in the _base_init

ARGUMENTS PASSED:
                vpu_dec -    pointer to vpu_decoder element structure

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
mfw_gst_vpudec_init(MfwGstVPU_Dec *vpu_dec, MfwGstVPU_DecClass *gclass)
{


    GstElementClass *klass = GST_ELEMENT_GET_CLASS(vpu_dec);
    /* create the sink and src pads */
    vpu_dec->sinkpad =
        gst_pad_new_from_template(gst_element_class_get_pad_template
        (klass, "sink"), "sink");
    vpu_dec->srcpad =
        gst_pad_new_from_template(src_templ(), "src");
    gst_element_add_pad(GST_ELEMENT(vpu_dec), vpu_dec->sinkpad);
    gst_element_add_pad(GST_ELEMENT(vpu_dec), vpu_dec->srcpad);
    vpu_dec->parent_class = g_type_class_peek_parent(gclass);
    gst_pad_set_chain_function(vpu_dec->sinkpad, mfw_gst_vpudec_chain);
    
    gst_pad_set_setcaps_function(vpu_dec->sinkpad,
        mfw_gst_vpudec_setcaps);
    gst_pad_set_event_function(vpu_dec->sinkpad,
			            GST_DEBUG_FUNCPTR(mfw_gst_vpudec_sink_event));
    vpu_dec->codec = STD_AVC;
    vpu_dec->loopback=0;
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

static gboolean plugin_init(GstPlugin *plugin)
{
    return gst_element_register(plugin, "mfw_vpudecoder",
				GST_RANK_PRIMARY, MFW_GST_TYPE_VPU_DEC);
}

GST_PLUGIN_DEFINE (
          GST_VERSION_MAJOR,	                    /* major version of gstreamer */
		  GST_VERSION_MINOR,	                    /* minor version of gstreamer */
		  "mfw_vpudecoder",	                        /* name of our  plugin */
		  "Decodes H264,MPEG4 and H263 Encoded"
          "data to Raw YUV Data ",                   /* what our plugin actually does */
		  plugin_init,	                            /* first function to be called */
		  VERSION,
		  GST_LICENSE_UNKNOWN,
		  "freescale semiconductor", "www.freescale.com")

/*======================================================================================

FUNCTION:       mfw_gst_type_vpu_dec_get_type

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
GType mfw_gst_type_vpu_dec_get_type(void)
{
    static GType vpu_dec_type = 0;
    if (!vpu_dec_type) 
    {
	    static const GTypeInfo vpu_dec_info = 
        {
	        sizeof(MfwGstVPU_DecClass),
	        (GBaseInitFunc) mfw_gst_vpudec_base_init,
	        NULL,
	        (GClassInitFunc) mfw_gst_vpudec_class_init,
	        NULL,
	        NULL,
	        sizeof(MfwGstVPU_Dec),
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

