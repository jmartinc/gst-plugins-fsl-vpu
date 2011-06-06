/*
 * Copyright 2011 Sascha Hauer, Pengutronix <s.hauer@pengutronix.de>
 * Copyright 2005-2007 Freescale Semiconductor, Inc. All Rights Reserved.
 *
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
#include <gst/gst.h>
#include "mfw_gst_vpu.h"
#include "mfw_gst_vpu_encoder.h"
#include "mfw_gst_vpu_decoder.h"

GType
mfw_gst_vpu_codec_get_type(void)
{
	static GType vpu_codec_type = 0;

	static GEnumValue vpu_codecs[] = {
		{STD_MPEG4, "0", "std_mpeg4"},
		{STD_H263, "1", "std_h263"},
		{STD_AVC, "2", "std_avc"},
		{0, NULL, NULL},
	};
	if (!vpu_codec_type) {
		vpu_codec_type =
		    g_enum_register_static("GstVpuCodecs", vpu_codecs);
	}
	return vpu_codec_type;
}

void mfw_gst_vpu_class_init_common(GObjectClass *gobject_class)
{
	g_object_class_install_property(gobject_class, MFW_GST_VPU_CODEC_TYPE,
					g_param_spec_enum("codec-type",
							  "codec_type",
							  "selects the codec type",
							  mfw_gst_vpu_codec_get_type(),
							  STD_AVC,
							  G_PARAM_READWRITE));

	g_object_class_install_property (gobject_class, MFW_GST_VPU_DEVICE,
			g_param_spec_string ("device", "vpu device location",
				"i.MX vpu encoder/decoder device location",
				VPU_DEVICE, G_PARAM_READWRITE));
}

static gboolean
plugin_init(GstPlugin * plugin)
{
	if (!gst_element_register(plugin, "vpuencoder",
				    GST_RANK_PRIMARY, MFW_GST_TYPE_VPU_ENC))
		return FALSE;
	
	if (!gst_element_register(plugin, "vpudecoder",
				GST_RANK_PRIMARY, MFW_GST_TYPE_VPU_DEC))
		return FALSE;
	
	return TRUE;

}

GST_PLUGIN_DEFINE(GST_VERSION_MAJOR,	/* major version of gstreamer */
		  GST_VERSION_MINOR,	/* minor version of gstreamer */
		  "vpu",	/* name of our  plugin */
		  "Encodes/Decodes Raw YUV Data to/from MPEG4 SP," "or H.264 BP, or H.263 Format" "data to/from Raw YUV Data ",	/* what our plugin actually does */
		  plugin_init,	/* first function to be called */
		  VERSION,
		  GST_LICENSE_UNKNOWN,
		  "freescale semiconductor", "www.freescale.com")


