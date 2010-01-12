
 /************************************************************************
  * Copyright 2005-2006 by Freescale Semiconductor, Inc.
  * All modifications are confidential and proprietary information
  * of Freescale Semiconductor, Inc. ALL RIGHTS RESERVED.
  ************************************************************************/

/*
This software module was originally developed by Apple Computer, Inc.
in the course of development of MPEG-4. 
This software module is an implementation of a part of one or 
more MPEG-4 tools as specified by MPEG-4. 
ISO/IEC gives users of MPEG-4 free license to this
software module or modifications thereof for use in hardware 
or software products claiming conformance to MPEG-4.
Those intending to use this software module in hardware or software
products are advised that its use may infringe existing patents.
The original developer of this software module and his/her company,
the subsequent editors and their companies, and ISO/IEC have no
liability for use of this software module or modifications thereof
in an implementation.
Copyright is not released for non MPEG-4 conforming
products. Apple Computer, Inc. retains full right to use the code for its own
purpose, assign or donate the code to a third party and to
inhibit third parties from using the code for non 
MPEG-4 conforming products.
This copyright notice must be included in all copies or
derivative works. Copyright (c) 1999.
*/
/*
	$Id: MP4OSMacros.h,v 1.7 2000/01/24 01:45:06 mc Exp $
*/

#ifndef INCLUDED_MP4OSMACROS_H
#define INCLUDED_MP4OSMACROS_H

#include <assert.h>
#include <stdio.h>
#include <malloc.h>

#ifdef __WINCE
#include <windows.h> //Required for definition on LONGLONG
#else
#ifndef WIN32
typedef long long LONGLONG; /* Add LONGLONG definition for LINUX */
#endif
#endif 

#ifndef TEST_RETURN
#define TEST_RETURN(err)
/*#define TEST_RETURN(err) assert((err)==0)*/
#endif

#define MP4_EXTERN(v) extern v

#if 1
#ifdef WIN32
typedef  unsigned __int64 u64;
typedef  __int64 s64;
#else
typedef  long long u64;
typedef  long long s64;
#endif

typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;
typedef int s32;
typedef short s16;
typedef char s8;
#else

typedef uint64_t u64;
typedef int64_t s64;
typedef uint32_t u32;
typedef int32_t s32;
typedef uint16_t u16;
typedef int16_t s16;
typedef uint8_t u8;
typedef int8_t s8;

#endif

#ifndef TRUE
    #define TRUE 1    
#endif

#ifndef FALSE
    #define FALSE 0    
#endif

#define MP4_FOUR_CHAR_CODE( a, b, c, d ) (((a)<<24)|((b)<<16)|((c)<<8)|(d))

/****************************************************************************
    memory operation routines                                             
****************************************************************************/

#ifndef MP4_MEM_DEBUG   
    /* default, no memory debug feature.MP4 never alloc/free memory by itself */

    /* Local Fucntion for calloc. */
    void* MP4LocalCalloc (u32 TotalNumber, 
    					u32 TotalSize);

    /* Local Fucntion for malloc. */
    void* MP4LocalMalloc (u32 TotalSize);

    /* Local Fucntion for Re Alloc. */
    void* MP4LocalReAlloc (void *MemoryBlock, 
    					 u32   TotalSize);

    /* Local Fucntion to Free the Memory. */
    void MP4LocalFree (void *MemoryBlock);
    
#else
    #ifdef MP4_MEM_DEBUG_SELF
        /* self memory debug */
        extern void *mm_calloc(int nobj, int size, const char *filename, int line);
        extern void *mm_malloc(int size, const char *filename, int line);
        extern void *mm_realloc(void *ptr, int size, const char *filename, int line);
        extern void mm_free(void *ptr, const char *filename, int line);

        #define MP4LocalMalloc(size)    mm_malloc((size), __FILE__ , __LINE__ )
        #define MP4LocalCalloc(nobj,size)   mm_calloc((nobj),(size), __FILE__ , __LINE__ )
        #define MP4LocalReAlloc(ptr,size)   mm_realloc((ptr), (size), __FILE__ , __LINE__ )
        #define MP4LocalFree(ptr)   mm_free((ptr), __FILE__ , __LINE__ )
        
    #else
        /* MP4_MEM_DEBUG_OTHER is defined, for 3-party memory debug tool, eg. valgrind */
        #define MP4LocalCalloc  calloc
        #define MP4LocalMalloc  malloc
        #define MP4LocalReAlloc realloc
        #define MP4LocalFree    free

    #endif
#endif


/****************************************************************************
    file/IO operation routines                                             
****************************************************************************/
#ifdef COLDFIRE
void * MP4LocalFileOpen (const u8 *FileName, 
						 const u8 *ModeToOpen);
#else
/* Local Fucntion for Opening the file. */ 
FILE * MP4LocalFileOpen (const u8 *FileName, 
						 const u8 *ModeToOpen);
#endif
#ifdef COLDFIRE
/* Local fucntion to close the file. */
u32 MP4LocalFileClose (void *StreamToClose);
#else
/* Local fucntion to close the file. */
u32 MP4LocalFileClose (FILE *StreamToClose);
#endif

#ifdef COLDFIRE
/* Local fucntion to write the file. */
u32 MP4LocalWriteInFile (const void *SourceBuffer,
						 u32         TotalSize,
						 u32         NumberOfTimes,
						 void       *FilePtr);
#else
/* Local fucntion to write the file. */
u32 MP4LocalWriteInFile (const void *SourceBuffer,
						 u32         TotalSize,
						 u32         NumberOfTimes,
						 FILE       *DestStream);
#endif

/* Local function to get the file size. */
LONGLONG MP4LocalFileSize(FILE *, void *);
/* Local function to seek to a file. */
void MP4LocalSeekFile(FILE *, int, int, void *);
/* Local function to read from a file. return value- actual byte read. 0 for EOF or other error.*/
u32  MP4LocalReadFile(void *, u32, u32, FILE *, void *);
LONGLONG MP4LocalGetCurrentFilePos(FILE *,void *);



#endif

