
 /************************************************************************
  * Copyright 2005-2006 by Freescale Semiconductor, Inc.
  * All modifications are confidential and proprietary information
  * of Freescale Semiconductor, Inc. ALL RIGHTS RESERVED.
  ************************************************************************/

/*=============================================================================
                                                                               
    Module Name:  MP4Parser.h  

    General Description:  Definitions and Declarations for MP4 Parser.                         
                            
===============================================================================
                           Freescale Confidential Proprietary
                                   WMSG - Multimedia
                   (c) Copyright Freescale 2005, All Rights Reserved
  
Revision History:
                               Modification  Tracking
Author                         Date          Number      Description of Changes
-----------------------------  -----------  ----------  -----------------------
Shashidhar H.M                 04-Apr-07    ENGR30444    Initial Draft Version.

Shashidhar H.M                 09-July-07   ENGR44147    changes to support a non playing
                                                         mp4 file(with discontinous tracks).

===============================================================================
                            INCLUDE FILES
=============================================================================*/
#ifndef MP4PARSER_H
#define MP4PARSER_H

#include "MP4OSMacros.h"

#ifdef __WINCE
#include <windows.h> //Required for definition on LONGLONG
#endif 

/*=============================================================================
                             CONSTANTS / VARIABLES
=============================================================================*/

/*=============================================================================
                              MACROS
=============================================================================*/

#define MP4_PARSER_MAX_STREAM                                                10
#define MP4_PARSER_STREAM_MORE_THEN_MAX                                      11
#define MP4_PARSER_SUCCESS                                                   0
#define MP4_PARSER_FAILURE_BIT_ALLOCATION                                  100
#define MP4_PARSER_CROSS_THE_TOTAL_BYTE_STREAM                             101
#define MP4_PARSER_MALLOC_FAILED                                            -1
//#define MP4_PARSER_FILE_TRACK_NUMBER    1 // Amanda:No longer used! Conflicting with MP4EOF, file reading/writing will map to this value.

#define MP4_H264_NAL_START_CODE_MARGIN 60 /* extra bytes to insert NAL start codes into H265 sample data.*/

/* Macros for the ADTS Header bit Allocation. */
#define BITS_FOR_SYNCWORD                                                   12
#define BITS_FOR_MPEG_ID                                                     1
#define BITS_FOR_MPEG_LAYER                                                  2
#define BITS_FOR_PROTECTION                                                  1
#define BITS_FOR_PROFILE                                                     2
#define BITS_FOR_SAMPLING_FREQ                                               4
#define BITS_FOR_PRIVATE_BIT                                                 1
#define BITS_FOR_CHANNEL_TYPE                                                3
#define BITS_FOR_ORIGINAL_COPY                                               1
#define BITS_FOR_HOME                                                        1
#define BITS_FOR_COPYRIGHT                                                   1
#define BITS_FOR_COPYRIGHT_START                                             1
#define BITS_FOR_FRAME_LENGTH                                               13
#define BITS_FOR_BUFFER_FULLNESS                                            11

/* Macros for the user data atom list. */
#define MP4_UDTA_LIST_TITLE                                                100
#define MP4_UDTA_LIST_ARTIST                                               101
#define MP4_UDTA_LIST_ALBUM                                                102
#define MP4_UDTA_LIST_COMMENT                                              103
#define MP4_UDTA_LIST_TOOL                                                 105
#define MP4_UDTA_LIST_ILST                                                 106
#define MP4_UDTA_LIST_HDLR                                                 107
#define MP4_UDTA_LIST_FREE                                                 108
#define MP4_UDTA_LIST_DATA                                                 109
#define MP4_UDTA_LIST_UNKOWN                                               110
#define MP4_UDTA_LIST_GNRE                                                 111
#define MP4_UDTA_LIST_YEAR                                                 112
#define MP4_UDTA_LIST_META                                                 113

/* Initialise for the sanity check in the code. */
#define MP4_UDTA_LIST_GNRE_SCI                                           10000

/*---------------------- STRUCTURE/UNION DATA TYPES --------------------------*/
/* Structure for the Number of bytes read abd the maximum buffer Size. */

typedef struct
{
    u32  numBytesRead;      /* Number of bytes Read by per Access unit */
                            /* And the ADTS header.                    */
    u8  *MaxBufferRequired; /* Buffer into which sample data has to be written.    */
    u32 max_bufsize; /* size of the "MaxBufferRequired", in bytes. Inferred from the "MaxBufferAu" of all A/V streams. */
    
    u8  *MaxTempBuffer; /* for writing H264 sample data if NAL length field is less than 4 bytes long. Same size as "MaxBufferRequired" */
    
	u64 media_timestamp; /* time stamp in ms */
    u32 sync;     /* ENGR54535: whether this is a sync sample. For video, only key frames are sync samples. */

}sMP4TrackReadByteInfo; /* ENGR75842: each track has its own working buffer, to support multi-thread reading */


typedef struct
{
    sMP4TrackReadByteInfo  track_read_info[MP4_PARSER_MAX_STREAM];

	u32 max_chunk_offset; /* for current seek time, larger chunk offset of A/V tracks */
	u32 objdes_offset[MP4_PARSER_MAX_STREAM];	
	      
} sMP4ParserReadByteInfo;

/* stream type values. 14496-1 section8.6.6 "DecoderConfigDescriptor" */
enum
{
    MP4_STREAMTYPE_FORBIDDEN =0,
    MP4_ODSM =1,            /* object description stream */
    MP4_CLK_REFERENCE =2,   /* clock reference stream */ 
    MP4_SDSM =3,            /* scene description stream */
    MP4_VIDEO=4,
    MP4_AUDIO=5,
    MP4_HINT =6, /* Warning:use the value reserved for MPEG7 stream. May change in the future! Don't directly use numerical vaue!*/
    MP4_QT_TIMECODE = 0x20 /* quicktime time code.Use user private value 0x20- 0x3f*/
};

enum
{
	/* JLF 12/00 : support for OD, returned by MP4GetInline... and MP4GetProfiles... */
	MP4HasRootOD              = 2,
	MP4EOF                    = 1,
	MP4NoErr                  = 0,
	MP4FileNotFoundErr        = -1,
	MP4BadParamErr            = -2,
	MP4NoMemoryErr            = -3,
	MP4IOErr                  = -4,
	MP4NoLargeAtomSupportErr  = -5,
	MP4BadDataErr             = -6,
	MP4VersionNotSupportedErr = -7,
	MP4InvalidMediaErr        = -8,
	MP4DataEntryTypeNotSupportedErr = -100,
	MP4NoQTAtomErr                  = -500,
	MP4NotImplementedErr            = -1000
};

struct MP4MovieRecord
{
	void*	data;
};
typedef struct MP4MovieRecord MP4MovieRecord;
typedef MP4MovieRecord*	MP4Movie;

struct MP4TrackRecord
{
	void*	data; /* ptr to 'trak' atom */
	u32     tracktype;
	u32     decodertype;
	u32     avgBitStream;
	u32     Audio_ObjectType;
	u32     Sbr_PresentFlag;
	u32     channels;
};
typedef struct MP4TrackRecord MP4TrackRecord, * MP4TrackRecordPtr;
typedef void *	MP4Track; /* engr59629: To hide atom detail. To use it as "MP4TrackAtomPtr", defined in "MP4Atoms.h". */


struct MP4MediaRecord
{
	void*	data; /* ptr to 'mdia' atom of this track */
	u32     specificInfo_size;
	u32     track_count;    /* index of current track to read, 0-based. Default value is 0. And newMedia()does not set the actual track count. */
	u32     peer_track_count; /* the peer track index, 0-based. Peer track is the track other than current track */ 
	u32     frame_count;	
	u32     media_width;
	u32     media_height;
	u64     media_duration;
    float   media_framerate;
	u32     media_channels;
	u32     level;
	u32     profile;
	u32     track_endflag; /*whether this track is end. But the longer track's flag is never set. So it's only indicative but not used. */
	u32     track_interleaved; 
    u8      nal_length_size; /* For H264 only. Size of NAL unit length field, in bytes. */
};
typedef struct MP4MediaRecord MP4MediaRecord, * MP4MediaRecordPtr;
typedef void *	MP4Media; /* engr59629: To hide atom detail. To use it as "MP4MediaAtomPtr", defined in "MP4Atoms.h". */

struct MP4TrackReaderRecord
{
	void *data;
};
typedef struct MP4TrackReaderRecord MP4TrackReaderRecord;
typedef MP4TrackReaderRecord* MP4TrackReader;
typedef char **MP4Handle;
typedef int MP4Err;
/* Struct Updates all Basic Data Structure Needed for MP4 Parser of a file. */
typedef struct
{
    u8  *FileAddress;

    u32  streamType;
    u32  objectTypeIndication;
    u32  decoderBufferSize;
    u32  upStream;
    u32  maxStream;
    u32  avgBitStream;
    u32  NumberBytes_ADTS;
    u32  TotalFrames[MP4_PARSER_MAX_STREAM];
    u32  OffsetByte;
    u32  SetFlag;
    u32  TotalBytes[MP4_PARSER_MAX_STREAM];
    u32  OffsetUDTA;
    u32  SizeUdta;
	u32  totalstreams;   /* number of A/V streams. Other type of streams are overlooked */
	u32  tarck_index;   /* Index of the target track to read, 0-based. Only A/V tracks are considered. */
	u32  audio_offset;
	u32  video_offset;

    /* Variables used for the ADTS headers. */
    u32  syncword;
    u32  id;
    u32  layer;
    u32  protection_abs;
    u32  profile;
    u32  sampl_freq_idx;
    u32  private_bit;
    u32  channel_config;
    u32  original_copy;
    u32  home;
    u32  copyR_id_bit;
    u32  copyR_id_start;
    u32  frame_length;
    u32  buff_fullness;
    u32  num_of_rdb;
    u32  crc_check;
    u32  rdb_position;
    u32  crc_check_rdb;
	u32  media_time_sacle[MP4_PARSER_MAX_STREAM];
	u32  chunk_offset[MP4_PARSER_MAX_STREAM]; 
                        /* Offset, in bytes, 
	                    of the 1st chunk of a track to read,
	                    when file is just opened or after a seeking is performed.
	                    Since seeking is peformed only on A/V tracks, 
	                    so this chunk offset of non-A/V tracks does not change after seeking */
                       
	u64  media_duration; /* media duration, in ns */
	
    MP4Movie        moov; 
    MP4TrackRecordPtr   trak[MP4_PARSER_MAX_STREAM];
    MP4MediaRecordPtr   media[MP4_PARSER_MAX_STREAM];
    MP4TrackReader  reader[MP4_PARSER_MAX_STREAM];
    MP4Handle       decoderSpecificInfoH[MP4_PARSER_MAX_STREAM];
    MP4Handle       sampleH[MP4_PARSER_MAX_STREAM];
		
} sMP4ParserObjectType;

enum
{
	MONO  =1,
	STEREO=2
};

/* codec type. Complied with "objectTypeIndication" values of DecoderConfigDescriptor, ISO/IEC 14496-1 section 8.6.6 */
typedef enum
{
	VIDEO_MPEG4  =0x20, /*Visual 14496-2 */
	VIDEO_H264   =0x21,
	AUDIO_AAC    =0x40, /* Audio 14496-3 (MPEG-4 AAC). 
	                       Also indicating general AAC audio type, including both MPEG-2 & MPEG-4 AAC */
	AUDIO_MPEG2_AAC = 0x66, /* Audio 13818-7 Main Profile (MPEG-2 AAC) */
	AUDIO_MPEG2_AAC_LC  = 0x67, /* Audio 13818-7 lowComplexity Profile (MPEG-2 AAC LC)*/
	AUDIO_MPEG2_AAC_SSR = 0x68, /* Audio 13818-7 Scalable Sampling Rate Profile (MPEG-2 AAC SSR)*/
	AUDIO_MPEG2  =0x69, /* Audio 13818-3 (MPEG-2 Audio)*/
    AUDIO_MP3    =0x6b, /* Audio 11172-3 (MPEG-1 Layer3)*/
	VIDEO_H263   =0xF2,
	AUDIO_AMR    =0xE1
} eCodecType;
	
/* Structure which keeps the basic Information of the Parsed Stream. */
typedef struct
{
    u32 streamType;
    u32 numberFrames;
    u32 decoderType;
    u32 avgBitRate;
    u32 TrackID;
    u32 MediaTimeScale;
    u32 StreamTotalBytes;
    u32 MaxBufferAu; /* maximum sample size of this stream in bytes = read max sample + 7 (ATS header for AAC audio) */
} sMP4ParserStreamInfo;


/* Structure to keep the information for the File. */
typedef struct
{
	u32 NumberOfStreams; /* Number of stream in file. */
	sMP4ParserStreamInfo   *MP4PmStreamArray[MP4_PARSER_MAX_STREAM];
} sMP4ParserFileInfo;


/* Structure to keep the information of the userdata list. */
typedef struct
{
	u8  *FileName;   /* Holds name of the file.     */
    u8  *ArtistName; /* Holds name of the artist.   */
    u8  *AlbumName;  /* Holds name of the album.    */


	u8  *Comment;    /* Holds comment of the file.  */
    u8  *Tool;       /* Holds tool used in file.    */
	u8  *Year;       /* Holds the release year.     */


   	u8  *Genre;      /* Holds Books and Spoken, or  */
                     /* Spoken Word, or Audio book. */
} sMP4ParserUdtaList;

typedef struct 
{
    void  (*MP4LocalSeekFile)(FILE *, int, int, void *);
    u32   (*MP4LocalReadFile)(void *, u32, u32, FILE *, void *);
    LONGLONG   (*MP4LocalGetCurrentFilePos)(FILE *, void *);
    FILE* (*MP4LocalFileOpen)(const u8 *, const u8 *);
    LONGLONG   (*MP4LocalFileSize)(FILE *, void *);
    u32   (*MP4LocalFileClose)(FILE *);
    u32   (*MP4LocalWriteInFile)(const void *, u32, u32, FILE *);

    void  (*MP4LocalFree)(void *); 
    void* (*MP4LocalCalloc)(u32, u32);
    void* (*MP4LocalMalloc)(u32);
    void* (*MP4LocalReAlloc)(void *, u32);

}sFunctionPtrTable;

/*---------------------------- GLOBAL DATA ----------------------------------*/
/*------------------------ FUNCTION PROTOTYPE(S) ----------------------------*/
/* To get the size of the header of the ADTS in terms of bytes. */
#define ADTS_HEADER_SIZE(p) (7 + (p->protection_abs ? 0: \
                                                    ((p->num_of_rdb + 1) * 2)))


#ifdef __cplusplus
#define EXTERN extern "C"
#else
#define EXTERN 
#endif

EXTERN const char * MPEG4ParserVersionInfo();

/* Get the ADTS header. */
EXTERN u32 MP4PutAdtsHeader (sMP4ParserObjectType *Source,
                      u8                   *Dest);


/* Alloctaed the number of bit reqired. */
EXTERN MP4Err MP4PutBits (u8  **PositionDest,
                   u32   Data,
                   u32   NunberOfBits);

/* Fucntion to Initialialise all the data Structure */
/* needed for the MP4 Parser Module. 
This is the 1st API to call for MP4 parser lib. 
NOTE: the last API to call is MP4ParserFreeMemory(). */
EXTERN MP4Err MP4ParserModuleInit (sMP4ParserObjectType  *ObjectType,
							FILE                  *fp,
                            u32                   *total_tracks,
							void                  *demuxer,
                            sFunctionPtrTable     *ptrFuncPtrTable);

/* Fucntion to get the Basic Information of the passed file. */
EXTERN MP4Err MP4ParserGetStreamInfo (sMP4ParserObjectType *ObjectType,
                               sMP4ParserFileInfo *FillObjectType,
							   u32 track_no, 
                               sMP4ParserReadByteInfo *);

/* Function to parse the compressed raw data to the PM module. */
EXTERN MP4Err MP4ParserReadFile (sMP4ParserObjectType   *ObjectType,
                          sMP4ParserReadByteInfo *ReadInfo,
						  u32                     track_count);

/* Function to get Total Number of Bytes in the file. */
EXTERN MP4Err MP4GetMediaTotalBytes (sMP4ParserObjectType *ObjectType,
                              u32                  *TotalBytes,
                              u32                  *MaximumBuffer,
							  u32                  track_index);

/* Function to seek the all streams in terms of time (in seconds). */
EXTERN MP4Err MP4ParserSeekFile (sMP4ParserObjectType  *ObjectType,
                          float                  seek_time,
						  u32                    total_tracks,
						  sMP4ParserReadByteInfo *ReadInfo);

/* Function to seek only one stream in terms of time (in ms). */
EXTERN MP4Err MP4ParserSeekTrack ( sMP4ParserObjectType  * ObjectType,
                            u32  track_index,
                            s64 *  seek_time_ms,
						    u32 *  sample_number);

/* Function to allocate the memory to hold each access unit*/
EXTERN MP4Err MP4ParserAllocateMemory(  sMP4ParserObjectType *ObjectType,
                                        sMP4ParserReadByteInfo *ReadByteInfo,
							            sMP4ParserFileInfo     *FillObjectType,
							            u32 total_tracks);

/* Function to get the chunk offset */
EXTERN MP4Err MP4ParserGetChunkOffset(sMP4ParserObjectType   *ObjectType,
							   u32                    total_tracks,
							   u32                    *track_id,
							   sMP4ParserReadByteInfo *ReadInfo);

/* Fucntion to compute the Type of Atom. */
EXTERN MP4Err MP4GetTypeOfUserDataFields (u8  *AtomType,
                                   u32 *SetType);

/* Function To store the size of the atom. */
EXTERN MP4Err MP4GetAtomSize (u8  *Atom,
                       u32 *AtomSize);


/* Function to fill data fields of UDTA wrt the type of atom. */
EXTERN MP4Err MP4FilludtaFields (u8   **FieldName,
                          void  *FilePtr,
						  sMP4ParserObjectType *ObjectType);


EXTERN void MP4FreeAtom(void);

EXTERN void MP4ParserFreeAtom(sMP4ParserObjectType *);

/* Function to free non-atom internal memory blocks that can not be freed by MP4FreeAtom().
This is the last API to call for MP4 parser lib.*/
EXTERN void MP4ParserFreeMemory(   sMP4ParserObjectType * MP4PmObjectType,
                            sMP4ParserFileInfo  * MP4PmFileInfo,
                            sMP4ParserReadByteInfo * ReadByteInfo,
                            sMP4ParserUdtaList     * MP4PmUserData,
                            u32 total_tracks);

					   
#ifdef COLDFIRE
/* Function to get the user data list. */
MP4Err MP4ParserGetUserData (sMP4ParserObjectType *ObjectType,
                             sMP4ParserUdtaList   *UserDataInfo,
							 FILE                  *fp);
							 
                             
#else
/* Function to get the user data list. */
EXTERN MP4Err MP4ParserGetUserData (sMP4ParserObjectType *ObjectType,
                             sMP4ParserUdtaList   *UserDataInfo,
							 FILE                  *fp);
							 
                            
#endif

/* Function to get the file information. */
EXTERN MP4Err MP4ParserGetFileInfo (sMP4ParserObjectType *ObjectType,
							 sMP4ParserFileInfo   *FileInfo);

#endif

