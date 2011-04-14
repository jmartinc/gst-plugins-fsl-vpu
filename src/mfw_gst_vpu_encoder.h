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

/*=============================================================================
                                                                               
    Module Name:                mfw_gst_vpu_encoder.h  

    General Description:        Include File for Hardware (VPU) Encoder Plugin 
                                for Gstreamer              
                            
===============================================================================
Portability:    compatable with Linux OS and Gstreamer 10.11 and below 

===============================================================================
                            INCLUDE FILES
=============================================================================*/
#ifndef __MFW_GST_VPU_ENCODER_H__
#define __MFW_GST_VPU_ENCODER_H__
/*=============================================================================
                                           CONSTANTS
=============================================================================*/
#define NUM_INPUT_BUF   3


/* None. */

/*=============================================================================
                                             ENUMS
=============================================================================*/

/* None. */

/*=============================================================================
                                            MACROS
=============================================================================*/

G_BEGIN_DECLS

#define MFW_GST_TYPE_VPU_ENC (mfw_gst_type_vpu_enc_get_type())

#define MFW_GST_VPU_ENC(obj)  \
    (G_TYPE_CHECK_INSTANCE_CAST((obj),MFW_GST_TYPE_VPU_ENC,GstVPU_Enc))

#define MFW_GST_VPU_ENC_CLASS(klass) \
    G_TYPE_CHECK_CLASS_CAST((klass),MFW_GST_TYPE_VPU_ENC,GstVPU_EncClass))

#define MFW_GST_IS_VPU_ENC(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj),MFW_GST_TYPE_VPU_ENC))

#define MFW_GST_IS_VPU_ENC_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass),MFW_GST_TYPE_VPU_ENC))

#define MFW_GST_TYPE_VPU_ENC_CODEC (mfw_gst_vpuenc_codec_get_type())

/*=============================================================================
                                 STRUCTURES AND OTHER TYPEDEFS
=============================================================================*/

typedef struct _GstVPU_EncClass 
{
    GstElementClass parent_class;

}GstVPU_EncClass;



/*=============================================================================
                                 GLOBAL VARIABLE DECLARATIONS
=============================================================================*/

/* None. */

/*=============================================================================
                                     FUNCTION PROTOTYPES
=============================================================================*/

GType mfw_gst_type_vpu_enc_get_type(void);
GType mfw_gst_vpuenc_codec_get_type(void);

#define	VPU_IOC_MAGIC		'V'
#define	VPU_IOC_SET_ENCODER	_IO(VPU_IOC_MAGIC, 5)
#define VPU_IOC_SET_DECODER	_IO(VPU_IOC_MAGIC, 6)
#define	VPU_IOC_ROTATE_MIRROR	_IO(VPU_IOC_MAGIC, 7)
#define VPU_IOC_CODEC		_IO(VPU_IOC_MAGIC, 8)

G_END_DECLS
#endif				/* __MFW_GST_VPU_ENCODER_H__ */
