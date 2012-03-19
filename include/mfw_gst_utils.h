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
	STD_AVC,
	STD_MJPG,
} CodStd;


#endif//__MFW_GST_UTILS_H__
