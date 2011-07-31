//  Copyright (c) 2010-2011 FFmpegSource Project
//
//  Permission is hereby granted, free of charge, to any person obtaining a copy
//  of this software and associated documentation files (the "Software"), to deal
//  in the Software without restriction, including without limitation the rights
//  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//  copies of the Software, and to permit persons to whom the Software is
//  furnished to do so, subject to the following conditions:
//
//  The above copyright notice and this permission notice shall be included in
//  all copies or substantial portions of the Software.
//
//  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
//  THE SOFTWARE.

#include "avs_common.h"
#include <string.h>
#include <stdio.h>
#include <libswscale/swscale.h>
#include <ffms.h>

#ifdef FFMS_USE_POSTPROC
#include <libpostproc/postprocess.h>
#endif

#if LIBSWSCALE_VERSION_INT >= AV_VERSION_INT(0, 12, 0)
#define USE_AVOPT_SWSCALE 1
#include <libavutil/opt.h>
#endif

int64_t avs_to_ff_cpu_flags( long avisynth_flags )
{
    int64_t flags = 0;
    if( avisynth_flags & AVS_CPU_MMX )
        flags |= FFMS_CPU_CAPS_MMX;
    if( avisynth_flags & AVS_CPU_INTEGER_SSE )
        flags |= FFMS_CPU_CAPS_MMX2;
    if( avisynth_flags & AVS_CPU_3DNOW_EXT )
        flags |= FFMS_CPU_CAPS_3DNOW;
    if( avisynth_flags & AVS_CPU_SSE2 )
        flags |= FFMS_CPU_CAPS_SSE2;
    return flags;
}

int avs_to_pp_cpu_flags( long avisynth_flags )
{
    int flags = 0;
#ifdef FFMS_USE_POSTPROC
    if( avisynth_flags & AVS_CPU_MMX )
        flags |= PP_CPU_CAPS_MMX;
    if( avisynth_flags & AVS_CPU_INTEGER_SSE )
        flags |= PP_CPU_CAPS_MMX2;
    if( avisynth_flags & AVS_CPU_3DNOW_EXT )
        flags |= PP_CPU_CAPS_3DNOW;
#endif // FFMS_USE_POSTPROC
    return flags;
}

int64_t avs_to_sws_cpu_flags( long avisynth_flags )
{
    int64_t flags = 0;
#ifdef SWS_CPU_CAPS_MMX
    if( avisynth_flags & AVS_CPU_MMX )
        flags |= SWS_CPU_CAPS_MMX;
    if( avisynth_flags & AVS_CPU_INTEGER_SSE )
        flags |= SWS_CPU_CAPS_MMX2;
    if( avisynth_flags & AVS_CPU_3DNOW_EXT )
        flags |= SWS_CPU_CAPS_3DNOW;
#ifdef SWS_CPU_CAPS_SSE2
    if( avisynth_flags & AVS_CPU_SSE2 )
        flags |= SWS_CPU_CAPS_SSE2;
#endif // SWS_CPU_CAPS_SSE2
#endif // SWS_CPU_CAPS_MMX
    return flags;
}

enum PixelFormat csp_name_to_pix_fmt_25( const char *csp_name, enum PixelFormat def )
{
    if( !csp_name || !strcmp( csp_name, "" ) )
        return def;
    if( !stricmp( csp_name, "YV12" ) )
        return PIX_FMT_YUV420P;
    if( !stricmp( csp_name, "YUY2" ) )
        return PIX_FMT_YUYV422;
    if( !stricmp( csp_name, "RGB24" ) )
        return PIX_FMT_BGR24;
    if( !stricmp( csp_name, "RGB32" ) )
        return PIX_FMT_BGRA;
    return PIX_FMT_NONE;
}

enum PixelFormat csp_name_to_pix_fmt_26( const char *csp_name, enum PixelFormat def )
{
    enum PixelFormat ret = csp_name_to_pix_fmt_25( csp_name, def );
    if( ret != PIX_FMT_NONE )
        return ret;
    else if( !stricmp( csp_name, "YV16" ) )
        return PIX_FMT_YUV422P;
    else if( !stricmp( csp_name, "YV24" ) )
        return PIX_FMT_YUV444P;
    else if( !stricmp( csp_name, "Y8" ) )
        return PIX_FMT_GRAY8;
    else if( !stricmp( csp_name, "YV411" ) )
        return PIX_FMT_YUV411P;
    return PIX_FMT_NONE;
}

enum PixelFormat vi_to_pix_fmt_25( const AVS_VideoInfo *vi )
{
    // so i can still use this entire function in the 2.6 version
    if( ffms_avs_lib->avs_is_yv12( vi ) )
        return PIX_FMT_YUV420P;
    else if( avs_is_yuy2( vi ) )
        return PIX_FMT_YUYV422;
    else if( avs_is_rgb24( vi ) )
        return PIX_FMT_BGR24;
    else if( avs_is_rgb32( vi ) )
        return PIX_FMT_BGRA;
    else
        return PIX_FMT_NONE;
}

enum PixelFormat vi_to_pix_fmt_26( const AVS_VideoInfo *vi )
{
    enum PixelFormat ret = vi_to_pix_fmt_25( vi );
    if( ret != PIX_FMT_NONE )
        return ret;
    else if( avs_is_yv16( vi ) )
        return PIX_FMT_YUV422P;
    else if( avs_is_yv24( vi ) )
        return PIX_FMT_YUV444P;
    else if( avs_is_y8( vi ) )
        return PIX_FMT_GRAY8;
    else if( avs_is_yv411( vi ) )
        return PIX_FMT_YUV411P;
    else
        return PIX_FMT_NONE;
}

int resizer_name_to_swscale_name( const char *resizer )
{
    if( !stricmp( resizer, "FAST_BILINEAR" ) )
        return SWS_FAST_BILINEAR;
    if( !stricmp( resizer, "BILINEAR" ) )
        return SWS_BILINEAR;
    if( !stricmp( resizer, "BICUBIC" ) )
        return SWS_BICUBIC;
    if( !stricmp( resizer, "X" ) )
        return SWS_X;
    if( !stricmp( resizer, "POINT" ) )
        return SWS_POINT;
    if( !stricmp( resizer, "AREA" ) )
        return SWS_AREA;
    if( !stricmp( resizer, "BICUBLIN" ) )
        return SWS_BICUBLIN;
    if( !stricmp( resizer, "GAUSS" ) )
        return SWS_GAUSS;
    if( !stricmp( resizer, "SINC" ) )
        return SWS_SINC;
    if( !stricmp( resizer, "LANCZOS" ) )
        return SWS_LANCZOS;
    if( !stricmp( resizer, "SPLINE" ) )
        return SWS_SPLINE;
    return 0;
}

void fill_avs_frame_data( AVS_VideoFrame *frm, uint8_t *ptr[3], int stride[3], char read, char vertical_flip )
{
    const static int plane[3] = { AVS_PLANAR_Y, AVS_PLANAR_U, AVS_PLANAR_V };
    uint8_t *(*p_get_ptr)( AVS_VideoFrame *frm, int plane );
    p_get_ptr = read ? avs_get_read_ptr_p : avs_get_write_ptr_p; /* this causes a compiler warning - ignore it */
    for( int i = 0; i < 3; i++ )
    {
        if( vertical_flip )
        {
            stride[i] = - avs_get_pitch_p( frm, plane[i] );
            ptr[i] = p_get_ptr( frm, plane[i] )
                 - stride[i] * (ffms_avs_lib->avs_get_height_p( frm, plane[i] ) - 1);
        }
        else
        {
            ptr[i] = p_get_ptr( frm, plane[i] );
            stride[i] = avs_get_pitch_p( frm, plane[i] );
        }
    }
}

char *ffms_avs_sprintf( const char *format, ... )
{
    char buf[512]; /* effective message max length */
    va_list list;
    va_start( list, format );
    vsnprintf( buf, sizeof(buf), format, list );
    va_end( list );
    return strdup( buf );
}

static int handle_jpeg( int *format )
{
	switch( *format )
    {
	case PIX_FMT_YUVJ420P: *format = PIX_FMT_YUV420P; return 1;
	case PIX_FMT_YUVJ422P: *format = PIX_FMT_YUV422P; return 1;
	case PIX_FMT_YUVJ444P: *format = PIX_FMT_YUV444P; return 1;
	case PIX_FMT_YUVJ440P: *format = PIX_FMT_YUV440P; return 1;
	default:                                          return 0;
	}
}

struct SwsContext *ffms_sws_get_context( int src_width, int src_height, int src_pix_fmt, int dst_width, int dst_height, int dst_pix_fmt, int64_t flags, int csp )
{
#if USE_AVOPT_SWSCALE
    struct SwsContext *ctx = sws_alloc_context();
    if( !ctx )
        return NULL;
    if( csp == -1 )
        csp = (src_width > 1024 || src_height >= 600) ? SWS_CS_ITU709 : SWS_CS_DEFAULT;
    int src_range = handle_jpeg( &src_pix_fmt );
    int dst_range = handle_jpeg( &dst_pix_fmt );

    av_set_int( ctx, "sws_flags", flags );
    av_set_int( ctx, "srcw", src_width );
    av_set_int( ctx, "srch", src_height );
    av_set_int( ctx, "dstw", dst_width );
    av_set_int( ctx, "dsth", dst_height );
    av_set_int( ctx, "src_range", src_range );
    av_set_int( ctx, "dst_range", dst_range );
    av_set_int( ctx, "src_format", src_pix_fmt );
    av_set_int( ctx, "dst_format", dst_pix_fmt );

    sws_setColorspaceDetails( ctx, sws_getCoefficients( csp ), src_range, sws_getCoefficients( csp ), dst_range, 0, 1<<16, 1<<16 );
    if( sws_init_context( ctx, NULL, NULL ) < 0 )
    {
        sws_freeContext( ctx );
        return NULL;
    }

    return ctx;
#else
    return sws_getContext( src_width, src_height, src_pix_fmt, dst_width, dst_height, dst_pix_fmt, flags, NULL, NULL, NULL );
#endif
}
