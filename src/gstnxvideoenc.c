/* GStreamer
 * Copyright (C) 2016 Biela.Jo <doriya@nexell.co.kr>
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
 * Free Software Foundation, Inc., 51 Franklin Street, Suite 500,
 * Boston, MA 02110-1335, USA.
 */
/**
 * SECTION:element-gstnxvideoenc
 *
 * The nxvideoenc element does FIXME stuff.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v fakesrc ! nxvideoenc ! FIXME ! fakesink
 * ]|
 * FIXME Describe what the pipeline does.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideoencoder.h>

#include <videodev2_nxp_media.h>
#include <linux/videodev2.h>

#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm/drm_fourcc.h>
#include <nexell/nexell_drm.h>
#include <mm_types.h>

#include <nx_video_alloc.h>

#include "gstnxvideoenc.h"

GST_DEBUG_CATEGORY_STATIC (gst_nxvideoenc_debug_category);
#define GST_CAT_DEFAULT gst_nxvideoenc_debug_category

/* prototypes */

static void gst_nxvideoenc_set_property (GObject * object,
		guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_nxvideoenc_get_property (GObject * object,
		guint property_id, GValue * value, GParamSpec * pspec);
static void gst_nxvideoenc_finalize (GObject * object);

static gboolean gst_nxvideoenc_start (GstVideoEncoder *encoder);
static gboolean gst_nxvideoenc_stop (GstVideoEncoder *encoder);
static gboolean gst_nxvideoenc_set_format (GstVideoEncoder *encoder, GstVideoCodecState *state);
static GstFlowReturn gst_nxvideoenc_handle_frame (GstVideoEncoder *encoder, GstVideoCodecFrame *frame);
static GstFlowReturn gst_nxvideoenc_finish (GstVideoEncoder *encoder);

#define MAX_IMAGE_WIDTH			1920
#define MAX_IMAGE_HEIGHT		1088

#define DEFAULT_CODEC           V4L2_PIX_FMT_H264
#define DEFAULT_BITRATE         1000 * 1024
#define DEFAULT_IMAGE_FORMAT	V4L2_PIX_FMT_YUV420M

enum
{
	PROP_0,

	PROP_CODEC,		// H264, H263, MPEG
	PROP_WIDTH,
	PROP_HEIGHT,
	PROP_FPS_N,
	PROP_FPS_D,
	PROP_GOP,
	PROP_BITRATE,
};

/* pad templates */

/* FIXME: add/remove formats you can handle */
static GstStaticPadTemplate gst_nxvideoenc_sink_template =
	GST_STATIC_PAD_TEMPLATE( "sink",
		GST_PAD_SINK,
		GST_PAD_ALWAYS,
		GST_STATIC_CAPS(
			"video/x-raw, "
			"format =(string) I420"
		)
	);

static GstStaticPadTemplate gst_nxvideoenc_src_template =
	GST_STATIC_PAD_TEMPLATE( "src",
		GST_PAD_SRC,
		GST_PAD_ALWAYS,
		GST_STATIC_CAPS(
			"video/x-h264, "
			"width = (int) [ 64, 1920 ], "
			"height = (int) [ 64, 1088 ], "
			"framerate=(fraction)[ 0/1, 30/1 ], "
			"stream-format=(string)byte-stream, "
			"alignment=(string)au; "

			"video/x-h263, "
			"width = (int) [ 64, 1920 ], "
			"height = (int) [ 64, 1088 ], "
			"framerate=(fraction)[ 0/1, 30/1 ]; "

			"video/mpeg, "
			"width = (int) [ 64, 1920 ], "
			"height = (int) [ 64, 1088 ], "
			"framerate = (fraction) [ 0/1, 30/1 ], "
			"mpegversion = (int) 4, "
			"systemstream = (boolean) FALSE; "
		)
	);

/* class initialization */

G_DEFINE_TYPE_WITH_CODE( GstNxvideoenc, gst_nxvideoenc, GST_TYPE_VIDEO_ENCODER,
	GST_DEBUG_CATEGORY_INIT (gst_nxvideoenc_debug_category, "nxvideoenc", 0,
	"debug category for nxvideoenc element") );

static void
gst_nxvideoenc_class_init( GstNxvideoencClass * klass )
{
	GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
	GstVideoEncoderClass *video_encoder_class = GST_VIDEO_ENCODER_CLASS (klass);

	/* Setting up pads and setting metadata should be moved to
		 base_class_init if you intend to subclass this class. */

	gst_element_class_add_pad_template( GST_ELEMENT_CLASS(klass),
			gst_static_pad_template_get(&gst_nxvideoenc_sink_template) );

	gst_element_class_add_pad_template( GST_ELEMENT_CLASS(klass),
			gst_static_pad_template_get(&gst_nxvideoenc_src_template) );

	gst_element_class_set_static_metadata (GST_ELEMENT_CLASS(klass),
		"S5P6818 H/W Video Encoder",
		"Codec/Encoder/Video",
		"Nexell H/W Video Encoder for S5P6818",
		"Biela.Jo <doriya@nexell.co.kr>"
	);

	gobject_class->set_property       = gst_nxvideoenc_set_property;
	gobject_class->get_property       = gst_nxvideoenc_get_property;
	gobject_class->finalize           = gst_nxvideoenc_finalize;

	video_encoder_class->start        = GST_DEBUG_FUNCPTR( gst_nxvideoenc_start );
	video_encoder_class->stop         = GST_DEBUG_FUNCPTR( gst_nxvideoenc_stop );
	video_encoder_class->set_format   = GST_DEBUG_FUNCPTR( gst_nxvideoenc_set_format );
	video_encoder_class->handle_frame = GST_DEBUG_FUNCPTR( gst_nxvideoenc_handle_frame );
	video_encoder_class->finish       = GST_DEBUG_FUNCPTR( gst_nxvideoenc_finish );

	g_object_class_install_property( G_OBJECT_CLASS (klass), PROP_CODEC,
		g_param_spec_string ("codec", "codec",
			"codec type",
			"h264",
			(GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)
		)
	);

	g_object_class_install_property( G_OBJECT_CLASS (klass), PROP_WIDTH,
		g_param_spec_uint ("width", "width",
			"image width",
			0, MAX_IMAGE_WIDTH, 0,
			(GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)
		)
	);

	g_object_class_install_property( G_OBJECT_CLASS (klass), PROP_HEIGHT,
		g_param_spec_uint ("height", "height",
			"image height",
			0, MAX_IMAGE_HEIGHT, 0,
			(GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)
		)
	);

	g_object_class_install_property( G_OBJECT_CLASS (klass), PROP_FPS_N,
		g_param_spec_uint ("fps-n", "fps-n",
			"fps numerator",
			0, G_MAXUINT, 0,
			(GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)
		)
	);

	g_object_class_install_property( G_OBJECT_CLASS (klass), PROP_FPS_D,
		g_param_spec_uint ("fps-d", "fps-d",
			"fps denominator",
			0, G_MAXUINT, 0,
			(GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)
		)
	);

	g_object_class_install_property( G_OBJECT_CLASS (klass), PROP_GOP,
		g_param_spec_uint ("gop", "gop",
			"gop ( group of pictures )",
			0, G_MAXUINT, 0,
			(GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)
		)
	);

	g_object_class_install_property( G_OBJECT_CLASS (klass), PROP_BITRATE,
		g_param_spec_uint ("bitrate", "Bitrate",
			"bitrate ( bit per second )",
			1, G_MAXUINT, DEFAULT_BITRATE,
			(GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)
		)
	);
}

static void
gst_nxvideoenc_init( GstNxvideoenc *nxvideoenc )
{
	gint i;

	nxvideoenc->input_state        = NULL;
	nxvideoenc->init               = FALSE;
	nxvideoenc->buf_index          = 0;
	nxvideoenc->buffer_type        = -1;
	nxvideoenc->drm_fd             = -1;

	for( i = 0; i < MAX_INPUT_BUFFER; i++ )
	{
		nxvideoenc->inbuf[i] = NULL;
	}

	nxvideoenc->enc                = NULL;
	nxvideoenc->codec              = DEFAULT_CODEC;
	nxvideoenc->width              = 0;
	nxvideoenc->height             = 0;
	nxvideoenc->keyFrmInterval     = 0;
	nxvideoenc->fpsNum             = 0;
	nxvideoenc->fpsDen             = 0;
	nxvideoenc->profile            = 0;
	nxvideoenc->bitrate            = DEFAULT_BITRATE;
	nxvideoenc->maximumQp          = 0;
	nxvideoenc->disableSkip        = 0;
	nxvideoenc->RCDelay            = 0;
	nxvideoenc->rcVbvSize          = 0;
	nxvideoenc->gammaFactor        = 0;
	nxvideoenc->initialQp          = 0;
	nxvideoenc->numIntraRefreshMbs = 0;
	nxvideoenc->searchRange        = 0;
	nxvideoenc->enableAUDelimiter  = 0;
	nxvideoenc->imgFormat          = DEFAULT_IMAGE_FORMAT;
	nxvideoenc->imgBufferNum       = MAX_ALLOC_BUFFER;
	nxvideoenc->rotAngle           = 0;
	nxvideoenc->mirDirection       = 0;
	nxvideoenc->jpgQuality         = 0;
}

gint
get_v4l2_codec( const gchar *codec )
{
	gint v4l2_codec = -1;
	if( !g_strcmp0( codec, "video/x-h264" ) ) v4l2_codec = V4L2_PIX_FMT_H264;
	else if( !g_strcmp0( codec, "video/x-h263") ) v4l2_codec = V4L2_PIX_FMT_H263;
	else if( !g_strcmp0( codec, "video/mpeg") ) v4l2_codec = V4L2_PIX_FMT_MPEG4;

	return v4l2_codec;
}

const gchar*
get_codec_name( gint codec )
{
	if( codec == V4L2_PIX_FMT_H264 )        return "video/x-h264";
	else if( codec == V4L2_PIX_FMT_H263 )   return "video/x-h263";
	else if( codec == V4L2_PIX_FMT_MPEG4 )  return "video/mpeg";

	return NULL;
}

void
gst_nxvideoenc_set_property( GObject * object, guint property_id,
		const GValue * value, GParamSpec * pspec )
{
	GstNxvideoenc *nxvideoenc = GST_NXVIDEOENC( object );

	GST_DEBUG_OBJECT( nxvideoenc, "set_property" );

	switch( property_id )
	{
		case PROP_CODEC:
			nxvideoenc->codec = get_v4l2_codec( g_value_get_string( value ) );
			break;

		case PROP_WIDTH:
			nxvideoenc->width = g_value_get_uint( value );
			break;

		case PROP_HEIGHT:
			nxvideoenc->height = g_value_get_uint( value );
			break;

		case PROP_FPS_N:
			nxvideoenc->fpsNum = g_value_get_uint( value );
			break;

		case PROP_FPS_D:
			nxvideoenc->fpsDen = g_value_get_uint( value );
			break;

		case PROP_GOP:
			nxvideoenc->keyFrmInterval = g_value_get_uint( value );
			break;

		case PROP_BITRATE:
			nxvideoenc->bitrate = g_value_get_uint( value );
			break;

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID( object, property_id, pspec );
			break;
	}
}

void
gst_nxvideoenc_get_property( GObject * object, guint property_id,
		GValue * value, GParamSpec * pspec )
{
	GstNxvideoenc *nxvideoenc = GST_NXVIDEOENC( object );

	GST_DEBUG_OBJECT( nxvideoenc, "get_property" );

	switch( property_id )
	{
		case PROP_CODEC:
			g_value_set_string( value, get_codec_name(nxvideoenc->codec) );
			break;

		case PROP_WIDTH:
			g_value_set_uint( value, nxvideoenc->width );
			break;

		case PROP_HEIGHT:
			g_value_set_uint( value, nxvideoenc->height );
			break;

		case PROP_FPS_N:
			g_value_set_uint( value, nxvideoenc->fpsNum );
			break;;

		case PROP_FPS_D:
			g_value_set_uint( value, nxvideoenc->fpsDen );
			break;

		case PROP_GOP:
			g_value_set_uint( value, nxvideoenc->keyFrmInterval );
			break;

		case PROP_BITRATE:
			g_value_set_uint( value, nxvideoenc->bitrate );
			break;

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID( object, property_id, pspec );
			break;
	}
}

void
gst_nxvideoenc_finalize( GObject * object )
{
	GstNxvideoenc *nxvideoenc = GST_NXVIDEOENC (object);

	GST_DEBUG_OBJECT (nxvideoenc, "finalize");

	/* clean up object here */

	G_OBJECT_CLASS (gst_nxvideoenc_parent_class)->finalize (object);
}

static gboolean
gst_nxvideoenc_start( GstVideoEncoder *encoder )
{
	GstNxvideoenc *nxvideoenc = GST_NXVIDEOENC (encoder);

	GST_DEBUG_OBJECT(nxvideoenc, "start");

	return TRUE;
}

static gboolean
gst_nxvideoenc_stop( GstVideoEncoder *encoder )
{
	GstNxvideoenc *nxvideoenc = GST_NXVIDEOENC (encoder);
	gint i, j;

	GST_DEBUG_OBJECT(nxvideoenc, "stop");

	if( MM_VIDEO_BUFFER_TYPE_GEM != nxvideoenc->buffer_type )
	{
		for( i = 0 ; i < MAX_ALLOC_BUFFER; i++ )
		{
			if( NULL != nxvideoenc->inbuf[i] )
			{
				NX_FreeVideoMemory( nxvideoenc->inbuf[i] );
				nxvideoenc->inbuf[i] = NULL;
			}
		}
	}
	else
	{
		for( i = 0; i < MAX_INPUT_BUFFER; i++ )
		{
			if( NULL != nxvideoenc->inbuf[i] )
			{
				for( j = 0; j < nxvideoenc->inbuf[i]->planes; j++ )
				{
					close( nxvideoenc->inbuf[i]->gemFd[j] );
					close( nxvideoenc->inbuf[i]->dmaFd[j] );
				}

				free( nxvideoenc->inbuf[i] );
				nxvideoenc->inbuf[i] = NULL;
			}
		}
	}

	if( NULL != nxvideoenc->enc )
	{
		NX_V4l2EncClose( nxvideoenc->enc );
		nxvideoenc->enc = NULL;
	}

	if( NULL != nxvideoenc->input_state )
	{
		gst_video_codec_state_unref( nxvideoenc->input_state );
		nxvideoenc->input_state = NULL;
	}

	if( 0 <= nxvideoenc->drm_fd );
	{
		close( nxvideoenc->drm_fd );
		nxvideoenc->drm_fd = -1;
	}

	return TRUE;
}

static gboolean
gst_nxvideoenc_set_format( GstVideoEncoder *encoder, GstVideoCodecState *state )
{
	GstNxvideoenc *nxvideoenc = GST_NXVIDEOENC (encoder);
	GstCaps *caps = NULL;
	GstCaps *outcaps = NULL;
	GstStructure *structure = NULL;
	GstStructure *outstructure = NULL;
	gint i;

	guint caps_num;
	gint ret = FALSE;

	GstVideoCodecState *output_state;

	GST_DEBUG_OBJECT(nxvideoenc, "set_format");

	if( nxvideoenc->input_state )
	{
		gst_video_codec_state_unref( nxvideoenc->input_state );
	}

	nxvideoenc->input_state = gst_video_codec_state_ref( state );

	nxvideoenc->width  = GST_VIDEO_INFO_WIDTH( &state->info );
	nxvideoenc->height = GST_VIDEO_INFO_HEIGHT( &state->info );
	nxvideoenc->fpsNum = GST_VIDEO_INFO_FPS_N( &state->info );
	nxvideoenc->fpsDen = GST_VIDEO_INFO_FPS_D( &state->info );

	structure = gst_caps_get_structure( state->caps, 0 );
	gst_structure_get_int( structure, "buffer-type", &nxvideoenc->buffer_type );

	if( nxvideoenc->width <= 1 || nxvideoenc->height <= 1 )
	{
		GST_ERROR("Fail, Invalid Width( %d ), height( %d )\n", nxvideoenc->width, nxvideoenc->height );
		return FALSE;
	}

	nxvideoenc->drm_fd = open( "/dev/dri/card0", O_RDWR );

	if( MM_VIDEO_BUFFER_TYPE_GEM != nxvideoenc->buffer_type )
	{
		for( i = 0;  i < MAX_ALLOC_BUFFER; i++ )
		{
			nxvideoenc->inbuf[i] = NX_AllocateVideoMemory(
				nxvideoenc->width,
				nxvideoenc->height,
				(nxvideoenc->imgFormat == V4L2_PIX_FMT_YUV420M) ? 3 : 2,
				DRM_FORMAT_YUV420,
				4096
			);

			if( NULL == nxvideoenc->inbuf[i] )
			{
				GST_ERROR("Fail, NX_AllocateVideoMemory().\n");
				return FALSE;
			}

			if( 0 != NX_MapVideoMemory( nxvideoenc->inbuf[i] ) )
			{
				GST_ERROR("Fail, NX_MapVideoMemory().\n");
				return FALSE;
			}
		}
		nxvideoenc->imgBufferNum = MAX_ALLOC_BUFFER;
	}
	else
	{
		nxvideoenc->imgBufferNum = MAX_INPUT_BUFFER;
	}

	//
	// Configuration Output Caps
	//
	caps = gst_static_pad_template_get_caps( &gst_nxvideoenc_src_template );
	for( caps_num = 0; caps_num < gst_caps_get_size( caps ); caps_num++ )
	{
		outstructure = gst_caps_get_structure( caps, caps_num );
		if( outstructure )
		{
			if( !strcmp( get_codec_name(nxvideoenc->codec), gst_structure_get_name(outstructure)) )
			{
				outcaps = gst_caps_copy_nth( caps, caps_num );
				break;
			}
		}
		else
		{
			return FALSE;
		}
	}

	if( outcaps != NULL )
	{
		output_state = gst_video_encoder_set_output_state( encoder, outcaps, state );
		gst_video_codec_state_unref( output_state );

		ret = gst_video_encoder_negotiate( encoder );

		gst_caps_unref( outcaps );
	}
	else
	{
		return FALSE;
	}

	if( ret == FALSE )
	{
		if( MM_VIDEO_BUFFER_TYPE_GEM != nxvideoenc->buffer_type )
		{
			for( i = 0;  i < MAX_ALLOC_BUFFER; i++ )
			{
				if( NULL != nxvideoenc->inbuf[i] )
				{
					NX_FreeVideoMemory( nxvideoenc->inbuf[i] );
					nxvideoenc->inbuf[i] = NULL;
				}
			}
		}

		if( NULL != nxvideoenc->enc )
		{
			NX_V4l2EncClose( nxvideoenc->enc );
			nxvideoenc->enc = NULL;
		}

		if( NULL != nxvideoenc->input_state )
		{
			gst_video_codec_state_unref( nxvideoenc->input_state );
			nxvideoenc->input_state = NULL;
		}
	}

	return ret;
}

static void
copy_to_videomemory( GstVideoFrame *pInframe, NX_VID_MEMORY_INFO *pImage )
{
	gint w = GST_VIDEO_FRAME_WIDTH( pInframe );
	gint h = GST_VIDEO_FRAME_HEIGHT( pInframe );
	guchar *pSrc, *pDst;
	gint i, j;

	pSrc = (guchar*)GST_VIDEO_FRAME_COMP_DATA( pInframe, 0 );
	pDst = (guchar*)pImage->pBuffer[0];

	for( i = 0; i < h; i++ )
	{
		memcpy( pDst, pSrc, w );

		pSrc += GST_VIDEO_FRAME_COMP_STRIDE( pInframe, 0 );
		pDst += pImage->stride[0];
	}

	for( j = 1; j < 3; j++ )
	{
		pSrc = (guchar*)GST_VIDEO_FRAME_COMP_DATA( pInframe, j );
		pDst = (guchar*)pImage->pBuffer[j];

		for( i = 0; i < h / 2; i++ )
		{
			memcpy( pDst, pSrc, w / 2 );
			pSrc += GST_VIDEO_FRAME_COMP_STRIDE( pInframe, j );
			pDst += pImage->stride[j];
		}
	}
}

static int drm_ioctl( int fd, unsigned long request, void *arg )
{
	int ret;

	do {
		ret = ioctl(fd, request, arg);
	} while (ret == -1 && (errno == EINTR || errno == EAGAIN));

	return ret;
}

static int gem_to_dmafd(int fd, int gem_fd)
{
	int ret;
	struct drm_prime_handle arg = {0, };

	arg.handle = gem_fd;
	ret = drm_ioctl(fd, DRM_IOCTL_PRIME_HANDLE_TO_FD, &arg);
	if (0 != ret)
		return -1;

	return arg.fd;
}

static int import_gem_from_flink( int fd, unsigned int flink_name )
{
	struct drm_gem_open arg = { 0, };
	/* struct nx_drm_gem_info info = { 0, }; */

	arg.name = flink_name;
	if (drm_ioctl(fd, DRM_IOCTL_GEM_OPEN, &arg)) {
		return -EINVAL;
	}

	return arg.handle;
}

static GstFlowReturn
gst_nxvideoenc_handle_frame( GstVideoEncoder *encoder, GstVideoCodecFrame *frame )
{
	GstNxvideoenc *nxvideoenc = GST_NXVIDEOENC (encoder);
	GstVideoFrame inframe;
	GstMemory *meta_block = NULL;
	MMVideoBuffer *mm_buf = NULL;

	GstMapInfo in_info;
	GstMapInfo out_info;

	NX_V4L2ENC_IN   encIn;
	NX_V4L2ENC_OUT  encOut;

	guchar* pSeqBuf  = NULL;
	gint    iSeqSize = 0;

	GST_DEBUG_OBJECT(nxvideoenc, "handle_frame");

	gst_video_codec_frame_ref( frame );

	if( MM_VIDEO_BUFFER_TYPE_GEM == nxvideoenc->buffer_type )
	{
		memset(&in_info, 0, sizeof(GstMapInfo));
		meta_block = gst_buffer_peek_memory( frame->input_buffer, 0 );

		if( !meta_block )
		{
			GST_ERROR("Fail, gst_buffer_peek_memory().\n");
		}

		gst_memory_map(meta_block, &in_info, GST_MAP_READ);
		mm_buf = (MMVideoBuffer*)in_info.data;
		if( !mm_buf )
		{
			GST_ERROR("Fail, get MMVideoBuffer.\n");
		}
		else
		{
			GST_DEBUG_OBJECT( nxvideoenc, "type: 0x%x, width: %d, height: %d, plane_num: %d, handle_num: %d, index: %d\n",
					mm_buf->type, mm_buf->width[0], mm_buf->height[0], mm_buf->plane_num, mm_buf->handle_num, mm_buf->buffer_index );

			if( FALSE == nxvideoenc->init )
			{
				NX_V4L2ENC_PARA param;
				memset( &param, 0x00, sizeof(NX_V4L2ENC_PARA) );

				param.width              = nxvideoenc->width;
				param.height             = nxvideoenc->height;
				param.keyFrmInterval     = nxvideoenc->keyFrmInterval ? (nxvideoenc->keyFrmInterval) : (nxvideoenc->fpsNum / nxvideoenc->fpsDen);
				param.fpsNum             = nxvideoenc->fpsNum;
				param.fpsDen             = nxvideoenc->fpsDen;
				param.profile            = (nxvideoenc->codec == V4L2_PIX_FMT_H263) ? V4L2_MPEG_VIDEO_H263_PROFILE_P3 : 0;
				param.bitrate            = nxvideoenc->bitrate;
				param.maximumQp          = nxvideoenc->maximumQp;
				param.disableSkip        = 0;
				param.RCDelay            = 0;
				param.rcVbvSize          = 0;
				param.gammaFactor        = 0;
				param.initialQp          = nxvideoenc->initialQp;
				param.numIntraRefreshMbs = 0;
				param.searchRange        = 0;
				param.enableAUDelimiter  = 0;
				param.imgFormat          = nxvideoenc->imgFormat;
				param.imgBufferNum       = nxvideoenc->imgBufferNum;
				param.imgPlaneNum        = mm_buf->handle_num;

				nxvideoenc->enc = NX_V4l2EncOpen( nxvideoenc->codec );
				if( NULL == nxvideoenc->enc )
				{
					GST_ERROR("Fail, NX_V4l2EncOpen().\n");
					return FALSE;
				}

				if( 0 > NX_V4l2EncInit( nxvideoenc->enc, &param ) )
				{
					GST_ERROR("Fail, NX_V4l2EncInit().\n");
					NX_V4l2EncClose( nxvideoenc->enc );
					nxvideoenc->enc = NULL;

					return FALSE;
				}

				NX_V4l2EncGetSeqInfo( nxvideoenc->enc, &pSeqBuf, &iSeqSize );
				nxvideoenc->init = TRUE;
			}

			if( NULL == nxvideoenc->inbuf[mm_buf->buffer_index] )
			{
				gint i;
				nxvideoenc->inbuf[mm_buf->buffer_index] = (NX_VID_MEMORY_INFO*)malloc( sizeof(NX_VID_MEMORY_INFO) );
				memset( nxvideoenc->inbuf[mm_buf->buffer_index], 0, sizeof(NX_VID_MEMORY_INFO) );

				nxvideoenc->inbuf[mm_buf->buffer_index]->width      = mm_buf->width[0];
				nxvideoenc->inbuf[mm_buf->buffer_index]->height     = mm_buf->height[0];
				nxvideoenc->inbuf[mm_buf->buffer_index]->planes     = mm_buf->handle_num;
				nxvideoenc->inbuf[mm_buf->buffer_index]->format     = V4L2_PIX_FMT_YUV420M;

				for( i = 0; i < mm_buf->handle_num; i++ )
				{
					nxvideoenc->inbuf[mm_buf->buffer_index]->size[i]    = mm_buf->size[i];
					nxvideoenc->inbuf[mm_buf->buffer_index]->pBuffer[i] = mm_buf->data[i];
					nxvideoenc->inbuf[mm_buf->buffer_index]->gemFd[i]   = import_gem_from_flink( nxvideoenc->drm_fd, mm_buf->handle.gem[i] );
					nxvideoenc->inbuf[mm_buf->buffer_index]->dmaFd[i]   = gem_to_dmafd( nxvideoenc->drm_fd, nxvideoenc->inbuf[mm_buf->buffer_index]->gemFd[i] );
				}

				for( i = 0; i < mm_buf->plane_num; i++ )
				{
					nxvideoenc->inbuf[mm_buf->buffer_index]->stride[i]  = mm_buf->stride_width[i];
				}
			}

			memset( &encIn, 0x00, sizeof(encIn) );
			encIn.pImage          = nxvideoenc->inbuf[mm_buf->buffer_index];
			encIn.imgIndex        = mm_buf->buffer_index;
			encIn.timeStamp       = 0;
			encIn.forcedIFrame    = 0;
			encIn.forcedSkipFrame = 0;
			encIn.quantParam      = (nxvideoenc->codec == V4L2_PIX_FMT_H264) ? nxvideoenc->initialQp : 10;	// FIX ME!!!

			if( 0 > NX_V4l2EncEncodeFrame( nxvideoenc->enc, &encIn, &encOut ) )
			{
				GST_ERROR("Fail, NX_V4l2EncEncodeFrame().\n");
				gst_video_codec_frame_unref( frame );
				return GST_FLOW_ERROR;
			}

			frame->output_buffer = gst_video_encoder_allocate_output_buffer( encoder, iSeqSize + encOut.strmSize );
			gst_buffer_map( frame->output_buffer, &out_info, GST_MAP_WRITE );

			if( 0 < iSeqSize ) memcpy( out_info.data, pSeqBuf, iSeqSize );
			memcpy( out_info.data + iSeqSize, encOut.strmBuf, encOut.strmSize );
			out_info.size += iSeqSize + encOut.strmSize;

			if( PIC_TYPE_I == encOut.frameType )
			{
				GST_VIDEO_CODEC_FRAME_SET_SYNC_POINT( frame );
			}
			else
			{
				GST_VIDEO_CODEC_FRAME_UNSET_SYNC_POINT( frame );
			}

			gst_buffer_unmap( frame->output_buffer, &out_info );
		}
		gst_memory_unmap( meta_block, &in_info );
	}
	else
	{
		if( FALSE == nxvideoenc->init )
		{
			NX_V4L2ENC_PARA param;
			memset( &param, 0x00, sizeof(NX_V4L2ENC_PARA) );

			param.width              = nxvideoenc->width;
			param.height             = nxvideoenc->height;
			param.keyFrmInterval     = nxvideoenc->keyFrmInterval ? (nxvideoenc->keyFrmInterval) : (nxvideoenc->fpsNum / nxvideoenc->fpsDen);
			param.fpsNum             = nxvideoenc->fpsNum;
			param.fpsDen             = nxvideoenc->fpsDen;
			param.profile            = (nxvideoenc->codec == V4L2_PIX_FMT_H263) ? V4L2_MPEG_VIDEO_H263_PROFILE_P3 : 0;
			param.bitrate            = nxvideoenc->bitrate;
			param.maximumQp          = nxvideoenc->maximumQp;
			param.disableSkip        = 0;
			param.RCDelay            = 0;
			param.rcVbvSize          = 0;
			param.gammaFactor        = 0;
			param.initialQp          = nxvideoenc->initialQp;
			param.numIntraRefreshMbs = 0;
			param.searchRange        = 0;
			param.enableAUDelimiter  = 0;
			param.imgFormat          = nxvideoenc->imgFormat;
			param.imgBufferNum       = nxvideoenc->imgBufferNum;
			param.imgPlaneNum        = 3;

			nxvideoenc->enc = NX_V4l2EncOpen( nxvideoenc->codec );
			if( NULL == nxvideoenc->enc )
			{
				GST_ERROR("Fail, NX_V4l2EncOpen().\n");
				return FALSE;
			}

			if( 0 > NX_V4l2EncInit( nxvideoenc->enc, &param ) )
			{
				GST_ERROR("Fail, NX_V4l2EncInit().\n");
				NX_V4l2EncClose( nxvideoenc->enc );
				nxvideoenc->enc = NULL;

				return FALSE;
			}

			NX_V4l2EncGetSeqInfo( nxvideoenc->enc, &pSeqBuf, &iSeqSize );
			nxvideoenc->init = TRUE;
		}

		if( !gst_video_frame_map( &inframe, &nxvideoenc->input_state->info, frame->input_buffer, GST_MAP_READ ) )
		{
			gst_video_codec_frame_unref( frame );
			return GST_FLOW_ERROR;
		}
		copy_to_videomemory( &inframe, nxvideoenc->inbuf[nxvideoenc->buf_index] );

		memset( &encIn, 0x00, sizeof(encIn) );
		encIn.pImage          = nxvideoenc->inbuf[nxvideoenc->buf_index];
		encIn.imgIndex        = nxvideoenc->buf_index;
		encIn.forcedIFrame    = 0;
		encIn.forcedSkipFrame = 0;
		encIn.quantParam      = (nxvideoenc->codec == V4L2_PIX_FMT_H264) ? nxvideoenc->initialQp : 10;	// FIX ME!!!

		nxvideoenc->buf_index = (nxvideoenc->buf_index + 1) % MAX_ALLOC_BUFFER;

		if( 0 > NX_V4l2EncEncodeFrame( nxvideoenc->enc, &encIn, &encOut ) )
		{
			GST_ERROR("Fail, NX_V4l2EncEncodeFrame().\n");
			gst_video_codec_frame_unref( frame );
			return GST_FLOW_ERROR;
		}

		frame->output_buffer = gst_video_encoder_allocate_output_buffer( encoder, iSeqSize + encOut.strmSize );
		gst_buffer_map( frame->output_buffer, &out_info, GST_MAP_WRITE );

		if( 0 < iSeqSize ) memcpy( out_info.data, pSeqBuf, iSeqSize );
		memcpy( out_info.data + iSeqSize, encOut.strmBuf, encOut.strmSize );
		out_info.size += iSeqSize + encOut.strmSize;

		if( PIC_TYPE_I == encOut.frameType )
		{
			GST_VIDEO_CODEC_FRAME_SET_SYNC_POINT( frame );
		}
		else
		{
			GST_VIDEO_CODEC_FRAME_UNSET_SYNC_POINT( frame );
		}

		gst_buffer_unmap( frame->output_buffer, &out_info );
		gst_video_frame_unmap( &inframe );
	}

	gst_video_codec_frame_unref( frame );

	return gst_video_encoder_finish_frame( encoder, frame );
}

static GstFlowReturn
gst_nxvideoenc_finish (GstVideoEncoder *encoder)
{
	GstNxvideoenc *nxvideoenc = GST_NXVIDEOENC (encoder);

	GST_DEBUG_OBJECT(nxvideoenc, "finish");

	return GST_FLOW_OK;
}

static gboolean
plugin_init( GstPlugin * plugin )
{
	/* FIXME Remember to set the rank if it's an element that is meant
		 to be autoplugged by decodebin. */

	return gst_element_register (plugin, "nxvideoenc", GST_RANK_NONE,
			GST_TYPE_NXVIDEOENC);
}

/* FIXME: these are normally defined by the GStreamer build system.
	 If you are creating an element to be included in gst-plugins-*,
	 remove these, as they're always defined.  Otherwise, edit as
	 appropriate for your external plugin package. */
#ifndef VERSION
#define VERSION "0.1.0"
#endif
#ifndef PACKAGE
#define PACKAGE "S5P6818 GStreamer PlugIn"
#endif
#ifndef PACKAGE_NAME
#define PACKAGE_NAME "S5P6818 GStreamer PlugIn"
#endif
#ifndef GST_PACKAGE_ORIGIN
#define GST_PACKAGE_ORIGIN "http://www.nexell.co.kr"
#endif

GST_PLUGIN_DEFINE(
	GST_VERSION_MAJOR,
	GST_VERSION_MINOR,
	nxvideoenc,
	"Nexell H/W Video Encoder for S5P6818",
	plugin_init, VERSION,
	"LGPL",
	PACKAGE_NAME,
	GST_PACKAGE_ORIGIN
)
