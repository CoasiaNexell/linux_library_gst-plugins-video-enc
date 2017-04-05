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

#include <gstmmvideobuffermeta.h>
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

#define DEFAULT_CODEC				V4L2_PIX_FMT_H264
#define DEFAULT_WIDTH				1920
#define DEFAULT_HEIGHT				1080
#define DEFAULT_FPS_NUM				30
#define DEFAULT_FPS_DEN				1
#define DEFAULT_KEYFRAME_INTERVAL	DEFAULT_FPS_NUM / DEFAULT_FPS_DEN
#define DEFAULT_BITRATE				3000 * 1024
#define DEFAULT_MAXIMUM_QP			0
#define DEFAULT_DISABLE_SKIP		0
#define DEFAULT_RC_DELAY			0
#define DEFAULT_RC_VBV_SIZE			0
#define DEFAULT_GAMMA_FACTOR		0
#define DEFAULT_INITIAL_QP			0
#define DEFAULT_INTRA_REFRESH_MBS	0
#define DEFAULT_SEARCH_RANGE		0
#define DEFAULT_ENABLE_AU_DELIMITER	FALSE

#define DEFAULT_IMAGE_FORMAT		V4L2_PIX_FMT_YUV420M
#define	H264_AU_LENGTH_SIZE			4

enum
{
	PROP_0,
	PROP_CODEC,				// video/x-h264, video/x-h263, video/mpeg
	PROP_WIDTH,
	PROP_HEIGHT,
	PROP_FPS_N,
	PROP_FPS_D,
	PROP_KEYFRAME_INTERVAL,
	PROP_BITRATE,
	PROP_MAXIMUM_QP,
	PROP_DISABLE_SKIP,
	PROP_RC_DELAY,
	PROP_RC_VBV_SIZE,
	PROP_GAMMA_FACTOR,
	PROP_INITIAL_QP,
	PROP_INTRA_REFRESH_MBS,
	PROP_SEARCH_RANGE,
	PROP_ENABLE_AU_DELIMITER,
};

/* pad templates */

/* FIXME: add/remove formats you can handle */
static GstStaticPadTemplate gst_nxvideoenc_sink_template =
	GST_STATIC_PAD_TEMPLATE( "sink",
		GST_PAD_SINK,
		GST_PAD_ALWAYS,
		GST_STATIC_CAPS(
			"video/x-raw, "
			"format =(string) I420; "
		)
	);

static GstStaticPadTemplate gst_nxvideoenc_src_template =
	GST_STATIC_PAD_TEMPLATE( "src",
		GST_PAD_SRC,
		GST_PAD_ALWAYS,
		GST_STATIC_CAPS(
			"video/x-h264, "
			"width = (int) [ 96, 1920 ], "
			"height = (int) [ 16, 1920 ], "
			"framerate = (fraction) [ 0/1, 65535/1 ], "
			"stream-format = (string) { byte-stream, avc }, "
			"alignment = (string) { au, nal }; "

			"video/x-h263, "
			"width = (int) [ 96, 1920 ], "
			"height = (int) [ 16, 1088 ], "
			"framerate = (fraction) [ 0/1, 65535/1 ]; "

			"video/mpeg, "
			"width = (int) [ 96, 1920 ], "
			"height = (int) [ 16, 1088 ], "
			"framerate = (fraction) [ 0/1, 65535/1 ], "
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
			"video/x-h264",
			(GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)
		)
	);

	g_object_class_install_property( G_OBJECT_CLASS (klass), PROP_WIDTH,
		g_param_spec_uint ("width", "width",
			"width of image",
			96, 1920, DEFAULT_WIDTH,
			(GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)
		)
	);

	g_object_class_install_property( G_OBJECT_CLASS (klass), PROP_HEIGHT,
		g_param_spec_uint ("height", "height",
			"height of image",
			16, 1920, DEFAULT_HEIGHT,
			(GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)
		)
	);

	g_object_class_install_property( G_OBJECT_CLASS (klass), PROP_FPS_N,
		g_param_spec_uint ("fps-n", "fps-n",
			"frame per second's numerator",
			0, G_MAXUINT16, DEFAULT_FPS_NUM,
			(GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)
		)
	);

	g_object_class_install_property( G_OBJECT_CLASS (klass), PROP_FPS_D,
		g_param_spec_uint ("fps-d", "fps-d",
			"frame per second's denominator",
			1, G_MAXUINT16, DEFAULT_FPS_DEN,
			(GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)
		)
	);

	g_object_class_install_property( G_OBJECT_CLASS (klass), PROP_KEYFRAME_INTERVAL,
		g_param_spec_uint ("key-interval", "key-interval",
			"size of key frame interval",
			0, G_MAXUINT, DEFAULT_KEYFRAME_INTERVAL,
			(GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)
		)
	);

	g_object_class_install_property( G_OBJECT_CLASS (klass), PROP_BITRATE,
		g_param_spec_uint ("bitrate", "bitrate",
			"target bitrate in bits per second",
			0, G_MAXUINT16 * 1024, DEFAULT_BITRATE,
			(GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)
		)
	);

	g_object_class_install_property( G_OBJECT_CLASS (klass), PROP_MAXIMUM_QP,
		g_param_spec_uint ("max-qp", "max-qp",
			"maximum quantization parameter",
			0, G_MAXUINT, DEFAULT_MAXIMUM_QP,
			(GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)
		)
	);

	g_object_class_install_property( G_OBJECT_CLASS (klass), PROP_DISABLE_SKIP,
		g_param_spec_uint ("skip", "skip",
			"disable skip frame mode",
			0, 1, DEFAULT_DISABLE_SKIP,
			(GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)
		)
	);

	g_object_class_install_property( G_OBJECT_CLASS (klass), PROP_RC_DELAY,
		g_param_spec_uint ("rc-delay", "rc-delay",
			"rate control delay",
			0, G_MAXUINT16, DEFAULT_RC_DELAY,
			(GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)
		)
	);

	g_object_class_install_property( G_OBJECT_CLASS (klass), PROP_RC_VBV_SIZE,
		g_param_spec_uint ("rc-vbv", "rc-vbv",
			"reference decoder buffer size in bits",
			0, G_MAXUINT / 2, DEFAULT_RC_VBV_SIZE,
			(GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)
		)
	);

	g_object_class_install_property( G_OBJECT_CLASS (klass), PROP_GAMMA_FACTOR,
		g_param_spec_uint ("gamma-factor", "gamma-factor",
			"user gamma factor",
			0, G_MAXUINT16 / 2, DEFAULT_GAMMA_FACTOR,
			(GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)
		)
	);

	g_object_class_install_property( G_OBJECT_CLASS (klass), PROP_INITIAL_QP,
		g_param_spec_uint ("init-qp", "init-qp",
			"initial quantization parameter",
			0, 51, DEFAULT_INITIAL_QP,
			(GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)
		)
	);

	g_object_class_install_property( G_OBJECT_CLASS (klass), PROP_INTRA_REFRESH_MBS,
		g_param_spec_uint ("intra", "intra",
			"Intra MB refresh number (Cyclic Intera Refresh)",
			0, 51, DEFAULT_INTRA_REFRESH_MBS,
			(GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)
		)
	);

	g_object_class_install_property( G_OBJECT_CLASS (klass), PROP_SEARCH_RANGE,
		g_param_spec_uint ("search-range", "search-range",
			"search range of motion estimation (0 : 128 x 64, 1 : 64 x 32, 2 : 32 x 16, 3 : 16 x 16)",
			0, 3, DEFAULT_SEARCH_RANGE,
			(GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)
		)
	);

	g_object_class_install_property( G_OBJECT_CLASS (klass), PROP_ENABLE_AU_DELIMITER,
		g_param_spec_boolean ("au-delimiter", "au-delimiter",
			"insert access unit delimiter befor NAL unit",
			DEFAULT_ENABLE_AU_DELIMITER,
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
	nxvideoenc->accelerable        = FALSE;
	nxvideoenc->buf_index          = 0;
	nxvideoenc->drm_fd             = -1;
	nxvideoenc->seq_buf            = NULL;
	nxvideoenc->seq_size           = 0;
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
	nxvideoenc->maximumQp          = DEFAULT_MAXIMUM_QP;
	nxvideoenc->disableSkip        = DEFAULT_DISABLE_SKIP;
	nxvideoenc->RCDelay            = DEFAULT_RC_DELAY;
	nxvideoenc->rcVbvSize          = DEFAULT_RC_VBV_SIZE;
	nxvideoenc->gammaFactor        = DEFAULT_GAMMA_FACTOR;
	nxvideoenc->initialQp          = DEFAULT_INITIAL_QP;
	nxvideoenc->numIntraRefreshMbs = DEFAULT_INTRA_REFRESH_MBS;
	nxvideoenc->searchRange        = DEFAULT_SEARCH_RANGE;
	nxvideoenc->enableAUDelimiter  = DEFAULT_ENABLE_AU_DELIMITER;
	nxvideoenc->imgFormat          = DEFAULT_IMAGE_FORMAT;
	nxvideoenc->imgBufferNum       = MAX_INPUT_BUFFER;
	nxvideoenc->rotAngle           = 0;
	nxvideoenc->mirDirection       = 0;
	nxvideoenc->jpgQuality         = 0;
}

static gint
get_v4l2_codec( const gchar *codec )
{
	gint v4l2_codec = -1;
	if( !g_strcmp0( codec, "video/x-h264" ) ) v4l2_codec = V4L2_PIX_FMT_H264;
	else if( !g_strcmp0( codec, "video/x-h263") ) v4l2_codec = V4L2_PIX_FMT_H263;
	else if( !g_strcmp0( codec, "video/mpeg") ) v4l2_codec = V4L2_PIX_FMT_MPEG4;

	return v4l2_codec;
}

static const gchar*
get_codec_name( gint codec )
{
	if( codec == V4L2_PIX_FMT_H264 )        return "video/x-h264";
	else if( codec == V4L2_PIX_FMT_H263 )   return "video/x-h263";
	else if( codec == V4L2_PIX_FMT_MPEG4 )  return "video/mpeg";

	return NULL;
}

static void
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

		case PROP_KEYFRAME_INTERVAL:
			nxvideoenc->keyFrmInterval = g_value_get_uint( value );
			break;

		case PROP_BITRATE:
			nxvideoenc->bitrate = g_value_get_uint( value );
			break;

		case PROP_MAXIMUM_QP:
			nxvideoenc->maximumQp = g_value_get_uint( value );
			break;

		case PROP_DISABLE_SKIP:
			nxvideoenc->disableSkip = g_value_get_uint( value );
			break;

		case PROP_RC_DELAY:
			nxvideoenc->RCDelay = g_value_get_uint( value );
			break;

		case PROP_RC_VBV_SIZE:
			nxvideoenc->rcVbvSize = g_value_get_uint( value );
			break;

		case PROP_GAMMA_FACTOR:
			nxvideoenc->gammaFactor = g_value_get_uint( value );
			break;

		case PROP_INITIAL_QP:
			nxvideoenc->initialQp = g_value_get_uint( value );
			break;

		case PROP_INTRA_REFRESH_MBS:
			nxvideoenc->numIntraRefreshMbs = g_value_get_uint( value );
			break;

		case PROP_SEARCH_RANGE:
			nxvideoenc->searchRange = g_value_get_uint( value );
			break;

		case PROP_ENABLE_AU_DELIMITER:
			nxvideoenc->enableAUDelimiter = g_value_get_boolean( value );
			break;

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID( object, property_id, pspec );
			break;
	}
}

static void
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

		case PROP_KEYFRAME_INTERVAL:
			g_value_set_uint( value, nxvideoenc->keyFrmInterval );
			break;

		case PROP_BITRATE:
			g_value_set_uint( value, nxvideoenc->bitrate );
			break;

		case PROP_MAXIMUM_QP:
			g_value_set_uint( value, nxvideoenc->maximumQp );
			break;

		case PROP_DISABLE_SKIP:
			g_value_set_uint( value, nxvideoenc->disableSkip );
			break;

		case PROP_RC_DELAY:
			g_value_set_uint( value, nxvideoenc->RCDelay );
			break;

		case PROP_RC_VBV_SIZE:
			g_value_set_uint( value, nxvideoenc->rcVbvSize );
			break;

		case PROP_GAMMA_FACTOR:
			g_value_set_uint( value, nxvideoenc->gammaFactor );
			break;

		case PROP_INITIAL_QP:
			g_value_set_uint( value, nxvideoenc->initialQp );
			break;

		case PROP_INTRA_REFRESH_MBS:
			g_value_set_uint( value, nxvideoenc->numIntraRefreshMbs );
			break;

		case PROP_SEARCH_RANGE:
			g_value_set_uint( value, nxvideoenc->searchRange );
			break;

		case PROP_ENABLE_AU_DELIMITER:
			g_value_set_boolean( value, nxvideoenc->enableAUDelimiter );
			break;

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID( object, property_id, pspec );
			break;
	}
}

static gboolean
h264_split_nal( guchar *in_buf, gint in_len, guchar **out_buf, gint *out_len )
{
	gint i, pos = 0;
	gboolean valid = FALSE;

	for( i = 0; i < in_len - 3; i++ )
	{
		if( (in_buf[i] == 0x00) && (in_buf[i + 1] == 0x00) && (in_buf[i + 2] == 0x01) )
		{
			if( valid ) break;

			*out_buf = in_buf + i;

			valid = TRUE;
			pos = i;
		}
	}

	if( i == in_len - 3 )
	{
		*out_len = in_len - pos;
	}
	else
	{
		if( i != 0 && in_buf[i-1] == 0 )
		{
			i--;
		}

		*out_len = i - pos;
	}

	return valid;
}

// in_buf format = nal start code + sps + nal start code + pps
static gboolean
h264_find_spspps_stream( guchar *in_buf, gint in_size, guchar **sps, gint *sps_len, guchar **pps, gint *pps_len )
{
	guchar *buf = in_buf;
	gint size = in_size;

	// find nal start code for sps
	if( FALSE == h264_split_nal( buf, size, sps, sps_len ) )
	{
		return FALSE;
	}

	// remove 3byte nal start code
	*sps     = *sps     + 3;
	*sps_len = *sps_len - 3;

	if( FALSE == h264_split_nal( in_buf + *sps_len + 3, in_size - *sps_len - 3, pps, pps_len ) )
	{
		return FALSE;
	}

	// remove 3byte nal start code
	*pps     = *pps     + 3;
	*pps_len = *pps_len - 3;

	return TRUE;
}

static GstBuffer*
h264_get_header( GstVideoEncoder *encoder )
{
	GstNxvideoenc *nxvideoenc = GST_NXVIDEOENC( encoder );

	NX_V4L2ENC_HANDLE enc = NULL;
	NX_V4L2ENC_PARA param;

	guchar *buf = NULL, *ptr = NULL;;
	gint buf_size = 0;

	guchar *seq_buf = NULL;
	gint seq_size = 0;

	GstBuffer *header_buffer;

	guchar *sps = NULL, *pps = NULL;
	gint sps_len = 0, pps_len = 0;

	memset( &param, 0x00, sizeof(NX_V4L2ENC_PARA) );
	param.width              = nxvideoenc->width;
	param.height             = nxvideoenc->height;
	param.keyFrmInterval     = nxvideoenc->keyFrmInterval ? (nxvideoenc->keyFrmInterval) : (nxvideoenc->fpsNum / nxvideoenc->fpsDen);
	param.fpsNum             = nxvideoenc->fpsNum;
	param.fpsDen             = nxvideoenc->fpsDen;
	param.profile            = (nxvideoenc->codec == V4L2_PIX_FMT_H263) ? V4L2_MPEG_VIDEO_H263_PROFILE_P3 : 0;
	param.bitrate            = nxvideoenc->bitrate;
	param.maximumQp          = nxvideoenc->maximumQp;
	param.disableSkip        = nxvideoenc->disableSkip;
	param.RCDelay            = nxvideoenc->RCDelay;
	param.rcVbvSize          = nxvideoenc->rcVbvSize;
	param.gammaFactor        = nxvideoenc->gammaFactor;
	param.initialQp          = nxvideoenc->initialQp;
	param.numIntraRefreshMbs = nxvideoenc->numIntraRefreshMbs;
	param.searchRange        = nxvideoenc->searchRange;
	param.enableAUDelimiter  = (nxvideoenc->codec == V4L2_PIX_FMT_H264) ? nxvideoenc->enableAUDelimiter : 0;
	param.imgFormat          = nxvideoenc->imgFormat;
	param.imgBufferNum       = MAX_INPUT_BUFFER;
	param.imgPlaneNum        = 3;

	enc = NX_V4l2EncOpen( nxvideoenc->codec );
	if( NULL == enc )
	{
		GST_ERROR("Fail, NX_V4l2EncOpen().\n");
		return NULL;
	}

	if( 0 > NX_V4l2EncInit( enc, &param ) )
	{
		GST_ERROR("Fail, NX_V4l2EncInit().\n");
		NX_V4l2EncClose( enc );
		return NULL;
	}

	NX_V4l2EncGetSeqInfo( enc, &seq_buf, &seq_size );

	// seq_buf format = nal start code + sps + nal start code + pps
	if( FALSE == h264_find_spspps_stream( seq_buf, seq_size, &sps, &sps_len, &pps, &pps_len ) )
	{
		GST_ERROR("Fail, Not Found SPS/PPS.\n");
		return NULL;
	}

	buf_size += sps_len + pps_len + 11;
	buf = g_malloc( buf_size * sizeof(guchar) );
	ptr = buf;

	// make avcC information
	*ptr++ = 0x01;
	*ptr++ = sps[1];									// profile
	*ptr++ = sps[2];									// compatibility
	*ptr++ = sps[3];									// level
	*ptr++ = ( 0x3F << 2 ) | (H264_AU_LENGTH_SIZE - 1);	// length size - 1 --> 4 length size
	*ptr++ = 0xE0 | 1;									// number of sps : 1
	*ptr++ = sps_len >> 8;								// length of sps : 2 bytes
	*ptr++ = sps_len & 0xFF;

	memcpy( ptr, sps, sps_len );
	ptr += sps_len;

	*ptr++ = 1;					// number of pps : 1
	*ptr++ = pps_len >> 8;		// length of pps : 2bytes
	*ptr++ = pps_len & 0xFF;

	memcpy( ptr, pps, pps_len );
	ptr += pps_len;

	header_buffer = gst_buffer_new_and_alloc( buf_size );
	gst_buffer_fill( header_buffer, 0, buf, buf_size );
	g_free( buf );

	NX_V4l2EncClose( enc );

	return header_buffer;
}

static GstCaps*
get_encoder_src_caps( GstVideoEncoder *encoder )
{
	GstNxvideoenc *nxvideoenc = GST_NXVIDEOENC( encoder );

	GstCaps *caps = NULL;
	GstCaps *src_caps = NULL;

	gint caps_num;

	caps = gst_static_pad_template_get_caps( &gst_nxvideoenc_src_template );
	if( NULL == caps )
	{
		return NULL;
	}

	for( caps_num = 0; caps_num < gst_caps_get_size( caps ); caps_num++ )
	{
		GstStructure *src_structure = gst_caps_get_structure( caps, caps_num );
		if( src_structure )
		{
			if( !g_strcmp0( get_codec_name(nxvideoenc->codec), gst_structure_get_name(src_structure)) )
			{
				src_caps = gst_caps_copy_nth( caps, caps_num );
				gst_caps_make_writable(src_caps);
				break;
			}
		}
	}
	gst_caps_unref( caps );

	if( NULL == src_caps )
	{
		return NULL;
	}

	caps = gst_pad_peer_query_caps( GST_VIDEO_ENCODER_SRC_PAD(encoder), NULL );
	if( NULL == caps )
	{
		return NULL;
	}

	if( TRUE == gst_caps_is_any(caps) )
	{
		GstStructure *src_structure;
		src_structure = gst_caps_get_structure( src_caps, 0 );
		if( !g_strcmp0( get_codec_name(nxvideoenc->codec), "video/x-h264") )
		{
			gst_structure_set( src_structure, "stream-format", G_TYPE_STRING, "byte-stream", NULL );
			g_strlcpy( nxvideoenc->stream_format, "byte-stream", MAX_STRING_SIZE );

			gst_structure_set( src_structure, "alignment", G_TYPE_STRING, "au", NULL );
			g_strlcpy( nxvideoenc->alignment, "au", MAX_STRING_SIZE );
		}

		gst_caps_unref( caps );

		return src_caps;
	}
	else
	{
		for( caps_num = 0; caps_num < gst_caps_get_size(caps); caps_num++ )
		{
			GstCaps *peer_caps = gst_caps_copy_nth( caps, caps_num );
			if( !peer_caps && gst_caps_is_empty(peer_caps) )
				continue;

			GstStructure *peer_structure = gst_caps_get_structure( peer_caps, 0 );
			if( peer_structure )
			{
				if( !g_strcmp0( get_codec_name(nxvideoenc->codec), gst_structure_get_name(peer_structure)) )
				{
					if( !g_strcmp0( get_codec_name(nxvideoenc->codec), "video/x-h264") )
					{
						GstStructure *src_structure;
						const gchar *stream_format, *alignment;


						src_structure = gst_caps_get_structure( src_caps, 0 );

						stream_format = gst_structure_get_string( peer_structure, "stream-format" );
						alignment = gst_structure_get_string( peer_structure, "alignment");

						if( NULL != stream_format )
						{
							gst_structure_set( src_structure, "stream-format", G_TYPE_STRING, stream_format, NULL );
							g_strlcpy( nxvideoenc->stream_format, stream_format, MAX_STRING_SIZE );
						}
						else
						{
							gst_structure_set( src_structure, "stream-format", G_TYPE_STRING, "byte-stream", NULL );
							g_strlcpy( nxvideoenc->stream_format, "byte-stream", MAX_STRING_SIZE );
						}

						if( NULL != alignment )
						{
							gst_structure_set( src_structure, "alignment", G_TYPE_STRING, alignment, NULL );
							g_strlcpy( nxvideoenc->alignment, alignment, MAX_STRING_SIZE );
						}
						else
						{
							gst_structure_set( src_structure, "alignment", G_TYPE_STRING, "au", NULL );
							g_strlcpy( nxvideoenc->alignment, "au", MAX_STRING_SIZE );
						}

						if( !g_strcmp0( nxvideoenc->stream_format, "avc" ) && !g_strcmp0( nxvideoenc->alignment, "au" ) )
						{
							GstBuffer *buffer = h264_get_header( encoder );
							if( NULL != buffer )
							{
								gst_structure_set( src_structure, "codec_data", GST_TYPE_BUFFER, buffer, NULL );
								gst_buffer_unref( buffer );
							}
						}

						gst_caps_unref( caps );
						gst_caps_unref( peer_caps );

						return src_caps;
					}

					if( !g_strcmp0( get_codec_name(nxvideoenc->codec), "video/x-h263") )
					{

						gst_caps_unref( caps );
						gst_caps_unref( peer_caps );

						return src_caps;
					}

					if( !g_strcmp0( get_codec_name(nxvideoenc->codec), "video/mpeg") )
					{

						gst_caps_unref( caps );
						gst_caps_unref( peer_caps );

						return src_caps;
					}
				}
				gst_caps_unref( peer_caps );
			}
		}
	}

	if( NULL != caps )
	{
		gst_caps_unref( caps );
	}

	return NULL;
}

static gboolean
gst_nxvideoenc_set_format( GstVideoEncoder *encoder, GstVideoCodecState *state )
{
	GstNxvideoenc *nxvideoenc = GST_NXVIDEOENC (encoder);
	GstCaps *src_caps = NULL;

	gint ret = FALSE;

	GST_DEBUG_OBJECT(nxvideoenc, "set_format");

	if( nxvideoenc->input_state )
	{
		gst_video_codec_state_unref( nxvideoenc->input_state );
	}

	nxvideoenc->input_state = gst_video_codec_state_ref( state );

	nxvideoenc->width          = !nxvideoenc->width ? GST_VIDEO_INFO_WIDTH( &state->info )  : nxvideoenc->width;
	nxvideoenc->height         = !nxvideoenc->height ? GST_VIDEO_INFO_HEIGHT( &state->info ) : nxvideoenc->height;
	nxvideoenc->fpsNum         = !nxvideoenc->fpsNum ? GST_VIDEO_INFO_FPS_N( &state->info ) : nxvideoenc->fpsNum;
	nxvideoenc->fpsDen         = !nxvideoenc->fpsDen ? GST_VIDEO_INFO_FPS_D( &state->info ) : nxvideoenc->fpsDen;
	nxvideoenc->keyFrmInterval = !nxvideoenc->keyFrmInterval ? nxvideoenc->fpsNum / nxvideoenc->fpsDen : nxvideoenc->keyFrmInterval;

	GST_VIDEO_INFO_WIDTH( &state->info )  = nxvideoenc->width;
	GST_VIDEO_INFO_HEIGHT( &state->info ) = nxvideoenc->height;
	GST_VIDEO_INFO_FPS_N( &state->info )  = nxvideoenc->fpsNum;
	GST_VIDEO_INFO_FPS_D( &state->info )  = nxvideoenc->fpsDen;

	if( nxvideoenc->width < 96 || nxvideoenc->height < 16 )
	{
		GST_ERROR("Fail, Invalid Width( %d ), height( %d )\n", nxvideoenc->width, nxvideoenc->height );
		return FALSE;
	}

	nxvideoenc->drm_fd = open( "/dev/dri/card0", O_RDWR );
	if( 0 > nxvideoenc->drm_fd )
	{
		GST_ERROR("Fail, drmOpen().\n");
		return FALSE;
	}

	drmDropMaster( nxvideoenc->drm_fd );

	src_caps = get_encoder_src_caps( encoder );
	if( src_caps != NULL )
	{
		gst_video_encoder_set_output_state( encoder, src_caps, state );

		ret = gst_video_encoder_negotiate( encoder );

		gst_caps_unref( src_caps );
	}
	else
	{
		return FALSE;
	}

	if( ret == FALSE )
	{
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

		if( 0 > nxvideoenc->drm_fd )
		{
			drmClose( nxvideoenc->drm_fd );
			nxvideoenc->drm_fd = -1;
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

static int gem_close( int fd, int gem_fd )
{
	struct drm_gem_close arg = { 0, };

	arg.handle = gem_fd;
	if (drm_ioctl(fd, DRM_IOCTL_GEM_CLOSE, &arg)) {
		return -EINVAL;
	}

	return 0;
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

static void
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

	if( FALSE == nxvideoenc->accelerable )
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
					gem_close(nxvideoenc->drm_fd, nxvideoenc->inbuf[i]->gemFd[j] );
					close( nxvideoenc->inbuf[i]->dmaFd[j] );
				}

				free( nxvideoenc->inbuf[i] );
				nxvideoenc->inbuf[i] = NULL;
			}
		}
	}

	if( NULL != nxvideoenc->enc )
	{
GST_DEBUG_OBJECT(nxvideoenc, "NX_V4l2EncClose ++");
		NX_V4l2EncClose( nxvideoenc->enc );
GST_DEBUG_OBJECT(nxvideoenc, "NX_V4l2EncClose --");
		nxvideoenc->enc = NULL;
	}

	if( NULL != nxvideoenc->input_state )
	{
		gst_video_codec_state_unref( nxvideoenc->input_state );
		nxvideoenc->input_state = NULL;
	}

	if( 0 <= nxvideoenc->drm_fd )
	{
		drmClose( nxvideoenc->drm_fd );
		nxvideoenc->drm_fd = -1;
	}
	GST_DEBUG_OBJECT(nxvideoenc, "stop --");

	return TRUE;
}

static GstFlowReturn
gst_nxvideoenc_handle_frame( GstVideoEncoder *encoder, GstVideoCodecFrame *frame )
{
	GstNxvideoenc *nxvideoenc = GST_NXVIDEOENC (encoder);
	GstVideoFrame inframe;

	GstMMVideoBufferMeta *meta = NULL;
	GstMapInfo in_info;
	GstMapInfo out_info;

	NX_V4L2ENC_IN   encIn;
	NX_V4L2ENC_OUT  encOut;

	GST_DEBUG_OBJECT(nxvideoenc, "handle_frame");

	gst_video_codec_frame_ref( frame );

	meta = gst_buffer_get_mmvideobuffer_meta( frame->input_buffer );
	if( NULL != meta && meta->memory_index >= 0 )
	{
		GstMemory *meta_block = NULL;
		MMVideoBuffer *mm_buf = NULL;

		meta_block = gst_buffer_peek_memory( frame->input_buffer, meta->memory_index );
		if( !meta_block )
		{
			GST_ERROR("Fail, gst_buffer_peek_memory().\n");
		}

		memset(&in_info, 0, sizeof(GstMapInfo));
		gst_memory_map(meta_block, &in_info, GST_MAP_READ);

		mm_buf = (MMVideoBuffer*)in_info.data;
		if( !mm_buf )
		{
			GST_ERROR("Fail, get MMVideoBuffer.\n");
		}
		else
		{
			GST_DEBUG_OBJECT( nxvideoenc, "type: 0x%x, width: %d, height: %d, plane_num: %d, handle_num: %d, index: %d",
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
				param.disableSkip        = nxvideoenc->disableSkip;
				param.RCDelay            = nxvideoenc->RCDelay;
				param.rcVbvSize          = nxvideoenc->rcVbvSize;
				param.gammaFactor        = nxvideoenc->gammaFactor;
				param.initialQp          = nxvideoenc->initialQp;
				param.numIntraRefreshMbs = nxvideoenc->numIntraRefreshMbs;
				param.searchRange        = nxvideoenc->searchRange;
				param.enableAUDelimiter  = (nxvideoenc->codec == V4L2_PIX_FMT_H264) ? nxvideoenc->enableAUDelimiter : 0;
				param.imgFormat          = nxvideoenc->imgFormat;
				param.imgBufferNum       = MAX_INPUT_BUFFER;
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

				NX_V4l2EncGetSeqInfo( nxvideoenc->enc, &nxvideoenc->seq_buf, &nxvideoenc->seq_size );

				nxvideoenc->init = TRUE;
				nxvideoenc->accelerable = TRUE;
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
			encIn.quantParam      = nxvideoenc->initialQp;

			if( 0 > NX_V4l2EncEncodeFrame( nxvideoenc->enc, &encIn, &encOut ) )
			{
				GST_ERROR("Fail, NX_V4l2EncEncodeFrame().\n");
				gst_video_codec_frame_unref( frame );
				return GST_FLOW_ERROR;
			}

			if( !g_strcmp0( nxvideoenc->alignment, "au" ) && !g_strcmp0( nxvideoenc->stream_format, "avc" ) )	// H.264 & AU
			{
				frame->output_buffer = gst_video_encoder_allocate_output_buffer( encoder, encOut.strmSize );
				gst_buffer_map( frame->output_buffer, &out_info, GST_MAP_WRITE );

				out_info.size = encOut.strmSize;
				memcpy( out_info.data, encOut.strmBuf, encOut.strmSize );

				// Nexell H.264 encoder output format : nal start code + 1 slice
				// Remark au size instead of nal start code
				GST_WRITE_UINT32_BE( out_info.data, encOut.strmSize - H264_AU_LENGTH_SIZE );
			}
			else // mpeg4 / h.263 / h.264 nal type
			{
				if( (V4L2_PIX_FMT_H263 != nxvideoenc->codec) && (PIC_TYPE_I == encOut.frameType) )
				{
					frame->output_buffer = gst_video_encoder_allocate_output_buffer( encoder, nxvideoenc->seq_size + encOut.strmSize );
					gst_buffer_map( frame->output_buffer, &out_info, GST_MAP_WRITE );

					out_info.size = nxvideoenc->seq_size + encOut.strmSize;
					memcpy( out_info.data, nxvideoenc->seq_buf, nxvideoenc->seq_size );
					memcpy( out_info.data + nxvideoenc->seq_size, encOut.strmBuf, encOut.strmSize );
				}
				else
				{
					frame->output_buffer = gst_video_encoder_allocate_output_buffer( encoder, encOut.strmSize );
					gst_buffer_map( frame->output_buffer, &out_info, GST_MAP_WRITE );

					out_info.size =  encOut.strmSize;
					memcpy( out_info.data, encOut.strmBuf, encOut.strmSize );
				}
			}

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
			gint i = 0;
			NX_V4L2ENC_PARA param;
			memset( &param, 0x00, sizeof(NX_V4L2ENC_PARA) );

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

			param.width              = nxvideoenc->width;
			param.height             = nxvideoenc->height;
			param.keyFrmInterval     = nxvideoenc->keyFrmInterval ? (nxvideoenc->keyFrmInterval) : (nxvideoenc->fpsNum / nxvideoenc->fpsDen);
			param.fpsNum             = nxvideoenc->fpsNum;
			param.fpsDen             = nxvideoenc->fpsDen;
			param.profile            = (nxvideoenc->codec == V4L2_PIX_FMT_H263) ? V4L2_MPEG_VIDEO_H263_PROFILE_P3 : 0;
			param.bitrate            = nxvideoenc->bitrate;
			param.maximumQp          = nxvideoenc->maximumQp;
			param.disableSkip        = nxvideoenc->disableSkip;
			param.RCDelay            = nxvideoenc->RCDelay;
			param.rcVbvSize          = nxvideoenc->rcVbvSize;
			param.gammaFactor        = nxvideoenc->gammaFactor;
			param.initialQp          = nxvideoenc->initialQp;
			param.numIntraRefreshMbs = nxvideoenc->numIntraRefreshMbs;
			param.searchRange        = nxvideoenc->searchRange;
			param.enableAUDelimiter  = (nxvideoenc->codec == V4L2_PIX_FMT_H264) ? nxvideoenc->enableAUDelimiter : 0;
			param.imgFormat          = nxvideoenc->imgFormat;
			param.imgBufferNum       = MAX_ALLOC_BUFFER;
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

			NX_V4l2EncGetSeqInfo( nxvideoenc->enc, &nxvideoenc->seq_buf, &nxvideoenc->seq_size );

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
		encIn.quantParam      = nxvideoenc->initialQp;

		nxvideoenc->buf_index = (nxvideoenc->buf_index + 1) % MAX_ALLOC_BUFFER;

		if( 0 > NX_V4l2EncEncodeFrame( nxvideoenc->enc, &encIn, &encOut ) )
		{
			GST_ERROR("Fail, NX_V4l2EncEncodeFrame().\n");
			gst_video_codec_frame_unref( frame );
			return GST_FLOW_ERROR;
		}

		if( !g_strcmp0( nxvideoenc->alignment, "au" ) && !g_strcmp0( nxvideoenc->stream_format, "avc" ) )	// H.264 & AU
		{
			frame->output_buffer = gst_video_encoder_allocate_output_buffer( encoder, encOut.strmSize );
			gst_buffer_map( frame->output_buffer, &out_info, GST_MAP_WRITE );

			out_info.size = encOut.strmSize;
			memcpy( out_info.data, encOut.strmBuf, encOut.strmSize );

			// Nexell H.264 encoder output format : nal start code + 1 slice
			// Remark au size instead of nal start code
			GST_WRITE_UINT32_BE( out_info.data, encOut.strmSize - H264_AU_LENGTH_SIZE );
		}
		else // mpeg4 / h.263 / h.264 nal type
		{
			if( (V4L2_PIX_FMT_H263 != nxvideoenc->codec) && (PIC_TYPE_I == encOut.frameType) )
			{
				frame->output_buffer = gst_video_encoder_allocate_output_buffer( encoder, nxvideoenc->seq_size + encOut.strmSize );
				gst_buffer_map( frame->output_buffer, &out_info, GST_MAP_WRITE );

				out_info.size = nxvideoenc->seq_size + encOut.strmSize;
				memcpy( out_info.data, nxvideoenc->seq_buf, nxvideoenc->seq_size );
				memcpy( out_info.data + nxvideoenc->seq_size, encOut.strmBuf, encOut.strmSize );
			}
			else
			{
				frame->output_buffer = gst_video_encoder_allocate_output_buffer( encoder, encOut.strmSize );
				gst_buffer_map( frame->output_buffer, &out_info, GST_MAP_WRITE );

				out_info.size =  encOut.strmSize;
				memcpy( out_info.data, encOut.strmBuf, encOut.strmSize );
			}
		}

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
#define VERSION "0.1.1"
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
