/*
 * Copyright 2004-2009 Freescale Semiconductor, Inc. All Rights Reserved.
 *
 * Copyright (c) 2006, Chips & Media.  All rights reserved.
 */

/*
 * The code contained herein is licensed under the GNU Lesser General
 * Public License.  You may obtain a copy of the GNU Lesser General
 * Public License Version 2.1 or later at the following locations:
 *
 * http://www.opensource.org/licenses/lgpl-license.html
 * http://www.gnu.org/copyleft/lgpl.html
 */

/*!
 * @file vpu_reg.h
 *
 * @brief header file for codec registers in VPU
 *
 * @ingroup VPU
 */

#ifndef __VPU__REG__H
#define __VPU__REG__H

/*---------------------------------------------------------------------------
 * HARDWARE REGISTER
 *-------------------------------------------------------------------------*/
#define BIT_CODE_RUN			0x000
#define BIT_CODE_DOWN			0x004
#define BIT_INT_REQ			0x008
#define BIT_INT_CLEAR			0x00C
#define	BIT_INT_STS			0x010 // Not prsent
#define	BIT_CODE_RESET			0x014
#define BIT_CUR_PC			0x018

/*---------------------------------------------------------------------------
 *  GLOBAL REGISTER
 *---------------------------------------------------------------------------*/
#define BIT_CODE_BUF_ADDR		0x100
#define BIT_WORK_BUF_ADDR		0x104
#define BIT_PARA_BUF_ADDR		0x108
#define BIT_BIT_STREAM_CTRL		0x10C
#define BIT_FRAME_MEM_CTRL		0x110
#define CMD_DEC_DISPLAY_REORDER		0x114
#define BIT_BIT_STREAM_PARAM		0x114
#define BIT_VPU_PIC_COUNT		0x118

#define BIT_RD_PTR_0			0x120
#define BIT_WR_PTR_0			0x124
#define BIT_RD_PTR_1			0x128
#define BIT_WR_PTR_1			0x12C
#define BIT_RD_PTR_2			0x130
#define BIT_WR_PTR_2			0x134
#define BIT_RD_PTR_3			0x138
#define BIT_WR_PTR_3			0x13C

#define V1_BIT_SEARCH_RAM_BASE_ADDR	0x140
#define V2_BIT_AXI_SRAM_USE		0x140

#define BIT_BUSY_FLAG			0x160
#define BIT_RUN_COMMAND			0x164
#define BIT_RUN_INDEX			0x168
#define BIT_RUN_COD_STD			0x16C
#define BIT_INT_ENABLE			0x170
#define BIT_INT_REASON			0x174
#define V2_BIT_RUN_AUX_STD		0x178


#define BIT_CMD_0			0x1E0
#define BIT_CMD_1			0x1E4

#define BIT_MSG_0			0x1F0
#define BIT_MSG_1			0x1F4
#define BIT_MSG_2			0x1F8
#define BIT_MSG_3			0x1FC

/*---------------------------------------------------------------------------
 * [DEC SEQ INIT] COMMAND
 *-------------------------------------------------------------------------*/
#define CMD_DEC_SEQ_BB_START		0x180
#define CMD_DEC_SEQ_BB_SIZE		0x184
#define CMD_DEC_SEQ_OPTION		0x188
#define CMD_DEC_SEQ_SRC_SIZE		0x18C
#define CMD_DEC_SEQ_START_BYTE		0x190
#define CMD_DEC_SEQ_PS_BB_START		0x194
#define CMD_DEC_SEQ_PS_BB_SIZE		0x198
#define CMD_DEC_SEQ_MP4_ASP_CLASS       0x19C

#define CMD_DEC_SEQ_INIT_ESCAPE		0x114

#define RET_DEC_SEQ_SUCCESS		0x1C0
#define RET_DEC_SEQ_SRC_FMT		0x1C4
#define RET_DEC_SEQ_SRC_SIZE		0x1C4
#define RET_DEC_SEQ_SRC_F_RATE		0x1C8
#define RET_DEC_SEQ_FRAME_NEED		0x1CC
#define RET_DEC_SEQ_FRAME_DELAY		0x1D0
#define RET_DEC_SEQ_INFO		0x1D4
#define RET_DEC_SEQ_CROP_LEFT_RIGHT	0x1D8
#define RET_DEC_SEQ_CROP_TOP_BOTTOM	0x1DC
#define RET_DEC_SEQ_NEXT_FRAME_NUM	0x1E0

/*--------------------------------------------------------------------------
 * [ENC SEQ INIT] COMMAND
 *-------------------------------------------------------------------------*/
#define CMD_ENC_SEQ_BB_START		0x180
#define CMD_ENC_SEQ_BB_SIZE		0x184
#define CMD_ENC_SEQ_OPTION		0x188

#define CMD_ENC_SEQ_COD_STD		0x18C
#define CMD_ENC_SEQ_SRC_SIZE		0x190
#define CMD_ENC_SEQ_SRC_F_RATE		0x194
#define CMD_ENC_SEQ_MP4_PARA		0x198
#define CMD_ENC_SEQ_263_PARA		0x19C
#define CMD_ENC_SEQ_264_PARA		0x1A0
#define CMD_ENC_SEQ_SLICE_MODE		0x1A4
#define CMD_ENC_SEQ_GOP_NUM		0x1A8
#define CMD_ENC_SEQ_RC_PARA		0x1AC
#define CMD_ENC_SEQ_RC_BUF_SIZE		0x1B0
#define CMD_ENC_SEQ_INTRA_REFRESH	0x1B4
#define V1_CMD_ENC_SEQ_FMO		0x1B8
#define V2_CMD_ENC_SEQ_SEARCH_BASE	0x1b8
#define V2_CMD_ENC_SEQ_SEARCH_SIZE	0x1bc
#define CMD_ENC_SEQ_RC_QP_MAX		0x1C8
#define RET_ENC_SEQ_SUCCESS		0x1C0

#define CMD_ENC_SEQ_JPG_PARA	        0x198
#define CMD_ENC_SEQ_JPG_RST_INTERVAL	0x19C
#define CMD_ENC_SEQ_JPG_THUMB_EN	0x1A0
#define CMD_ENC_SEQ_JPG_THUMB_SIZE	0x1A4
#define CMD_ENC_SEQ_JPG_THUMB_OFFSET	0x1A8

/*--------------------------------------------------------------------------
 * [ENC PARA CHANGE] COMMAND :
 *------------------------------------------------------------------------*/
#define CMD_ENC_SEQ_PARA_CHANGE_ENABLE	0x180
#define CMD_ENC_SEQ_PARA_RC_GOP		0x184
#define CMD_ENC_SEQ_PARA_RC_INTRA_QP	0x188
#define CMD_ENC_SEQ_PARA_RC_BITRATE     0x18C
#define CMD_ENC_SEQ_PARA_RC_FRAME_RATE  0x190
#define	CMD_ENC_SEQ_PARA_INTRA_MB_NUM	0x194
#define CMD_ENC_SEQ_PARA_SLICE_MODE	0x198
#define CMD_ENC_SEQ_PARA_HEC_MODE	0x19C

#define RET_ENC_SEQ_PARA_CHANGE_SUCCESS	0x1C0

/*---------------------------------------------------------------------------
 * [DEC PIC RUN] COMMAND
 *-------------------------------------------------------------------------*/
#define CMD_DEC_PIC_ROT_MODE		0x180
#define CMD_DEC_PIC_ROT_ADDR_Y		0x184
#define CMD_DEC_PIC_ROT_ADDR_CB		0x188
#define CMD_DEC_PIC_ROT_ADDR_CR		0x18C

#define CMD_DEC_PIC_DBK_ADDR_Y		0x190
#define CMD_DEC_PIC_DBK_ADDR_CB		0x194
#define CMD_DEC_PIC_DBK_ADDR_CR		0x198

#define CMD_DEC_PIC_ROT_STRIDE		0x190
#define CMD_DEC_PIC_OPTION		0x194
#define CMD_DEC_PIC_SKIP_NUM		0x198
#define CMD_DEC_PIC_CHUNK_SIZE		0x19C
#define CMD_DEC_PIC_BB_START		0x1A0
#define CMD_DEC_PIC_START_BYTE		0x1A4

#define RET_DEC_PIC_FRAME_NUM		0x1C0
#define RET_DEC_PIC_FRAME_IDX		0x1C4
#define RET_DEC_PIC_ERR_MB		0x1C8
#define RET_DEC_PIC_TYPE		0x1CC

#define RET_DEC_PIC_POST		0x1D0

/*---------------------------------------------------------------------------
 * [ENC PIC RUN] COMMAND
 *--------------------------------------------------------------------------*/
#define CMD_ENC_PIC_SRC_ADDR_Y		0x180
#define CMD_ENC_PIC_SRC_ADDR_CB		0x184
#define CMD_ENC_PIC_SRC_ADDR_CR		0x188
#define CMD_ENC_PIC_QS			0x18C
#define CMD_ENC_PIC_ROT_MODE		0x190
#define CMD_ENC_PIC_OPTION		0x194
#define CMD_ENC_PIC_BB_START		0x198
#define CMD_ENC_PIC_BB_SIZE		0x19C

#define RET_ENC_PIC_FRAME_NUM		0x1C0
#define RET_ENC_PIC_TYPE		0x1C4
#define RET_ENC_PIC_FRAME_IDX		0x1C8
#define RET_ENC_PIC_SLICE_NUM		0x1CC
#define RET_ENC_PIC_FLAG		0x1D0

/*----------------------------------------------------------------------------
 * [SET FRAME BUF] COMMAND
 *---------------------------------------------------------------------------*/
#define CMD_SET_FRAME_BUF_NUM		0x180
#define CMD_SET_FRAME_BUF_STRIDE	0x184
#define CMD_SET_FRAME_SLICE_BB_START	0x188
#define CMD_SET_FRAME_SLICE_BB_SIZE	0x18C
#define V2_CMD_SET_FRAME_AXI_BIT_ADDR	0x190
#define V2_CMD_SET_FRAME_AXI_IPACDC_ADDR	0x194
#define V2_CMD_SET_FRAME_AXI_DBKY_ADDR	0x198
#define V2_CMD_SET_FRAME_AXI_DBKC_ADDR	0x19C
#define V2_CMD_SET_FRAME_AXI_OVL_ADDR	0x1A0
#define CMD_SET_FRAME_SOURCE_BUF_STRIDE	0x1A8

/*---------------------------------------------------------------------------
 * [ENC HEADER] COMMAND
 *-------------------------------------------------------------------------*/
#define CMD_ENC_HEADER_CODE		0x180
#define CMD_ENC_HEADER_BB_START		0x184
#define CMD_ENC_HEADER_BB_SIZE		0x188

/*----------------------------------------------------------------------------
 * [DEC_PARA_SET] COMMAND
 *---------------------------------------------------------------------------*/
#define CMD_DEC_PARA_SET_TYPE		0x180
#define CMD_DEC_PARA_SET_SIZE		0x184

/*----------------------------------------------------------------------------
 * [ENC_PARA_SET] COMMAND
 *--------------------------------------------------------------------------*/
#define CMD_ENC_PARA_SET_TYPE		0x180
#define RET_ENC_PARA_SET_SIZE		0x1c0

/*---------------------------------------------------------------------------
 * [FIRMWARE VERSION] COMMAND
 * [32:16] project number =>
 * [16:0]  version => xxxx.xxxx.xxxxxxxx
 *-------------------------------------------------------------------------*/
#define RET_VER_NUM			0x1c0

#endif
