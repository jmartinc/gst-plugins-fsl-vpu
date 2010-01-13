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
 * Module Name:    mfw_gst_v4lsink.h
 *
 * Description:    Header file for V4L Sink Plug-in for GStreamer.
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

#ifndef _MFW_GST_V4LSINK_H_
#define _MFW_GST_V4LSINK_H_

/*=============================================================================
                                CONSTANTS
=============================================================================*/
/* None. */

/*=============================================================================
                                ENUMS
=============================================================================*/

/* None. */

/*=============================================================================
                                MACROS
=============================================================================*/
G_BEGIN_DECLS
/* #defines don't like whitespacey bits */
#define MFW_GST_TYPE_V4LSINK (mfw_gst_v4lsink_get_type())
#define MFW_GST_V4LSINK(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj),MFW_GST_TYPE_V4LSINK,MFW_GST_V4LSINK_INFO_T))
#define MFW_GST_V4LSINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),MFW_GST_TYPE_V4LSINK,MFW_GST_V4LSINK_INFO_CLASS_T))
#define MFW_GST_IS_V4LSINK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),MFW_GST_TYPE_V4LSINK))
#define MFW_GST_IS_V4LSINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),MFW_GST_TYPE_V4LSINK))
#define MFW_GST_TYPE_V4LSINK_BUFFER (mfw_gst_v4lsink_buffer_get_type())
#define MFW_GST_IS_V4LSINK_BUFFER(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MFW_GST_TYPE_V4LSINK_BUFFER))
#define MFW_GST_V4LSINK_BUFFER(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST ((obj), MFW_GST_TYPE_V4LSINK_BUFFER, MFWGstV4LSinkBuffer))
#define MFW_GST_V4LSINK_BUFFER_GET_CLASS(obj)  \
    (G_TYPE_INSTANCE_GET_CLASS ((obj), MFW_GST_TYPE_V4LSINK_BUFFER, MFWGstV4LSinkBufferClass))




/*=============================================================================
                  STRUCTURES AND OTHER TYPEDEFS
=============================================================================*/
    typedef struct MFW_GST_V4LSINK_INFO_S {

    GstVideoSink videosink;
    GMutex *lock;//lock for v4ldevice operation
    gint framerate_n;
    gint framerate_d;
    gboolean full_screen;
    gboolean init;
    guint fourcc;		        /* our fourcc from the caps            */
#ifdef ENABLE_TVOUT
    /*For TV-Out & change para on-the-fly */
    gboolean tv_out;
    gint tv_mode;
    gint fd_tvout;
#endif 
    gboolean setpara;
    gint width;
    gint height;		        /* the size of the incoming YUV stream */
    gint disp_height;		    /* resize display height */
    gint disp_width;		    /* resize display width */
    gint axis_top;		        /* diplay top co-ordinate */
    gint axis_left;		        /* diplay left co-ordinate */
    gint rotate;		        /* display rotate angle */
    gint v4l_id;		        /* device ID */
    gint cr_left_bypixel;       /* crop left offset set by decoder in caps */
    gint cr_right_bypixel;      /* crop right offset set by decoder in caps */
    gint cr_top_bypixel;        /* crop top offset set by decoder in caps */
    gint cr_bottom_bypixel;     /* crop bottom offset set by decoder in caps */
    gint crop_left;             /* crop left offset set through propery */
    gint crop_right;            /* crop right offset set through propery */
    gint crop_top;              /* crop top offset set through propery */
    gint crop_bottom;           /* crop bottom offset set through propery */
    gint fullscreen_width;
    gint fullscreen_height;
    gint base_offset;
    gboolean buffer_alloc_called;
    GstCaps  *store_caps;
    GstElementClass *parent_class;
#ifdef ENABLE_DUMP
    gint cr_left_bypixel_orig;  /* original crop left offset set by decoder in caps */
    gint cr_right_bypixel_orig; /* original crop right offset set by decoder in caps */
    gint cr_top_bypixel_orig;   /* original crop top offset set by decoder in caps */
    gint cr_bottom_bypixel_orig;/* original crop bottom offset set by decoder in caps */
    gboolean enable_dump;
    gchar *dump_location;
    FILE *dumpfile;
    guint64 dump_length;
#endif

    gint  qbuff_count;		        /* buffer counter, increase when frame queued to v4l device */

    guint buffers_required;         /* hwbuffer limitation */
    gint  swbuffer_max;             /* swbuffer limitation */
    
    gint querybuf_index;            /* pre-allocated hw/sw buffer counter */
    gint  swbuffer_count;           /* pre-allocated sw buffer counter */

    GMutex *    pool_lock;          /* lock for buffer pool operation */
    GSList *    free_pool;          /* pool for free v4l buffer */
    
    void * reservedhwbuffer_list;   /* list to a hw v4l buffer reserved for render a swbuffer 
                                     */
    gint v4lqueued;                 /* counter for queued v4l buffer in device queue */
    void *  * all_buffer_pool; /* malloced array to store all hw/sw buffers */
    int  additional_buffer_depth;
    int frame_dropped;
    guint outformat;
} MFW_GST_V4LSINK_INFO_T;

typedef struct MFW_GST_V4LSINK_INFO_CLASS_S {
    GstVideoSinkClass parent_class;
} MFW_GST_V4LSINK_INFO_CLASS_T;

typedef struct _MFWGstV4LSinkBuffer MFWGstV4LSinkBuffer;
typedef struct _MFWGstV4LSinkBufferClass MFWGstV4LSinkBufferClass;

/*use buf state to control drop frame and avoid memory leak*/
/*ENGR33442:No picture out when doing state switch between FS and RS continuously */

typedef enum {
    BUF_STATE_ILLEGAL,  
    BUF_STATE_ALLOCATED,/* buffer occured by codec or pp */
    BUF_STATE_SHOWED,   /* buffer is successlly showed */
    BUF_STATE_SHOWING,  /* buffer is showing(in v4l queue) */
    BUF_STATE_IDLE,     /* buffer is idle, can be allocated to codec or pp */
    BUF_STATE_FREE,     /* buffer need to be freed, the acctually free precedure will happen when future unref */
} BUF_STATE;

struct _MFWGstV4LSinkBuffer {
    GstBuffer buffer;
    MFW_GST_V4LSINK_INFO_T *v4lsinkcontext;
    struct v4l2_buffer v4l_buf;

/*use buf state to control drop frame and avoid memory leak*/
 	/*ENGR33442:No picture out when doing state switch between FS and RS continuously */
    BUF_STATE bufstate;

};

struct v4l2_mxc_offset {
    guint32 u_offset;
    guint32 v_offset;
};


/*=============================================================================
                  GLOBAL VARIABLE DECLARATIONS
=============================================================================*/

/* None. */

/*=============================================================================
                  FUNCTION PROTOTYPES
=============================================================================*/

extern GType mfw_gst_v4lsink_get_type(void);

G_END_DECLS
/*===========================================================================*/
#endif				/* _MFW_GST_V4LSINK_H_ */
