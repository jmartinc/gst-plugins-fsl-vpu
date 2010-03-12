/*
 * Copyright (C) 2008 Freescale Semiconductor, Inc. All rights reserved.
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
 * Module Name:    mfw_gst_utils.h
 *
 * Description:    Head file of utilities for Gstreamer plugins.
 *
 * Portability:    This code is written for Linux OS and Gstreamer
 */  
 
/*
 * Changelog: 
 *
 * Mar, 10 2008 Sario HU<b01138@freescale.com>
 * - Initial version
 * - Add direct render related macros.
 *
 * Jun, 13 2008 Dexter JI<b01140@freescale.com>
 * - Add framedrop algorithm related macros.
 *
 * Aug, 26 2008 Sario HU<b01138@freescale.com>
 * - Add misc macros. Rename to mfw_gst_utils.h.
 */


#ifndef __MFW_GST_UTILS_H__
#define __MFW_GST_UTILS_H__

/*=============================================================================
                                 MACROS
=============================================================================*/

#define _STR(s)     #s
#define STR(s)      _STR(s)  

/* ANSI color print */
#define COLOR_RED       31
#define COLOR_GREEN     32
#define COLOR_YELLOW    33
#define COLOR_BLUE      34
#define COLOR_PURPLE    35
#define COLOR_CYAN      36

#define COLORFUL_STR(color, format, ...)\
    "\33[1;" STR(color) "m" format "\33[0m", __VA_ARGS__

#define YELLOW_STR(format,...)      COLORFUL_STR(COLOR_YELLOW,format, __VA_ARGS__)
#define RED_STR(format,...)         COLORFUL_STR(COLOR_RED,format, __VA_ARGS__)
#define GREEN_STR(format,...)       COLORFUL_STR(COLOR_GREEN,format, __VA_ARGS__)
#define BLUE_STR(format,...)        COLORFUL_STR(COLOR_BLUE,format,__VA_ARGS__)
#define PURPLE_STR(format,...)      COLORFUL_STR(COLOR_PURPLE,format,__VA_ARGS__)
#define CYAN_STR(format,...)        COLORFUL_STR(COLOR_CYAN,format,__VA_ARGS__)


/* version info print */
#define PRINT_CORE_VERSION(ver)\
    do{\
        g_print(YELLOW_STR("%s.\n",(ver)));\
    }while(0)
    
#define PRINT_PLUGIN_VERSION(ver)\
    do {\
        g_print(GREEN_STR("%s %s build on %s %s.\n", #ver,(ver),__DATE__,__TIME__));\
    }while(0)

#define FLAG_SET_BIT(flag, bit)\
    do {\
        (flag) |= (bit);\
    }while(0)

#define FLAG_CLEAR_BIT(flag, bit)\
    do {\
        (flag) &= (~(bit));\
    }while(0)    

#define FLAG_TEST_BIT(flag, bit)\
    ((flag) & (bit))


/* common resolution limitation by platform */
#if defined (_MX51)
// 1080p
#define MAX_RESOLUTION_WIDTH    1920
#define MAX_RESOLUTION_HEIGHT   1080
#define MIN_RESOLUTION_WIDTH    64 
#define MIN_RESOLUTION_HEIGHT   64 

#elif defined(_MX37)
// SVGA
#define MAX_RESOLUTION_WIDTH    800
#define MAX_RESOLUTION_HEIGHT   600
#define MIN_RESOLUTION_WIDTH    64 
#define MIN_RESOLUTION_HEIGHT   64 

#elif defined(_MX31)|| defined(_MX35) 
// D1
#define MAX_RESOLUTION_WIDTH    720
#define MAX_RESOLUTION_HEIGHT   576
#define MIN_RESOLUTION_WIDTH    64 
#define MIN_RESOLUTION_HEIGHT   64 

#else
#define MAX_RESOLUTION_WIDTH    720
#define MAX_RESOLUTION_HEIGHT   576
#define MIN_RESOLUTION_WIDTH    64 
#define MIN_RESOLUTION_HEIGHT   64 
#endif


/*=============================================================================
                     DIRECT RENDER RELATED MACROS
=============================================================================*/

#if (DIRECT_RENDER_VERSION==2)
/*Direct render v2, support get/release decoder interface only*/

#ifndef BM_FLOW
#define BM_FLOW(...)
#endif

#ifndef BM_TRACE_BUFFER
#define BM_TRACE_BUFFER(...)
#endif

typedef enum {
    BMDIRECT = 0,
	BMINDIRECT = 1
}BMMode;

#define BMFLAG (GST_BUFFER_FLAG_LAST>>1)

static BMMode bm_mode = BMDIRECT;
static GSList * bm_list = NULL;
static gint bm_buf_num = 0;

#define BM_CLEAN_LIST do{\
        while(bm_list){\
            if (GST_BUFFER_FLAG_IS_SET(bm_list->data, BMFLAG))\
                gst_buffer_unref(bm_list->data);\
            gst_buffer_unref(bm_list->data);\
            (bm_list) = g_slist_remove((bm_list), (bm_list)->data);\
		};\
	}while(0)

#define BM_INIT(rmdmode, decbufnum, rendbufnum) do{\
        BM_FLOW("BM_INIT\n", 0);\
        bm_buf_num = decbufnum;\
        BM_CLEAN_LIST;\
	}while(0)
        
#define BM_GET_BUFFER(tgtpad, size, pdata) do{\
        GstBuffer * buffer;\
		GstFlowReturn result;\
	    GstCaps *src_caps = NULL;\
		src_caps = GST_PAD_CAPS((tgtpad));\
		result = gst_pad_alloc_buffer_and_set_caps((tgtpad), 0,(size), src_caps,&buffer);\
		if (result==GST_FLOW_OK){\
		    GST_BUFFER_FLAG_SET(buffer, BMFLAG);\
			(pdata) = GST_BUFFER_DATA(buffer);\
			gst_buffer_ref(buffer);\
			bm_list=g_slist_append(bm_list, buffer);\
			BM_FLOW("BM_GET_BUFFERv2 %p:d%p\n", buffer, pdata);\
			BM_TRACE_BUFFER("codec request %p:d%p\n", buffer, pdata);\
			break;\
		}\
    	if (result!=GST_FLOW_OK){\
            (pdata)=NULL;\
            g_print("BM_GET_BUFFERv2 no buffer, %d in codec\n", g_slist_length(bm_list));\
    	}\
	}while(0)

#define BM_QUERY_HWADDR(pdata, hwbuffer) do{\
		GSList * tmp = (bm_list);\
		GstBuffer * buffer;\
		while(tmp){\
			buffer = (GstBuffer *)(tmp->data);\
			if (GST_BUFFER_DATA(buffer)==(pdata)){\
                (hwbuffer) = GST_BUFFER_OFFSET(buffer);\
				BM_FLOW("BM_HWTRANSITION v%p=h%p\n", buffer, (hwbuffer));\
				break;\
			}\
			tmp = tmp->next;\
		}\
		if (tmp==NULL)\
			g_print("BM_HWTRANSITION illegal %p!\n", pdata);\
	}while (0)

#define BM_RELEASE_BUFFER(pdata) do{\
		GSList * tmp = (bm_list);\
		GstBuffer * buffer;\
		while(tmp){\
			buffer = (GstBuffer *)(tmp->data);\
			if (GST_BUFFER_DATA(buffer)==(pdata)){\
				BM_FLOW("BM_RELEASE_BUFFERv2 %p:d%p\n", buffer, pdata);\
				if (GST_BUFFER_FLAG_IS_SET(buffer, BMFLAG)){\
                    GST_BUFFER_FLAG_UNSET(buffer, BMFLAG);\
                }else{\
				    (bm_list) = g_slist_remove((bm_list), buffer);\
				    BM_TRACE_BUFFER("codec release %p:d%p\n", buffer, pdata);\
                }\
				gst_buffer_unref(buffer);\
				break;\
			}\
			tmp = tmp->next;\
		}\
		if (tmp==NULL)\
			g_print("BM_RELEASE_BUFFERv2 illegal %p!\n", pdata);\
	}while (0)

#define BM_REJECT_BUFFER(pdata) do{\
		GSList * tmp = (bm_list);\
		GstBuffer * buffer;\
		g_print("BM_REJECT_BUFFER shuold not here %p!\n", pdata);\
		while(tmp){\
			buffer = (GstBuffer *)(tmp->data);\
			if (GST_BUFFER_DATA(buffer)==(pdata)){\
				BM_FLOW("BM_REJECT_BUFFERv2 %p:d%p\n", buffer, pdata);\
				if (GST_BUFFER_FLAG_IS_SET(buffer, BMFLAG)){\
                    GST_BUFFER_FLAG_UNSET(buffer, BMFLAG);\
                }else{\
				    (bm_list) = g_slist_remove((bm_list), buffer);\
				    BM_TRACE_BUFFER("codec release %p:d%p\n", buffer, pdata);\
				}\
				gst_buffer_unref(buffer);\
				break;\
			}\
			tmp = tmp->next;\
		}\
		if (tmp==NULL)\
			g_print("BM_RELEASE_BUFFERv2 illegal %p!\n", pdata);\
	}while (0)

#define BM_RENDER_BUFFER(pdata, tgtpad, status, timestamp, duration) do{\
        GSList * tmp = (bm_list);\
		GstBuffer * buffer;\
		while(tmp){\
			buffer = (GstBuffer *)(tmp->data);\
			if (GST_BUFFER_DATA(buffer)==(pdata)){\
				BM_FLOW("BM_RENDER_BUFFERv2 %p:d%p\n", buffer, pdata);\
				BM_FLOW("Render timestamp %lld\n",(timestamp)/1000000);\
				if (GST_BUFFER_FLAG_IS_SET(buffer, BMFLAG)){\
                    GST_BUFFER_FLAG_UNSET(buffer, BMFLAG);\
                }else{\
				    (bm_list) = g_slist_remove((bm_list), buffer);\
				    BM_TRACE_BUFFER("codec release %p:d%p\n", buffer, pdata);\
				}\
				GST_BUFFER_TIMESTAMP(buffer) = (timestamp);\
				GST_BUFFER_DURATION(buffer) = (duration);\
                status = gst_pad_push((tgtpad), buffer);\
				break;\
			}\
			tmp = tmp->next;\
		}\
		if (tmp==NULL)\
			g_print("BM_RENDER_BUFFERv2 illegal %p!\n", pdata);\
	}while (0)

#define BM_GET_MODE bm_mode
#define BM_GET_BUFFERNUM bm_buf_num

#endif//(DIRECT_RENDER_VERSION==2)


/*=============================================================================
                  FRAME DROPING RELATED MACROS/FUNCTIONS
=============================================================================*/

#ifdef FRAMEDROPING_ENALBED

#define OVERHEAD_TIME 50
#define GST_BUFFER_FLAG_IS_SYNC (GST_BUFFER_FLAG_LAST<<2)
#define KEY_FRAME_SHIFT 3
#define KEY_FRAME_ARRAY (1<<KEY_FRAME_SHIFT)
#define KEY_FRAME_MASK (KEY_FRAME_ARRAY-1)

struct sfd_frames_info {
    int total_frames;
    int dropped_frames;
    int dropped_iframes;
    int is_dropped_iframes;
    int estimate_decoding_time;
    int decoded_time;
	int curr_nonekey_frames, total_key_frames;
    int key_frames_interval[8];
    struct timeval tv_start, tv_stop; 
};

#define INIT_SFD_INFO(x)                \
do {                                    \
    gint i;                             \
    (x)->total_frames = 0;              \
    (x)->dropped_frames = 0;            \
    (x)->dropped_iframes = 0;           \
    (x)->is_dropped_iframes = 0;        \
    (x)->estimate_decoding_time = 0;    \
    (x)->decoded_time = 0;              \
    (x)->curr_nonekey_frames = 0;    	\
    (x)->total_key_frames = 0;     		\
    for(i=0;i<KEY_FRAME_ARRAY;i++) {    \
        (x)->key_frames_interval[i]=0;  \
    }                                   \
} while(0);

#define CALC_SFD_DECODED_TIME(x)                                            \
do {                                                                        \
    int decoded_time;                                                       \
    int decoded_frames = (x)->total_frames-(x)->dropped_frames;             \
    decoded_time = ((x)->tv_stop.tv_sec - (x)->tv_start.tv_sec) * 1000000   \
        + (x)->tv_stop.tv_usec - (x)->tv_start.tv_usec;                     \
    (x)->decoded_time += decoded_time;                                      \
    if (decoded_frames == 0) {                                              \
        (x)->estimate_decoding_time = decoded_time;                         \
    } else {                                                                \
    if ( decoded_time > (x)->estimate_decoding_time)                        \
        (x)->estimate_decoding_time = (x)->decoded_time / decoded_frames ;  \
    }                                                                       \
        GST_DEBUG("SFD info:\ntotal frames : %d,\tdropped frames : %d.\n",  \
            (x)->total_frames,(x)->dropped_frames);                         \
        GST_DEBUG("Decoded time: %d,\tAverage decoding time : %d.\n",       \
            decoded_time, (x)->estimate_decoding_time);                     \
}while(0);

#define GST_ADD_SFD_FIELD(caps)                     \
do {                                                \
    GValue sfd_value = { G_TYPE_INT, 1};            \
    GstStructure *s,*structure;                     \
    structure = gst_caps_get_structure((caps),0);   \
    s = gst_structure_copy(structure);              \
    gst_structure_set_value(s,"sfd",&sfd_value);    \
    gst_caps_remove_structure((caps), 0);           \
    gst_caps_append_structure((caps), s);           \
}while(0);

#define MIN_DELAY_TIME 2000000
#define MAX_DELAY_TIME 3000000

#define GST_QOS_EVENT_HANDLE(pSfd_info,diff,framerate) do {						\
	if  ((pSfd_info)->is_dropped_iframes == 0) {                                \
    	int key_frames_interval,next_key_frame_time;							\
    	int micro_diff = (diff)/1000;                                           \
    	gint i;                                                                 \
    	if (micro_diff>MAX_DELAY_TIME) {                                        \
        	(pSfd_info)->is_dropped_iframes =1;									\
            GST_ERROR ("The time of decoding is far away the system,"           \
                "so should drop some frames\n");								\
            break;                                                              \
        }                                                                       \
    	if((pSfd_info)->total_key_frames >= KEY_FRAME_ARRAY) {					\
            for(i=0;i<KEY_FRAME_ARRAY;i++) {                                    \
                key_frames_interval += (pSfd_info)->key_frames_interval[i];     \
            }                                                                   \
            key_frames_interval >>= KEY_FRAME_SHIFT;                            \
    		next_key_frame_time = (1000000 / (framerate)) * 					\
    			(key_frames_interval - (pSfd_info)->curr_nonekey_frames);		\
    	}																		\
    	else																	\
    		next_key_frame_time = 0;											\
        if ( (micro_diff > MIN_DELAY_TIME) &&                                   \
		  (next_key_frame_time) && (next_key_frame_time < micro_diff) ) {		\
        	(pSfd_info)->is_dropped_iframes =1;									\
            GST_ERROR ("The time of decoding is after the system," 	            \
                "so should drop some frames\n");								\
        	GST_ERROR ("key frame interval: %d,"                                \
        	    "estimate next I frames: %d.\n",key_frames_interval, 	        \
        		key_frames_interval-(pSfd_info)->curr_nonekey_frames);			\
        	GST_ERROR ("diff time: %d, to next I frame time: %d\n",	            \
        		(micro_diff),next_key_frame_time);							    \
	    }																		\
    }                                                                           \
} while(0);
#define GET_TIME(x) do {        \
    gettimeofday((x), 0);       \
} while(0);

/*=============================================================================
FUNCTION:               Strategy_FD

DESCRIPTION:            Strategy of Frame dropping in.

ARGUMENTS PASSED:       None.


RETURN VALUE:           GstFlowReturn
                        GST_FLOW_ERROR: The GST buffer should be dropped.
                        GST_FLOW_OK: original case.

PRE-CONDITIONS:  	    None

POST-CONDITIONS:   	    None

IMPORTANT NOTES:   	    None
=============================================================================*/
static GstFlowReturn Strategy_FD(int is_keyframes,   
            struct sfd_frames_info * psfd_info                                
            )                                                   
{                                                                               
    psfd_info->total_frames++;
	psfd_info->curr_nonekey_frames++;

    if (is_keyframes) {
        (psfd_info)->is_dropped_iframes = 0;  
        (psfd_info)->key_frames_interval[(psfd_info)->total_key_frames&(KEY_FRAME_MASK)]
            = (psfd_info)->curr_nonekey_frames;
        (psfd_info)->total_key_frames++;
	    (psfd_info)->curr_nonekey_frames = 0;
    }
    if ((psfd_info)->is_dropped_iframes)
    {                                                                           
        if (!(is_keyframes)) {                      
            (psfd_info)->dropped_frames++;                                      
            GST_WARNING("SFD info:\ntotal frames : %d,\tdropped frames : %d.\n",
                (psfd_info)->total_frames,(psfd_info)->dropped_frames);
            return GST_FLOW_ERROR;                                              
        }                                                                       
    }
    return GST_FLOW_OK;                                                         
} 

#endif


/*=============================================================================
                      DEMO PROTECTION RELATED MACROS
=============================================================================*/

/* The following is for DEMO protection */
#define DEMO_STR "DEMO"

#define INIT_DEMO_MODE(strVer,demomode)     \
do {                                        \
    if (strstr((strVer), DEMO_STR)>0)       \
        (demomode) = 1;                     \
    else                                    \
        (demomode) = 0;                     \
}while(0);

#define DEMO_LIVE_TIME 120    

#define DEMO_LIVE_CHECK(demomode,timestamp,srcpad)              \
do {                                                            \
    if (                                                        \
        ( (demomode) == 1 ) &&                                  \
        ( ((timestamp) / GST_SECOND ) > DEMO_LIVE_TIME)         \
        )                                                       \
    {                                                           \
        GstEvent *event;                                        \
        GST_WARNING("This is a demo version,                    \
        and the time exceed 2 minutes.                          \
            Sending EOS event.\n");                             \
        event = gst_event_new_eos();                            \
        gst_pad_push_event ((srcpad), event);                   \
        (demomode) = 2;                                         \
    }                                                           \
}while(0);

/*=============================================================================
                                 STRUCTURES AND OTHER TYPEDEFS
=============================================================================*/

typedef enum {
	MIRDIR_NONE,
	MIRDIR_VER,
	MIRDIR_HOR,
	MIRDIR_HOR_VER,
} MirrorDirection;

typedef enum {
	STD_MPEG2 = -1,
	STD_VC = -1,
	STD_MPEG4 = 0,
	STD_H263,
	STD_AVC
} CodStd;


#endif//__MFW_GST_UTILS_H__			

