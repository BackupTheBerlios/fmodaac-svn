/*===============================================================================================
 CODEC_AAC.DLL

 ACC Webstream decoder by MoX

===============================================================================================*/


//#include <stdio.h>
//#include <io.h>
//#include <stdlib.h>
#include "neaacdec.h"
#include "fmod.h"
#include <string.h>
//#include "fmod_errors.h"

#ifdef WIN32
#include <malloc.h>
#else
typedef __int64_t __int64;
#endif

#ifndef MAKEFOURCC
#ifdef _BIG_ENDIAN
#define MAKEFOURCC(ch0, ch1, ch2, ch3)                              \
                ((DWORD)(BYTE)(ch3) | ((DWORD)(BYTE)(ch2) << 8) |   \
                ((DWORD)(BYTE)(ch1) << 16) | ((DWORD)(BYTE)(ch0) << 24 ))
#else
#define MAKEFOURCC(ch0, ch1, ch2, ch3)                              \
                ((DWORD)(BYTE)(ch0) | ((DWORD)(BYTE)(ch1) << 8) |   \
                ((DWORD)(BYTE)(ch2) << 16) | ((DWORD)(BYTE)(ch3) << 24 ))
#endif
#endif

#define MAX_CHANNELS 2
#define BUFFER_SIZE FAAD_MIN_STREAMSIZE*MAX_CHANNELS

typedef unsigned long       DWORD;
typedef int                 BOOL;
typedef unsigned char       BYTE;
typedef unsigned short      WORD;
typedef struct {
	unsigned int initbytes;
	DWORD sr;
	BYTE nch;
	NeAACDecHandle neaac;
	unsigned char fbuf[BUFFER_SIZE];
	DWORD fbuflen;
} info;

#ifdef WIN32
//#pragma comment(linker,"/ignore:4078")
//#pragma comment(linker,"/merge:.rdata=.data")
//#pragma comment(linker,"/merge:.text=.data")
//#pragma comment(linker,"/OPT:WIN98")
#endif

FMOD_RESULT F_CALLBACK aacopen(FMOD_CODEC_STATE *codec, FMOD_MODE usermode, FMOD_CREATESOUNDEXINFO *userexinfo);
FMOD_RESULT F_CALLBACK aacclose(FMOD_CODEC_STATE *codec);
FMOD_RESULT F_CALLBACK aacread(FMOD_CODEC_STATE *codec, void *buffer, unsigned int size, unsigned int *read);
FMOD_RESULT F_CALLBACK aacsetposition(FMOD_CODEC_STATE *codec, int subsound, unsigned int position, FMOD_TIMEUNIT postype);

FMOD_CODEC_DESCRIPTION aaccodec =
{
    "FMOD AAC Codec",   // Name.
    0x00010000,                         // Version 0xAAAABBBB   A = major, B = minor.
    0,                                  // Don't force everything using this codec to be a stream,
	FMOD_TIMEUNIT_PCMBYTES,             // The time format we would like to accept into setposition/getposition.
    &aacopen,                           // Open callback.
    &aacclose,                          // Close callback.
    &aacread,                           // Read callback.
    0,                                  // Getlength callback.  (If not specified FMOD return the length in FMOD_TIMEUNIT_PCM, FMOD_TIMEUNIT_MS or FMOD_TIMEUNIT_PCMBYTES units based on the lengthpcm member of the FMOD_CODEC structure).
    &aacsetposition,                    // Setposition callback.
    0,                                  // Getposition callback. (only used for timeunit types that are not FMOD_TIMEUNIT_PCM, FMOD_TIMEUNIT_MS and FMOD_TIMEUNIT_PCMBYTES).
    0                                   // Sound create callback (don't need it)
};


static int get_AAC_format(info* x)
{
	unsigned int a=0;
	do {
#if 0
		if (*(DWORD*)(x->fbuf+a)==MAKEFOURCC('A','D','I','F')) { // "ADIF" signature
			x->initbytes+=a;
			return -1; //Not supported
		}
#endif
		if (x->fbuf[a]==0xff && (x->fbuf[a+1]&0xf6)==0xf0 && ((x->fbuf[a+2]&0x3C)>>2)<12) { // ADTS header syncword
			x->initbytes+=a;
			return 0;
		}
	} while (++a<x->fbuflen-4);
	return -1;
}

/*
FMODGetCodecDescription is mandatory for every fmod plugin.  This is the symbol the registerplugin function searches for.
Must be declared with F_API to make it export as stdcall.
MUST BE EXTERN'ED AS C!  C++ functions will be mangled incorrectly and not load in fmod.
*/


#ifdef __cplusplus
extern "C" {
#endif

F_DECLSPEC F_DLLEXPORT FMOD_CODEC_DESCRIPTION * F_API FMODGetCodecDescription()
{
    return &aaccodec;
}

#ifdef __cplusplus
}
#endif

static FMOD_CODEC_WAVEFORMAT    aacwaveformat;

/*
    The actual codec code.

    Note that the callbacks uses FMOD's supplied file system callbacks.

    This is important as even though you might want to open the file yourself, you would lose the following benefits.
    1. Automatic support of memory files, CDDA based files, and HTTP/TCPIP based files.
    2. "fileoffset" / "length" support when user calls System::createSound with FMOD_CREATESOUNDEXINFO structure.
    3. Buffered file access.
    FMOD files are high level abstracts that support all sorts of 'file', they are not just disk file handles.
    If you want FMOD to use your own filesystem (and potentially lose the above benefits) use System::setFileSystem.
*/

FMOD_RESULT F_CALLBACK aacopen(FMOD_CODEC_STATE *codec, FMOD_MODE usermode, FMOD_CREATESOUNDEXINFO *userexinfo)
{
	if(!codec) return FMOD_ERR_INTERNAL;

    aacwaveformat.channels     = 2;
    aacwaveformat.format       = FMOD_SOUND_FORMAT_PCM16;
    aacwaveformat.frequency    = 44100;
    aacwaveformat.blockalign   = 4096*2;//aacwaveformat.channels * 2;          /* 2 = 16bit pcm */
	aacwaveformat.lengthpcm    = 0xffffffff;// codec->filesize;// / aacwaveformat.blockalign;   /* bytes converted to PCM samples */;

    codec->numsubsounds = 0;                    /* number of 'subsounds' in this sound.  For most codecs this is 0, only multi sound codecs such as FSB or CDDA have subsounds. */
    codec->waveformat   = &aacwaveformat;

	unsigned int readBytes = 0;
	FMOD_RESULT r;
	info* x = new info;
	if (!x) return FMOD_ERR_INTERNAL;
	memset(x,0,sizeof(info));

	codec->plugindata = x;   /* user data value */
	
	r = codec->fileread(codec->filehandle, x->fbuf, BUFFER_SIZE, &readBytes,0);

	if (r != FMOD_OK || readBytes == 0)
		return FMOD_ERR_FILE_EOF;

	x->fbuflen += readBytes;
	x->initbytes = 0;
	if(get_AAC_format(x) == -1)
		return FMOD_ERR_FILE_BAD;
	
	if(! (x->neaac = NeAACDecOpen()))
		return FMOD_ERR_INTERNAL;

	if (x->initbytes < 0 || x->initbytes > BUFFER_SIZE)
		return FMOD_ERR_INTERNAL;

	memmove(x->fbuf, x->fbuf + x->initbytes, BUFFER_SIZE - x->initbytes);
	x->fbuflen -= x->initbytes;

	r = codec->fileread(codec->filehandle, x->fbuf + x->fbuflen, BUFFER_SIZE - x->fbuflen, &readBytes,0);
	if (r != FMOD_OK)
		return FMOD_ERR_FILE_EOF;

	x->fbuflen += readBytes;

	long byt = NeAACDecInit(x->neaac, x->fbuf, x->fbuflen, &x->sr, &x->nch);
	if (byt < 0)
		return FMOD_ERR_INTERNAL;
	if (byt > 0) {
		memmove(x->fbuf, x->fbuf + byt, BUFFER_SIZE - byt);
		x->fbuflen -= byt;
	}

	NeAACDecConfigurationPtr config = NeAACDecGetCurrentConfiguration(x->neaac);
	config->outputFormat = FAAD_FMT_16BIT;
	config->defSampleRate = 44100;
	NeAACDecSetConfiguration(x->neaac, config);
    return FMOD_OK;
}

FMOD_RESULT F_CALLBACK aacclose(FMOD_CODEC_STATE *codec)
{
	info* x = (info*)codec->plugindata;
	NeAACDecClose(x->neaac);
	delete(x);
	
    return FMOD_OK;
}


FMOD_RESULT F_CALLBACK aacread(FMOD_CODEC_STATE *codec, void *buffer, unsigned int size, unsigned int *read)
{
	if(size < 4096*2) {
		memset(buffer, 0, size);
		*read = size;
		return FMOD_OK;
	}
	info* x = (info*)codec->plugindata;
	if(!x || !read)
		return FMOD_ERR_INTERNAL;

	void* buf = NULL;
	unsigned int buflen = 0;
	unsigned int r;
	
	NeAACDecFrameInfo info;

	bool eof = false;
	while(buflen < size || eof){
		do {
			r = 0;
			FMOD_RESULT res;
			res = codec->fileread(codec->filehandle, x->fbuf + x->fbuflen, BUFFER_SIZE - x->fbuflen, &r, 0);
			if (res == FMOD_ERR_FILE_EOF)
				eof = true;
			else if(res != FMOD_OK)
				return FMOD_ERR_INTERNAL;

			x->fbuflen += r;
			buf = NeAACDecDecode(x->neaac, &info, x->fbuf, x->fbuflen);
			x->fbuflen -= info.bytesconsumed;
			memmove(x->fbuf, x->fbuf + info.bytesconsumed, x->fbuflen); // shift remaining data to start of buffer
			if (info.error != 0) return FMOD_ERR_FILE_BAD;
		} while (!info.samples || eof);
		if(info.samples != 0) {
			if (!buf)
				return FMOD_ERR_INTERNAL;
			memcpy((unsigned char*)buffer + buflen, buf, info.samples*2);
			buflen += info.samples*2;
		}
	}
	*read = buflen;
	if(eof) return FMOD_ERR_FILE_EOF;
	return FMOD_OK;
}

FMOD_RESULT F_CALLBACK aacsetposition(FMOD_CODEC_STATE *codec, int subsound, unsigned int position, FMOD_TIMEUNIT postype)
{
	return FMOD_OK; FMOD_ERR_FILE_COULDNOTSEEK;
}

