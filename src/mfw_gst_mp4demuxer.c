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
 * Module Name:    mfw_gst_mp4_demuxer.c
 *
 * Description:    mp4/m4a Demuxer plug-in for Gstreamer.
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
#include <string.h>
#include <gst/gst.h>
#include "mp4_parser/MP4Parser.h"
#include "mfw_gst_mp4demuxer.h"
#include "mfw_gst_utils.h"
#include "gst-plugins-fsl-parser_config.h"

//#define _ORIG_ONE_TASK 1
/*=============================================================================
                            LOCAL CONSTANTS
=============================================================================*/
/* None */

/*=============================================================================
                           STATIC VARIABLES
=============================================================================*/
static GstStaticPadTemplate mfw_gst_mp4_demuxer_sink_factory =
GST_STATIC_PAD_TEMPLATE("sink",
			GST_PAD_SINK,
			GST_PAD_ALWAYS,
			GST_STATIC_CAPS("video/quicktime;"
			"audio/x-m4a;""application/x-3gp")
    );


/*=============================================================================
                            LOCAL MACROS
=============================================================================*/

#define GST_CAT_DEFAULT mfw_gst_mp4demuxer_debug

#define QUERY_SIZE 4096

#define DEMUX_WAIT_INTERVAL 5000

#define MFW_GST_MP4_VIDEO_CAPS \
        "video/mpeg; " \
        "video/x-h264 "

#define MFW_GST_MP4_AUDIO_CAPS \
        "audio/mpeg; " \
        "audio/AMR-WB; " \
		"audio/AMR "

/* define a tag name */
#define	MFW_GST_TAG_WIDTH		        "width"
#define MFW_GST_TAG_HEIGHT	            "height"
#define MFW_GST_TAG_FRAMERATE           "framerate"
#define MFW_GST_TAG_SAMPLING_FREQUENCY  "sampling frequency"
#define MFW_GST_TAG_YEAR                "year"

/*=============================================================================
                           LOCAL VARIABLES
=============================================================================*/
/* None. */

/*=============================================================================
                        STATIC FUNCTION PROTOTYPES
=============================================================================*/
GST_DEBUG_CATEGORY_STATIC(mfw_gst_mp4demuxer_debug);
static void mfw_gst_mp4_demuxer_class_init(MFW_GST_MP4DEMUX_INFO_CLASS_T
					   *);
static void mfw_gst_mp4_demuxer_base_init(MFW_GST_MP4DEMUX_INFO_CLASS_T *);
static void mfw_gst_mp4_demuxer_init(MFW_GST_MP4DEMUX_INFO_T *,
				     MFW_GST_MP4DEMUX_INFO_CLASS_T *);
static gboolean mfw_gst_mp4_demuxer_set_caps(GstPad *, GstCaps *);
static GstStateChangeReturn mfw_gst_mp4demuxer_change_state(GstElement *,
							    GstStateChange);
static gboolean mfw_gst_mp4demuxer_activate(GstPad *);
static gboolean mfw_gst_mp4demuxer_activate_pull(GstPad *, gboolean);
static gboolean mfw_gst_mp4demuxer_sink_event(GstPad *, GstEvent *);
static gboolean mp4_write_audio_data(MFW_GST_MP4DEMUX_INFO_T *,
									 const void *, u32, u64, guint);
static gboolean mp4_write_video_data(MFW_GST_MP4DEMUX_INFO_T *,
									 const void *, u32, u64, guint,guint32);
static GstPadTemplate *audio_src_templ(void);
static GstPadTemplate *video_src_templ(void);
static gboolean mp4_demuxer_fill_stream_info(MFW_GST_MP4DEMUX_INFO_T *,
					     guint32);
static gboolean mp4_demuxer_set_pad_caps(MFW_GST_MP4DEMUX_INFO_T *,
					 guint32);
static void MP4CreateTagList(MFW_GST_MP4DEMUX_INFO_T *);
static gboolean mfw_gst_mp4demuxer_parse(MFW_GST_MP4DEMUX_INFO_T *);
static gboolean mfw_gst_mp4demuxer_handle_src_query(GstPad *,
						    GstQuery *);

static void mfw_gst_mp4_demuxer_close(MFW_GST_MP4DEMUX_INFO_T *);
static const GstQueryType *gst_mp4demuxer_get_src_query_types(GstPad *);
							      
static gboolean mfw_gst_mp4demuxer_handle_src_event(GstPad*,GstEvent*);
static gboolean mfw_gst_mp4demuxer_seek(MFW_GST_MP4DEMUX_INFO_T*,GstEvent*);

FILE *app_MP4LocalFileOpen(const u8 *,const u8 *);
LONGLONG app_MP4LocalFileSize(FILE *,void *);
void app_MP4LocalFree(void *);
LONGLONG app_MP4LocalGetCurrentFilePos(FILE *,void *);
void *app_MP4LocalMalloc(guint32);
u32 app_MP4LocalReadFile(void *,guint32,
		                 guint32, FILE *,
					     void *);
void *app_MP4LocalReAlloc(void *,guint32);
void app_MP4LocalSeekFile(FILE *, int, int,void *);
void *app_MP4LocalCalloc(guint32,guint32);
static void mfw_gst_mp4_demuxer_finalize(GObject * object);
/*=============================================================================
                            STATIC VARIABLES
=============================================================================*/


static GstElementClass *parent_class = NULL;

/*=============================================================================
                            LOCAL FUNCTIONS
=============================================================================*/

/*=============================================================================
FUNCTION:            mfw_gst_mp4demuxer_parse
        
DESCRIPTION:         This is the main functions, where calls to MP4 Parser 
					 are made.

ARGUMENTS PASSED:    demuxer - The context of the main mp4 demuxer element.
        
  
RETURN VALUE:        
                     None
        
        

PRE-CONDITIONS:      None
   	    
 
POST-CONDITIONS:     None
   	    

IMPORTANT NOTES:     None
   	    
=============================================================================*/

static gboolean mfw_gst_mp4demuxer_parse(MFW_GST_MP4DEMUX_INFO_T * demuxer)
{

    GstPad *src_pad;
    gboolean ret_value;
    MP4Err Err;			/* MP4Parser error */
    guint track_count;		/* track counter */
    guint total_tracks;		/* total number of tracks */
    guint stream_count;		/* number of streams */
	guint64 duration;     /* stream duration */ 
    FILE *fp;			/* file pointer      */
	gchar *text_msg=NULL;

    Err = MP4NoErr;
    fp = NULL;        /*file pointer is set to NULL(This is passed as an argument because
					    most of the parser library files use it)*/   


   
    /* Allocate the memories for the objects */

    demuxer->parser_info.mp4_parser_object_type =
	(sMP4ParserObjectType *) app_MP4LocalCalloc(1,
						sizeof
						(sMP4ParserObjectType));

    if (demuxer->parser_info.mp4_parser_object_type == NULL) {
	GST_ERROR
	    ("Error in allocating the memory for mp4 parser object type\n");
	
	text_msg = " Unable to allocate the memory ";

	goto Err_Msg;
	}

    demuxer->parser_info.mp4_parser_read_byte_info =
	(sMP4ParserReadByteInfo *) app_MP4LocalCalloc(1,
						  sizeof
						  (sMP4ParserReadByteInfo));

    if (demuxer->parser_info.mp4_parser_read_byte_info == NULL) {
	GST_ERROR
	    ("Error in allocating the memory for mp4 parser read byte information\n");

	text_msg = " Unable to allocate the memory ";
		
	goto Err_Msg;
    }

    demuxer->parser_info.mp4_parser_data_list =
	(sMP4ParserUdtaList *) app_MP4LocalCalloc(1,
					      sizeof(sMP4ParserUdtaList));

    if (demuxer->parser_info.mp4_parser_data_list == NULL) {
	GST_ERROR
	    ("Error in allocating the memory for mp4 user data list\n");
	
	text_msg = " Unable to allocate the memory ";
	
	goto Err_Msg;
    }

    demuxer->parser_info.mp4_parser_file_info =
	(sMP4ParserFileInfo *) app_MP4LocalCalloc(1,
					      sizeof(sMP4ParserFileInfo));

    if (demuxer->parser_info.mp4_parser_file_info == NULL) {
	GST_ERROR
	    ("Error in allocating the memory for mp4 parser file info\n");
	
    text_msg = " Unable to allocate the memory ";

	goto Err_Msg;
    }

	demuxer->ptrFuncPtrTable = (sFunctionPtrTable *)g_malloc(sizeof(sFunctionPtrTable));
    if(demuxer->ptrFuncPtrTable==NULL)
    {
       	GST_ERROR
	    ("Error in allocating the memory for function pointer table\n");

    text_msg = " Unable to allocate the memory "; 
	
	goto Err_Msg;
    }
    
     demuxer->ptrFuncPtrTable->MP4LocalCalloc             = &app_MP4LocalCalloc;
     demuxer->ptrFuncPtrTable->MP4LocalFileOpen           = &app_MP4LocalFileOpen;
     demuxer->ptrFuncPtrTable->MP4LocalFileSize           = &app_MP4LocalFileSize;
     demuxer->ptrFuncPtrTable->MP4LocalFree               = &app_MP4LocalFree;
     demuxer->ptrFuncPtrTable->MP4LocalGetCurrentFilePos  = &app_MP4LocalGetCurrentFilePos;
     demuxer->ptrFuncPtrTable->MP4LocalMalloc             = &app_MP4LocalMalloc;
     demuxer->ptrFuncPtrTable->MP4LocalReadFile           = &app_MP4LocalReadFile;
     demuxer->ptrFuncPtrTable->MP4LocalReAlloc            = &app_MP4LocalReAlloc;
     demuxer->ptrFuncPtrTable->MP4LocalSeekFile           = &app_MP4LocalSeekFile;
        
    
    /* Initialising the mp4 parser */
    Err =
	MP4ParserModuleInit(demuxer->parser_info.mp4_parser_object_type,
			    fp, &total_tracks,(void *)demuxer,demuxer->ptrFuncPtrTable);

    if (Err) {
	GST_ERROR("Error in initializing the mp4_parser\n");
	text_msg = " Error in parsing:corrupted stream";
	
	goto Err_Msg;
    }

    /* Get the file information of the passed file */
    Err = MP4ParserGetFileInfo(demuxer->parser_info.mp4_parser_object_type,
			       demuxer->parser_info.mp4_parser_file_info);

    if (Err) {
	GST_ERROR("Error in getting the file info\n");
	text_msg = " Error in parsing:corrupted stream";

	goto Err_Msg;
    }

    /* Get the stream information of the passed file */
    Err =
	MP4ParserGetStreamInfo(demuxer->parser_info.mp4_parser_object_type,
			       demuxer->parser_info.mp4_parser_file_info,
			       total_tracks,demuxer->parser_info.
			       mp4_parser_read_byte_info);
    if (Err) {
	GST_ERROR("Error in getting the stream info\n");
    text_msg = "Error in parsing:Unsupported Media";

	goto Err_Msg;
    }

    /* Seek the file to the desired time */
    Err =
	MP4ParserSeekFile(demuxer->parser_info.mp4_parser_object_type,0,
			  total_tracks,demuxer->parser_info.
			       mp4_parser_read_byte_info);


    if (Err) {
	GST_ERROR("Error in seeking the file \n");
    text_msg = " Error in parsing:corrupted stream";

	goto Err_Msg;
	}

	/* Get the user data from the file */
	Err = MP4ParserGetUserData(demuxer->parser_info.mp4_parser_object_type,
			       demuxer->parser_info.mp4_parser_data_list,
			       fp);

    if (Err) {
	GST_ERROR("Error in getting the user data in the file \n");
	
    }

    /* Alloacte the memory required for the output stream */
    Err =
	MP4ParserAllocateMemory(demuxer->parser_info.mp4_parser_object_type,
                                demuxer->parser_info.
				mp4_parser_read_byte_info,
				demuxer->parser_info.mp4_parser_file_info,
				total_tracks);
    if (Err) {
	GST_ERROR("Error in allocating the memory \n");
	text_msg = " Unable to allocate the memory";

	goto Err_Msg;
    }

    if (demuxer->stop_request == FALSE) {
    /**** Setting Pads for the streams present ****/
	ret_value = mp4_demuxer_fill_stream_info(demuxer, total_tracks);
	if (!ret_value) {
	    GST_ERROR("Error in setting the pad capability in fill stream info\n");
	    text_msg = " media formats are not matching";

		goto Err_Msg;
	}
    } else {
	/* reseting the capabilities of the pad */
	ret_value = mp4_demuxer_set_pad_caps(demuxer, total_tracks);
	if (!ret_value) {
	    GST_ERROR("Error in setting the pad capability \n");
		text_msg = " media formats are not matching";

		goto Err_Msg;
	    
	}
    }

	/* Signalling no more pads to the application. */
	GST_DEBUG("signaling no more pads from mfw_mp4demuxer");
	gst_element_no_more_pads (GST_ELEMENT (demuxer));

    /* duration of the mp4 file */
	duration=demuxer->parser_info.
	         mp4_parser_object_type->media_duration;

	/* Set the duration of the segment to duration in nanoseconds */
    gst_segment_set_duration(&demuxer->segment,
				     GST_FORMAT_TIME,
				     duration);

    /* register the query functions on to the src pad created */
    for (track_count = 0; track_count < demuxer->total_src_pad;
	 track_count++) {
	gst_pad_set_query_type_function(demuxer->srcpad[track_count],
					GST_DEBUG_FUNCPTR
					(gst_mp4demuxer_get_src_query_types));


	gst_pad_set_query_function(demuxer->srcpad[track_count],
				   GST_DEBUG_FUNCPTR
				   (mfw_gst_mp4demuxer_handle_src_query));

	gst_pad_set_event_function (demuxer->srcpad[track_count],GST_DEBUG_FUNCPTR (
		                          mfw_gst_mp4demuxer_handle_src_event));



    }


    for (track_count = 0; track_count < demuxer->total_tracks;
	 track_count++) {

    /* If the mp4 file has mpeg4 as one of the stream,send video object information
		 before sending actual video raw data(This is very much required for mpeg4
		 decoder to decode video frames )*/

	if ((demuxer->parser_info.mp4_parser_object_type->
	    trak[track_count]->tracktype == MP4_VIDEO)&&
		(demuxer->parser_info.mp4_parser_object_type->
		trak[track_count]->decodertype==VIDEO_MPEG4))
	{

        /* the video new segment flag should be set to TRUE,as new segment should 
		   not be sent along with video object information */ 

//		demuxer->new_seg_flag_video=TRUE;

	    /* write the video object information */

        /* Copy the Video Object Data onto a buffer. Once the first frame is
            parsed, merge this buffer with the first frame and then push the buffer.
         */

        demuxer->video_object_buffer = gst_buffer_new_and_alloc(demuxer->parser_info.
				                mp4_parser_object_type->media[track_count]->specificInfo_size);
        if(NULL == demuxer->video_object_buffer)
        {
            GST_ERROR("\n Cannot allocate memory for Video Spcific Object");
            goto Err_Msg;
        }
        else
        {
            unsigned char *temp = GST_BUFFER_DATA(demuxer->video_object_buffer);
            memcpy              (temp, *(demuxer->parser_info.
				                mp4_parser_object_type->
				                decoderSpecificInfoH[track_count]), demuxer->parser_info.
				                mp4_parser_object_type->media[track_count]->specificInfo_size);
        }
	   
	/* the video new segment flag should be set to FALSE,as new segment should be
	   sent along with the next video raw data*/

		demuxer->new_seg_flag_video=TRUE;
	}

	/*  If the mp4 file has h264 as one of the stream,send video object information
		 before sending actual video raw data(This is very much required for h264
		 decoder to decode video frames )*/

	else if ((demuxer->parser_info.mp4_parser_object_type->
	    trak[track_count]->tracktype == MP4_VIDEO)&&
		(demuxer->parser_info.mp4_parser_object_type->
		trak[track_count]->decodertype==VIDEO_H264))
	{

        /* the video new segment flag should be set to TRUE,as new segment should 
		   not be sent along with video object information */
//		demuxer->new_seg_flag_video=TRUE;

        /* Copy the Video Object Data onto a buffer. Once the first frame is
            parsed, merge this buffer with the first frame and then push the buffer.
         */

        demuxer->video_object_buffer = gst_buffer_new_and_alloc(demuxer->parser_info.
				                mp4_parser_object_type->media[track_count]->specificInfo_size);
        if(NULL == demuxer->video_object_buffer)
        {
            GST_ERROR("\n Cannot allocate memory for Video Spcific Object");
            goto Err_Msg;
        }
        else
        {
            unsigned char *temp = GST_BUFFER_DATA(demuxer->video_object_buffer);
            memcpy              (temp, *(demuxer->parser_info.
				                mp4_parser_object_type->
				                decoderSpecificInfoH[track_count]), demuxer->parser_info.
				                mp4_parser_object_type->media[track_count]->specificInfo_size);
        }
	    

	/* the video new segment flag should be set to FALSE,as new segment should be
	   sent along with the next video raw data*/

	    demuxer->new_seg_flag_video=TRUE;
	}

	/* If the mp4 file has amr as the audio content send amr header before sending the 
	   actual amr data */

	else if ((demuxer->parser_info.mp4_parser_object_type->
	    trak[track_count]->tracktype == MP4_AUDIO)&&
		(demuxer->parser_info.mp4_parser_object_type->
		trak[track_count]->decodertype==AUDIO_AMR))
	{
    
     /* the audio new segment flag should be set to TRUE,as new segment should not be
		sent along with amr header information */ 

    demuxer->new_seg_flag_audio=TRUE;

	    /* write the amr header */
	    ret_value =
		mp4_write_audio_data(demuxer,*
				     (demuxer->parser_info.
				      mp4_parser_object_type->
				      decoderSpecificInfoH[track_count]),
				     demuxer->parser_info.
				     mp4_parser_object_type->
				     media[track_count]->specificInfo_size,
				     0, track_count);
	if (ret_value == FALSE) {
	    GST_ERROR("\n error in writing the audio data:result is %d\n",
		      ret_value);
		text_msg = " media formats are not matching";

		goto Err_Msg;

	}

	/* the audio new segment flag should be set to FALSE,as new segment should be
	   sent along with the next audio raw data*/
//	demuxer->new_seg_flag_audio=FALSE;

    }


    }

	/* the first track in the media data */
	demuxer->track_index=demuxer->parser_info.mp4_parser_object_type->tarck_index;	  
    
	/* Add the user data information to the taglist */
	MP4CreateTagList(demuxer);

    return TRUE;

	/* send an error message for an unrecoverable error */
Err_Msg:
	{
    GstMessage *message = NULL;
	GError *gerror = NULL;
	gerror = g_error_new_literal(1, 0, text_msg);

	message =
	    gst_message_new_error(GST_OBJECT(GST_ELEMENT(demuxer)), gerror,
				  "debug none");
	gst_element_post_message(GST_ELEMENT(demuxer), message);
	g_error_free(gerror);
	return FALSE;
	}
 }

/*=============================================================================
FUNCTION:            mfw_gst_mp4demuxer_taskfunc
        
DESCRIPTION:         This is the main functions, where audio and video data are
                     Read from the file 

ARGUMENTS PASSED:    demuxer - The context of the main mp4 demuxer element.
        
  
RETURN VALUE:        
                     None
        
        

PRE-CONDITIONS:      None
   	    
 
POST-CONDITIONS:     None
   	    

IMPORTANT NOTES:     None
   	    
=============================================================================*/

void mfw_gst_mp4demuxer_taskfunc(GstPad    *src_pad)
{
	
	guint32   Err;
	gboolean  ret_value;
	FILE      *fp;
//	GstPad    *src_pad;
	guint     track_index;
	guint     track_count,i;
	MFW_GST_MP4DEMUX_INFO_T *demuxer =	MFW_GST_MP4_DEMUXER(GST_PAD_PARENT(src_pad));
	
	Err = MP4NoErr;
    fp  = NULL;
	
	
	/* The taskfunction reads one frame of video/audio data from the container,pushes
    it to the respective pad and returns */
	
	if(demuxer->seek_flag==TRUE)
	{
    /* if seek has happened,for the H264 decoder,a Video Object Data has to be sent before
	   pushing the actual video frame to which video is seeked(Always an I frame).
		The Video Object Data is always sent along with the seeked I frame */
		
		for (track_count = 0; track_count < MAX_SRC_PADS; track_count++) 
		{
		    track_index = demuxer->srcpadnum2trackindex[track_count].trackindex;
			if ((demuxer->srcpadnum2trackindex[track_count].srcpad) 
                 &&(demuxer->parser_info.mp4_parser_object_type->trak[track_index]->tracktype == MP4_VIDEO)
                 &&(demuxer->parser_info.mp4_parser_object_type->trak[track_index]->decodertype==VIDEO_H264))
			{
				
			/* Copy the Video Object Data onto a buffer. Once the Intra frame is
            parsed, merge this buffer with the Intra frame and then push the buffer.
				*/
				
				demuxer->video_object_buffer = 
				    gst_buffer_new_and_alloc(demuxer->parser_info.mp4_parser_object_type->media[track_index]->specificInfo_size);
				if(NULL == demuxer->video_object_buffer)
				{
					GST_ERROR("\n Cannot allocate memory for Video Spcific Object");
					goto pause_task;
				}
				else
				{
					unsigned char *temp = GST_BUFFER_DATA(demuxer->video_object_buffer);
					memcpy(temp, *(demuxer->parser_info.mp4_parser_object_type->decoderSpecificInfoH[track_index]), 
						demuxer->parser_info.mp4_parser_object_type->media[track_index]->specificInfo_size);
				}
				
			}
		}
		
		demuxer->seek_flag=FALSE;
	}
    /*FIXME:what track to be read now must be decide by parser plugin*/
	//    demuxer->track_index = 0;//demuxer->parser_info.mp4_parser_object_type->tarck_index;
	
    for (track_count = 0; track_count < MAX_SRC_PADS; track_count++)
	{
		if (demuxer->srcpadnum2trackindex[track_count].srcpad == src_pad)
		{
			track_index = demuxer->srcpadnum2trackindex[track_count].trackindex;
			break;
		}
	}
	GST_DEBUG("Accessing each frame no %d data from the container \n",demuxer->track_index);
	
	
	/* Read The each Access unit.demuxer->track_index indicates which tarck to read
	   i.e audio/video */
	g_mutex_lock(demuxer->media_file_lock);
//	g_print("<mfw_gst_mp4demuxer_taskfunc_video> MP4ParserReadFile entered!\n");
	Err =
		MP4ParserReadFile(demuxer->parser_info.mp4_parser_object_type,
		demuxer->parser_info.mp4_parser_read_byte_info, 
		track_index/*demuxer->track_index*/);
//	g_print("<mfw_gst_mp4demuxer_taskfunc_video> MP4ParserReadFile exited!\n");
	g_mutex_unlock(demuxer->media_file_lock);
	
	/* if EOF file is reached,i.e nor more audio video data is present in the container.
	So send the end of stream event through the src pad */
	if (Err == MP4EOF) 
	{
		
//		src_pad = demuxer->srcpad[track_index];
		
		
		/* Handle an event on the sink pad with event as End of Stream */
		ret_value =
			mfw_gst_mp4demuxer_sink_event(src_pad, gst_event_new_eos());
		
		if (ret_value != TRUE) {
			GST_ERROR("\n Error in pushing the event, result is %d\n",
				ret_value);
		}
		
		/* set the end of stream flag */
		demuxer->eos_flag[track_count/*demuxer->track_index*/]=TRUE;     
		
		goto pause_task;
		
    }
	
	
	
	if (demuxer->parser_info.mp4_parser_object_type->
 		trak[track_index/*demuxer->track_index*/]->tracktype == MP4_VIDEO) 
	{
        /*if this sample is a sync sample, and the mp4_demuxer->video_object_buffer != NULL, then copy 
          the decoderSpecificInfoH to it -- for frame drop purpose*/
        if (demuxer->parser_info.mp4_parser_read_byte_info->track_read_info[track_index].sync && demuxer->video_object_buffer == NULL)
        {
    		if ((demuxer->parser_info.mp4_parser_object_type->trak[track_index]->tracktype == MP4_VIDEO)&&
    			(demuxer->parser_info.mp4_parser_object_type->trak[track_index]->decodertype==VIDEO_H264))
    		{
    			
    		    /* Copy the Video Object Data onto a buffer. Once the Intra frame is
    		       parsed, merge this buffer with the Intra frame and then push the buffer.
    			*/
    			
    			demuxer->video_object_buffer = gst_buffer_new_and_alloc(demuxer->parser_info.
    				mp4_parser_object_type->media[track_index]->specificInfo_size);
    			if(NULL == demuxer->video_object_buffer)
    			{
    				GST_ERROR("\n Cannot allocate memory for Video Spcific Object");
    				goto pause_task;
    			}
    			else
    			{
    				unsigned char *temp = GST_BUFFER_DATA(demuxer->video_object_buffer);
    				memcpy(temp, *(demuxer->parser_info.mp4_parser_object_type->decoderSpecificInfoH[track_index]), 
    					demuxer->parser_info.mp4_parser_object_type->media[track_index]->specificInfo_size);
    			}
    			
    		}
        }
            
		/* push the parsed audio data to video src pad  */
		ret_value =
			mp4_write_video_data(demuxer,
			demuxer->parser_info.mp4_parser_read_byte_info->track_read_info[track_index].MaxBufferRequired,
			demuxer->parser_info.mp4_parser_read_byte_info->track_read_info[track_index].numBytesRead,
			demuxer->parser_info.mp4_parser_read_byte_info->track_read_info[track_index].media_timestamp, 
			track_count,//track_index/*demuxer->track_index*/,
			demuxer->parser_info.mp4_parser_read_byte_info->track_read_info[track_index].sync);
        if (ret_value == FALSE) {
			GST_ERROR
				("\n error in writing the video data:result is %d\n",
				ret_value);
			
			goto pause_task;
			
		}
		
	}
	else
	if (demuxer->parser_info.mp4_parser_object_type->trak[track_index/*demuxer->track_index*/]->tracktype == MP4_AUDIO) 
	{
		
		/* push the parsed audio data to audio src pad  */
		ret_value =
			mp4_write_audio_data(demuxer,
			demuxer->parser_info.mp4_parser_read_byte_info->track_read_info[track_index].MaxBufferRequired,
			demuxer->parser_info.mp4_parser_read_byte_info->track_read_info[track_index].numBytesRead,
			demuxer->parser_info.mp4_parser_read_byte_info->track_read_info[track_index].media_timestamp, 
			track_count);//track_index/*demuxer->track_index*/);
		
		if (ret_value == FALSE) {
			GST_ERROR
				("\n error in writing the audio data:result is %d\n",
				ret_value);
			
			goto pause_task;
			
		}
	}

    if (demuxer->videosent) {
        /* Keep the balance of demuxer and codec */    
        usleep(DEMUX_WAIT_INTERVAL);
        demuxer->videosent = FALSE;
    }
	
	return;
	/*when got eof or error*/
pause_task:
	/* Pause the task actived on the pad */
#ifdef _ORIG_ONE_TASK
	ret_value = gst_pad_pause_task(demuxer->sinkpad);
	if (FALSE == ret_value)
		GST_ERROR("\n There is no task on this sink pad !! \n");
#else
	ret_value = gst_pad_pause_task(demuxer->srcpad[track_count]);
	if (FALSE == ret_value)
		GST_ERROR("\n There is no task on this sink pad !! \n");
#endif
	
	return;
	
}


/*=============================================================================
FUNCTION:            mp4_demuxer_fill_stream_info
        
DESCRIPTION:         Add and Creates the audio pad sets the 
					 corresponding capabilities.

ARGUMENTS PASSED:    demuxer      - The main structure of mp4 parser plugin context.
                     total_tracks - Total number of tracks present in the file. 
        
  
RETURN VALUE:
       TRUE       -	 if the function is performed properly
	   FALSE	  -	 if there is any errors encounterd
        

PRE-CONDITIONS:
   	    None
 
POST-CONDITIONS:
   	    None

IMPORTANT NOTES:
   	    None
=============================================================================*/

static gboolean mp4_demuxer_fill_stream_info(MFW_GST_MP4DEMUX_INFO_T *
					     demuxer, guint32 total_tracks)
{
    GstPadTemplate *templ = NULL;
    GstPad *pad = NULL;
    gchar *padname = NULL;
    guint total_pads;
    guint count;
    GstPad *src_pad;
    guint32 version;
    gboolean set;
    guint frame_width;
    guint frame_height;
    float frame_rate;
	gchar *text_msg=NULL;
	guint video_offset=0;
	guint audio_offset=0;
	guint sampling_frequency=0;

    const char *media_type;
    sMP4ParserFileInfo *file_info;
    sMP4ParserObjectType *parser_info;
    gboolean type_find = TRUE; 
    
	demuxer->total_tracks=total_tracks;
    
    file_info = demuxer->parser_info.mp4_parser_file_info;
    parser_info = demuxer->parser_info.mp4_parser_object_type;
	video_offset=demuxer->parser_info.mp4_parser_object_type->video_offset;
	audio_offset=demuxer->parser_info.mp4_parser_object_type->audio_offset;

    GST_DEBUG("\n In fill stream Info to create and set Pads");
    /* the number of src pads are equal to the number of streams in the container */   
	demuxer->total_src_pad = demuxer->parser_info.mp4_parser_object_type->totalstreams;
    GST_DEBUG("\n total number of streams %d", demuxer->total_src_pad);

    for (count = 0; count < demuxer->total_src_pad; count++) {
    	GstCaps *caps = NULL;

	    if (demuxer->parser_info.mp4_parser_object_type->trak[count+audio_offset]->tracktype == MP4_AUDIO) {
    	    src_pad = demuxer->srcpad[count];
    	    padname = g_strdup_printf("audio/mpeg", 0);

    		demuxer->srcpadnum2trackindex[count].trackindex = count+audio_offset;

    	    if (file_info->MP4PmStreamArray[count+audio_offset]->decoderType ==
    		    AUDIO_MP3)
    		{
                GST_DEBUG("\n Audio Media type is MP3");
                media_type="audio/mpeg";  
                version = 1;
                sampling_frequency = file_info->MP4PmStreamArray[count+audio_offset]->MediaTimeScale;
		}
	    else if (file_info->MP4PmStreamArray[count+audio_offset]->decoderType ==
		     AUDIO_MPEG2_AAC )
		{
            GST_DEBUG("\n Audio Media type is MPEG2 AAC");
            media_type="audio/mpeg";
            version = 2;
		}
	    else if (file_info->MP4PmStreamArray[count+audio_offset]->decoderType ==
		     AUDIO_MPEG2_AAC_LC)
		{
            GST_DEBUG("\n Audio Media type is MPEG2 AAC LC");
            media_type="audio/mpeg";
            version = 2;
		}
    	    else if (file_info->MP4PmStreamArray[count+audio_offset]->decoderType ==
    		     AUDIO_AAC)
    		{
                GST_DEBUG("\n Audio Media type is MPEG4 AAC");
                media_type="audio/mpeg";
                version = 4;
    		}
    		else if (file_info->MP4PmStreamArray[count+audio_offset]->decoderType ==
    		     AUDIO_AMR)
    		{
                GST_DEBUG("\n Audio Media type is AMR");
                version = 4;
        		if (file_info->MP4PmStreamArray[count+audio_offset]->MediaTimeScale==16000)
                    media_type="audio/AMR-WB";
        		else if (file_info->MP4PmStreamArray[count+audio_offset]->MediaTimeScale==8000)
                    media_type="audio/AMR";

        		sampling_frequency=file_info->MP4PmStreamArray[count+audio_offset]->MediaTimeScale;
    		}
            else {
                GST_ERROR("\n audio type is unknown %d!", file_info->MP4PmStreamArray[count+audio_offset]->decoderType);
                type_find = FALSE;
            }
    	    /* defines the capability for audio pad */
            if (type_find) {
            /* Defines the audio src pad template */
                templ = audio_src_templ();
                caps = gst_caps_new_simple(media_type,
                	       "mpegversion", G_TYPE_INT, version,
                	       "framed", G_TYPE_INT, 1,
                	       "bitrate", G_TYPE_INT,
                	       demuxer->parser_info.
                	       mp4_parser_file_info->
                	       MP4PmStreamArray[count+audio_offset]->avgBitRate,
                		   "rate", G_TYPE_INT,sampling_frequency,
                           "channels",G_TYPE_INT,MONO,
                	       NULL);
                type_find = TRUE;
            }
    				       
    	}

	    if (demuxer->parser_info.mp4_parser_object_type->trak[count+video_offset]->tracktype == MP4_VIDEO) {
    	    src_pad = demuxer->srcpad[count];
    	    padname = g_strdup_printf("video_%02d", 0);

    		demuxer->srcpadnum2trackindex[count].trackindex = count+video_offset;

    	    frame_width = parser_info->media[count+video_offset]->media_width;
    	    frame_height = parser_info->media[count+video_offset]->media_height;
    	    frame_rate = parser_info->media[count+video_offset]->media_framerate;
            		
    	    if (file_info->MP4PmStreamArray[count+video_offset]->decoderType==VIDEO_MPEG4)
            {
                GST_DEBUG("\n Video Media type is MPEG4");
                media_type = "video/mpeg";
    		}
    	    else if (file_info->MP4PmStreamArray[count+video_offset]->decoderType ==VIDEO_H264)
    		{
                GST_DEBUG("\n Video Media type is H.264");
                media_type = "video/x-h264";
    		}
    		else if (file_info->MP4PmStreamArray[count+video_offset]->decoderType ==
    		     VIDEO_H263)
    		{
                GST_DEBUG("\n Video Media type is H.263");
                media_type = "video/x-h263";
    		}
            else 
            {
                type_find = FALSE;
                GST_DEBUG("\n video type is unknown!");
            }

            if ((frame_width > MAX_RESOLUTION_WIDTH)  || (frame_width < MIN_RESOLUTION_WIDTH) ||
                (frame_height > MAX_RESOLUTION_HEIGHT)|| (frame_height < MIN_RESOLUTION_HEIGHT))
            {
                text_msg = "Unsupported video resolution!";
                g_print(RED_STR("Unsupported resolution %dx%d (supported from %dx%d to %dx%d).\n")
                                frame_width, frame_height, MIN_RESOLUTION_WIDTH, MIN_RESOLUTION_HEIGHT,
                                MAX_RESOLUTION_WIDTH, MAX_RESOLUTION_HEIGHT);
                goto Err_Msg;
            }
                
            if (type_find){
                /* Defines the video src pad template */
                templ = video_src_templ();

                /* defines the capability for video pad */
                caps = gst_caps_new_simple(media_type,
                		       "mpegversion", G_TYPE_INT, 4,
                		       "systemstream", G_TYPE_INT, 0,
                		       "height", G_TYPE_INT, frame_height,
                		       "width", G_TYPE_INT, frame_width,
                		       "framerate", GST_TYPE_FRACTION,
                		       (gint32) (frame_rate * 1000), 1000,
                		       NULL);
            }

    	}

	demuxer->caps[count] = caps;



	/* no caps means no stream */
	if (!caps) {
	    GST_ERROR_OBJECT(GST_ELEMENT(demuxer),"Did not find caps for stream %s", padname);
		g_free(padname);
        demuxer->srcpad[count] = NULL; /* Set invalidate */
	    return TRUE;
	}

	/* Creates a new pad with the name padname from the template templ */


	pad = demuxer->srcpad[count] =
	    gst_pad_new_from_template(templ, padname);
	demuxer->srcpadnum2trackindex[count].srcpad = pad;

	g_free(padname);


	gst_pad_use_fixed_caps(pad);

	/* set the stream_ptr on to the pad */
	gst_pad_set_element_private(pad, demuxer->srcpad[count]);

	/* set the capabilities of the pad */
	set = gst_pad_set_caps(pad, caps);

	if (set == FALSE) {
	    GST_ERROR
		("\n unable to set the capability of the pad:result is %d\n",
		 set);
	    return set;
	}

	/* check if the caps represents media formats */
	if (gst_caps_is_empty(gst_pad_get_caps(pad)))
        {
	    GST_ERROR("\n caps is empty\n");
            gst_caps_unref(caps);
            return FALSE;
        }

	/* Activates the given pad */
	if (gst_pad_set_active(pad, TRUE) == TRUE) {
	    GST_DEBUG("\nThe pad was activated successfully\n");
	}

	/* adds a pad to the element */
	gst_element_add_pad(GST_ELEMENT(demuxer), pad);


	GST_LOG_OBJECT(GST_ELEMENT(demuxer), "Added pad %s with caps %p",
		        GST_PAD_NAME(pad), caps);

	/* unref a GstCap */
	gst_caps_unref(caps);

    }

    return TRUE;

	/* send an error message for an unrecoverable error */
Err_Msg:
	{
    GstMessage *message = NULL;
	GError *gerror = NULL;

    gerror = g_error_new_literal(1, 0, text_msg);

	message =
	    gst_message_new_error(GST_OBJECT(GST_ELEMENT(demuxer)), gerror,
				  "debug none");
	gst_element_post_message(GST_ELEMENT(demuxer), message);
	g_error_free(gerror);
	return FALSE;
	}

}

/*=============================================================================
FUNCTION:            mp4_demuxer_set_pad_caps
        
DESCRIPTION:         Reset the capabilities of the pads created before.

ARGUMENTS PASSED:    demuxer      - The main structure of mp4 parser plugin context.
                     total_tracks - Total number of tracks present in the file. 
        
  
RETURN VALUE:
       TRUE       -	 if the function is performed properly
	   FALSE	  -	 if there is any errors encounterd
        

PRE-CONDITIONS:
   	    None
 
POST-CONDITIONS:
   	    None

IMPORTANT NOTES:
   	    None
   	
=============================================================================*/

static gboolean mp4_demuxer_set_pad_caps(MFW_GST_MP4DEMUX_INFO_T * demuxer,
					 guint32 total_tracks)
{
    guint track_count;
    gboolean set;

#if 0
	guint video_offset=0;
	guint audio_offset=0;
   
	video_offset=demuxer->parser_info.mp4_parser_object_type->video_offset;
	audio_offset=demuxer->parser_info.mp4_parser_object_type->audio_offset;


    for (track_count = 0; track_count < demuxer->total_src_pad; track_count++) {

	if (demuxer->parser_info.mp4_parser_object_type->trak[track_count+audio_offset]->tracktype == MP4_AUDIO) {

	    set =
		gst_pad_set_caps(demuxer->srcpad[track_count+audio_offset],
				 demuxer->caps[track_count]);
	    if (set == FALSE) {
		GST_ERROR
		    ("\n unable to set the capability of the pad:result is %d\n",
		     set);
		return FALSE;
	    }
	}

	else if (demuxer->parser_info.mp4_parser_object_type->
		 trak[track_count+video_offset]->tracktype == MP4_VIDEO) {

	    set =
		gst_pad_set_caps(demuxer->srcpad[track_count+video_offset],
				 demuxer->caps[track_count]);
	    if (set == FALSE) {
		GST_ERROR
		    ("\n unable to set the capability of the pad:result is %d\n",
		     set);
		return FALSE;
	    }
	}
    
    }
#else
    for (track_count=0; track_count<MAX_SRC_PADS;track_count++){
        if ((demuxer->srcpad[track_count]) && (demuxer->caps[track_count])){
            set = gst_pad_set_caps(demuxer->srcpad[track_count],
				                    demuxer->caps[track_count]);
    	    if (set == FALSE) {
        		GST_ERROR
        		    ("\n unable to set the capability of the pad:result is %d\n",
        		     set);
        		return FALSE;
    	    }
        }
    }
#endif
    
    return TRUE;
}

/*=============================================================================
FUNCTION:            audio_src_templ
        
DESCRIPTION:         defines the audio source pad template.

ARGUMENTS PASSED:    None
        
  
RETURN VALUE:        Pointer to GstPadTemplate. 
        

PRE-CONDITIONS:      None
   	    
 
POST-CONDITIONS:     None
   	    

IMPORTANT NOTES:     None
   	
=============================================================================*/

static GstPadTemplate *audio_src_templ(void)
{
    static GstPadTemplate *templ = NULL;

    if (!templ) {
	GstCaps *caps = NULL;
	GstCaps *caps_amr_wb = NULL;
	GstCaps *caps_amr_nb = NULL;
	GstStructure *structure;

	caps = gst_caps_new_simple("audio/mpeg",
				   "mpegversion", GST_TYPE_INT_RANGE, 1, 4,
				   "framed", GST_TYPE_INT_RANGE, 0, 1,
				   "bitrate", GST_TYPE_INT_RANGE, 0,
				   G_MAXINT,NULL);

	caps_amr_wb = gst_caps_new_simple("audio/AMR-WB",
				   "mpegversion", GST_TYPE_INT_RANGE, 1, 4,
				   "framed", GST_TYPE_INT_RANGE, 0, 1,
				   "bitrate", GST_TYPE_INT_RANGE, 0,G_MAXINT,
				   "rate", GST_TYPE_INT_RANGE, 8000,16000,
				   "channels",GST_TYPE_INT_RANGE,1,2,
				   NULL);

	caps_amr_nb = gst_caps_new_simple("audio/AMR",
				   "mpegversion", GST_TYPE_INT_RANGE, 1, 4,
				   "framed", GST_TYPE_INT_RANGE, 0, 1,
				   "bitrate", GST_TYPE_INT_RANGE, 0,G_MAXINT,
				   "rate", GST_TYPE_INT_RANGE, 8000,16000,
				   "channels",GST_TYPE_INT_RANGE,1,2,
				   NULL);


	gst_caps_append(caps,caps_amr_wb);
	gst_caps_append(caps,caps_amr_nb);

	templ = gst_pad_template_new(MFW_GST_MP4_AUDIO_CAPS, GST_PAD_SRC,
				     GST_PAD_SOMETIMES, caps);
    }
    return templ;
}

/*=============================================================================
FUNCTION:            video_src_templ
        
DESCRIPTION:         defines the audio source pad template.

ARGUMENTS PASSED:    None
        
  
RETURN VALUE:        Pointer to GstPadTemplate. 
        

PRE-CONDITIONS:      None
   	    
 
POST-CONDITIONS:     None
   	    

IMPORTANT NOTES:     None
   	
=============================================================================*/

static GstPadTemplate *video_src_templ()
{
    static GstPadTemplate *templ = NULL;

    if (!templ) {
	GstCaps *caps      = NULL;
	GstCaps *caps_h264 = NULL;
	GstCaps *caps_h263 = NULL;
	GstStructure *structure;

	caps     = gst_caps_new_simple("video/mpeg",
				   "mpegversion", GST_TYPE_INT_RANGE, 1, 4,
				   "systemstream", GST_TYPE_INT_RANGE, 0,
				   1,
				   "width", GST_TYPE_INT_RANGE, MIN_RESOLUTION_WIDTH, MAX_RESOLUTION_WIDTH,
				   "height", GST_TYPE_INT_RANGE, MIN_RESOLUTION_HEIGHT, MAX_RESOLUTION_HEIGHT,
				   NULL);


	caps_h264 = gst_caps_new_simple("video/x-h264",
				   "systemstream", GST_TYPE_INT_RANGE, 0,
				   1, 
				   "width", GST_TYPE_INT_RANGE, MIN_RESOLUTION_WIDTH, MAX_RESOLUTION_WIDTH,
				   "height", GST_TYPE_INT_RANGE, MIN_RESOLUTION_HEIGHT, MAX_RESOLUTION_HEIGHT,
				   NULL);

	caps_h263 = gst_caps_new_simple("video/x-h263",
				   "systemstream", GST_TYPE_INT_RANGE, 0,
				   1, 
				   "width", GST_TYPE_INT_RANGE, MIN_RESOLUTION_WIDTH, MAX_RESOLUTION_WIDTH,
				   "height", GST_TYPE_INT_RANGE, MIN_RESOLUTION_HEIGHT, MAX_RESOLUTION_HEIGHT,
				   NULL);

	gst_caps_append(caps,caps_h264);
	gst_caps_append(caps,caps_h263);

	templ = gst_pad_template_new(MFW_GST_MP4_VIDEO_CAPS, GST_PAD_SRC,
				     GST_PAD_SOMETIMES, caps);
    }
    return templ;
}

/*=============================================================================
FUNCTION:            mp4_write_audio_data
        
DESCRIPTION:         Pushes the parsed data to respective source pads.

ARGUMENTS PASSED:    
  source_buffer  -   pointer to the data which needs to be 
					 pushed to source pad.
  total_size    	 length/Size of the data pushed.
  time_stamp     -   The time stamp of each frame      
  src_pad_index  -   the src pad no to which data has to be pushed
 		
        
  
RETURN VALUE:        
          TRUE   -	 if write_audio_data could be performed
	      FALSE	 -	 if write_audio_data could not be performed
        

PRE-CONDITIONS:      None
   	    
 
POST-CONDITIONS:     None
   	    

IMPORTANT NOTES:     None
   	    
=============================================================================*/

static gboolean mp4_write_audio_data(MFW_GST_MP4DEMUX_INFO_T *mp4_demuxer,
									 const void *source_buffer,
				                     guint32 total_size, u64 time_stamp,
				                     guint src_pad_index)
{
    GstPad *src_pad;
    GstClockTime stream_time;
    GstCaps *src_caps = NULL;
    GstBuffer *src_data_buf = NULL;
    unsigned char *tmp_buf = NULL;
    gint64 start;
    guint src_buf_size = 0;
    GstFlowReturn result = GST_FLOW_OK;
	guint audio_offset=0;

	audio_offset=mp4_demuxer->parser_info.mp4_parser_object_type->audio_offset;
	
    stream_time = time_stamp * 1000 * 1000;	/* put the time stamp */
    src_pad = mp4_demuxer->srcpad[src_pad_index];//[src_pad_index-audio_offset];


    if (mp4_demuxer->new_seg_flag_audio == TRUE) {
		GST_DEBUG("sending new segment event on to audio src pad \n ");
		start=time_stamp*1000*1000;
	
		if (!gst_pad_push_event
			(src_pad,
			gst_event_new_new_segment(FALSE, 1.0, GST_FORMAT_TIME, start,
					       GST_CLOCK_TIME_NONE, start))) {
			GST_ERROR("\nCannot send new segment to the src pad\n");
			mp4_demuxer->new_seg_flag_audio = FALSE;
			goto War_Msg;
		}
		mp4_demuxer->new_seg_flag_audio = FALSE;
    }


    /* get caps of the pad */
    src_caps = GST_PAD_CAPS(src_pad);

    if (src_caps == NULL) {
	GST_ERROR("\n Caps not Correct \n");
	return FALSE;
    }

    src_buf_size = total_size;

    /* Allocates a new, empty buffer optimized to push to pad stream_ptr->src_pad */
    result = gst_pad_alloc_buffer(src_pad, 0, src_buf_size,
				  src_caps, &src_data_buf);

    if (result == GST_FLOW_WRONG_STATE) {
	GST_ERROR("\n Cannot Create Output Buffer, result is %d", result);
	return FALSE;
    }
	else if (result == GST_FLOW_NOT_LINKED  ) {
	//GST_ERROR("\n Cannot Create Output Buffer, result is %d", result);
	return TRUE;
	}
    tmp_buf = GST_BUFFER_DATA(src_data_buf);

    memcpy(tmp_buf, source_buffer, total_size);

    /* get the timestamp of the data in the buffer */
    GST_BUFFER_TIMESTAMP(src_data_buf) = stream_time;

    GST_DEBUG("pushing audio data on to src pad \n ");

    /* pushes buffer to the peer element */
    result = gst_pad_push(src_pad, src_data_buf);
    if (result != GST_FLOW_OK) {
	GST_ERROR("\n Cannot Push Data onto next element");
	return FALSE;
    }

    /* Set the last observed stop position in the segment to position */
    gst_segment_set_last_stop(&mp4_demuxer->segment, GST_FORMAT_TIME,
			      stream_time);

    return TRUE;

	War_Msg:
	{
    GstMessage *message = NULL;
	GError *gerror = NULL;
	gerror = g_error_new_literal(1, 0, "pads are not negotiated!");

	message =
	    gst_message_new_warning(GST_OBJECT(GST_ELEMENT(mp4_demuxer)), gerror,
				  "audio pads are not negotiated!");
	gst_element_post_message(GST_ELEMENT(mp4_demuxer), message);
	g_error_free(gerror);
	return TRUE;
	}
}
#define GST_BUFFER_FLAG_IS_SYNC (GST_BUFFER_FLAG_LAST<<2)


/*=============================================================================
FUNCTION:            mp4_write_video_data
        
DESCRIPTION:         Pushes the parsed data to respective source pads.

ARGUMENTS PASSED:    
  source_buffer  -   pointer to the data which needs to be 
					 pushed to source pad.
  total_size    	 length/Size of the data pushed.
  time_stamp     -   The time stamp of each frame      
  src_pad_index  -   the src pad no to which data has to be pushed

 		
        
  
RETURN VALUE:        
          TRUE   -	 if write_video_data could be performed
	      FALSE	 -	 if write_video_data could not be performed
        

PRE-CONDITIONS:      None
   	    
 
POST-CONDITIONS:     None
   	    

IMPORTANT NOTES:     None
   	    
=============================================================================*/

static gboolean mp4_write_video_data(MFW_GST_MP4DEMUX_INFO_T *mp4_demuxer,
									 const void *source_buffer,
				                     guint32 total_size, u64 time_stamp,
				                     guint src_pad_index,
									 guint32 sync)
{
    GstClockTime stream_time;
    GstPad *src_pad;
    GstEvent *event;
	
    GstCaps *src_caps = NULL;
    GstBuffer *src_data_buf = NULL;
    unsigned char *tmp_buf = NULL;
    gint64 start = 0;
    int src_buf_size = 0;
    GstFlowReturn result = GST_FLOW_OK;
	guint video_offset=0;
	
	video_offset=mp4_demuxer->parser_info.mp4_parser_object_type->video_offset;
	
    stream_time = time_stamp * 1000 * 1000;	/*put the video time stamp */
	
    src_pad = mp4_demuxer->srcpad[src_pad_index];//[src_pad_index-video_offset];
	
	
    if (mp4_demuxer->new_seg_flag_video == TRUE) {
		GST_DEBUG("sending new segment event on to video src pad \n ");
		
		start=time_stamp*1000*1000;
		
		if (!gst_pad_push_event
			(src_pad,
			gst_event_new_new_segment(FALSE, 1.0, GST_FORMAT_TIME, start,
			GST_CLOCK_TIME_NONE, start))) {
			GST_ERROR("\nCannot send new segment to the src pad\n");
			mp4_demuxer->new_seg_flag_video = FALSE;
			goto War_Msg;
		}
		mp4_demuxer->new_seg_flag_video = FALSE;
    }
	
	
    /* get caps of the pad */
    src_caps = GST_PAD_CAPS(src_pad);
	
    if (src_caps == NULL) {
		GST_ERROR("\n Caps not Correct \n");
		return FALSE;
    }
	
    if(mp4_demuxer->video_object_buffer != NULL)
        src_buf_size = total_size + GST_BUFFER_SIZE(mp4_demuxer->video_object_buffer);
    else
		src_buf_size = total_size;
	
    /* Allocates a new, empty buffer optimized to push to pad stream_ptr->src_pad */
    result = gst_pad_alloc_buffer(src_pad, 0, src_buf_size,
		src_caps, &src_data_buf);
	
    if (result == GST_FLOW_WRONG_STATE) 
    {
		GST_ERROR("\n Cannot Create Output Buffer, result is %d", result);
		return FALSE;
    }
	else if (result == GST_FLOW_NOT_LINKED  ) 
    {
		GST_ERROR("\n Cannot Create Output Buffer, result is %d", result);
		return TRUE;
	}
    tmp_buf = GST_BUFFER_DATA(src_data_buf);
	
    if(mp4_demuxer->video_object_buffer != NULL)
    {
        unsigned char *video_data = GST_BUFFER_DATA(mp4_demuxer->video_object_buffer);
        unsigned int video_data_size = GST_BUFFER_SIZE(mp4_demuxer->video_object_buffer);
        memcpy  (tmp_buf, video_data, video_data_size); 
        memcpy(tmp_buf + video_data_size, source_buffer, total_size);
        gst_buffer_unref(mp4_demuxer->video_object_buffer);
        mp4_demuxer->video_object_buffer = NULL;
    }
    else
		memcpy(tmp_buf, source_buffer, total_size);
	
    
	
    /* get the timestamp of the data in the buffer */
    GST_BUFFER_TIMESTAMP(src_data_buf) = stream_time;
	
	if (sync)
	{
		GST_BUFFER_FLAG_SET(src_data_buf, GST_BUFFER_FLAG_IS_SYNC);
	} 
    GST_DEBUG("pushing video data on to src pad \n ");
	
    /* pushes buffer to the peer element */
    result = gst_pad_push(src_pad, src_data_buf);
    if (result != GST_FLOW_OK) {
		GST_ERROR("\n Cannot Push Data onto next element, reason is %d", result);
		return FALSE;
    }

    mp4_demuxer->videosent = TRUE;
    
    /* Set the last observed stop position in the segment to position */
    gst_segment_set_last_stop(&mp4_demuxer->segment, GST_FORMAT_TIME,
		stream_time);
    
    return TRUE;
	
War_Msg:
	{
		GstMessage *message = NULL;
		GError *gerror = NULL;
		gerror = g_error_new_literal(1, 0, "pads are not negotiated!");
		
		message =
			gst_message_new_warning(GST_OBJECT(GST_ELEMENT(mp4_demuxer)), gerror,
			"video pads are not negotiated!");
		gst_element_post_message(GST_ELEMENT(mp4_demuxer), message);
		g_error_free(gerror);
		return TRUE;
	}
}

/*=============================================================================
FUNCTION:            MP4LocalCalloc
        
DESCRIPTION:         Implements the calloc function
  

ARGUMENTS PASSED:
        TotalNumber -   total number of memory blocks
        TotalSize   -   size of each block
        
  
RETURN VALUE:         memory pointer
        

PRE-CONDITIONS:
   	    None
 
POST-CONDITIONS:
   	    None

IMPORTANT NOTES:
   	    None
=============================================================================*/

void *app_MP4LocalCalloc(guint32 TotalNumber, guint32 TotalSize)
{


    /* Void Pointer. */
    void *PtrCalloc = NULL;
    if (TotalNumber==0) TotalNumber=1;
    /* Allocate the memory. */
    PtrCalloc = (void *) g_malloc(TotalNumber * TotalSize);
    memset(PtrCalloc, 0, TotalNumber * TotalSize);

    return (PtrCalloc);
}

/*=============================================================================
FUNCTION:            MP4LocalMalloc
        
DESCRIPTION:         Implements the malloc function
  

ARGUMENTS PASSED:
        TotalSize   - size of the memory requested
        
  
RETURN VALUE:         memory pointer
        

PRE-CONDITIONS:
   	    None
 
POST-CONDITIONS:
   	    None

IMPORTANT NOTES:
   	    None
=============================================================================*/

void *app_MP4LocalMalloc(guint32 TotalSize)
{

    /* Void pointer to the malloc. */
    void *PtrMalloc = NULL;

    /* Allocate the mempry. */
    PtrMalloc = g_malloc(TotalSize);

    return (PtrMalloc);
}

/*=============================================================================
FUNCTION:            MP4LocalFree
        
DESCRIPTION:         Implements the memory free function
  

ARGUMENTS PASSED:
        MemoryBlock - memory pointer
        
  
RETURN VALUE:         memory pointer
        

PRE-CONDITIONS:
   	    None
 
POST-CONDITIONS:
   	    None

IMPORTANT NOTES:
   	    None
=============================================================================*/

void app_MP4LocalFree(void *MemoryBlock)
{
    g_free(MemoryBlock);
    MemoryBlock = NULL;
}

/*=============================================================================
FUNCTION:            MP4LocalReAlloc
        
DESCRIPTION:         Implements the memory reallocation function
  

ARGUMENTS PASSED:
        MemoryBlock - memory pointer
        
  
RETURN VALUE:         memory pointer
        

PRE-CONDITIONS:
   	    None
 
POST-CONDITIONS:
   	    None

IMPORTANT NOTES:
   	    None
=============================================================================*/

void *app_MP4LocalReAlloc(void *MemoryBlock, guint32 TotalSize)
{

    /* Void Pointer. */
    void *PtrRealloc = NULL;

    /* Re alloc the memory. */
    PtrRealloc = (void *) realloc(MemoryBlock, TotalSize);

    return (PtrRealloc);
}

/*=============================================================================
FUNCTION:            MP4PullData
        
DESCRIPTION:         Implements pulling a buffer from peer pad
  

ARGUMENTS PASSED:
        moved_loc -  The start offset of the buffer 
        
  
RETURN VALUE:        GstBuffer pointer
        

PRE-CONDITIONS:
   	    None
 
POST-CONDITIONS:
   	    None

IMPORTANT NOTES:
   	    None
=============================================================================*/

GstBuffer *MP4PullData(MFW_GST_MP4DEMUX_INFO_T *mp4_demuxer)
{
    guint moved_loc=mp4_demuxer->file_info.buf_offset;
    GstBuffer *tmpbuf = NULL;

    /* Pulls a buffer from the peer pad */
    if (gst_pad_pull_range(mp4_demuxer->sinkpad, moved_loc,
			   QUERY_SIZE, &tmpbuf) != GST_FLOW_OK) {
	GST_ERROR(" FILE_READ: not able to read the data from %d\n",
		  moved_loc);
	return NULL;

    } else
	return tmpbuf;

}

/*=============================================================================
FUNCTION:            MP4LocalReadFile
        
DESCRIPTION:         This function reads the requestd amount of data from
                     a block of data.
 
  
ARGUMENTS PASSED:        
      SourceBuffer   buffer to write the data 
      TotalSize      size of each of block of data
      NumberOfTimes  The number of blocks of data
      DestStream     the file handle.


RETURN VALUE :       size of the data read.

PRE-CONDITIONS:      None
   	    
 
POST-CONDITIONS:     None
   	    

IMPORTANT NOTES:     None
   	    
=============================================================================*/

u32 app_MP4LocalReadFile(void *SourceBuffer,guint32 TotalSize,
		             guint32 NumberOfTimes, FILE * DestStream,
					 void *app_context)
{

    GstBuffer *residue_buf = NULL;
    GstBuffer *new_buf = NULL;
	MFW_GST_MP4DEMUX_INFO_T *mp4_demuxer = NULL;
    guint32 residue_size = 0;
    guint32 data_req = 0;
	mp4_demuxer = (MFW_GST_MP4DEMUX_INFO_T *)app_context;

    data_req = TotalSize * NumberOfTimes;	/* requested data size */

    while (1)
    {
	residue_size = mp4_demuxer->buf_size;
	if (data_req > residue_size) {

	    if (residue_size != 0) {	/* if data is remaining in the buffer */

		residue_buf = gst_buffer_new_and_alloc(residue_size);

		memcpy(residue_buf->data, mp4_demuxer->inbuff,
		       residue_size);

		/* pulls block of data */
		new_buf = MP4PullData(mp4_demuxer);

		if (new_buf == NULL) {
		    GST_ERROR("not able to pull the data\n");
		    return 0;
		}
		if (mp4_demuxer->tmpbuf != NULL) {
		    gst_buffer_unref(mp4_demuxer->tmpbuf);
		    mp4_demuxer->tmpbuf = NULL;
		}
		mp4_demuxer->tmpbuf =
		    gst_buffer_join(residue_buf, new_buf);
	    } else {
		/* if no data is remaining in the buffer */

		if (mp4_demuxer->tmpbuf != NULL) {
		    gst_buffer_unref(mp4_demuxer->tmpbuf);
		    mp4_demuxer->tmpbuf = NULL;
		}

		/* pulls block of data */
		mp4_demuxer->tmpbuf =
		    MP4PullData(mp4_demuxer);

		if (mp4_demuxer->tmpbuf == NULL) {
		    GST_ERROR("not able to pull the data\n");
		    return 0;
		}

	    }

	    mp4_demuxer->buf_size = GST_BUFFER_SIZE(mp4_demuxer->tmpbuf);	/* buffer size */
	    mp4_demuxer->inbuff = GST_BUFFER_DATA(mp4_demuxer->tmpbuf);	/* buffer pointer */
	    mp4_demuxer->file_info.buf_offset += QUERY_SIZE;	/* next buffer offset */

	    if (data_req <= mp4_demuxer->buf_size)
		break;
	    else
		continue;

	}
	break;
    }

    memcpy(SourceBuffer, mp4_demuxer->inbuff, data_req);

    mp4_demuxer->file_info.offset += data_req;

    mp4_demuxer->inbuff += data_req;
    mp4_demuxer->buf_size -= data_req;

    return (data_req);

}


/*=============================================================================
FUNCTION:            MP4LocalFileSize
        
DESCRIPTION:         This function gets the size of the file.

ARGUMENTS PASSED:    None
        
  
RETURN VALUE:        the size of the file.
        

PRE-CONDITIONS:      None
   	    
 
POST-CONDITIONS:     None
   	    

IMPORTANT NOTES:     None
   	    
=============================================================================*/

LONGLONG app_MP4LocalFileSize(FILE * fileHandle,void *app_context)
{
    GstPad *my_peer_pad = NULL;
	MFW_GST_MP4DEMUX_INFO_T *mp4_demuxer = NULL;
    GstFormat fmt = GST_FORMAT_BYTES;

	mp4_demuxer = (MFW_GST_MP4DEMUX_INFO_T *)app_context;

    my_peer_pad = gst_pad_get_peer(mp4_demuxer->sinkpad);
    gst_pad_query_duration(my_peer_pad, &fmt,
			   &(mp4_demuxer->file_info.length));

	gst_object_unref(my_peer_pad);
    return (mp4_demuxer->file_info.length);
}

/*=============================================================================
FUNCTION:            MP4LocalGetCurrentFilePos
        
DESCRIPTION:         this function gets the current position of the file handler

ARGUMENTS PASSED:    
        fileHandle   the handle to the file.
  
RETURN VALUE:        the position of the file handler
        

PRE-CONDITIONS:      None
   	    
 
POST-CONDITIONS:     None
   	    

IMPORTANT NOTES:     None
   	    
=============================================================================*/

LONGLONG app_MP4LocalGetCurrentFilePos(FILE * fileHandle,void *app_context)
{
	MFW_GST_MP4DEMUX_INFO_T *mp4_demuxer = NULL;
	mp4_demuxer = (MFW_GST_MP4DEMUX_INFO_T *)app_context;

    return (mp4_demuxer->file_info.offset);
}

/*=============================================================================
FUNCTION:            MP4LocalFileOpen
        
DESCRIPTION:         this function gets the file handle of the file.

ARGUMENTS PASSED:    
      FileName       the name of the file
      ModeToOpen     mode in which the file has to be opened.
	          
  
RETURN VALUE:        NULL pointer
        

PRE-CONDITIONS:      None
   	    
 
POST-CONDITIONS:     None
   	    

IMPORTANT NOTES:     None
   	    
=============================================================================*/

FILE *app_MP4LocalFileOpen(const u8 * FileName, const u8 * ModeToOpen)
{
    FILE *FpOpen = NULL;
    return (FpOpen);
}

/*=============================================================================
FUNCTION:            MP4LocalSeekFile
        
DESCRIPTION:         this function moves the file handle to the desired number of locations

ARGUMENTS PASSED:    
      fileHandle     the handle to the file
      offset         offset to which the handler to be moved.
	  origin         the file handler position from which it has to be moved
        
  
RETURN VALUE:        None
        

PRE-CONDITIONS:      None
   	    
 
POST-CONDITIONS:     None
   	    

IMPORTANT NOTES:     None
   	    
=============================================================================*/

void app_MP4LocalSeekFile(FILE * fileHandle, int offset, int origin,
				 void *app_context)
{
    int seekStatus = 0;
    GstBuffer *tmpbuf = NULL;
    int file_read = 0;
    int index = 0;
    int moved_loc = 0;
	MFW_GST_MP4DEMUX_INFO_T *mp4_demuxer = NULL;

	mp4_demuxer = (MFW_GST_MP4DEMUX_INFO_T *)app_context;

    moved_loc = mp4_demuxer->file_info.offset;

    GST_DEBUG("file seek with file handle=%d and origin = %d\n",
	      fileHandle, origin);

    switch (origin) {
    case SEEK_SET:
	{
	    moved_loc = 0;
	    moved_loc += offset;
	    break;
	}

    case SEEK_CUR:
	{
	    moved_loc += offset;
	    break;
	}

    case SEEK_END:
	{
	    moved_loc = mp4_demuxer->file_info.length;
	    break;
	}

    default:
	{

	    break;
	}
    }

    mp4_demuxer->file_info.offset = moved_loc;
    mp4_demuxer->file_info.buf_offset = moved_loc;
    mp4_demuxer->buf_size = 0;

}

/*=============================================================================
FUNCTION:            mfw_gst_mp4demuxer_sink_event
        
DESCRIPTION:         This functions handles the events that triggers the
				     sink pad of the mp4demuxer element.
  

ARGUMENTS PASSED:
        pad      -   pointer to pad
        event    -   pointer to event
        
  
RETURN VALUE:
       TRUE       -	 if event is sent to sink properly
	   FALSE	  -	 if event is not sent to sink properly
        

PRE-CONDITIONS:
   	    None
 
POST-CONDITIONS:
   	    None

IMPORTANT NOTES:
   	    None
=============================================================================*/

static gboolean mfw_gst_mp4demuxer_sink_event(GstPad * pad,
					      GstEvent * event)
{

    gboolean result = TRUE;

    switch (GST_EVENT_TYPE(event)) {
    case GST_EVENT_NEWSEGMENT:
	{
	    GstFormat format;
	    gst_event_parse_new_segment(event, NULL, NULL, &format, NULL,
					NULL, NULL);
	    if (format == GST_FORMAT_TIME) {
		GST_LOG("\nCame to the FORMAT_TIME call\n");
		gst_event_unref(event);
		result = TRUE;
	    } else {
		GST_LOG("dropping newsegment event in format %s",
			gst_format_get_name(format));
		gst_event_unref(event);
		result = TRUE;
	    }
	    break;
	}

    case GST_EVENT_EOS:
	{
	    GST_DEBUG("\nDemuxer: Sending EOS to Decoder\n");
	    result = gst_pad_push_event(pad, event);

	    if (result != TRUE) {
		GST_ERROR("\n Error in pushing the event, result is %d\n",
			  result);
	    }
	    break;
	}

    default:
	{
	    result = gst_pad_event_default(pad, event);
	    return TRUE;
	}

    }

    return result;
}

/*=============================================================================
FUNCTION:   	     mfw_gst_mp4demuxer_activate

DESCRIPTION:         it will call gst_pad_activate_pull which activates or
                     deactivates the given pad in pull mode via dispatching to the
                     pad's activepullfunc

ARGUMENTS PASSED:
         pad       - pointer to GstPad to activate or deactivate

RETURN VALUE:
         TRUE      - if operation was successful
         FALSE     - if operation was unsuccessful

PRE-CONDITIONS:      None
        

POST-CONDITIONS:     None
        

IMPORTANT NOTES:     None
        
=============================================================================*/

static gboolean mfw_gst_mp4demuxer_activate(GstPad * pad)
{


    if (gst_pad_check_pull_range(pad)) {
	return gst_pad_activate_pull(pad, TRUE);
    } else {
	return FALSE;
    }

    return TRUE;

}

/*=============================================================================
FUNCTION:			 mfw_gst_mp4demuxer_activate_pull

DESCRIPTION:		 This will start the demuxer task function in "pull" based	
					 scheduling model.

ARGUMENTS PASSED:
	pad		         sink pad, where the data will be pulled into, from	
					 file source.
	active 	         pad is active or not(TRUE/FALSE)
 

RETURN VALUE:		
		TRUE      -  if operation was successful
        FALSE     -  if operation was unsuccessful

PRE-CONDITIONS:      None

POST-CONDITIONS:     None

IMPORTANT NOTES:     None
=============================================================================*/

static gboolean
mfw_gst_mp4demuxer_activate_pull(GstPad * pad, gboolean active)
{
    gboolean ret_val = TRUE;
    MFW_GST_MP4DEMUX_INFO_T *demuxer_data =
		MFW_GST_MP4_DEMUXER(GST_PAD_PARENT(pad));
	gint track_count;
	guint video_offset=0;
	guint audio_offset=0;
	

	
    if (active) 
	{
		
		/* read the track information present in the container */
		ret_val=mfw_gst_mp4demuxer_parse(demuxer_data);
		
		if(ret_val==FALSE)
		{
		/* if the parsing failed due to unsupported stream or 
			corrupted stream,return back */
			mfw_gst_mp4_demuxer_close(demuxer_data);
			return FALSE;
		}
		
		
		/* start a Task, which will read each frame of audio/video data from the 
		container */
		video_offset=demuxer_data->parser_info.mp4_parser_object_type->video_offset;
		audio_offset=demuxer_data->parser_info.mp4_parser_object_type->audio_offset;
#ifdef _ORIG_ONE_TASK
		ret_val = gst_pad_start_task(pad, (GstTaskFunction)mfw_gst_mp4demuxer_taskfunc, demuxer_data);
#else
#if 0

		for (track_count = 0; track_count < demuxer_data->total_src_pad; track_count++) 
		{
			if (demuxer_data->parser_info.mp4_parser_object_type->trak[track_count+audio_offset]->tracktype == MP4_AUDIO)
			{
				ret_val = gst_pad_start_task(demuxer_data->srcpad[track_count+audio_offset],
					(GstTaskFunction)mfw_gst_mp4demuxer_taskfunc,
					demuxer_data->srcpad[track_count+audio_offset]);
				break;
			}
		}

		for (track_count = 0; track_count < demuxer_data->total_src_pad; track_count++) 
		{
			if (demuxer_data->parser_info.mp4_parser_object_type->trak[track_count+video_offset]->tracktype == MP4_VIDEO)
			{
				ret_val = gst_pad_start_task(demuxer_data->srcpad[track_count+video_offset],
					(GstTaskFunction)mfw_gst_mp4demuxer_taskfunc,
					demuxer_data->srcpad[track_count+video_offset]);
				break;
			}
		}
#else
        for (track_count = 0; track_count < demuxer_data->total_src_pad; track_count++) {
            if (demuxer_data->srcpad[track_count]){
                ret_val = gst_pad_start_task(demuxer_data->srcpad[track_count],
    					(GstTaskFunction)mfw_gst_mp4demuxer_taskfunc,
    					demuxer_data->srcpad[track_count]);
            }
        }
		
#endif
#endif
		
		
		if (ret_val == FALSE) {
			GST_ERROR("Task could not be started \n");
			MP4ParserFreeAtom(demuxer_data->parser_info.mp4_parser_object_type);
			mfw_gst_mp4_demuxer_close(demuxer_data);
			return ret_val;
		}
		
		return TRUE;
    }/* if (active)*/
	else 
	{
		/* pause the Task */
#ifdef _ORIG_ONE_TASK
		return gst_pad_pause_task(pad);
#else
		for (track_count = 0; track_count < demuxer_data->total_src_pad; track_count++)
		{
			ret_val = gst_pad_pause_task(demuxer_data->srcpad[track_count]);
			if (ret_val == FALSE) {
				GST_ERROR("Task could not be paused \n");
				return ret_val;
			}
		}
		return TRUE;
#endif
		
    }
	
}

/*=============================================================================
FUNCTION:   	     MP4CreateTagList
	
DESCRIPTION:	     This function Add the user data information of the mp4 container
                     to the taglist.

ARGUMENTS PASSED:
      demuxer_info   : - The main structure of mp4 parser plugin context.
	  
     
RETURN VALUE       
                     None
        

PRE-CONDITIONS:      None
        

POST-CONDITIONS:     None
        

IMPORTANT NOTES:     None
        
=============================================================================*/

static void MP4CreateTagList(MFW_GST_MP4DEMUX_INFO_T *demuxer_info)
{
	sMP4ParserUdtaList *mp4_userdata_list=NULL;
	gchar *tag_name = NULL;
	guint count=0;
	gchar  *codec_name = NULL;
	sMP4ParserFileInfo *file_info=NULL;
	sMP4ParserObjectType *parser_info=NULL;
	
    GstTagList *list_tag = gst_tag_list_new();
	mp4_userdata_list    = demuxer_info->parser_info.mp4_parser_data_list;
	file_info            = demuxer_info->parser_info.mp4_parser_file_info;
	parser_info          = demuxer_info->parser_info.mp4_parser_object_type;
	   

	/* sets the value for the decoder info tags */

	tag_name = GST_TAG_DURATION;
	    gst_tag_list_add(list_tag, GST_TAG_MERGE_APPEND, tag_name,
			  (guint64)parser_info->media_duration,NULL);

	for (count = 0; count < demuxer_info->total_tracks; count++)
	{

		if (demuxer_info->parser_info.mp4_parser_object_type->trak[count]->
	    tracktype == MP4_AUDIO) 
		{
		if (file_info->MP4PmStreamArray[count]->decoderType ==
		AUDIO_MP3)
		{
			if(parser_info->trak[count]->channels==MONO)
                codec_name = "MP3,MONO";

			else if(parser_info->trak[count]->channels==STEREO)
				codec_name = "MP3,STEREO";
		}
		else if (file_info->MP4PmStreamArray[count]->decoderType ==
		     AUDIO_MPEG2_AAC_LC)
		{
			if(parser_info->trak[count]->channels==MONO)
                codec_name = "AAC LC,MONO";

			else if(parser_info->trak[count]->channels==STEREO)
				codec_name = "AAC LC,STEREO";
		}
        else if (file_info->MP4PmStreamArray[count]->decoderType ==
                AUDIO_AAC
                || file_info->MP4PmStreamArray[count]->decoderType ==
                AUDIO_MPEG2_AAC)
		{
			if(parser_info->trak[count]->channels==MONO)
                codec_name = "AAC,MONO";

			else if(parser_info->trak[count]->channels==STEREO)
				codec_name = "AAC,STEREO";
		}
		if (file_info->MP4PmStreamArray[count]->decoderType ==
		AUDIO_AMR)
		{
			if(file_info->MP4PmStreamArray[count]->MediaTimeScale==8000)
		        codec_name = "AMR Narrowband";
			else if(file_info->MP4PmStreamArray[count]->MediaTimeScale==16000)
				codec_name = "AMR Wideband";
		}

		tag_name = GST_TAG_AUDIO_CODEC;
	    gst_tag_list_add(list_tag, GST_TAG_MERGE_APPEND, tag_name,
			  codec_name, NULL);

		tag_name = GST_TAG_BITRATE;
	    gst_tag_list_add(list_tag, GST_TAG_MERGE_APPEND, tag_name,
			  file_info->MP4PmStreamArray[count]->avgBitRate, NULL);

		tag_name = MFW_GST_TAG_SAMPLING_FREQUENCY;
	    gst_tag_list_add(list_tag, GST_TAG_MERGE_APPEND, tag_name,
			  file_info->MP4PmStreamArray[count]->MediaTimeScale, NULL);


		}

		if (demuxer_info->parser_info.mp4_parser_object_type->trak[count]->
	    tracktype == MP4_VIDEO) 
		{
		if (file_info->MP4PmStreamArray[count]->decoderType ==
		VIDEO_MPEG4)
		codec_name = "MPEG4";
	    else if (file_info->MP4PmStreamArray[count]->decoderType ==
		     VIDEO_H264)
        codec_name = "H264";
		else if (file_info->MP4PmStreamArray[count]->decoderType ==
		     VIDEO_H263)
        codec_name = "H263";

		tag_name = GST_TAG_VIDEO_CODEC;
	    gst_tag_list_add(list_tag, GST_TAG_MERGE_APPEND, tag_name,
			  codec_name, NULL);

		
		tag_name = MFW_GST_TAG_WIDTH;
	    gst_tag_list_add(list_tag, GST_TAG_MERGE_APPEND, tag_name,
			  parser_info->media[count]->media_width, NULL);

		tag_name = MFW_GST_TAG_HEIGHT;
	    gst_tag_list_add(list_tag, GST_TAG_MERGE_APPEND, tag_name,
			  parser_info->media[count]->media_height, NULL);

		tag_name = MFW_GST_TAG_FRAMERATE;
	    gst_tag_list_add(list_tag, GST_TAG_MERGE_APPEND, tag_name,
			  parser_info->media[count]->media_framerate, NULL);
		

		}
	}
	
    /* sets the value for the user info tags */

	if (mp4_userdata_list->FileName) {
	tag_name = GST_TAG_TITLE;
	gst_tag_list_add(list_tag, GST_TAG_MERGE_APPEND, tag_name,
			 mp4_userdata_list->FileName, NULL);
	}

	if (mp4_userdata_list->ArtistName) {
	tag_name = GST_TAG_ARTIST;
	gst_tag_list_add(list_tag, GST_TAG_MERGE_APPEND, tag_name,
			 mp4_userdata_list->ArtistName, NULL);
	}

	if (mp4_userdata_list->AlbumName) {
	tag_name = GST_TAG_ALBUM;
	gst_tag_list_add(list_tag, GST_TAG_MERGE_APPEND, tag_name,
			 mp4_userdata_list->AlbumName, NULL);
	}

	if (mp4_userdata_list->Genre) {
	tag_name = GST_TAG_GENRE;
	gst_tag_list_add(list_tag, GST_TAG_MERGE_APPEND, tag_name,
			 mp4_userdata_list->Genre, NULL);
	}

	if (mp4_userdata_list->Comment) {
	tag_name = GST_TAG_COMMENT;
	gst_tag_list_add(list_tag, GST_TAG_MERGE_APPEND, tag_name,
			 mp4_userdata_list->Comment, NULL);
	}

	if (mp4_userdata_list->Year) {
	tag_name = MFW_GST_TAG_YEAR;
	gst_tag_list_add(list_tag, GST_TAG_MERGE_APPEND, tag_name,
			 mp4_userdata_list->Year, NULL);
	}

	if (mp4_userdata_list->Tool) {
	tag_name = GST_TAG_ENCODER;
	gst_tag_list_add(list_tag, GST_TAG_MERGE_APPEND, tag_name,
			 mp4_userdata_list->Tool, NULL);
	}


	gst_element_found_tags(GST_ELEMENT(demuxer_info), list_tag);

	return;
}
/*=============================================================================
FUNCTION:   	     mfw_gst_mp4demuxer_change_state
	
DESCRIPTION:	     This function keeps track of different states of pipeline.

ARGUMENTS PASSED:
      element    :   pointer to the mp4 demuxer element.
	  transition :   state of the pipeline.
     
RETURN VALUE       
        GST_STATE_CHANGE_FAILURE    - the state change failed
        GST_STATE_CHANGE_SUCCESS    - the state change succeeded
        GST_STATE_CHANGE_ASYNC      - the state change will happen
                                        asynchronously
        GST_STATE_CHANGE_NO_PREROLL - the state change cannot be prerolled
        

PRE-CONDITIONS:      None
        

POST-CONDITIONS:     None
        

IMPORTANT NOTES:     None
        
=============================================================================*/

static GstStateChangeReturn
mfw_gst_mp4demuxer_change_state(GstElement * element,
				GstStateChange transition)
{
    GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
    MFW_GST_MP4DEMUX_INFO_T *mp4_demuxer = MFW_GST_MP4_DEMUXER(element);
    guint32 stream_count;

    GstFormat fmt = GST_FORMAT_TIME;
    gint64 pos = 0, len = 0;
    gboolean ret_value;
    guint count;

	/***** UPWARDS *****/
    switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
	{
	    GST_DEBUG
		("\nCame to Demuxer GST_STATE_CHANGE_NULL_TO_READY\n");

	    mp4_demuxer->parser_info.mp4_parser_object_type = NULL;
	    mp4_demuxer->parser_info.mp4_parser_read_byte_info = NULL;
	    mp4_demuxer->parser_info.mp4_parser_data_list = NULL;
	    mp4_demuxer->parser_info.mp4_parser_file_info = NULL;

	    mp4_demuxer->file_info.length = 0;
	    mp4_demuxer->file_info.offset = 0;
	    mp4_demuxer->file_info.buf_offset = 0;
	    mp4_demuxer->segment.last_stop = 0;
	    mp4_demuxer->new_seg_flag_audio = FALSE;
		mp4_demuxer->new_seg_flag_video = FALSE;
	    mp4_demuxer->stop_request = FALSE;
	    mp4_demuxer->buf_size = 0;
	    mp4_demuxer->tmpbuf = NULL;
	    mp4_demuxer->inbuff = NULL;
		mp4_demuxer->seek_flag = FALSE;
		mp4_demuxer->eos_flag[0] = FALSE;
		mp4_demuxer->eos_flag[1] = FALSE;
        mp4_demuxer->video_object_buffer = NULL;
		mp4_demuxer->do_seek_flag = FALSE;
		mp4_demuxer->videosent = FALSE;
	    break;
	}

    case GST_STATE_CHANGE_READY_TO_PAUSED:
	{

	    GST_DEBUG
		("\nCame to Demuxer GST_STATE_CHANGE_READY_TO_PAUSED\n");
       
        /* Registers a new tag type for the use with GStreamer's type system */
	    gst_tag_register (MFW_GST_TAG_WIDTH, GST_TAG_FLAG_DECODED,G_TYPE_UINT,
            "image width","image width(pixel)", NULL);
        gst_tag_register (MFW_GST_TAG_HEIGHT, GST_TAG_FLAG_DECODED,G_TYPE_UINT,
            "image height","image height(pixel)", NULL); 
	    gst_tag_register (MFW_GST_TAG_FRAMERATE, GST_TAG_FLAG_DECODED,G_TYPE_FLOAT,
            "video framerate","number of video frames in a second", NULL);
	    gst_tag_register (MFW_GST_TAG_SAMPLING_FREQUENCY, GST_TAG_FLAG_DECODED,G_TYPE_UINT,
            "sampling frequency","number of audio samples per frame per second", NULL);
	    gst_tag_register (MFW_GST_TAG_YEAR, GST_TAG_FLAG_DECODED,G_TYPE_UINT,
            "year","year of creation", NULL);
		
        

	    /* Initialize segment to its default values. */

	    gst_segment_init(&mp4_demuxer->segment, GST_FORMAT_UNDEFINED);


	    /* Sets the given activate function for the pad. The activate function will
	       dispatch to activate_push or activate_pull to perform the actual activation. */

	    gst_pad_set_activate_function(mp4_demuxer->sinkpad,
					  mfw_gst_mp4demuxer_activate);

	    /* Sets the given activate_pull function for the pad. An activate_pull
	       function prepares the element and any upstream connections for pulling. */

	    gst_pad_set_activatepull_function(mp4_demuxer->sinkpad,
					      GST_DEBUG_FUNCPTR
					      (mfw_gst_mp4demuxer_activate_pull));
        break;
	}

    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
	{

	    GST_DEBUG
		("\nCame to Demuxer GST_STATE_CHANGE_PAUSED_TO_PLAYING\n");
	    break;
	}

    default:
	break;

    }

    
    ret = parent_class->change_state(element, transition);
	


	 /***** DOWNWARDS *****/
    switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
	{
	    break;
	}
    case GST_STATE_CHANGE_PAUSED_TO_READY:
	{
		/* received stop request,do the following */
		MP4ParserFreeAtom(mp4_demuxer->parser_info.mp4_parser_object_type);
	    mfw_gst_mp4_demuxer_close(mp4_demuxer);
		if((mp4_demuxer->ptrFuncPtrTable!= NULL)&&(mp4_demuxer->eos_flag[0])&&(mp4_demuxer->eos_flag[1]))
		{
        app_MP4LocalFree(mp4_demuxer->ptrFuncPtrTable);
        
		}
	   
	    /* Initialize segment to its default values. */
	    gst_segment_init(&mp4_demuxer->segment, GST_FORMAT_UNDEFINED);

	    mp4_demuxer->file_info.length = 0;
	    mp4_demuxer->file_info.offset = 0;
	    mp4_demuxer->segment.last_stop = 0;
	    mp4_demuxer->file_info.buf_offset = 0;
	    mp4_demuxer->new_seg_flag_video = FALSE;
		mp4_demuxer->new_seg_flag_audio = FALSE;
	    mp4_demuxer->stop_request = TRUE;
	    mp4_demuxer->buf_size = 0;
	    mp4_demuxer->inbuff = NULL;
		mp4_demuxer->seek_flag=FALSE;
		mp4_demuxer->eos_flag[0] = FALSE;
		mp4_demuxer->eos_flag[1] = FALSE;
        mp4_demuxer->video_object_buffer = NULL;
		mp4_demuxer->do_seek_flag = FALSE;
	    break;

	}

    case GST_STATE_CHANGE_READY_TO_NULL:
	{
	    break;
	}

    default:
	break;
    }

    return ret;
}

/*=============================================================================
FUNCTION:   mfw_gst_mp4demuxer_handle_sink_query   

DESCRIPTION:    performs query on src pad.    

ARGUMENTS PASSED:
        pad     -   pointer to GstPad
        query   -   pointer to GstQuery        
            
RETURN VALUE:
        TRUE    -   success
        FALSE   -   failure

PRE-CONDITIONS:
        None

POST-CONDITIONS:
		None

IMPORTANT NOTES:
        None

==============================================================================*/

static const GstQueryType *gst_mp4demuxer_get_src_query_types(GstPad * pad)
{
    static const GstQueryType types[] = {
	GST_QUERY_POSITION,
	GST_QUERY_DURATION,
	0
    };

    return types;
}

/*=============================================================================
FUNCTION:   mfw_gst_mp4demuxer_handle_sink_query   

DESCRIPTION:    performs query on src pad.    

ARGUMENTS PASSED:
        pad     -   pointer to GstPad
        query   -   pointer to GstQuery        
            
RETURN VALUE:
        TRUE    -   success
        FALSE   -   failure

PRE-CONDITIONS:
        None

POST-CONDITIONS:
		None

IMPORTANT NOTES:
        None

==============================================================================*/

static gboolean mfw_gst_mp4demuxer_handle_src_query(GstPad * pad,
						    GstQuery * query)
{
    GstFormat format = GST_FORMAT_TIME;
    gint64 dest_value;

    MFW_GST_MP4DEMUX_INFO_T *demuxer_info;
    gboolean res = TRUE;
    demuxer_info = MFW_GST_MP4_DEMUXER(gst_pad_get_parent(pad));

    GST_DEBUG("handling %s query",
	      gst_query_type_get_name(GST_QUERY_TYPE(query)));

    switch (GST_QUERY_TYPE(query)) {
    case GST_QUERY_DURATION:
	{
		
	    /* save requested format */
	    gst_query_parse_duration(query, &format, NULL);
	  	    

	    if (format == GST_FORMAT_TIME) {

		gst_query_set_duration(query, GST_FORMAT_TIME,
				       demuxer_info->segment.duration);
	    } else if (format == GST_FORMAT_BYTES) {
		gst_query_set_duration(query, GST_FORMAT_BYTES,
				       demuxer_info->segment.duration);

	    }

	    res = TRUE;
	    break;
	}

    case GST_QUERY_POSITION:
	{
	    GstFormat format;

	    /* save requested format */
	    gst_query_parse_position(query, &format, NULL);

	    gst_query_set_position(query, GST_FORMAT_TIME,
				   demuxer_info->segment.last_stop);

	    res = TRUE;
	    break;
	}

    default:
	res = FALSE;

    }
    gst_object_unref(demuxer_info);
    return res;

}

/*=============================================================================
FUNCTION:   	mfw_gst_mp4demuxer_handle_src_event

DESCRIPTION:	Handles an event on the source pad.

ARGUMENTS PASSED:
        pad        -    pointer to pad
        event      -    pointer to event
RETURN VALUE:
        TRUE       -	if event is sent to source properly
	    FALSE	   -	if event is not sent to source properly

PRE-CONDITIONS:    None

POST-CONDITIONS:   None

IMPORTANT NOTES:   None
=============================================================================*/

static gboolean mfw_gst_mp4demuxer_handle_src_event(GstPad* src_pad, GstEvent* event)
{
    gboolean res = TRUE;
	gint i;

	MFW_GST_MP4DEMUX_INFO_T *demuxer_info = MFW_GST_MP4_DEMUXER(GST_PAD_PARENT (src_pad));
 
	switch (GST_EVENT_TYPE (event)) 
	{
		case GST_EVENT_SEEK:
		{
			GstSeekFlags flags;
	  	    gboolean     flush;
			gdouble      rate;
			GstSeekType  cur_type;
	        GstSeekType	 stop_type;
	        gint64       cur;
	        GstFormat    format;
	        gint64       stop;
           
            GST_DEBUG("Handling the seek event\n");

			gst_event_ref (event);

			/* parse a seek event */
            gst_event_parse_seek(event, &rate, &format, &flags,
			&cur_type, &cur, &stop_type, &stop);
			
			if (demuxer_info->do_seek_flag == FALSE && (demuxer_info->total_src_pad > 1)/*!strncmp("video", GST_PAD_NAME(src_pad), 5)*/)
			{
				demuxer_info->do_seek_flag = TRUE;
				gst_event_unref (event);
				return TRUE;
			}
            /* demuxer cannot do seek if format is not bytes */
            if (format != GST_FORMAT_TIME)
            {
                GST_WARNING("SEEK event not TIME format.\n");
                gst_event_unref (event);
                return TRUE;
	        }

			/* if the seek time is greater than the stream duration */
            if(cur > demuxer_info->segment.duration) 
            {
                GST_WARNING("SEEK event exceed the maximum duration.\n");
                cur = demuxer_info->segment.duration;
            }   
		
            flush = flags & GST_SEEK_FLAG_FLUSH;

		    if (flush)
            {
                guint strem_count;

				/* sends an event to upstream elements with event as new flush start event*/
                res = gst_pad_push_event (demuxer_info->sinkpad, gst_event_new_flush_start ());
                if (!res) 
				{
                    GST_ERROR("Failed to push event upstream!");
                }
                
                for(strem_count = 0; strem_count < demuxer_info->total_src_pad; strem_count++)
                {
                    /* sends an event to downstream elements with event as new flush start event*/                   
                    res = gst_pad_push_event (demuxer_info->srcpad[strem_count], 
						                      gst_event_new_flush_start());
                    if (!res)
					{
                        GST_ERROR("Failed to push event downstream!");
                    }
                }
            }/*if (flush) */
			else
			{
#if 0
				gst_pad_pause_task (demuxer_info->sinkpad);
#else
				for (i = 0; i < demuxer_info->total_src_pad; i++)
				{
					gst_pad_pause_task(demuxer_info->srcpad[i]);
				}
#endif
			}

            GST_WARNING("Stop all the task.\n");
            for (i = 0; i < demuxer_info->total_src_pad; i++)
			{
				gst_pad_pause_task(demuxer_info->srcpad[i]);
			}
			/*Lock the stream lock of sinkpad*/
			GST_PAD_STREAM_LOCK(demuxer_info->sinkpad);
            
			/* perform seek */
//			if (!strncmp("video", GST_PAD_NAME(src_pad), 5))
				res = mfw_gst_mp4demuxer_seek(demuxer_info,event);

			if(res == FALSE)
			{
              GST_ERROR("Failed in demuxer seek !!\n");

			   /*Unlock the stream lock of sink pad*/	
              GST_PAD_STREAM_UNLOCK(demuxer_info->sinkpad);
			  return FALSE;
			}
           
           
			/* sends an event to upstream elements with event as new flush stop event
			   , to resume data flow */
			res = gst_pad_push_event (demuxer_info->sinkpad, gst_event_new_flush_stop ());
   	        
            if(res == FALSE)
			{
              GST_ERROR("Failed to push event upstream!");
			}
			
            
			 /* send flush stop to down stream elements,to enable data flow*/
            if(flush)
			{
              guint strem_count;
              for(strem_count = 0; strem_count < demuxer_info->total_src_pad; strem_count++)
              {
                          
                  res = gst_pad_push_event (demuxer_info->srcpad[strem_count],
					                      gst_event_new_flush_stop());
                  if (!res) 
				  {
                      GST_ERROR("Failed to push event downstream!");
                  }
              }
          }
         
		 /*Unlock the stream lock of sink pad*/	
         GST_PAD_STREAM_UNLOCK(demuxer_info->sinkpad);
#ifdef _ORIG_ONE_TASK
          /* streaming can continue now */
          gst_pad_start_task (demuxer_info->sinkpad,
              (GstTaskFunction)mfw_gst_mp4demuxer_taskfunc,demuxer_info);



#else
	for (i = 0; i < demuxer_info->total_src_pad; i++)
	{
		if (!strncmp("video", GST_PAD_NAME(demuxer_info->srcpad[i]), 5))
		{
			demuxer_info->seek_flag=TRUE;
			demuxer_info->new_seg_flag_video=TRUE;
			GST_DEBUG("seek video src_pad !\n");
			res = gst_pad_start_task(demuxer_info->srcpad[i],
				     (GstTaskFunction)
				     mfw_gst_mp4demuxer_taskfunc,
 				     demuxer_info->srcpad[i]);
			  if (!res) 
			  {
				  GST_ERROR("Failed to start task!\n");
			  }
		}
//		else
		if (!strncmp("audio", GST_PAD_NAME(demuxer_info->srcpad[i]), 5))
		{
			demuxer_info->new_seg_flag_audio=TRUE;
			GST_DEBUG("seek audio src_pad !\n");
			res = gst_pad_start_task(demuxer_info->srcpad[i],
				     (GstTaskFunction)
				     mfw_gst_mp4demuxer_taskfunc,
				     demuxer_info->srcpad[i]);

 			  if (!res) 
			  {
				  GST_ERROR("Failed to start task!\n");
			  }
		}
	}
#endif

          break;
		}

		default:
		res = FALSE;
		break;
	}
	demuxer_info->do_seek_flag = FALSE;	
	gst_event_unref (event);
	return TRUE;
}

/*=============================================================================
FUNCTION:   	mfw_gst_mp4demuxer_seek

DESCRIPTION:    This function is called when seek event occurs. 

ARGUMENTS PASSED:
		 

RETURN VALUE:
         TRUE      -    if operation was successful
         FALSE     -    if operation was unsuccessful

PRE-CONDITIONS:
        None

POST-CONDITIONS:
        None

IMPORTANT NOTES:
        None
=============================================================================*/

static gboolean
mfw_gst_mp4demuxer_seek(MFW_GST_MP4DEMUX_INFO_T* demuxer_info, GstEvent * event)
{

	GstSeekFlags flags;
    GstSeekType  cur_type;
	GstSeekType	 stop_type;
	gint64       cur;
	GstFormat    format;
	gdouble      rate;
    gint64       stop;
	gfloat       seek_sec;
	gboolean     ret_value;
	guint        track_count; 
			
	MP4Err       Err;
	    		
	/* parse the event */
	gst_event_parse_seek (event, &rate, &format, &flags, &cur_type, &cur,
                          &stop_type, &stop);

	/* if the seek format is in time */
	if(format==GST_FORMAT_TIME)
	{

      GST_DEBUG("received seek Time : %" GST_TIME_FORMAT  " \r",GST_TIME_ARGS (cur));

      seek_sec  = (float)cur/(1000*1000*1000);

	  
 	}
	    
	/* Seek the mp4 file to the desired time */	
  	Err = MP4ParserSeekFile(demuxer_info->parser_info.mp4_parser_object_type,
							seek_sec,
							demuxer_info->total_tracks,
		                    demuxer_info->parser_info.mp4_parser_read_byte_info);

	if(Err!=MP4NoErr)
        return FALSE;	

    /* paser lib knows the first track in the media data */ 
    
	return TRUE;
 }
 

/*=============================================================================
FUNCTION:            mfw_gst_mp4_demuxer_close
        
DESCRIPTION:         This function  Frees All Memories Allocated and Resourses. 

ARGUMENTS PASSED:    None
        
  
RETURN VALUE:        None
        

PRE-CONDITIONS:      None
   	    
 
POST-CONDITIONS:     None
   	    

IMPORTANT NOTES:     None
   	    
=============================================================================*/

void mfw_gst_mp4_demuxer_close(MFW_GST_MP4DEMUX_INFO_T *mp4_demuxer)
{
    guint32 stream_count;
    

    GST_DEBUG("Freeing all the allocated memories()\n");

    MP4ParserFreeMemory(mp4_demuxer->parser_info.mp4_parser_object_type,
                        mp4_demuxer->parser_info.mp4_parser_file_info,
                        mp4_demuxer->parser_info.mp4_parser_read_byte_info,
                        mp4_demuxer->parser_info.mp4_parser_data_list,
                        MP4_PARSER_MAX_STREAM);     

    if (mp4_demuxer->parser_info.mp4_parser_object_type != NULL) {
	app_MP4LocalFree(mp4_demuxer->parser_info.mp4_parser_object_type);
    mp4_demuxer->parser_info.mp4_parser_object_type = NULL;

    }

    if (mp4_demuxer->parser_info.mp4_parser_file_info != NULL) {
	app_MP4LocalFree(mp4_demuxer->parser_info.mp4_parser_file_info);
    mp4_demuxer->parser_info.mp4_parser_file_info = NULL;
    }

    if (mp4_demuxer->parser_info.mp4_parser_read_byte_info != NULL) {	
        app_MP4LocalFree(mp4_demuxer->parser_info.mp4_parser_read_byte_info);
        mp4_demuxer->parser_info.mp4_parser_read_byte_info = NULL;
    }

    if (mp4_demuxer->parser_info.mp4_parser_data_list != NULL) {
	    app_MP4LocalFree(mp4_demuxer->parser_info.mp4_parser_data_list);
        mp4_demuxer->parser_info.mp4_parser_data_list = NULL;
    }

    if (mp4_demuxer->tmpbuf != NULL) {
	gst_buffer_unref(mp4_demuxer->tmpbuf);
	mp4_demuxer->tmpbuf=NULL;

    }


    return;

}

/*=============================================================================
FUNCTION:            mfw_gst_type_mp4_demuxer_get_type
        
DESCRIPTION:         intefaces are initiated in this function.you can register one
                     or more interfaces  after having registered the type itself.


ARGUMENTS PASSED:    None
        
  
RETURN VALUE:        A numerical value ,which represents the unique identifier of this
                     element(mp4demuxer)

        

PRE-CONDITIONS:      None
   	 
 
POST-CONDITIONS:     None
   	    

IMPORTANT NOTES:     None
   	    
=============================================================================*/

GType mfw_gst_type_mp4_demuxer_get_type(void)
{
    static GType mp4demuxer_type = 0;

    if (!mp4demuxer_type) {
	static const GTypeInfo mp4demuxer_info = {
	    sizeof(MFW_GST_MP4DEMUX_INFO_CLASS_T),
	    (GBaseInitFunc) mfw_gst_mp4_demuxer_base_init,
	    NULL,
	    (GClassInitFunc) mfw_gst_mp4_demuxer_class_init,
	    NULL,
	    NULL,
	    sizeof(MFW_GST_MP4DEMUX_INFO_T),
	    0,
	    (GInstanceInitFunc) mfw_gst_mp4_demuxer_init,
	};
	mp4demuxer_type = g_type_register_static(GST_TYPE_ELEMENT,
						 "MFW_GST_MP4DEMUX_INFO_T",
						 &mp4demuxer_info, 0);

	GST_DEBUG_CATEGORY_INIT(mfw_gst_mp4demuxer_debug, "mfw_mp4demuxer",
				0, "mp4 demuxer");
    }
    return mp4demuxer_type;
}

/*=============================================================================
FUNCTION:            mfw_gst_mp4_demuxer_base_init    
        
DESCRIPTION:         mp4demuxer element details are registered with the plugin during
                     _base_init ,This function will initialise the class and child
                     class properties during each new child class creation

ARGUMENTS PASSED:
        Klass   -    pointer to mp4demuxer plug-in class
  
RETURN VALUE:        None
        

PRE-CONDITIONS:      None
   	    
 
POST-CONDITIONS:     None
   	    

IMPORTANT NOTES:     None
   	    
=============================================================================*/

static void
mfw_gst_mp4_demuxer_base_init(MFW_GST_MP4DEMUX_INFO_CLASS_T * klass)
{

//    GST_DEBUG("Registering the element details with the plugin\n");

    static GstElementDetails mfw_gst_mp4demuxer_details = {
	"freescale-mp4 demuxer plugin",
	"Codec/Demuxer",
	"Demultiplex an mp4 data into audio and video data",
	"Multimedia Team <mmmsw@freescale.com>"
    };

    GstElementClass *element_class = GST_ELEMENT_CLASS(klass);
    gst_element_class_add_pad_template(element_class, audio_src_templ());
	gst_element_class_add_pad_template(element_class, video_src_templ());

    gst_element_class_add_pad_template(element_class,
				       gst_static_pad_template_get
				       (&mfw_gst_mp4_demuxer_sink_factory));

    gst_element_class_set_details(element_class,
				  &mfw_gst_mp4demuxer_details);

    return;
}

/*=============================================================================
FUNCTION:            mfw_gst_mp4_demuxer_class_init
        
DESCRIPTION:         Initialise the class only once (specifying what signals,
                     arguments and virtual functions the class has and setting up
                     global state)

ARGUMENTS PASSED:    klass   - pointer to mp4demuxer element class
        
  
RETURN VALUE:        None
        

PRE-CONDITIONS:      None
   	    
 
POST-CONDITIONS:     None
   	    

IMPORTANT NOTES:     None
   	    
=============================================================================*/

static void
mfw_gst_mp4_demuxer_class_init(MFW_GST_MP4DEMUX_INFO_CLASS_T * klass)
{
    GObjectClass *gobject_class = NULL;
    GstElementClass *gstelement_class = NULL;

    GST_DEBUG("Initialise the demuxer class\n");

    gobject_class = (GObjectClass *) klass;
    gstelement_class = (GstElementClass *) klass;
    parent_class = g_type_class_peek_parent(klass);

    gstelement_class->change_state = mfw_gst_mp4demuxer_change_state;

	gobject_class->finalize = GST_DEBUG_FUNCPTR (mfw_gst_mp4_demuxer_finalize);

    return;
}

/*=============================================================================
FUNCTION:            mfw_gst_mp4_demuxer_init
        
DESCRIPTION:         This function creates the pads on the elements and register the
			         function pointers which operate on these pads. 

ARGUMENTS PASSED:    
      demuxer_info  :pointer to the mp4demuxer plugin structure.
	  gclass        :pointer to mp4demuxer element class.
  
RETURN VALUE:        None
        

PRE-CONDITIONS:      None 
   	  
 
POST-CONDITIONS:     None
 

IMPORTANT NOTES:     None
   	   
=============================================================================*/
static void
mfw_gst_mp4_demuxer_init(MFW_GST_MP4DEMUX_INFO_T * demuxer_info,
			 MFW_GST_MP4DEMUX_INFO_CLASS_T * gclass)
{

    gchar *padname = NULL;
    GstCaps *caps = NULL;
    GstPadTemplate *templ = NULL;
    GstPad *pad = NULL;
    gboolean set;


    GstElementClass *klass = GST_ELEMENT_GET_CLASS(demuxer_info);
    

    demuxer_info->sinkpad =
	gst_pad_new_from_template(gst_element_class_get_pad_template
				  (klass, "sink"), "sink");

    GST_DEBUG("Register the function pointers on to the sink pad\n");

    gst_pad_set_setcaps_function(demuxer_info->sinkpad,
				 mfw_gst_mp4_demuxer_set_caps);

    gst_element_add_pad(GST_ELEMENT(demuxer_info), demuxer_info->sinkpad);

    gst_pad_set_event_function(demuxer_info->sinkpad,
			       GST_DEBUG_FUNCPTR
			       (mfw_gst_mp4demuxer_sink_event));

#define MFW_GST_MP4_PARSER_PLUGIN VERSION
    PRINT_CORE_VERSION(MPEG4ParserVersionInfo());
    PRINT_PLUGIN_VERSION(MFW_GST_MP4_PARSER_PLUGIN);

	demuxer_info->media_file_lock= g_mutex_new();

    return;
}

static void
mfw_gst_mp4_demuxer_finalize(GObject * object)
{
	MFW_GST_MP4DEMUX_INFO_T *demuxer_info;
	
	demuxer_info = MFW_GST_MP4_DEMUXER (object);
	g_mutex_free (demuxer_info->media_file_lock);

}
/*=============================================================================
FUNCTION:            mfw_gst_mp4_demuxer_set_caps
         
DESCRIPTION:         this function handles the link with other plug-ins and used for
                     capability negotiation  between pads  

ARGUMENTS PASSED:    
        pad      -   pointer to GstPad
        caps     -   pointer to GstCaps
        
  
RETURN VALUE:        None
        

PRE-CONDITIONS:      None
   	    
 
POST-CONDITIONS:     None
   	    

IMPORTANT NOTES:     None
   	    
=============================================================================*/

static gboolean mfw_gst_mp4_demuxer_set_caps(GstPad * pad, GstCaps * caps)
{

    MFW_GST_MP4DEMUX_INFO_T *demuxer_info;

    GstStructure *structure = gst_caps_get_structure(caps, 0);
    demuxer_info = MFW_GST_MP4_DEMUXER(gst_pad_get_parent(pad));

    gst_object_unref(demuxer_info);
    return TRUE;
}

/*=============================================================================
FUNCTION:            plugin_init
        
ESCRIPTION:          special function , which is called as soon as the plugin or
                     element is loaded and information returned by this function
                     will be cached in central registry


ARGUMENTS PASSED:    
          plugin -   pointer to container that contains features loaded
                     from shared object module
        
  
RETURN VALUE:        return TRUE or FALSE depending on whether it loaded initialized any
                     dependency correctly
        

PRE-CONDITIONS:      None
   	    
 
POST-CONDITIONS:     None
   	   

IMPORTANT NOTES:     None
   	    
=============================================================================*/


static gboolean plugin_init(GstPlugin * plugin)
{
    return gst_element_register(plugin, "mfw_mp4demuxer",
				GST_RANK_PRIMARY,
				MFW_GST_TYPE_MP4_DEMUXER);
}

/*   This is used to define the entry point and meta data of plugin   */

GST_PLUGIN_DEFINE(GST_VERSION_MAJOR,	/* major version of Gstreamer  */
		  GST_VERSION_MINOR,	/* minor version of Gstreamer  */
		  "mfw_mp4demuxer",	/* name of the plugin          */
		  "demuxes audio streams from mp4/m4a file",	/* what plugin actually does   */
		  plugin_init,	/* first function to be called */
		  VERSION,
		  GST_LICENSE_UNKNOWN,
		  "freescale semiconductor", "http://freescale.com/")

