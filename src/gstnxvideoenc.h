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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef _GST_NXVIDEOENC_H_
#define _GST_NXVIDEOENC_H_

#include <gst/video/video.h>
#include <gst/video/gstvideoencoder.h>

#include <stdio.h>
#include <nx_video_api.h>

G_BEGIN_DECLS

#define GST_TYPE_NXVIDEOENC				(gst_nxvideoenc_get_type())
#define GST_NXVIDEOENC(obj)				(G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_NXVIDEOENC,GstNxvideoenc))
#define GST_NXVIDEOENC_CLASS(klass)		(G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_NXVIDEOENC,GstNxvideoencClass))
#define GST_IS_NXVIDEOENC(obj)			(G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_NXVIDEOENC))
#define GST_IS_NXVIDEOENC_CLASS(obj)	(G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_NXVIDEOENC))

typedef struct _GstNxvideoenc		GstNxvideoenc;
typedef struct _GstNxvideoencClass	GstNxvideoencClass;

#define MAX_INPUT_BUFFER	32
#define MAX_ALLOC_BUFFER	8

#define MAX_STRING_SIZE		128

struct _GstNxvideoenc
{
	GstVideoEncoder base_nxvideoenc;
	GstVideoCodecState *input_state;

	gboolean init;
	gboolean accelerable;

	NX_V4L2ENC_HANDLE enc;
	NX_VID_MEMORY_HANDLE inbuf[MAX_INPUT_BUFFER];
	int drm_fd;
	guint buf_index;

	guchar *seq_buf;
	gint seq_size;

	guint codec;
	guint width;
	guint height;
	guint keyFrmInterval;
	guint fpsNum;
	guint fpsDen;
	guint profile;
	guint bitrate;
	guint maximumQp;
	guint disableSkip;
	guint RCDelay;
	guint rcVbvSize;
	guint gammaFactor;
	guint initialQp;
	guint numIntraRefreshMbs;
	guint searchRange;
	guint enableAUDelimiter;
	guint imgFormat;
	guint imgBufferNum;
	guint rotAngle;
	guint mirDirection;
	guint jpgQuality;

	gchar stream_format[MAX_STRING_SIZE];
	gchar alignment[MAX_STRING_SIZE];
};

struct _GstNxvideoencClass
{
	GstVideoEncoderClass base_nxvideoenc_class;
};

GType gst_nxvideoenc_get_type (void);

G_END_DECLS

#endif
