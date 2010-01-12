/*
 * Copyright (C) 2005-2008 Freescale Semiconductor, Inc. All rights reserved.
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
 * Module Name:    mfw_gst_mp4_demuxer.h
 *
 * Description: This Header file contains all the declarations 
 *              of MP4/M4a Demuxer Plugin for GStreamer.                         
 * Portability:    This code is written for Linux OS and Gstreamer
 */  
 
/*
 * Changelog: 
 *
 */

/*=============================================================================
                            INCLUDE FILES
=============================================================================*/

#ifndef _MFW_GST_MP4_DEMUXER_H
#define _MFW_GST_MP4_DEMUXER_H

/*=============================================================================
                              CONSTANTS
=============================================================================*/

/* None. */

/*=============================================================================
                               ENUMS
=============================================================================*/
/* None */

/*=============================================================================
                               MACROS
=============================================================================*/
G_BEGIN_DECLS

#define MAX_SRC_PADS  2
#define MFW_GST_TYPE_MP4_DEMUXER \
  (mfw_gst_type_mp4_demuxer_get_type())
#define MFW_GST_MP4_DEMUXER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),MFW_GST_TYPE_MP4_DEMUXER,MFW_GST_MP4DEMUX_INFO_T))
#define MFW_GST_MP4_DEMUXER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),MFW_GST_TYPE_MP4_DEMUXER,MFW_GST_MP4DEMUX_INFO_CLASS_T))
#define MFW_GST_IS_MP4_DEMUXER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),MFW_GST_TYPE_MP4_DEMUXER))
#define GST_IS_MP4_DEMUXER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),MFW_GST_TYPE_MP4_DEMUXER))
/*=============================================================================
                            STRUCTURES AND OTHER TYPEDEFS
=============================================================================*/
    typedef struct _mp4_input_file_info {
    gint64 length;		                               /* Length of the file/buffer */
    guint offset;	                                   /* Present location.         */
    guint buf_offset;

} mp4_input_file_info;

typedef struct{
	GstPad *srcpad;
	gint32 trackindex;
}Pad2Trackindex;

typedef struct {
    sMP4ParserObjectType *mp4_parser_object_type;	    /* main parsser data structure */
    sMP4ParserFileInfo *mp4_parser_file_info;	        /* mp4 parser file info        */
    sMP4ParserReadByteInfo *mp4_parser_read_byte_info;	/* Access unit info            */
    sMP4ParserUdtaList *mp4_parser_data_list;	        /* User data info              */
} mp4_parser_info;

typedef struct _MFW_GST_MP4DEMUX_INFO_T {
    GstElement element;		                            /* instance of base class      */
    GstPad *sinkpad;		                            /* sink pad of element         */
    GstPad *srcpad[MAX_SRC_PADS];	                    /* src pads for output data    */
    guint32 total_src_pad;	                            /* number of src pads          */
	guint32 total_tracks;                               /* total number of tracks      */
	guint32 track_index; 
    mp4_input_file_info file_info;	                    /* file information            */
    mp4_parser_info parser_info;
    GstSegment segment;		                            /* configured play segment     */
    GstPad *src_pad;
    gboolean stop_request;	                            /* stop request flag           */
    gboolean new_seg_flag_video;
	gboolean new_seg_flag_audio;
    GstCaps *caps[MAX_SRC_PADS];	                    /* src pad capability          */
    GstBuffer *tmpbuf;		                            /* buffer to store block of 
				                                                 input data            */
    guint buf_size;		                                /* temparory buffer size       */
    guint8 *inbuff;
	gboolean seek_flag;
	sFunctionPtrTable      *ptrFuncPtrTable;            /* pointer to function pinter
		                                                    table                      */
	gboolean eos_flag[MAX_SRC_PADS];
    GstBuffer *video_object_buffer;
	GMutex *media_file_lock;//lock for v4ldevice operation
	gboolean do_seek_flag;
    gboolean videosent;
	Pad2Trackindex srcpadnum2trackindex[MAX_SRC_PADS];

} MFW_GST_MP4DEMUX_INFO_T;


typedef struct _MFW_GST_MP4DEMUX_INFO_CLASS_T {
    GstElementClass parent_class;

} MFW_GST_MP4DEMUX_INFO_CLASS_T;

/*=============================================================================
                           GLOBAL VARIABLE DECLARATIONS
=============================================================================*/

/* None. */

/*=============================================================================
                            FUNCTION PROTOTYPES
=============================================================================*/
GType mfw_gst_type_mp4_demuxer_get_type(void);

/*===========================================================================*/
G_END_DECLS
#endif /*_MFW_GST_MP4_DEMUXER_H*/
