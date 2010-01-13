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
 * Module Name:    mfw_gst_v4lsink.c
 *
 * Description:    Implementation of V4L Sink Plugin for Gstreamer
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


#include <gst/gst.h>
#include <gst/video/gstvideosink.h>
#include <linux/videodev.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <string.h>
#include <unistd.h>
#include "mfw_gst_utils.h"
#include "mfw_gst_v4lsink.h"
#include "gst-plugins-fsl_config.h"

#if defined(ENABLE_TVOUT) && defined (_MX27)
/*For TV-Out & change para on-the-fly*/
#include <errno.h>
#include <sys/time.h>
struct v4l2_output_dev 
{
    __u32 disp_num;             /* output device index, for TV is 2, for LCD is 3 */
    __u32 id_len;               /* string id length */
    __u8 id[16];                /* string id of deivce, e.g. TV "DISP3 TV" */
};
#define VIDIOC_PUT_OUTPUT       _IOW  ('V', 90, struct v4l2_output_dev)
#define VIDIOC_GET_OUTPUT       _IOW  ('V', 91, struct v4l2_output_dev)
#endif
/*=============================================================================
                            LOCAL CONSTANTS
=============================================================================*/
/* None */

/*=============================================================================
                LOCAL TYPEDEFS (STRUCTURES, UNIONS, ENUMS)
=============================================================================*/

enum {
    PROP_0,
    PROP_FULLSCREEN,		/* enable full screen image display */
    DISP_WIDTH,			/* display image width */
    DISP_HEIGHT,		/* display image height */
    AXIS_TOP,			/* image top axis offset */
    AXIS_LEFT,			/* image left axis offset */
    ROTATE,			/* image rotation value (0 - 7) */
    CROP_LEFT,			/* input image cropping in the left */
    CROP_RIGHT,			/* input image cropping in the right */
    CROP_TOP,			/* input image cropping in the top */
    CROP_BOTTOM,		/* input image cropping in the top */
    FULL_SCREEN_WIDTH,		/* full screen width of the display */
    FULL_SCREEN_HEIGHT,		/* full screen height of the display */
    BASE_OFFSET,
#ifdef ENABLE_TVOUT
    TV_OUT,
    TV_MODE,
#endif 
#ifdef ENABLE_DUMP
    DUMP_LOCATION,
#endif
    ADDITIONAL_BUFFER_DEPTH,
    SETPARA

};

/*=============================================================================
                              LOCAL MACROS
=============================================================================*/
/* used for debugging */


#define GST_CAT_DEFAULT mfw_gst_v4lsink_debug

#if defined(_MX37) || defined(_MX51)
#define MIN_BUFFER_NUM              2   /* minimal 2 is default for compability of non-updated codec like wmv */
#define MAX_BUFFER_NUM              10  /* this number is related to driver */
#define BUFFER_RESERVED_NUM         0   /* 0 addtional buffer need reserved for v4l queue in vpu based decoder */
#define MAX_V4L_ALLOW_SIZE_IN_MB    15  /* 15MB limitation */
#else
#if defined(_MX27)
#define MIN_BUFFER_NUM              2   /* minimal 2, 5 is default for compability of non-updated codec like wmv */
#define MAX_BUFFER_NUM              10  /* this number is related to driver */
#else
#define MIN_BUFFER_NUM              5   /* minimal 2, 5 is default for compability of non-updated codec like wmv */
#define MAX_BUFFER_NUM              10  /* this number is related to driver */
#endif
#define BUFFER_RESERVED_NUM         2   /* 2 addtional buffer need reserved for v4l queue */
#define MAX_V4L_ALLOW_SIZE_IN_MB    7   /* 7MB limitation */
#endif

#define BUFFER_NEW_RETRY_MAX        500
#define WAIT_ON_DQUEUE_FAIL_IN_MS   30000

#define MAX_V4L_ALLOW_SIZE_IN_BYTE  (MAX_V4L_ALLOW_SIZE_IN_MB*1024*1024) 


#define RESERVEDHWBUFFER_DEPTH    2  /* must less than MIN_BUFFER_NUM */

#define IS_RESERVEDHWBUFFER_FULL(v4linfo) \
    (((v4linfo)->swbuffer_max==0) \
    || (g_slist_length((v4linfo)->reservedhwbuffer_list)>=RESERVEDHWBUFFER_DEPTH))

#define PUSHRESERVEDHWBUFFER(v4linfo, buffer) \
    if (buffer){\
        ((v4linfo)->reservedhwbuffer_list)=g_slist_append(((v4linfo)->reservedhwbuffer_list),\
                                                   (buffer));\
    }

#define POPRESERVEDHWBUFFER(v4linfo, buffer) \
    if (((v4linfo)->reservedhwbuffer_list)==NULL){\
        (buffer) = NULL;\
    }else{\
        GSList * tmp;\
        tmp = ((v4linfo)->reservedhwbuffer_list);\
        buffer = tmp->data;\
        ((v4linfo)->reservedhwbuffer_list) = \
            g_slist_delete_link(((v4linfo)->reservedhwbuffer_list), tmp);\
    }
        
    
    


#define DQUEUE_MAX_LOOP		200
#define NEXTDQ_WAIT_MSEC	30

#ifdef ENABLE_TVOUT
/*For TV-Out & change para on-the-fly*/
#include <errno.h>
#include <sys/time.h>

#define NTSC    0
#define PAL     1
#define NV_MODE 2
#endif


/*=============================================================================
                             STATIC VARIABLES
=============================================================================*/

static GstElementDetails mfw_gst_v4lsink_details =
GST_ELEMENT_DETAILS("Freescale: v4l_sink",
		    "Sink/Video",
		    "Video rendering device plugin used to display"
		    "YUV 4:2:0 data with the support to crop the input image "
		    "used in the direct rendering of Padded YUV data",
		    "i.MX series");

/*=============================================================================
                             GLOBAL VARIABLES
=============================================================================*/
/* None */

/*=============================================================================
                        LOCAL FUNCTION PROTOTYPES
=============================================================================*/

GST_DEBUG_CATEGORY_STATIC(mfw_gst_v4lsink_debug);
static void mfw_gst_v4lsink_base_init(gpointer);
static void mfw_gst_v4lsink_class_init(MFW_GST_V4LSINK_INFO_CLASS_T *);
static void mfw_gst_v4lsink_init(MFW_GST_V4LSINK_INFO_T *,
                                 MFW_GST_V4LSINK_INFO_CLASS_T *);

static void mfw_gst_v4lsink_get_property(GObject *, 
                                         guint, GValue *,
                                         GParamSpec *);
static void mfw_gst_v4lsink_set_property(GObject *, 
                                         guint, const GValue *,
                                         GParamSpec *);

static GstStateChangeReturn mfw_gst_v4lsink_change_state
    (GstElement *, GstStateChange);

static gboolean mfw_gst_v4lsink_setcaps(GstBaseSink *, GstCaps *);

static GstFlowReturn mfw_gst_v4lsink_show_frame
                            (GstBaseSink *, GstBuffer *);

static gboolean mfw_gst_v4lsink_output_init(MFW_GST_V4LSINK_INFO_T *,
                                            guint, guint, guint);
static gboolean mfw_gst_v4lsink_output_setup(struct v4l2_format *,
                                             MFW_GST_V4LSINK_INFO_T *);
/* static MFWGstV4LSinkBuffer* mfw_gst_v4lsink_hwbuffer_new(MFW_GST_V4LSINK_INFO_T *); */

static GstFlowReturn mfw_gst_v4lsink_buffer_alloc(GstBaseSink * bsink, 
                                                  guint64 offset,
                                                  guint size, GstCaps * caps,
                                                  GstBuffer ** buf);


/*=============================================================================
                            LOCAL FUNCTIONS
=============================================================================*/

#ifdef ENABLE_DUMP
static gboolean dumpfile_open (MFW_GST_V4LSINK_INFO_T * v4l_sink_info)
{
  /* open the file */
  if (v4l_sink_info->dump_location == NULL || v4l_sink_info->dump_location[0] == '\0')
    goto no_dumpfilename;

  v4l_sink_info->dumpfile = fopen (v4l_sink_info->dump_location, "wb");
  if (v4l_sink_info->dumpfile == NULL)
    goto open_failed;

  v4l_sink_info->dump_length = 0;

  GST_DEBUG_OBJECT (v4l_sink_info, "opened file %s", v4l_sink_info->dump_location);

  return TRUE;

  /* ERRORS */
no_dumpfilename:
  {
    GST_ERROR ("No file name specified for writing.");
    return FALSE;
  }
open_failed:
  {
    GST_ERROR (("Could not open file \"%s\" for writing."), v4l_sink_info->dump_location);
    return FALSE;
  }
}

static void dumpfile_close (MFW_GST_V4LSINK_INFO_T * v4l_sink_info)
{
  if (v4l_sink_info->dumpfile) {
    if (fclose (v4l_sink_info->dumpfile) != 0)
      goto close_failed;

    GST_DEBUG_OBJECT (v4l_sink_info, "closed file");
    v4l_sink_info->dumpfile = NULL;
  }
  return;

  /* ERRORS */
close_failed:
  {
    GST_ERROR (("Error closing file \"%s\"."), v4l_sink_info->dump_location);
    return;
  }
}

static gboolean dumpfile_set_location (MFW_GST_V4LSINK_INFO_T * v4l_sink_info, const gchar * location)
{
  if (v4l_sink_info->dumpfile)
    goto was_open;

  g_free (v4l_sink_info->dump_location);
  if (location != NULL) {
    v4l_sink_info->enable_dump = TRUE;
    v4l_sink_info->dump_location = g_strdup (location);
  } else {
    v4l_sink_info->enable_dump = FALSE;
    v4l_sink_info->dump_location = NULL;
  }

  return TRUE;

  /* ERRORS */
was_open:
  {
    g_warning ("Changing the `dump_location' property on v4lsink when "
        "a file is open not supported.");
    return FALSE;
  }
}

static gboolean dumpfile_write (MFW_GST_V4LSINK_INFO_T * v4l_sink_info, GstBuffer * buffer)
{
  guint64 cur_pos;
  guint size;

  size = GST_BUFFER_SIZE (buffer);

  cur_pos = v4l_sink_info->dump_length;

  GST_DEBUG_OBJECT (v4l_sink_info, "writing %u bytes at %" G_GUINT64_FORMAT,
      size, cur_pos);

  if (size > 0 && GST_BUFFER_DATA (buffer) != NULL)
  {
      if (v4l_sink_info->cr_left_bypixel != 0 || v4l_sink_info->cr_right_bypixel != 0
          || v4l_sink_info->cr_top_bypixel != 0 || v4l_sink_info->cr_bottom_bypixel != 0)
      {
          /* remove black edge */
          gint y;
          char *p;
          gint cr_left = v4l_sink_info->cr_left_bypixel_orig;
          gint cr_right = v4l_sink_info->cr_right_bypixel_orig;
          gint cr_top = v4l_sink_info->cr_top_bypixel_orig;
          gint cr_bottom = v4l_sink_info->cr_bottom_bypixel_orig;
          gint stride = v4l_sink_info->width + cr_left + cr_right;

          /* Y */
          for (y = cr_top; y < v4l_sink_info->height + cr_top; y++)
          {
              p = (char *) (GST_BUFFER_DATA(buffer)) + 
                  y * stride + cr_left;
              fwrite (p, 1, v4l_sink_info->width, v4l_sink_info->dumpfile);
              v4l_sink_info->dump_length += v4l_sink_info->width;
          }

          /* U */
          for (y = cr_top / 2; y < (v4l_sink_info->height + cr_top) / 2; y++)
          {
              p = (char *) (GST_BUFFER_DATA(buffer)) + 
                  stride * (v4l_sink_info->height + cr_top + cr_bottom) +
                  (y * stride + cr_left) / 2;
              fwrite (p, 1, v4l_sink_info->width / 2, v4l_sink_info->dumpfile);
              v4l_sink_info->dump_length += (v4l_sink_info->width / 2);
          }

          /* V */
          for (y = cr_top / 2; y < (v4l_sink_info->height + cr_top) / 2; y++)
          {
              p = (char *) (GST_BUFFER_DATA(buffer)) + 
                  stride * (v4l_sink_info->height + cr_top + cr_bottom) * 5 / 4 +
                  (y * stride + cr_left) / 2;
              fwrite (p, 1, v4l_sink_info->width / 2, v4l_sink_info->dumpfile);
              v4l_sink_info->dump_length += (v4l_sink_info->width / 2);
          }
      }
      else
      {
          if (fwrite (GST_BUFFER_DATA (buffer), size, 1, v4l_sink_info->dumpfile) != 1)
              goto handle_error;

          v4l_sink_info->dump_length += size;
      }
  }

  return TRUE;

handle_error:
  {
    switch (errno) {
      case ENOSPC:{
        GST_ELEMENT_ERROR (v4l_sink_info, RESOURCE, NO_SPACE_LEFT, (NULL), (NULL));
        break;
      }
      default:{
        GST_ELEMENT_ERROR (v4l_sink_info, RESOURCE, WRITE,
            (("Error while writing to file \"%s\"."), v4l_sink_info->dump_location),
            ("%s", g_strerror (errno)));
      }
    }
    return FALSE;
  }
}

#endif

#if defined(ENABLE_TVOUT) && defined (_MX27)
void tv_out_open(MFW_GST_V4LSINK_INFO_T * v4l_sink_info)
{

    struct v4l2_output_dev odev = {
        .disp_num = 2,
        .id_len = 11,
        .id = "DISP3 TVOUT"
    };

    v4l_sink_info->fd_tvout = open("/dev/fb/1", O_RDWR);
    if (v4l_sink_info->fd_tvout < 0) {
        g_print("Unable to open /dev/fb/1\n");
    }

    if (ioctl(v4l_sink_info->v4l_id, VIDIOC_PUT_OUTPUT, &odev) < 0)
        g_print("TV-OUT ioctl VIDIOC_PUT_OUTPUT failed!\n");

}

void tv_out_close(MFW_GST_V4LSINK_INFO_T * v4l_sink_info)
{
    if (v4l_sink_info->fd_tvout > 0) {
        struct v4l2_output_dev odev = {
            .disp_num = 2,
            .id_len = 11,
            .id = "DISP3 TVOUT"
        };

        if (ioctl(v4l_sink_info->v4l_id, VIDIOC_GET_OUTPUT, &odev) < 0)
            g_print("TV-OUT ioctl VIDIOC_GET_OUTPUT failed!\n");

        close(v4l_sink_info->fd_tvout);
        v4l_sink_info->fd_tvout = 0;
    }
}
#endif

static void
mfw_gst_v4lsink_buffer_finalize(MFWGstV4LSinkBuffer *v4lsink_buffer_released)
{
    MFW_GST_V4LSINK_INFO_T * v4lsink_info;
    
    /*use buf state to control drop frame */
    g_return_if_fail(v4lsink_buffer_released != NULL);
    v4lsink_info = v4lsink_buffer_released->v4lsinkcontext;
    
    switch (v4lsink_buffer_released->bufstate) {
    case BUF_STATE_ALLOCATED:	
        GST_WARNING("Buffer %d maybe dropped.\n", v4lsink_buffer_released->v4l_buf.index);
        v4lsink_info->frame_dropped++;
        if (!(v4lsink_info->frame_dropped & 0x3f)){
            g_print("%d dropped while %d showed!\n", v4lsink_info->frame_dropped, v4lsink_info->qbuff_count);
        }
    case BUF_STATE_SHOWED:	     
        g_mutex_lock(v4lsink_info->pool_lock);
        
        v4lsink_buffer_released->bufstate = BUF_STATE_IDLE;
	    if (GST_BUFFER_FLAG_IS_SET(v4lsink_buffer_released, GST_BUFFER_FLAG_LAST)){
            /* hwbuffer, put on the head of free pool. */
            if (!IS_RESERVEDHWBUFFER_FULL(v4lsink_info)){
                PUSHRESERVEDHWBUFFER(v4lsink_info, v4lsink_buffer_released);
            }else{
                v4lsink_info->free_pool = g_slist_prepend(v4lsink_info->free_pool, v4lsink_buffer_released);
            }
        }else{
            /* swbuffer, put on the tail of free pool. */
            v4lsink_info->free_pool = g_slist_append(v4lsink_info->free_pool, v4lsink_buffer_released);
        }
        g_mutex_unlock(v4lsink_info->pool_lock);
	    gst_buffer_ref(GST_BUFFER_CAST(v4lsink_buffer_released));
	break;

    case BUF_STATE_FREE:	
        /*free it,do not need ref. */
        GST_WARNING("Buffer %d is freed.\n", v4lsink_buffer_released->v4l_buf.index);

        if (GST_BUFFER_DATA(v4lsink_buffer_released)){
            if (GST_BUFFER_FLAG_IS_SET(v4lsink_buffer_released, GST_BUFFER_FLAG_LAST)){
                munmap(GST_BUFFER_DATA(v4lsink_buffer_released),
	                v4lsink_buffer_released->v4l_buf.length);
            }else{
                g_free(GST_BUFFER_DATA(v4lsink_buffer_released));
            }
        }
        v4lsink_info->all_buffer_pool[v4lsink_buffer_released->v4l_buf.index] = NULL;
        
        GST_BUFFER_DATA(v4lsink_buffer_released) = NULL;
        
        v4lsink_info->querybuf_index--;
        if (v4lsink_info->querybuf_index==0){
            /* close the v4l driver */
            g_free(v4lsink_info->all_buffer_pool);
            v4lsink_info->all_buffer_pool = NULL;
            close(v4lsink_info->v4l_id);
            g_print("All buffer freed, close device.\n");
        }
	break;
    
    default:
        gst_buffer_ref(GST_BUFFER_CAST(v4lsink_buffer_released));
        g_print("Buffer %d:%p is unref with error state %d!\n",  v4lsink_buffer_released->v4l_buf.index, v4lsink_buffer_released, v4lsink_buffer_released->bufstate);
    }

    return;

}


/*=============================================================================
FUNCTION:           mfw_gst_v4lsink_buffer_init    
        
DESCRIPTION:        This funtion initialises the buffer class of the V4lsink
                    plug-in

ARGUMENTS PASSED:
        v4lsink_buffer -   pointer to V4Lsink buffer class
        g_class        -   global pointer

  
RETURN VALUE:       None
PRE-CONDITIONS:     None
POST-CONDITIONS:    None
IMPORTANT NOTES:    None
=============================================================================*/


static void
mfw_gst_v4lsink_buffer_init(MFWGstV4LSinkBuffer * v4lsink_buffer,
			    gpointer g_class)
{

    memset(&v4lsink_buffer->v4l_buf, 0, sizeof(struct v4l2_buffer));
    return;
}


/*=============================================================================
FUNCTION:           mfw_gst_v4lsink_buffer_class_init    
        
DESCRIPTION:        This funtion registers the  funtions used by the 
                    buffer class of the V4lsink plug-in

ARGUMENTS PASSED:
        g_class        -   class from which the mini objext is derived
        class_data     -   global class data
  
RETURN VALUE:       None
PRE-CONDITIONS:     None
POST-CONDITIONS:    None
IMPORTANT NOTES:    None
=============================================================================*/

static void
mfw_gst_v4lsink_buffer_class_init(gpointer g_class, gpointer class_data)
{
    GstMiniObjectClass *mini_object_class = GST_MINI_OBJECT_CLASS(g_class);
    mini_object_class->finalize = (GstMiniObjectFinalizeFunction)mfw_gst_v4lsink_buffer_finalize;
    return;

}

/*=============================================================================
FUNCTION:           mfw_gst_v4lsink_buffer_get_type    
        
DESCRIPTION:        This funtion registers the  buffer class 
                    on to the V4L sink plugin

ARGUMENTS PASSED:   None
  
RETURN VALUE:       return the registered buffer class
      
PRE-CONDITIONS:     None
POST-CONDITIONS:    None
IMPORTANT NOTES:    None
=============================================================================*/

GType mfw_gst_v4lsink_buffer_get_type(void)
{

    static GType _mfw_gst_v4lsink_buffer_type;

    if (G_UNLIKELY(_mfw_gst_v4lsink_buffer_type == 0)) {
	static const GTypeInfo v4lsink_buffer_info = {
	    sizeof(GstBufferClass),
	    NULL,
	    NULL,
	    mfw_gst_v4lsink_buffer_class_init,
	    NULL,
	    NULL,
	    sizeof(MFWGstV4LSinkBuffer),
	    0,
	    (GInstanceInitFunc) mfw_gst_v4lsink_buffer_init,
	    NULL
	};
	_mfw_gst_v4lsink_buffer_type =
	    g_type_register_static(GST_TYPE_BUFFER, "MFWGstV4LSinkBuffer",
				   &v4lsink_buffer_info, 0);
    }

    return _mfw_gst_v4lsink_buffer_type;
}

/*=============================================================================
FUNCTION:          mfw_gst_v4lsink_close    
        
DESCRIPTION:       This funtion clears the list of all the buffers maintained 
                   in the buffer pool. swirches of the video stream and closes
                   the V4L device driver.               

ARGUMENTS PASSED:   v4l_sink_info  - V4lsink plug-in context
  
RETURN VALUE:       None
PRE-CONDITIONS:     None
POST-CONDITIONS:    None
IMPORTANT NOTES:    None
=============================================================================*/
static void mfw_gst_v4lsink_close(MFW_GST_V4LSINK_INFO_T * v4l_sink_info)
{
    MFWGstV4LSinkBuffer *v4lsink_buffer = NULL;
    gint type;
    gint totalbuffernum = (v4l_sink_info->buffers_required+v4l_sink_info->swbuffer_max);
    gint i;
    
    // Exit if we have already closed before to avoid hangs
    if (v4l_sink_info->pool_lock == NULL)
    {
        return;
    }

    g_mutex_lock(v4l_sink_info->pool_lock);
    
    if (v4l_sink_info->init)
    {
        /* switch off the video stream */
        type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
        ioctl(v4l_sink_info->v4l_id, VIDIOC_STREAMOFF, &type);
    }

    if (v4l_sink_info->all_buffer_pool)
    {
        /* try to unref all buffer in pool */
        for (i=0;i<totalbuffernum;i++)
        {
            v4lsink_buffer = (MFWGstV4LSinkBuffer *)(v4l_sink_info->all_buffer_pool[i]);

            /* for buffers in IDLE and SHOWING state, no unref outside, explicit unref it */ 
            if (v4lsink_buffer)
            {
                GST_WARNING("try to free buffer %d at state %d\n", 
                v4lsink_buffer->v4l_buf.index, v4lsink_buffer->bufstate);
	
                if ((v4lsink_buffer->bufstate == BUF_STATE_IDLE) ||
                    (v4lsink_buffer->bufstate == BUF_STATE_SHOWING))
                {
                    if (v4lsink_buffer->bufstate == BUF_STATE_IDLE)
                    {
                        v4l_sink_info->free_pool = g_slist_remove(v4l_sink_info->free_pool, v4lsink_buffer);
                        v4l_sink_info->reservedhwbuffer_list = 
                            g_slist_remove(v4l_sink_info->reservedhwbuffer_list, v4lsink_buffer);
                    }
                    v4lsink_buffer->bufstate = BUF_STATE_FREE;
                    gst_buffer_unref(v4lsink_buffer);
                } else {
                    v4lsink_buffer->bufstate = BUF_STATE_FREE;
                }
            }
        }
    }

    
    g_mutex_unlock(v4l_sink_info->pool_lock);

    GST_WARNING("Close the v4l device.\n");


#ifdef ENABLE_DUMP
    if (v4l_sink_info->enable_dump)
        dumpfile_close(v4l_sink_info);
#endif
    g_mutex_free(v4l_sink_info->pool_lock);
    v4l_sink_info->pool_lock = NULL;

#ifdef ENABLE_TVOUT
    if(v4l_sink_info->tv_out == TRUE) 
#if defined(_MX31) || defined(_MX35)
    {
        /* switch to LCD mode when playing over*/
        FILE *pfb0_mode;
        gchar *mode = "U:480x640p-67\n";

        pfb0_mode = fopen("/sys/class/graphics/fb0/mode", "w");
        fwrite(mode, 1, strlen(mode), pfb0_mode);
        fflush(pfb0_mode);
        close(pfb0_mode);

    }
#endif
#if defined(_MX37) || defined(_MX51)
    {
        /* blank fb1 for tv*/
        FILE *pfb1_blank;
        gchar *blank = "4\n";

        pfb1_blank = fopen("/sys/class/graphics/fb1/blank", "w");
        fwrite(blank, 1, strlen(blank), pfb1_blank);
        fflush(pfb1_blank);
        close(pfb1_blank);
    }
#endif
    
#if defined (_MX27)
    {
        tv_out_close(v4l_sink_info);
    }
#endif
    
#endif
    v4l_sink_info->init=FALSE;

    return;
}



/*=============================================================================
FUNCTION:           mfw_gst_v4lsink_new_swbuffer    
        
DESCRIPTION:        This function allocate a new software display buffer. 

ARGUMENTS PASSED:
        v4l_sink_info -   pointer to MFW_GST_V4LSINK_INFO_T
  
RETURN VALUE:       returns the pointer to the software display buffer
      
PRE-CONDITIONS:     None
POST-CONDITIONS:    None
IMPORTANT NOTES:    None
=============================================================================*/
MFWGstV4LSinkBuffer *mfw_gst_v4lsink_new_swbuffer(MFW_GST_V4LSINK_INFO_T * v4l_sink_info)
{
    MFWGstV4LSinkBuffer *v4lsink_buffer;
    struct v4l2_buffer *v4lbuf;
    void * pdata;
    gint buf_size;
    
    g_return_val_if_fail(MFW_GST_IS_V4LSINK(v4l_sink_info), NULL);
    
    v4lsink_buffer = (MFWGstV4LSinkBuffer *)gst_mini_object_new(MFW_GST_TYPE_V4LSINK_BUFFER);

    v4lsink_buffer->bufstate = BUF_STATE_FREE;

    /* try to allocate data buffer for swbuffer */
    buf_size = (v4l_sink_info->width+v4l_sink_info->cr_left_bypixel+v4l_sink_info->cr_right_bypixel)*
                    (v4l_sink_info->width+v4l_sink_info->cr_left_bypixel+v4l_sink_info->cr_right_bypixel)*3/2; 

    pdata = g_malloc(buf_size);
    
    if (pdata==NULL){
        GST_ERROR("Can not allocate data buffer for swbuffer!\n");
        gst_buffer_unref(v4lsink_buffer);
        return NULL;
    }
    
    GST_BUFFER_DATA(v4lsink_buffer) = pdata;
    GST_BUFFER_OFFSET(v4lsink_buffer) = 0;

    v4lbuf = &v4lsink_buffer->v4l_buf;
    
    memset(v4lbuf, 0, sizeof(struct v4l2_buffer));
    v4lbuf->index = v4l_sink_info->querybuf_index;
   
    v4lsink_buffer->v4lsinkcontext = v4l_sink_info;

    v4lsink_buffer->bufstate = BUF_STATE_IDLE;
    /* register swbuffer to buffer pool */
    v4l_sink_info->all_buffer_pool[v4l_sink_info->querybuf_index++] = v4lsink_buffer;

    return v4lsink_buffer;
}

/*=============================================================================
FUNCTION:           mfw_gst_v4lsink_new_hwbuffer    
        
DESCRIPTION:        This function allocated a new hardware V4L  buffer. 

ARGUMENTS PASSED:
        v4l_sink_info -   pointer to MFW_GST_V4LSINK_INFO_T
  
RETURN VALUE:       returns the pointer to the hardware V4L buffer
      
RETURN VALUE:       None
PRE-CONDITIONS:     None
POST-CONDITIONS:    None
IMPORTANT NOTES:    None
=============================================================================*/
MFWGstV4LSinkBuffer *mfw_gst_v4lsink_new_hwbuffer(MFW_GST_V4LSINK_INFO_T* v4l_sink_info)
{
    MFWGstV4LSinkBuffer *v4lsink_buffer = NULL;
    guint image_width = 0;
    struct v4l2_buffer *v4lbuf;
    gint cr_left = 0, cr_right = 0, cr_top = 0;
    
    g_return_val_if_fail(MFW_GST_IS_V4LSINK(v4l_sink_info), NULL);
    
    v4lsink_buffer = (MFWGstV4LSinkBuffer *)gst_mini_object_new(MFW_GST_TYPE_V4LSINK_BUFFER);
    memset(&v4lsink_buffer->v4l_buf, 0, sizeof(struct v4l2_buffer));

    v4lbuf = &v4lsink_buffer->v4l_buf;
    v4lbuf->index = v4l_sink_info->querybuf_index;
    v4lbuf->type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    v4lbuf->memory = V4L2_MEMORY_MMAP;

    /* Buffer queried from the /V4L driver */
    
    if (ioctl(v4l_sink_info->v4l_id, VIDIOC_QUERYBUF, v4lbuf) < 0) {
    	g_print("VIDIOC_QUERYBUF failed %d\n", v4l_sink_info->querybuf_index);
    	v4lsink_buffer = NULL;
    	goto queryret;

    }

    /* Buffer queried for is mapped from the /V4L driver space */
    GST_BUFFER_OFFSET(v4lsink_buffer) = (size_t) v4lbuf->m.offset;
    GST_BUFFER_DATA(v4lsink_buffer) =
	mmap(NULL, v4lbuf->length,
	     PROT_READ | PROT_WRITE, MAP_SHARED,
	     v4l_sink_info->v4l_id, v4lbuf->m.offset);

    if (GST_BUFFER_DATA(v4lsink_buffer) == NULL) {
	g_print("v4l2_out test: mmap failed\n");
	v4lsink_buffer = NULL;
	goto queryret;
    }


    cr_left = v4l_sink_info->cr_left_bypixel;
    cr_right = v4l_sink_info->cr_right_bypixel;
    cr_top = v4l_sink_info->cr_top_bypixel;

    /* The input cropping is set here */
    if ((cr_left != 0) || (cr_right != 0) || (cr_top != 0)) {
	image_width = v4l_sink_info->width;
	v4lbuf->m.offset = v4lbuf->m.offset
	    + (cr_top * (image_width + cr_left + cr_right))
	    + cr_left;
    }
    v4lsink_buffer->bufstate = BUF_STATE_IDLE;
    GST_BUFFER_FLAG_SET(v4lsink_buffer, GST_BUFFER_FLAG_LAST);
    v4lsink_buffer->v4lsinkcontext = v4l_sink_info;
    v4l_sink_info->all_buffer_pool[v4l_sink_info->querybuf_index++] = v4lsink_buffer;
queryret:
    return v4lsink_buffer;
}



/*=============================================================================
FUNCTION:           mfw_gst_v4lsink_new_buffer    
        
DESCRIPTION:        This function gets a  v4l buffer, hardware or software. 

ARGUMENTS PASSED:
        v4l_sink_info -   pointer to MFW_GST_V4LSINK_INFO_T
    
RETURN VALUE:       returns the pointer to the v4l buffer
      
PRE-CONDITIONS:     None
POST-CONDITIONS:    None
IMPORTANT NOTES:    None
=============================================================================*/

MFWGstV4LSinkBuffer *mfw_gst_v4lsink_new_buffer(MFW_GST_V4LSINK_INFO_T* v4l_sink_info)
{
    MFWGstV4LSinkBuffer *v4lsink_buffer = NULL;

    {
        int loopcount = 0;
        GSList * tmp;
        int ret;
        struct v4l2_buffer v4l2buf;

        g_mutex_lock(v4l_sink_info->pool_lock);
	/* XXX: the code used to be :
	 *  if ((IS_RESERVEDHWBUFFER_FULL(v4l_sink_info)) && (tmp = v4l_sink_info->free_pool)){ 
	 *  which is really bad. since the v4l_sink_info is not check anyway. We
	 *  don't add a check here and assign tmp to v4l_sink_info->free_pool
	 *  directly */
	tmp = v4l_sink_info->free_pool;
        if ((IS_RESERVEDHWBUFFER_FULL(v4l_sink_info)) && (tmp)){
                v4lsink_buffer = (MFWGstV4LSinkBuffer *)tmp->data;
                    v4lsink_buffer->bufstate = BUF_STATE_ALLOCATED;
                v4l_sink_info->free_pool = g_slist_delete_link(v4l_sink_info->free_pool, tmp);
                g_mutex_unlock(v4l_sink_info->pool_lock);
                return v4lsink_buffer;
        }
        
        while ((loopcount++)<BUFFER_NEW_RETRY_MAX){
            ret = 0;
            memset(&v4l2buf, 0, sizeof(struct v4l2_buffer));
		    v4l2buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
		    v4l2buf.memory = V4L2_MEMORY_MMAP;

    		if ((v4l_sink_info->v4lqueued>1)
                &&(!((ret = ioctl(v4l_sink_info->v4l_id, VIDIOC_DQBUF, &v4l2buf))< 0))) {
                    MFWGstV4LSinkBuffer  * v4lsinkbuffer;
                    v4lsinkbuffer = (MFWGstV4LSinkBuffer  *)(v4l_sink_info->all_buffer_pool[v4l2buf.index]);
                    if ((v4lsinkbuffer) && (v4lsinkbuffer->bufstate == BUF_STATE_SHOWING)){
                    v4l_sink_info->v4lqueued --;
                    v4lsinkbuffer->bufstate = BUF_STATE_SHOWED;
                    g_mutex_unlock(v4l_sink_info->pool_lock);
                    gst_buffer_unref(GST_BUFFER_CAST(v4lsinkbuffer));
                    g_mutex_lock(v4l_sink_info->pool_lock);
                    }
            }
            tmp = v4l_sink_info->free_pool;

            if (tmp){
                v4lsink_buffer = (MFWGstV4LSinkBuffer *)tmp->data;
                v4lsink_buffer->bufstate = BUF_STATE_ALLOCATED;
                v4l_sink_info->free_pool = g_slist_delete_link(v4l_sink_info->free_pool, tmp);
                g_mutex_unlock(v4l_sink_info->pool_lock);
                return v4lsink_buffer;
            }
            if (v4l_sink_info->v4lqueued<2){//no hardware buffer
                if (v4l_sink_info->swbuffer_count<v4l_sink_info->swbuffer_max){
                    v4lsink_buffer = mfw_gst_v4lsink_new_swbuffer(v4l_sink_info);
                    v4lsink_buffer->bufstate = BUF_STATE_ALLOCATED;
                    v4l_sink_info->swbuffer_count++;
                    g_mutex_unlock(v4l_sink_info->pool_lock);

                    return v4lsink_buffer;
                }
                
            }
            if (ret<0){
                g_mutex_unlock(v4l_sink_info->pool_lock);
                usleep(WAIT_ON_DQUEUE_FAIL_IN_MS);
                g_mutex_lock(v4l_sink_info->pool_lock);
            }
            
        }
        g_print("try new buffer failed, ret %d %s queued %d\n", errno, strerror(errno), v4l_sink_info->v4lqueued);

        g_mutex_unlock(v4l_sink_info->pool_lock);
        return NULL;
    }
}

/*=============================================================================
FUNCTION:           mfw_gst_v4lsink_output_setup    
        
DESCRIPTION:        This function set up the display device format 

ARGUMENTS PASSED:
        fmt            -   pointer to format for the display device   
        v4l_sink_info  -   pointer to MFW_GST_V4LSINK_INFO_T    
  
RETURN VALUE:       TRUE/FALSE( sucess/failure)
      
PRE-CONDITIONS:     None
POST-CONDITIONS:    None
IMPORTANT NOTES:    None
=============================================================================*/

static gboolean
mfw_gst_v4lsink_output_setup(struct v4l2_format *fmt,
			                 MFW_GST_V4LSINK_INFO_T * v4l_sink_info)
{
    struct v4l2_requestbuffers buf_req;

	int ret;

    if ((ret=ioctl(v4l_sink_info->v4l_id, VIDIOC_S_FMT, fmt)) < 0) {
	g_print("set format failed %d\n",ret);
	return FALSE;
    }
    
    if (ioctl(v4l_sink_info->v4l_id, VIDIOC_G_FMT, fmt) < 0) {
	g_print("get format failed\n");
	return FALSE;
    }
 
#if 0//test code for sw copy render, also need set MIN_BUFFER_NUM 2
    v4l_sink_info->swbuffer_max = v4l_sink_info->buffers_required-2;
    v4l_sink_info->buffers_required = 2;

#endif
    
    while(v4l_sink_info->buffers_required>=MIN_BUFFER_NUM){
        
        memset(&buf_req, 0, sizeof(buf_req));
        buf_req.count = v4l_sink_info->buffers_required;
        buf_req.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
        buf_req.memory = V4L2_MEMORY_MMAP;

        
        if (ioctl(v4l_sink_info->v4l_id, VIDIOC_REQBUFS, &buf_req) >= 0) {
    	    GST_WARNING("%d hwbuffers sucessfully allocated.\n", v4l_sink_info->buffers_required);
    	    return TRUE;
        }
        if (v4l_sink_info->swbuffer_max == 0)
            v4l_sink_info->swbuffer_max = 2;
        v4l_sink_info->swbuffer_max++;
        v4l_sink_info->buffers_required--;
    }

    v4l_sink_info->buffers_required =
        v4l_sink_info->swbuffer_max = 0;
    
    return FALSE;
}


/*=============================================================================
FUNCTION:           mfw_gst_v4lsink_output_init    
        
DESCRIPTION:        This function initialise the display device with the specified parameters. 

ARGUMENTS PASSED:
        v4l_sink_info  -   pointer to MFW_GST_V4LSINK_INFO_T   
        inp_format     -   the display foramt
        disp_width     -   width to be displayed
        disp_height    -   height to be displayed
        
RETURN VALUE:       TRUE/FALSE (SUCCESS/FAIL)

PRE-CONDITIONS:     None
POST-CONDITIONS:    None
IMPORTANT NOTES:    None
=============================================================================*/

gboolean mfw_gst_v4lsink_output_init(MFW_GST_V4LSINK_INFO_T *v4l_sink_info, 
                                     guint inp_format,
                                     guint disp_width, guint disp_height)
{
    struct v4l2_control ctrl;
    struct v4l2_cropcap cropcap;
    struct v4l2_crop crop;
    struct v4l2_format fmt;
    gboolean retval = TRUE;
    gchar v4l_device1[100] = "/dev/video16";
    guint in_fmt = V4L2_PIX_FMT_YUV420;
    guint in_width = 0, display_width = 0;
    guint in_height = 0, display_height = 0;
    gint cr_left = 0;
    gint cr_top = 0;
    gint cr_right = 0;
    gint cr_bottom = 0;

    guint in_width_chroma = 0, in_height_chroma = 0;
    gint crop_left_chroma = 0;
    gint crop_right_chroma = 0;
    gint crop_top_chroma = 0;
    gint crop_bottom_chroma = 0;

    struct v4l2_mxc_offset off;

    if (v4l_sink_info->init == FALSE)
	v4l_sink_info->querybuf_index = 0;

    v4l_sink_info->qbuff_count = 0;
    v4l_sink_info->frame_dropped = 0;

    /* Read the variables passed by the user */
    display_width = disp_width;
    display_height = disp_height;
    in_fmt = inp_format;

    /*No need to open v4l device when it has opened--change para on-the-fly */
    if (v4l_sink_info->init == FALSE) {
        /* open the V4l device */
        if ((v4l_sink_info->v4l_id =
            open(v4l_device1, O_RDWR | O_NONBLOCK, 0)) < 0) {
            g_print("Unable to open %s\n", v4l_device1);
            retval = FALSE;
            goto err0;
        }
    }

#ifdef ENABLE_TVOUT
#if defined(_MX31) || defined(_MX35) 

    if (TRUE == v4l_sink_info->tv_out){
        gint out;
        gchar *mode;
        FILE *pfb0_mode;

        pfb0_mode = fopen("/sys/class/graphics/fb0/mode", "w");
        g_print("pfb0_mode : %x\n", pfb0_mode );
        if(v4l_sink_info->tv_mode == PAL) {
            mode = "U:640x480p-50\n";
            fwrite(mode, 1, strlen(mode), pfb0_mode);
        }
        else if(v4l_sink_info->tv_mode == NTSC) {
            mode = "U:640x480p-60\n";
            fwrite(mode, 1, strlen(mode), pfb0_mode);
        }
        else {
            g_print("Wrong TV mode.\n");
            close(pfb0_mode);
            goto err0;
        }
        fflush(pfb0_mode);
        close(pfb0_mode);

        out = 3;
        ioctl(v4l_sink_info->v4l_id, VIDIOC_S_OUTPUT, &out);
    }
    else {
        gint out = 3;
        gchar *mode;
        FILE *pfb0_mode;

        if((v4l_sink_info->tv_mode == PAL) || (v4l_sink_info->tv_mode == NTSC)) {
            v4l_sink_info->tv_mode = NV_MODE;
            pfb0_mode = fopen("/sys/class/graphics/fb0/mode", "w");
            mode = "U:480x640p-67\n";
            fwrite(mode, 1, strlen(mode), pfb0_mode);
            fflush(pfb0_mode);
            close(pfb0_mode);
        }
        ioctl(v4l_sink_info->v4l_id, VIDIOC_S_OUTPUT, &out);
    }
 #endif 

#if defined(_MX37) || defined(_MX51)
    if (TRUE == v4l_sink_info->tv_out){
        gint out;
        gchar *mode;
        FILE *pfb1_mode;

        pfb1_mode = fopen("/sys/class/graphics/fb1/mode", "w");
        if(v4l_sink_info->tv_mode == PAL) {
            mode = "U:720x576i-50\n";
            fwrite(mode, 1, strlen(mode), pfb1_mode);
        }
        else if(v4l_sink_info->tv_mode == NTSC) {
            mode = "U:720x480i-60\n";
            fwrite(mode, 1, strlen(mode), pfb1_mode);
        }
        else {
            g_print("Wrong TV mode.\n");
            close(pfb1_mode);
            goto err0;
        }
        fflush(pfb1_mode);
        close(pfb1_mode);

        out = 5;
        ioctl(v4l_sink_info->v4l_id, VIDIOC_S_OUTPUT, &out);
    }
    else {
        gint out = 3;
        gchar *blank = "4\n";
        FILE *pfb1_blank;

        pfb1_blank = fopen("/sys/class/graphics/fb1/blank", "w");
        fwrite(blank, 1, strlen(blank), pfb1_blank);
        fflush(pfb1_blank);
        close(pfb1_blank);

        v4l_sink_info->tv_mode = NV_MODE;
        ioctl(v4l_sink_info->v4l_id, VIDIOC_S_OUTPUT, &out);
    }
#endif
    
#if  defined(_MX27)
    if (TRUE == v4l_sink_info->tv_out)
                tv_out_open(v4l_sink_info);
    else
                tv_out_close(v4l_sink_info);
#endif    
#endif

#ifdef ENABLE_DUMP
    if (TRUE == v4l_sink_info->enable_dump)
        dumpfile_open(v4l_sink_info);
#endif
    memset(&cropcap, 0, sizeof(cropcap));
    cropcap.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    
    if (ioctl(v4l_sink_info->v4l_id, VIDIOC_CROPCAP, &cropcap) < 0) {
        g_print("get crop capability failed\n");
        retval = FALSE;
        goto err0;
    }

    //g_print (" cropcap bound left=%d, top=%d, width=%d height=%d\n",cropcap.bounds.left,cropcap.bounds.top, cropcap.bounds.width, cropcap.bounds.height);
    //g_print (" cropcap defrect left=%d, top=%d, width=%d height=%d\n",cropcap.defrect.left,cropcap.defrect.top, cropcap.defrect.width, cropcap.defrect.height);
    //g_print (" cropcap pixelaspect num=%d, den=%d\n",cropcap.pixelaspect.numerator,cropcap.pixelaspect.denominator);

    /* set the image rectangle of the display by 
       setting the appropriate parameters */
    crop.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    if (v4l_sink_info->full_screen) {
        crop.c.width = v4l_sink_info->fullscreen_width;
        crop.c.height = v4l_sink_info->fullscreen_height;
        crop.c.top = 0;
        crop.c.left = 0;
    } else {
        crop.c.width = display_width;
        crop.c.height = display_height;
        crop.c.top = v4l_sink_info->axis_top;
        crop.c.left = v4l_sink_info->axis_left;
    }
    if (ioctl(v4l_sink_info->v4l_id, VIDIOC_S_CROP, &crop) < 0) {
        g_print("set crop failed\n");
        retval = FALSE;
        goto err0;
    }

    /* Set the rotation */
    /* Base offset removed as it is changed in the new BSP */
    ctrl.id = V4L2_CID_PRIVATE_BASE; //+ v4l_sink_info->base_offset;
    ctrl.value = v4l_sink_info->rotate;
    if (ioctl(v4l_sink_info->v4l_id, VIDIOC_S_CTRL, &ctrl) < 0) {
        g_print("set ctrl failed\n");
        retval = FALSE;
        goto err0;
    }

    /*No need to set input fmt and req buffer again when change para on-the-fly */
    if (v4l_sink_info->init == FALSE) 
    {
        /* set the input cropping parameters */
        in_width = v4l_sink_info->width;
        in_height = v4l_sink_info->height;
        memset(&fmt, 0, sizeof(fmt));
        fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
        fmt.fmt.pix.width = in_width;
        fmt.fmt.pix.height = in_height;
        fmt.fmt.pix.pixelformat = in_fmt;

        cr_left = v4l_sink_info->cr_left_bypixel;
        cr_top = v4l_sink_info->cr_top_bypixel;
        cr_right = v4l_sink_info->cr_right_bypixel;
        cr_bottom = v4l_sink_info->cr_bottom_bypixel;

        in_width_chroma = in_width / 2;
        in_height_chroma = in_height / 2;

        crop_left_chroma = cr_left / 2;
        crop_right_chroma = cr_right / 2;
        crop_top_chroma = cr_top / 2;
        crop_bottom_chroma = cr_bottom / 2;

        if ((cr_left!=0) || (cr_top!=0) || (cr_right!=0) || (cr_bottom!=0)) 
        {
            off.u_offset =
                ((cr_left + cr_right + in_width) * (in_height + cr_bottom)) - cr_left 
                + (crop_top_chroma * (in_width_chroma + crop_left_chroma + crop_right_chroma)) 
                + crop_left_chroma;
            off.v_offset = off.u_offset +
                (crop_left_chroma + crop_right_chroma + in_width_chroma)
                * ( in_height_chroma  + crop_bottom_chroma + crop_top_chroma);

            fmt.fmt.pix.bytesperline = in_width + cr_left + cr_right;
            fmt.fmt.pix.priv = (guint32) & off;
            fmt.fmt.pix.sizeimage = (in_width + cr_left + cr_right)
                * (in_height + cr_top + cr_bottom) * 3 / 2;
        } else {
            fmt.fmt.pix.bytesperline = in_width;
            fmt.fmt.pix.priv = 0;
            fmt.fmt.pix.sizeimage = 0;
        }

        retval = mfw_gst_v4lsink_output_setup(&fmt, v4l_sink_info);
        if (retval == FALSE) 
        {
            GST_ERROR("Error in mfw_gst_v4lsink_output_setup\n");
        }
    }
 err0:
    return retval;
}

/*=============================================================================
FUNCTION:           mfw_gst_v4lsink_buffer_alloc   
        
DESCRIPTION:        This function initailise the v4l driver
                    and gets the new buffer for display             

ARGUMENTS PASSED:  
          bsink :   pointer to GstBaseSink
		  buf   :   pointer to new GstBuffer
		  size  :   size of the new buffer
          offset:   buffer offset
		  caps  :   pad capability
        
RETURN VALUE:       GST_FLOW_OK/GST_FLOW_ERROR
      
PRE-CONDITIONS:     None
POST-CONDITIONS:    None
IMPORTANT NOTES:    None
=============================================================================*/
static GstFlowReturn
mfw_gst_v4lsink_buffer_alloc(GstBaseSink * bsink, guint64 offset,
				     guint size, GstCaps * caps,
				     GstBuffer ** buf)
{
    GstBuffer *newbuf = NULL;
    MFW_GST_V4LSINK_INFO_T *v4l_sink_info = MFW_GST_V4LSINK(bsink);
    MFWGstV4LSinkBuffer *v4lsink_buffer = NULL;
    GstStructure *s = NULL;
    
    gint frame_buffer_size;
    guint max_frames;
    gint hwbuffernumforcodec;
    
    gboolean result = FALSE;
    gint aspectratio_n = 0, aspectratio_d = 0;
    v4l_sink_info->buffer_alloc_called = TRUE;
    if (G_UNLIKELY(v4l_sink_info->init == FALSE)) 
    {
        caps = gst_caps_make_writable(caps);
        s = gst_caps_get_structure(caps, 0);

        v4l_sink_info->buffers_required = 0;
    
        gst_structure_get_int(s, "width", &v4l_sink_info->width);
        gst_structure_get_int(s, "height", &v4l_sink_info->height);
        gst_structure_get_fraction(s, "pixel-aspect-ratio", &aspectratio_n,
				   &aspectratio_d);
        gst_structure_get_int(s, "crop-left-by-pixel",
			      &v4l_sink_info->cr_left_bypixel);
        gst_structure_get_int(s, "crop-top-by-pixel",
			      &v4l_sink_info->cr_top_bypixel);
        gst_structure_get_int(s, "crop-right-by-pixel",
			      &v4l_sink_info->cr_right_bypixel);
        gst_structure_get_int(s, "crop-bottom-by-pixel",
			      &v4l_sink_info->cr_bottom_bypixel);
        gst_structure_get_uint(s, "num-buffers-required",
			      &v4l_sink_info->buffers_required);

        gst_structure_get_fourcc(s, "format",
			     &v4l_sink_info->fourcc);

#ifndef _MX27
        if (v4l_sink_info->fourcc==GST_STR_FOURCC("NV12")){
            g_print("set to nv12 mode\n");
            v4l_sink_info->outformat = V4L2_PIX_FMT_NV12;
        } 
#else
        v4l_sink_info->outformat = V4L2_PIX_FMT_YUV420;
#endif

    
#ifdef ENABLE_DUMP
        v4l_sink_info->cr_left_bypixel_orig = v4l_sink_info->cr_left_bypixel;
        v4l_sink_info->cr_right_bypixel_orig = v4l_sink_info->cr_right_bypixel;
        v4l_sink_info->cr_top_bypixel_orig = v4l_sink_info->cr_top_bypixel;
        v4l_sink_info->cr_bottom_bypixel_orig = v4l_sink_info->cr_bottom_bypixel;
#endif

        GST_DEBUG("crop_left_bypixel=%d\n",v4l_sink_info->cr_left_bypixel);
        GST_DEBUG("crop_top_by_pixel=%d\n",v4l_sink_info->cr_top_bypixel);
        GST_DEBUG("crop_right_bypixel=%d\n",v4l_sink_info->cr_right_bypixel);
        GST_DEBUG("crop_bottom_by_pixel=%d\n",v4l_sink_info->cr_bottom_bypixel);

        GST_DEBUG("aspectratio_n=%d\n", aspectratio_n);
        GST_DEBUG("aspectratio_d=%d\n", aspectratio_d);
        GST_DEBUG("\n Decoded Width = %d, Decoded Height = %d\n",
		      v4l_sink_info->width, v4l_sink_info->height);

        g_print("Decoder maximal reserved %d buffers.\n", 
                v4l_sink_info->buffers_required);

        v4l_sink_info->buffers_required += BUFFER_RESERVED_NUM;
    
        if (v4l_sink_info->buffers_required < MIN_BUFFER_NUM) 
        {
	        v4l_sink_info->buffers_required = MIN_BUFFER_NUM;
        }

        frame_buffer_size = (v4l_sink_info->width* v4l_sink_info->height)*3/2;
        max_frames = MAX_V4L_ALLOW_SIZE_IN_BYTE/frame_buffer_size;
    
        if (v4l_sink_info->buffers_required > max_frames)
        {
            v4l_sink_info->swbuffer_max = 2;
            v4l_sink_info->swbuffer_max += (v4l_sink_info->buffers_required-max_frames);
            v4l_sink_info->buffers_required = max_frames;
        }
 
#ifndef _MX27
        if ((v4l_sink_info->cr_left_bypixel == 0) &&
            (v4l_sink_info->crop_left != 0)) 
#endif
        {
            v4l_sink_info->cr_left_bypixel = v4l_sink_info->crop_left;
            v4l_sink_info->cr_top_bypixel = v4l_sink_info->crop_top;
            v4l_sink_info->cr_right_bypixel = v4l_sink_info->crop_right;
            v4l_sink_info->cr_bottom_bypixel = v4l_sink_info->crop_bottom;
        }

        v4l_sink_info->width = v4l_sink_info->width -
            v4l_sink_info->cr_left_bypixel - 
            v4l_sink_info->cr_right_bypixel;

        v4l_sink_info->height = v4l_sink_info->height -
            v4l_sink_info->cr_top_bypixel - 
            v4l_sink_info->cr_bottom_bypixel;

        /* if the aspect ratio is set by the decoder compute the 
            display size and the pixet offsets for the display origin
            so that the display is resized to the given aspect ratio 
            and it is displayed in the centre */

        if (aspectratio_d != 0 && aspectratio_n != 0) 
        {
            if (v4l_sink_info->disp_height == 0) 
            {    
                v4l_sink_info->disp_width =
                    ((gfloat) aspectratio_n / aspectratio_d)
		             * v4l_sink_info->width;
                v4l_sink_info->disp_height =
		            ((gfloat) aspectratio_n / aspectratio_d)
		            * v4l_sink_info->height;
            }

            if ((v4l_sink_info->axis_top == 0) && (v4l_sink_info->axis_left == 0)) 
            {
                if (v4l_sink_info->disp_height > v4l_sink_info->fullscreen_height) 
                {
                    v4l_sink_info->axis_top = 0;
                } else {
                    v4l_sink_info->axis_top =
                        (v4l_sink_info->fullscreen_height -
                        v4l_sink_info->disp_height) / 2;
                }

                if (v4l_sink_info->disp_width > v4l_sink_info->fullscreen_width) {
		            v4l_sink_info->axis_left = 0;
                } else {
                    v4l_sink_info->axis_left =
                        (v4l_sink_info->fullscreen_width -
                        v4l_sink_info->disp_width) / 2;
                }
            }
        }

        GST_DEBUG("\n display Width = %d, display Height = %d\n",
            v4l_sink_info->disp_width,
            v4l_sink_info->disp_height);

        GST_DEBUG("\n display origin offset top = %d, display origin offset left \
                = %d\n", v4l_sink_info->axis_top, v4l_sink_info->axis_left);
        g_mutex_lock(v4l_sink_info->pool_lock);

        /* initialize the V4l driver based on the parameters set by
	       the previous element */
        if (((v4l_sink_info->disp_width == 0) &&
             (v4l_sink_info->disp_height == 0))) 
        {
            result = mfw_gst_v4lsink_output_init(v4l_sink_info,
                                    v4l_sink_info->outformat, 
                                    v4l_sink_info->width,
                                    v4l_sink_info->height);
        } else {
            result = mfw_gst_v4lsink_output_init(v4l_sink_info,
                                    v4l_sink_info->outformat,
                                    v4l_sink_info->disp_width,
                                    v4l_sink_info->disp_height);
        }
    
        g_mutex_unlock(v4l_sink_info->pool_lock);

        if (result != TRUE) {
	        GST_ERROR("\nFailed to initalize the v4l driver\n");
	        mfw_gst_v4lsink_close(v4l_sink_info);
	        v4l_sink_info->init = FALSE;
	        return GST_FLOW_ERROR;
        }

        hwbuffernumforcodec = v4l_sink_info->buffers_required;
        if (v4l_sink_info->swbuffer_max == 0){
            hwbuffernumforcodec -= BUFFER_RESERVED_NUM;
        } else {
            hwbuffernumforcodec -= (BUFFER_RESERVED_NUM+RESERVEDHWBUFFER_DEPTH);
        }

        {
            GValue value = {0};
	    g_value_init (&value, G_TYPE_INT);
	    g_value_set_int (&value, hwbuffernumforcodec);
            gst_structure_set_value(s,  "num-buffers-required", &value);  
        }

        g_print("Actually buffer status:\n\thardware buffer : %d\n\tsoftware buffer : %d\n",
            v4l_sink_info->buffers_required, 
            v4l_sink_info->swbuffer_max);


        if (v4l_sink_info->all_buffer_pool){
            g_free(v4l_sink_info->all_buffer_pool);
            v4l_sink_info->all_buffer_pool = NULL;
        }
    
        v4l_sink_info->all_buffer_pool = g_malloc(sizeof(GstBuffer *)*
                            (v4l_sink_info->buffers_required+v4l_sink_info->swbuffer_max));

        if (v4l_sink_info->all_buffer_pool == NULL) {
	        GST_ERROR("\nFailed to allocate buffer pool container\n");
            return GST_FLOW_ERROR;
        }

        /* no software buffer at all, no reserved needed */
    
        v4l_sink_info->swbuffer_count = 0;
    
        memset(v4l_sink_info->all_buffer_pool, 0, (sizeof(GstBuffer *)*
                            (v4l_sink_info->buffers_required+v4l_sink_info->swbuffer_max)));
        v4l_sink_info->init = TRUE;
        {
            MFWGstV4LSinkBuffer * tmpbuffer;

            g_mutex_lock(v4l_sink_info->pool_lock);
        
            while(!IS_RESERVEDHWBUFFER_FULL(v4l_sink_info))
            {    
                tmpbuffer = mfw_gst_v4lsink_new_hwbuffer(v4l_sink_info);
                if (tmpbuffer){
    	            PUSHRESERVEDHWBUFFER(v4l_sink_info, tmpbuffer);
                }
            }

            while ((guint)v4l_sink_info->querybuf_index < v4l_sink_info->buffers_required) 
            {    
                tmpbuffer = mfw_gst_v4lsink_new_hwbuffer(v4l_sink_info);
                if (tmpbuffer){
                    v4l_sink_info->free_pool = g_slist_prepend(v4l_sink_info->free_pool, tmpbuffer);
                }
            }
        
            g_mutex_unlock(v4l_sink_info->pool_lock);
    
        }
    }

    /* get the V4L hardware buffer */
    v4lsink_buffer = mfw_gst_v4lsink_new_buffer(v4l_sink_info);
    if (v4lsink_buffer == NULL) {
        GST_ERROR("\n!!Could not allocate buffer from V4L Driver\n");
        *buf = NULL;
        return GST_FLOW_ERROR;
    } else {
        GST_BUFFER_SIZE(v4lsink_buffer) = size;
    	newbuf = GST_BUFFER_CAST(v4lsink_buffer);
    	gst_buffer_set_caps(newbuf, caps);

    	*buf = newbuf;
    	return GST_FLOW_OK;
    }
}

/*=============================================================================
FUNCTION:           mfw_gst_v4lsink_set_property   
        
DESCRIPTION:        This function is notified if application changes the 
                    values of a property.            

ARGUMENTS PASSED:
        object  -   pointer to GObject   
        prop_id -   id of element
        value   -   pointer to Gvalue
        pspec   -   pointer to GParamSpec
        
RETURN VALUE:       GST_FLOW_OK/GST_FLOW_ERROR
PRE-CONDITIONS:     None
POST-CONDITIONS:    None
IMPORTANT NOTES:    None
=============================================================================*/

static void
mfw_gst_v4lsink_set_property(GObject * object, guint prop_id,
			     const GValue * value, GParamSpec * pspec)
{

    MFW_GST_V4LSINK_INFO_T *v4l_sink_info = MFW_GST_V4LSINK(object);
    switch (prop_id) {
    case PROP_FULLSCREEN:
	v4l_sink_info->full_screen = g_value_get_boolean(value);
	GST_DEBUG("fullcreen = %d\n", v4l_sink_info->full_screen);
	break;
    case DISP_WIDTH:
	v4l_sink_info->disp_width = g_value_get_int(value);
	GST_DEBUG("width = %d\n", v4l_sink_info->disp_width);
	break;
    case DISP_HEIGHT:
	v4l_sink_info->disp_height = g_value_get_int(value);
	GST_DEBUG("height = %d\n", v4l_sink_info->disp_height);
	break;
    case AXIS_TOP:
	v4l_sink_info->axis_top = g_value_get_int(value);
	GST_DEBUG("axis_top = %d\n", v4l_sink_info->axis_top);
	break;
    case AXIS_LEFT:
	v4l_sink_info->axis_left = g_value_get_int(value);
	GST_DEBUG("axis_left = %d\n", v4l_sink_info->axis_left);
	break;
    case ROTATE:
	v4l_sink_info->rotate = g_value_get_int(value);
	GST_DEBUG("rotate = %d\n", v4l_sink_info->rotate);
	break;
    case CROP_LEFT:
	v4l_sink_info->crop_left = g_value_get_int(value);
	GST_DEBUG("crop_left = %d\n", v4l_sink_info->crop_left);
	break;

    case CROP_RIGHT:
	v4l_sink_info->crop_right = g_value_get_int(value);
	GST_DEBUG("crop_right = %d\n", v4l_sink_info->crop_right);
	break;

    case CROP_TOP:
	v4l_sink_info->crop_top = g_value_get_int(value);
	GST_DEBUG("crop_top = %d\n", v4l_sink_info->crop_top);
	break;

    case CROP_BOTTOM:
	v4l_sink_info->crop_bottom = g_value_get_int(value);
	GST_DEBUG("crop_bottom = %d\n", v4l_sink_info->crop_bottom);
	break;


    case FULL_SCREEN_WIDTH:
	v4l_sink_info->fullscreen_width = g_value_get_int(value);
	GST_DEBUG("fullscreen_width = %d\n", v4l_sink_info->fullscreen_width);
	break;

    case FULL_SCREEN_HEIGHT:
	v4l_sink_info->fullscreen_height = g_value_get_int(value);
	GST_DEBUG("fullscreen_height = %d\n", v4l_sink_info->fullscreen_height);
	break;

    case BASE_OFFSET:
	v4l_sink_info->base_offset = g_value_get_int(value);
	GST_DEBUG("base_offset = %d\n", v4l_sink_info->base_offset);
	break;
#ifdef ENABLE_TVOUT
    case TV_OUT:
	v4l_sink_info->tv_out = g_value_get_boolean(value);
	break;
    case TV_MODE:
	v4l_sink_info->tv_mode = g_value_get_int(value);
	break;
	/*It's an ugly code,consider how to realize it by event */
#endif
#ifdef ENABLE_DUMP
    case DUMP_LOCATION:
    dumpfile_set_location (v4l_sink_info, g_value_get_string (value));
    break;
#endif
    case SETPARA:
	v4l_sink_info->setpara = TRUE;
	break;
    
    case ADDITIONAL_BUFFER_DEPTH:
    v4l_sink_info->additional_buffer_depth = g_value_get_int(value);
    break;

    default:
	G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
	break;
    }

    return;
}

/*=============================================================================
FUNCTION:           mfw_gst_v4lsink_get_property    
        
DESCRIPTION:        This function is notified if application requests the 
                    values of a property.                  

ARGUMENTS PASSED:
        object  -   pointer to GObject   
        prop_id -   id of element
        value   -   pointer to Gvalue
        pspec   -   pointer to GParamSpec
  
RETURN VALUE:       None
PRE-CONDITIONS:     None
POST-CONDITIONS:    None
IMPORTANT NOTES:    None
=============================================================================*/

static void
mfw_gst_v4lsink_get_property(GObject * object, guint prop_id,
			     GValue * value, GParamSpec * pspec)
{
    MFW_GST_V4LSINK_INFO_T *v4l_sink_info = MFW_GST_V4LSINK(object);
    switch (prop_id) {
    case PROP_FULLSCREEN:
	g_value_set_boolean(value, v4l_sink_info->full_screen);
	break;
    case DISP_WIDTH:
	g_value_set_int(value, v4l_sink_info->disp_width);
	break;
    case DISP_HEIGHT:
	g_value_set_int(value, v4l_sink_info->disp_height);
	break;
    case AXIS_TOP:
	g_value_set_int(value, v4l_sink_info->axis_top);
	break;
    case AXIS_LEFT:
	g_value_set_int(value, v4l_sink_info->axis_left);
	break;
    case ROTATE:
	g_value_set_int(value, v4l_sink_info->rotate);
	break;
    case CROP_LEFT:
	g_value_set_int(value, v4l_sink_info->crop_left);
	break;
    case CROP_TOP:
	g_value_set_int(value, v4l_sink_info->crop_top);
	break;
    case CROP_RIGHT:
	g_value_set_int(value, v4l_sink_info->crop_right);
	break;
    case CROP_BOTTOM:
	g_value_set_int(value, v4l_sink_info->crop_bottom);
	break;

    case FULL_SCREEN_WIDTH:
	g_value_set_int(value, v4l_sink_info->fullscreen_width);
	break;
    case FULL_SCREEN_HEIGHT:
	g_value_set_int(value, v4l_sink_info->fullscreen_height);
	break;

    case BASE_OFFSET:
	g_value_set_int(value, v4l_sink_info->base_offset);
	break;

#ifdef ENABLE_TVOUT
    case TV_OUT:
	g_value_set_boolean(value, v4l_sink_info->tv_out);
	break;
    case TV_MODE:
	g_value_set_int(value, v4l_sink_info->tv_mode);
	break;
#endif
#ifdef ENABLE_DUMP
    case DUMP_LOCATION:
	g_value_set_string(value, v4l_sink_info->dump_location);
    break;
#endif
    case SETPARA:
	g_value_set_boolean(value, v4l_sink_info->setpara);
	break;
	
    case ADDITIONAL_BUFFER_DEPTH:
	g_value_set_int(value, v4l_sink_info->additional_buffer_depth);
	break;

    default:
	G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
	break;
    }

    return;
}

#define DEQUEUE_TIMES_IN_SHOW 16

/*=============================================================================
FUNCTION:           mfw_gst_v4lsink_show_frame   
        
DESCRIPTION:        Process data to display      

ARGUMENTS PASSED:
        pad -   pointer to GstPad;
        buf -   pointer to GstBuffer
        
RETURN VALUE:       GST_FLOW_OK/GST_FLOW_ERROR
PRE-CONDITIONS:     None
POST-CONDITIONS:    None
IMPORTANT NOTES:    None
=============================================================================*/
static GstFlowReturn mfw_gst_v4lsink_show_frame(GstBaseSink * basesink,
                                                GstBuffer * buf)
{
    MFW_GST_V4LSINK_INFO_T *mfw_gst_v4lsink = MFW_GST_V4LSINK(basesink);
    struct v4l2_buffer *v4l_buf=NULL;
    GstBuffer *outbuffer=NULL;
    gint type;

    guint8 i = 0;
    MFWGstV4LSinkBuffer *v4lsink_buffer = NULL;
    /* This is to enable the integration of the peer elements which do not 
    call the gst_pad_alloc_buffer() to allocate their output buffers */

    if(G_UNLIKELY(mfw_gst_v4lsink->buffer_alloc_called == FALSE))
    {
        mfw_gst_v4lsink_buffer_alloc(basesink,0,GST_BUFFER_SIZE(buf),
            mfw_gst_v4lsink->store_caps,&outbuffer);
        memcpy(GST_BUFFER_DATA(outbuffer),GST_BUFFER_DATA(buf),GST_BUFFER_SIZE(buf));
        //gst_buffer_unref(outbuffer);
        mfw_gst_v4lsink->buffer_alloc_called = FALSE;
    }
    else
    {
        outbuffer=buf;
    }

    if (G_UNLIKELY(mfw_gst_v4lsink->setpara == TRUE)) {
    	gint type;
    	gboolean result = FALSE;
        GST_WARNING("start setpara begin\n");
        g_mutex_lock(mfw_gst_v4lsink->pool_lock);
    	/* swicth off the video stream */
    	type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
        
    	ioctl(mfw_gst_v4lsink->v4l_id, VIDIOC_STREAMOFF, &type);
    	/* initialize the V4l driver based on the parameters set by
    	   the previous element */
    	if (((mfw_gst_v4lsink->disp_width == 0) &&
    	     (mfw_gst_v4lsink->disp_height == 0))) {
    	    result =
    		mfw_gst_v4lsink_output_init(mfw_gst_v4lsink,
    					    mfw_gst_v4lsink->outformat,
    					    mfw_gst_v4lsink->width,
    					    mfw_gst_v4lsink->height);
    	} else {
    	    result =
    		mfw_gst_v4lsink_output_init(mfw_gst_v4lsink,
    					    mfw_gst_v4lsink->outformat,
    					    mfw_gst_v4lsink->disp_width,
    					    mfw_gst_v4lsink->disp_height);
    	}
        if (result != TRUE) {
    	    GST_ERROR("\nFailed to initalize the v4l driver\n");
            g_mutex_unlock(mfw_gst_v4lsink->pool_lock);
    	    return GST_FLOW_ERROR;
    	}

        {
            MFWGstV4LSinkBuffer * v4lsinkbuffer;
            for (i=0;i<mfw_gst_v4lsink->buffers_required;i++){
                v4lsinkbuffer = mfw_gst_v4lsink->all_buffer_pool[i];
                if (v4lsinkbuffer){
                    if (v4lsinkbuffer->bufstate == BUF_STATE_SHOWING){
                        GST_WARNING("stream off, buffer %p state changed from SHOWING\n", v4lsinkbuffer);
                        v4lsinkbuffer->bufstate = BUF_STATE_SHOWED;
                        g_mutex_unlock(mfw_gst_v4lsink->pool_lock);
                        gst_buffer_unref(v4lsinkbuffer);
                        g_mutex_lock(mfw_gst_v4lsink->pool_lock);
                        mfw_gst_v4lsink->v4lqueued --;
                    }
                }
            }
        }
        g_mutex_unlock(mfw_gst_v4lsink->pool_lock);
        GST_WARNING("setpara end\n");
        
    	mfw_gst_v4lsink->setpara = FALSE;
    }

    g_mutex_lock(mfw_gst_v4lsink->pool_lock);
    
    if (G_UNLIKELY(!GST_BUFFER_FLAG_IS_SET(outbuffer, GST_BUFFER_FLAG_LAST))){//sw buffer
        
        POPRESERVEDHWBUFFER(mfw_gst_v4lsink, v4lsink_buffer);
        
        if (v4lsink_buffer){
            memcpy(GST_BUFFER_DATA(v4lsink_buffer), GST_BUFFER_DATA(outbuffer), 
                GST_BUFFER_SIZE(outbuffer));
            ((MFWGstV4LSinkBuffer *)outbuffer)->bufstate = BUF_STATE_SHOWED;
        }else{
            //try to dq once only
            struct v4l2_buffer v4l2buf;
            
            memset(&v4l2buf, 0, sizeof(struct v4l2_buffer));
		    v4l2buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
		    v4l2buf.memory = V4L2_MEMORY_MMAP;
            

    		if (mfw_gst_v4lsink->v4lqueued>1){
                if(!((ioctl(mfw_gst_v4lsink->v4l_id, VIDIOC_DQBUF, &v4l2buf))< 0)) {

                    MFWGstV4LSinkBuffer  * v4lsinkbuffer;
                    v4lsinkbuffer = (MFWGstV4LSinkBuffer  *)(mfw_gst_v4lsink->all_buffer_pool[v4l2buf.index]);
                    if ((v4lsinkbuffer) && (v4lsinkbuffer->bufstate == BUF_STATE_SHOWING)){
                        mfw_gst_v4lsink->v4lqueued --;
                        v4lsinkbuffer->bufstate = BUF_STATE_SHOWED;
                        g_mutex_unlock(mfw_gst_v4lsink->pool_lock);
                        gst_buffer_unref(GST_BUFFER_CAST(v4lsinkbuffer));
                        g_mutex_lock(mfw_gst_v4lsink->pool_lock);
                    }
                }
    		}

            POPRESERVEDHWBUFFER(mfw_gst_v4lsink, v4lsink_buffer);

            if (v4lsink_buffer){
                
                memcpy(GST_BUFFER_DATA(v4lsink_buffer), GST_BUFFER_DATA(outbuffer), 
                    GST_BUFFER_SIZE(outbuffer));
                ((MFWGstV4LSinkBuffer *)outbuffer)->bufstate = BUF_STATE_SHOWED;
            }else{
                g_mutex_unlock(mfw_gst_v4lsink->pool_lock);
                GST_WARNING("drop because no reserved hwbuffer%d\n", mfw_gst_v4lsink->v4lqueued);
                return GST_FLOW_OK;
            }
        }
    }else{
        v4lsink_buffer = (MFWGstV4LSinkBuffer *)outbuffer;
        if (mfw_gst_v4lsink->buffer_alloc_called==TRUE){
            gst_buffer_ref(GST_BUFFER_CAST(v4lsink_buffer));
        }
    }
    
    v4l_buf = &v4lsink_buffer->v4l_buf;
#ifdef ENABLE_DUMP
    if (mfw_gst_v4lsink->enable_dump){
        dumpfile_write (mfw_gst_v4lsink, v4lsink_buffer);
        v4lsink_buffer->bufstate = BUF_STATE_SHOWED;
        g_mutex_unlock(mfw_gst_v4lsink->pool_lock);
        gst_buffer_unref(GST_BUFFER_CAST(v4lsink_buffer));
        return GST_FLOW_OK;
        
    }
#endif
    {
        /*display immediately */

	    struct timeval queuetime;
	    gettimeofday(&queuetime, NULL);
	    v4l_buf->timestamp = queuetime;
    }

    /* queue the buffer to be displayed into the V4L queue */
    
    if (G_UNLIKELY(ioctl(mfw_gst_v4lsink->v4l_id, VIDIOC_QBUF, v4l_buf) < 0)) {
    	g_print("VIDIOC_QBUF failed\n");
        g_mutex_unlock(mfw_gst_v4lsink->pool_lock);
    	return GST_FLOW_ERROR;
    }
    
    mfw_gst_v4lsink->v4lqueued++;

    v4lsink_buffer->bufstate = BUF_STATE_SHOWING;

    /* Switch on the stream display as soon as there are more than 1 buffer 
       in the V4L queue */
    if (G_UNLIKELY(mfw_gst_v4lsink->qbuff_count == 1)) {
    	type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    	if (ioctl(mfw_gst_v4lsink->v4l_id, VIDIOC_STREAMON, &type) < 0) {
    	    g_print("Could not start stream\n");
            
        }
    }
    
    mfw_gst_v4lsink->qbuff_count++;

    if (G_LIKELY(GST_BUFFER_FLAG_IS_SET(outbuffer, GST_BUFFER_FLAG_LAST))) {
        /* for direct rendering, try dequeue, just try twice */
        gint cnt;
        for (cnt = 0; cnt < DEQUEUE_TIMES_IN_SHOW; cnt++) {
            gint ret = 0;
            struct v4l2_buffer v4l2buf;

            memset(&v4l2buf, 0, sizeof(struct v4l2_buffer));
            v4l2buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
            v4l2buf.memory = V4L2_MEMORY_MMAP;

            if (mfw_gst_v4lsink->v4lqueued <= 2)
                break;
            if (!((ret = ioctl(mfw_gst_v4lsink->v4l_id, VIDIOC_DQBUF, &v4l2buf))< 0)) {
                MFWGstV4LSinkBuffer  * v4lsinkbuffer;
                v4lsinkbuffer = (MFWGstV4LSinkBuffer  *)(mfw_gst_v4lsink->all_buffer_pool[v4l2buf.index]);
                if ((v4lsinkbuffer) && (v4lsinkbuffer->bufstate == BUF_STATE_SHOWING)){
                    mfw_gst_v4lsink->v4lqueued --;
                    v4lsinkbuffer->bufstate = BUF_STATE_SHOWED;
                    g_mutex_unlock(mfw_gst_v4lsink->pool_lock);
                    gst_buffer_unref(GST_BUFFER_CAST(v4lsinkbuffer));
                    g_mutex_lock(mfw_gst_v4lsink->pool_lock);
                }
                break;
            }

            if (cnt == (DEQUEUE_TIMES_IN_SHOW-1) ) {
                GST_WARNING("Can not dequeue buffer in show frame.\n");
                break;
            }

            if (ret<0){
                g_mutex_unlock(mfw_gst_v4lsink->pool_lock);
                usleep(WAIT_ON_DQUEUE_FAIL_IN_MS);
                g_mutex_lock(mfw_gst_v4lsink->pool_lock);
            }
        }
    }

    g_mutex_unlock(mfw_gst_v4lsink->pool_lock);


    return GST_FLOW_OK;
}

/*=============================================================================
FUNCTION:           mfw_gst_v4lsink_setcaps
         
DESCRIPTION:        This function does the capability negotiation between adjacent pad  

ARGUMENTS PASSED:    
        basesink    -   pointer to v4lsink
        vscapslist  -   pointer to GstCaps
          
RETURN VALUE:       TRUE or FALSE depending on capability is negotiated or not.
        
PRE-CONDITIONS:     None
POST-CONDITIONS:    None
IMPORTANT NOTES:    None
   	    
=============================================================================*/

static gboolean mfw_gst_v4lsink_setcaps(GstBaseSink * basesink,
                                        GstCaps * vscapslist)
{
    MFW_GST_V4LSINK_INFO_T *mfw_gst_v4lsink = MFW_GST_V4LSINK(basesink);
    GstStructure *structure = NULL;
    mfw_gst_v4lsink->store_caps = vscapslist;

    structure = gst_caps_get_structure(vscapslist, 0);
    gst_structure_get_fourcc(structure, "format",
			     &mfw_gst_v4lsink->fourcc);
    gst_structure_get_fraction(structure, "framerate",
			       &mfw_gst_v4lsink->framerate_n,
			       &mfw_gst_v4lsink->framerate_d);

{
    gint sfd_val = 0;
    gboolean ret;

    ret = gst_structure_get_int(structure,"sfd",&sfd_val);
    if (ret == TRUE) {
        GST_DEBUG("sfd = %d.\n",sfd_val);
        if (sfd_val == 1)
            basesink->abidata.ABI.max_lateness = -1;

    }else {
        GST_WARNING("no sfd field found in caps.\n");
    }

}
    g_print("Set max lateness = %lld.\n",basesink->abidata.ABI.max_lateness);
    

    if (mfw_gst_v4lsink->fourcc == GST_STR_FOURCC("I420")) {
	    
    }else if (mfw_gst_v4lsink->fourcc == GST_STR_FOURCC("NV12")){
        mfw_gst_v4lsink->outformat = V4L2_PIX_FMT_NV12;
    }else{
        GST_ERROR("\nWrong FOURCC Type for Display\n");
        return FALSE;
    }



    return TRUE;
}

/*=============================================================================
FUNCTION:           mfw_gst_V4Lsink_change_state   
        
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
=============================================================================*/
static GstStateChangeReturn mfw_gst_v4lsink_change_state(GstElement * element, 
                                                         GstStateChange transition) 
{
    MFW_GST_V4LSINK_INFO_T *v4l_sink_info = MFW_GST_V4LSINK(element);
    GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
    switch (transition) {
        case GST_STATE_CHANGE_NULL_TO_READY:
            v4l_sink_info->width = -1;
            v4l_sink_info->height = -1;
            v4l_sink_info->framerate_n = 0;
            v4l_sink_info->framerate_d = 1;
            v4l_sink_info->init = FALSE;
            v4l_sink_info->buffer_alloc_called=FALSE;
            v4l_sink_info->pool_lock = g_mutex_new();
            v4l_sink_info->free_pool = NULL;
            v4l_sink_info->reservedhwbuffer_list = NULL;
            v4l_sink_info->v4lqueued = 0;
            v4l_sink_info->swbuffer_count = 0;
            v4l_sink_info->frame_dropped = 0;
            v4l_sink_info->swbuffer_max = 0;
            v4l_sink_info->cr_left_bypixel = 0;
            v4l_sink_info->cr_right_bypixel = 0;
            v4l_sink_info->cr_top_bypixel = 0;
            v4l_sink_info->cr_bottom_bypixel = 0;

        break;
        case GST_STATE_CHANGE_READY_TO_PAUSED:
        break;
        case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
        break;
        default:
        break;
    }

    ret = GST_ELEMENT_CLASS(v4l_sink_info->parent_class)->
    change_state(element, transition);

    switch (transition) {
        case GST_STATE_CHANGE_PAUSED_TO_READY:
	    break;
        case GST_STATE_CHANGE_READY_TO_NULL:
        {
            mfw_gst_v4lsink_close(v4l_sink_info);
            v4l_sink_info->init = FALSE;
            break;
        }
        default:			/* do nothing */
        break;
    }
    return ret;
}

/*=============================================================================
FUNCTION:           mfw_gst_v4lsink_init   
        
DESCRIPTION:        Create the pad template that has been registered with the 
                    element class in the _base_init and do library table 
                    initialization      

ARGUMENTS PASSED:
        v4l_sink_info  -    pointer to v4lsink element structure      
  
RETURN VALUE:       NONE
PRE-CONDITIONS:     _base_init and _class_init are called 
POST-CONDITIONS:    None
IMPORTANT NOTES:    None
=============================================================================*/

static void mfw_gst_v4lsink_init(MFW_GST_V4LSINK_INFO_T * v4l_sink_info,
				                 MFW_GST_V4LSINK_INFO_CLASS_T * klass)
{
    v4l_sink_info->all_buffer_pool=NULL;
    v4l_sink_info->disp_height = 0;
    v4l_sink_info->disp_width = 0;
    v4l_sink_info->axis_top = 0;
    v4l_sink_info->axis_left = 0;
    v4l_sink_info->rotate = 0;
    v4l_sink_info->crop_left = 0;
    v4l_sink_info->crop_top = 0;
    v4l_sink_info->fullscreen_width = 240;
    v4l_sink_info->fullscreen_height = 320;
    v4l_sink_info->full_screen = FALSE;
    v4l_sink_info->base_offset = 0;
    v4l_sink_info->parent_class = g_type_class_peek_parent(klass);
#ifdef ENABLE_TVOUT
/*For TV-Out & para change on-the-fly*/
    v4l_sink_info->tv_out = FALSE;
    v4l_sink_info->tv_mode = NV_MODE;
#endif
#ifdef ENABLE_DUMP
    v4l_sink_info->enable_dump = FALSE;
    v4l_sink_info->dump_location = NULL;
    v4l_sink_info->dumpfile = NULL;
    v4l_sink_info->dump_length = 0;
    v4l_sink_info->cr_left_bypixel_orig = 0;
    v4l_sink_info->cr_right_bypixel_orig = 0;
    v4l_sink_info->cr_top_bypixel_orig = 0;
    v4l_sink_info->cr_bottom_bypixel_orig = 0;
#endif
    v4l_sink_info->setpara = FALSE;
    v4l_sink_info->outformat = V4L2_PIX_FMT_YUV420;

#define MFW_GST_V4LSINK_PLUGIN VERSION
    PRINT_PLUGIN_VERSION(MFW_GST_V4LSINK_PLUGIN);
    return;


}

/*=============================================================================
FUNCTION:           mfw_gst_v4lsink_class_init    
        
DESCRIPTION:        Initialise the class only once (specifying what signals,
                    arguments and virtual functions the class has and 
                    setting up global state)    

ARGUMENTS PASSED:
            klass   -   pointer to mp3decoder element class
  
RETURN VALUE:       None
PRE-CONDITIONS:     None
POST-CONDITIONS:    None
IMPORTANT NOTES:    None
=============================================================================*/
static void
mfw_gst_v4lsink_class_init(MFW_GST_V4LSINK_INFO_CLASS_T * klass)
{

    GObjectClass *gobject_class;
    GstElementClass *gstelement_class;
    GstBaseSinkClass *gstvs_class;



    gobject_class = (GObjectClass *) klass;
    gstelement_class = (GstElementClass *) klass;
    gstvs_class = (GstBaseSinkClass *) klass;



    gstelement_class->change_state =
	GST_DEBUG_FUNCPTR(mfw_gst_v4lsink_change_state);

    gobject_class->set_property = mfw_gst_v4lsink_set_property;

    gobject_class->get_property = mfw_gst_v4lsink_get_property;

    gstvs_class->set_caps = GST_DEBUG_FUNCPTR(mfw_gst_v4lsink_setcaps);
    gstvs_class->render = GST_DEBUG_FUNCPTR(mfw_gst_v4lsink_show_frame);
    gstvs_class->buffer_alloc = GST_DEBUG_FUNCPTR(mfw_gst_v4lsink_buffer_alloc);

    g_object_class_install_property(gobject_class, PROP_FULLSCREEN,
				    g_param_spec_boolean("fullscreen",
							 "Fullscreen",
							 "If true it will be Full screen",
							 FALSE,
							 G_PARAM_READWRITE));

    g_object_class_install_property(gobject_class, DISP_WIDTH,
				    g_param_spec_int("disp_width",
						     "Disp_Width",
						     "gets the width of the image to be displayed",
						     0, G_MAXINT, 0,
						     G_PARAM_READWRITE));

    g_object_class_install_property(gobject_class, DISP_HEIGHT,
				    g_param_spec_int("disp_height",
						     "Disp_Height",
						     "gets the height of the image to be displayed",
						     0, G_MAXINT, 0,
						     G_PARAM_READWRITE));

    g_object_class_install_property(gobject_class, AXIS_TOP,
				    g_param_spec_int("axis_top",
						     "axis-top",
						     "gets the top co-ordinate of the origin of display",
						     0, G_MAXINT, 0,
						     G_PARAM_READWRITE));

    g_object_class_install_property(gobject_class, AXIS_LEFT,
				    g_param_spec_int("axis_left",
						     "axis-left",
						     "gets the left co-ordinate of the origin of display",
						     0, G_MAXINT, 0,
						     G_PARAM_READWRITE));

    g_object_class_install_property(gobject_class, ROTATE,
				    g_param_spec_int("rotate", "Rotate",
						     "gets the angle at which display is to be rotated",
						     0, G_MAXINT, 0,
						     G_PARAM_READWRITE));

    g_object_class_install_property(gobject_class, CROP_LEFT,
				    g_param_spec_int("crop_left_by_pixel",
						     "crop-left-by-pixel",
						     "set the input image cropping in the left (width)",
						     0, G_MAXINT, 0,
						     G_PARAM_READWRITE));

    g_object_class_install_property(gobject_class, CROP_RIGHT,
				    g_param_spec_int("crop_right_by_pixel",
						     "crop-right-by-pixel",
						     "set the input image cropping in the right (width)",
						     0, G_MAXINT, 0,
						     G_PARAM_READWRITE));


    g_object_class_install_property(gobject_class, CROP_TOP,
				    g_param_spec_int("crop_top_by_pixel",
						     "crop-top-by-pixel",
						     "set the input image cropping in the top (height)",
						     0, G_MAXINT, 0,
						     G_PARAM_READWRITE));

    g_object_class_install_property(gobject_class, CROP_BOTTOM,
				    g_param_spec_int("crop_bottom_by_pixel",
				     "crop-bottom-by-pixel",
				     "set the input image cropping in the bottom (height)",
						     0, G_MAXINT, 0,
						     G_PARAM_READWRITE));


    g_object_class_install_property(gobject_class, FULL_SCREEN_WIDTH,
				    g_param_spec_int("fullscreen_width",
						     "fullscreen-width",
						     "gets the full screen width of the display",
						     0, G_MAXINT, 0,
						     G_PARAM_READWRITE));

    g_object_class_install_property(gobject_class, FULL_SCREEN_HEIGHT,
				    g_param_spec_int("fullscreen_height",
						     "fullscreen-height",
						     "gets the full screen height of the display",
						     0, G_MAXINT, 0,
						     G_PARAM_READWRITE));

    g_object_class_install_property(gobject_class, BASE_OFFSET,
				    g_param_spec_int("base-offset",
						     "base_offset",
						     "gets the base offet for the V4L Driver for a given BSP",
						     0, 1, 0,
						     G_PARAM_READWRITE));

/*For TV-Out & para change on-the-fly*/
    g_object_class_install_property(gobject_class, SETPARA,
				   g_param_spec_boolean ("setpara", "Setpara",
							 "set parameter of V4L2",
							 FALSE,
							 G_PARAM_READWRITE));
#ifdef ENABLE_TVOUT
    g_object_class_install_property(gobject_class, TV_OUT,
				   g_param_spec_boolean ("tv-out", "TV-OUT",
							 "set output to TV-OUT",
							 FALSE,
							 G_PARAM_READWRITE));
    g_object_class_install_property(gobject_class, TV_MODE,
				   g_param_spec_int ("tv-mode", "TV-MODE",
							 "set mode to TV-OUT",
							 0, G_MAXINT, 0,
							 G_PARAM_READWRITE));
#endif	
#ifdef ENABLE_DUMP
    g_object_class_install_property(gobject_class, DUMP_LOCATION,
				   g_param_spec_string ("dump_location", "Dump File Location",
							 "Location of the file to write cropped video YUV stream.",
							 NULL,
							 G_PARAM_READWRITE));
#endif	


    g_object_class_install_property(gobject_class, ADDITIONAL_BUFFER_DEPTH,
    				    g_param_spec_int("add-buffer-len",
    				     "addtional buffer length",
    				     "set addtional buffer length",
    						     0, G_MAXINT, 0,
    						     G_PARAM_READWRITE));
    return;

}


/*=============================================================================
FUNCTION:           mfw_gst_v4lsink_base_init   
        
DESCRIPTION:       v4l Sink element details are registered with the plugin during
                   _base_init ,This function will initialise the class and child 
                    class properties during each new child class creation       

ARGUMENTS PASSED:
            Klass   -   void pointer
  
RETURN VALUE:       None
PRE-CONDITIONS:     None
POST-CONDITIONS:    None
IMPORTANT NOTES:    None
=============================================================================*/
static void mfw_gst_v4lsink_base_init(gpointer g_class)
{
    GstElementClass *element_class = GST_ELEMENT_CLASS(g_class);
    GstCaps *capslist;
    GstPadTemplate *sink_template = NULL;
    guint i;
    guint32 formats[] = {
	GST_MAKE_FOURCC('I', '4', '2', '0'),
	GST_MAKE_FOURCC('Y', 'V', '1', '2'),
	GST_MAKE_FOURCC('Y', 'U', 'Y', '2'),
	GST_MAKE_FOURCC('N', 'V', '1', '2')
	
    };


    /* make a list of all available caps */
    capslist = gst_caps_new_empty();
    for (i = 0; i < G_N_ELEMENTS(formats); i++) {
	gst_caps_append_structure(capslist,
				  gst_structure_new("video/x-raw-yuv",
						    "format",
						    GST_TYPE_FOURCC,
						    formats[i], "width",
						    GST_TYPE_INT_RANGE, 1,
						    G_MAXINT, "height",
						    GST_TYPE_INT_RANGE, 1,
						    G_MAXINT, "framerate",
						    GST_TYPE_FRACTION_RANGE,
						    0, 1, 100, 1, NULL));
    }

    sink_template = gst_pad_template_new("sink",
					 GST_PAD_SINK, GST_PAD_ALWAYS,
					 capslist);

    gst_element_class_add_pad_template(element_class, sink_template);
    gst_element_class_set_details(element_class, &mfw_gst_v4lsink_details);

    GST_DEBUG_CATEGORY_INIT(mfw_gst_v4lsink_debug, "mfw_v4lsink", 0,
			    "V4L video sink element");
    return;

}

/*=============================================================================
FUNCTION:           mfw_gst_v4lsink_get_type    
        
DESCRIPTION:        Interfaces are initiated in this function.you can register one 
                    or more interfaces  after having registered the type itself.

ARGUMENTS PASSED:   None
  
RETURN VALUE:       A numerical value ,which represents the unique identifier 
                    of this element(v4lsink)

PRE-CONDITIONS:     None
POST-CONDITIONS:    None
IMPORTANT NOTES:    None
=============================================================================*/
GType mfw_gst_v4lsink_get_type(void)
{
    static GType mfwV4Lsink_type = 0;

    if (!mfwV4Lsink_type) {
	static const GTypeInfo mfwV4Lsink_info = {
	    sizeof(MFW_GST_V4LSINK_INFO_CLASS_T),
	    mfw_gst_v4lsink_base_init,
	    NULL,
	    (GClassInitFunc) mfw_gst_v4lsink_class_init,
	    NULL,
	    NULL,
	    sizeof(MFW_GST_V4LSINK_INFO_T),
	    0,
	    (GInstanceInitFunc) mfw_gst_v4lsink_init,
	};

	mfwV4Lsink_type = g_type_register_static(GST_TYPE_VIDEO_SINK,
						 "MFW_GST_V4LSINK_INFO_T",
						 &mfwV4Lsink_info, 0);
    }

    GST_DEBUG_CATEGORY_INIT(mfw_gst_v4lsink_debug, "mfw_v4lsink",
			    0, "V4L sink");

    mfw_gst_v4lsink_buffer_get_type();

    return mfwV4Lsink_type;
}



/*=============================================================================
FUNCTION:           plugin_init

DESCRIPTION:        Special function , which is called as soon as the plugin or 
                    element is loaded and information returned by this function 
                    will be cached in central registry

ARGUMENTS PASSED:
        plugin     -    pointer to container that contains features loaded 
                        from shared object module

RETURN VALUE:
        return TRUE or FALSE depending on whether it loaded initialized any 
        dependency correctly

PRE-CONDITIONS:     None
POST-CONDITIONS:    None
IMPORTANT NOTES:    None
=============================================================================*/
static gboolean plugin_init(GstPlugin * plugin)
{
    if (!gst_element_register(plugin, "mfw_v4lsink", GST_RANK_PRIMARY,
			      MFW_GST_TYPE_V4LSINK))
        return FALSE;

    return TRUE;
}

/*****************************************************************************/
/*    This is used to define the entry point and meta data of plugin         */
/*****************************************************************************/

GST_PLUGIN_DEFINE(GST_VERSION_MAJOR,	/* major version of Gstreamer */
		  GST_VERSION_MINOR,	/* minor version of Gstreamer    */
		  "mfw_v4lsink",	/* name of the plugin            */
		  "Video display plug in based on V4L2",	/* what plugin actually does     */
		  plugin_init,	/* first function to be called   */
		  VERSION,
		  GST_LICENSE_UNKNOWN,
		  "Freescale Semiconductor", "www.freescale.com ")
