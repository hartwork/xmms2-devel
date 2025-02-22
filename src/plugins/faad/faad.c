/** @file faad.c
 *  Decoder plugin for AAC and MP4 audio formats
 *
 *  Copyright (C) 2005-2023 XMMS2 Team
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 */

#include <xmms/xmms_xformplugin.h>
#include <xmms/xmms_bindata.h>
#include <xmms/xmms_sample.h>
#include <xmms/xmms_log.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <neaacdec.h>
#include <glib.h>

#define FAAD_BUFFER_SIZE 4096

#define FAAD_TYPE_UNKNOWN 0
#define FAAD_TYPE_MP4 1
#define FAAD_TYPE_ADIF 2
#define FAAD_TYPE_ADTS 3

static int faad_mpeg_samplerates[] = { 96000, 88200, 64000, 48000, 44100,
                                       32000, 24000, 22050, 16000, 12000,
                                       11025, 8000, 7350, 0, 0, 0 };

typedef struct {
	NeAACDecHandle decoder;
	gint filetype;

	guchar buffer[FAAD_BUFFER_SIZE];
	guint buffer_length;
	guint buffer_size;

	guint channels;
	guint bitrate;
	guint samplerate;
	xmms_sample_format_t sampleformat;

	GString *outbuf;
} xmms_faad_data_t;

static gboolean xmms_faad_plugin_setup (xmms_xform_plugin_t *xform_plugin);
static gboolean xmms_faad_init (xmms_xform_t *xform);
static void xmms_faad_destroy (xmms_xform_t *xform);
static gint xmms_faad_read_some (xmms_xform_t *xform, xmms_error_t *err);
static gint xmms_faad_read (xmms_xform_t *xform, xmms_sample_t *buf, gint len, xmms_error_t *err);
static gint xmms_faad_get_framesize (xmms_xform_t *xform);
static gint xmms_faad_get_framesize_upper_bound (xmms_xform_t *xform);
static gint64 xmms_faad_seek (xmms_xform_t *xform, gint64 samples, xmms_xform_seek_mode_t whence, xmms_error_t *err);
static gboolean xmms_faad_gapless_try (xmms_xform_t *xform);
static void xmms_faad_get_mediainfo (xmms_xform_t *xform);

/*
 * Plugin header
 */

XMMS_XFORM_PLUGIN_DEFINE ("faad",
                          "AAC Decoder", XMMS_VERSION,
                          "Advanced Audio Coding decoder",
                          xmms_faad_plugin_setup);

static gboolean
xmms_faad_plugin_setup (xmms_xform_plugin_t *xform_plugin)
{
	xmms_xform_methods_t methods;

	XMMS_XFORM_METHODS_INIT (methods);
	methods.init = xmms_faad_init;
	methods.destroy = xmms_faad_destroy;
	methods.read = xmms_faad_read;
	methods.seek = xmms_faad_seek;

	xmms_xform_plugin_methods_set (xform_plugin, &methods);

	xmms_xform_plugin_indata_add (xform_plugin,
	                              XMMS_STREAM_TYPE_MIMETYPE,
	                              "audio/aac",
	                              XMMS_STREAM_TYPE_END);

	xmms_magic_add ("mpeg aac header", "audio/aac",
	                "0 beshort&0xfff6 0xfff0", NULL);

	xmms_magic_add ("adif header", "audio/aac",
	                "0 string ADIF", NULL);

	return TRUE;
}

static void
xmms_faad_destroy (xmms_xform_t *xform)
{
	xmms_faad_data_t *data;

	g_return_if_fail (xform);

	data = xmms_xform_private_data_get (xform);
	g_return_if_fail (data);

	NeAACDecClose (data->decoder);
	g_string_free (data->outbuf, TRUE);
	g_free (data);
}

static gboolean
xmms_faad_init (xmms_xform_t *xform)
{
	xmms_faad_data_t *data;
	xmms_error_t error;

	NeAACDecConfigurationPtr config;
	gint bytes_read;
	gulong samplerate;
	guchar channels;
	const gchar *mime;

	g_return_val_if_fail (xform, FALSE);

	data = g_new0 (xmms_faad_data_t, 1);
	data->outbuf = g_string_new (NULL);
	data->buffer_size = FAAD_BUFFER_SIZE;

	xmms_xform_private_data_set (xform, data);

	data->decoder = NeAACDecOpen ();
	config = NeAACDecGetCurrentConfiguration (data->decoder);
	config->defObjectType = LC;
	config->defSampleRate = 44100;
	config->outputFormat = FAAD_FMT_16BIT;
	config->downMatrix = 0;
	config->dontUpSampleImplicitSBR = 0;
	NeAACDecSetConfiguration (data->decoder, config);

	switch (config->outputFormat) {
	case FAAD_FMT_16BIT:
		data->sampleformat = XMMS_SAMPLE_FORMAT_S16;
		break;
	case FAAD_FMT_24BIT:
		/* we don't have 24-bit format to use in xmms2 */
		data->sampleformat = XMMS_SAMPLE_FORMAT_S32;
		break;
	case FAAD_FMT_32BIT:
		data->sampleformat = XMMS_SAMPLE_FORMAT_S32;
		break;
	case FAAD_FMT_FLOAT:
		data->sampleformat = XMMS_SAMPLE_FORMAT_FLOAT;
		break;
	case FAAD_FMT_DOUBLE:
		data->sampleformat = XMMS_SAMPLE_FORMAT_DOUBLE;
		break;
	}

	while (data->buffer_length < 8) {
		xmms_error_reset (&error);
		bytes_read = xmms_xform_read (xform,
		                              (gchar *) data->buffer + data->buffer_length,
		                              data->buffer_size - data->buffer_length,
		                              &error);
		data->buffer_length += bytes_read;

		if (bytes_read < 0) {
			xmms_log_error ("Error while trying to read data on init");
			goto err;
		} else if (bytes_read == 0) {
			XMMS_DBG ("Not enough bytes to check the AAC header");
			goto err;
		}
	}

	/* which type of file are we dealing with? */
	data->filetype = FAAD_TYPE_UNKNOWN;
	if (xmms_xform_auxdata_has_val (xform, "decoder_config")) {
		data->filetype = FAAD_TYPE_MP4;
	} else if (!strncmp ((char *) data->buffer, "ADIF", 4)) {
		data->filetype = FAAD_TYPE_ADIF;
	} else {
		int i;

		/* ADTS mpeg file can be a stream and start in the middle of a
		 * frame so we need to have extra loop check here */
		for (i=0; i<data->buffer_length-1; i++) {
			if (data->buffer[i] == 0xff && (data->buffer[i+1]&0xf6) == 0xf0) {
				data->filetype = FAAD_TYPE_ADTS;
				g_memmove (data->buffer, data->buffer+i, data->buffer_length-i);
				data->buffer_length -= i;
				break;
			}
		}
	}

	if (data->filetype == FAAD_TYPE_ADTS || data->filetype == FAAD_TYPE_ADIF) {
		bytes_read = NeAACDecInit (data->decoder, data->buffer,
		                          data->buffer_length, &samplerate,
		                          &channels);
	} else if (data->filetype == FAAD_TYPE_MP4) {
		const guchar *tmpbuf;
		gsize tmpbuflen;
		guchar *copy;

		if (!xmms_xform_auxdata_get_bin (xform, "decoder_config", &tmpbuf,
		                                 &tmpbuflen)) {
			XMMS_DBG ("AAC decoder config data found but it's wrong type! (something broken?)");
			goto err;
		}

		copy = g_memdup (tmpbuf, tmpbuflen);
		bytes_read = NeAACDecInit2 (data->decoder, copy, tmpbuflen,
		                           &samplerate, &channels);
		g_free (copy);
	}

	if (bytes_read < 0) {
		XMMS_DBG ("Error initializing decoder library.");
		goto err;
	}

	/* Get mediainfo and skip the possible header */
	xmms_faad_get_mediainfo (xform);
	g_memmove (data->buffer, data->buffer + bytes_read,
	           data->buffer_length - bytes_read);
	data->buffer_length -= bytes_read;

	data->samplerate = samplerate;
	data->channels = channels;

	/* XXX: Because of decoder delay the first frame is bad (as is the first
	 * frame after seek). Frame 0 gets automatically discarded by libfaad2 (but
	 * not the first frame after seek). However frame 0 is included in gapless
	 * and durations calculations... So we cheat and tell libfaad2 we're feeding
	 * it frame 1.
	 */
	NeAACDecPostSeekReset (data->decoder, 1);

	/* FIXME: Because for HE AAC files some versions of libfaad return the wrong
	 * samplerate in init, we have to do one read and let it decide the real
	 * parameters. After changing sample parameters and format is supported,
	 * this hack should be removed and handled in read instead.
	 */
	xmms_error_reset (&error);
	if (xmms_faad_read_some (xform, &error) <= 0) {
		XMMS_DBG ("First read from faad decoder failed!");
		return FALSE;
	}

	if (xmms_faad_gapless_try (xform)) {
		mime = "audio/x-uncut-pcm";
	} else {
		mime = "audio/pcm";
	}

	xmms_xform_outdata_type_add (xform,
	                             XMMS_STREAM_TYPE_MIMETYPE,
	                             mime,
	                             XMMS_STREAM_TYPE_FMT_FORMAT,
	                             data->sampleformat,
	                             XMMS_STREAM_TYPE_FMT_CHANNELS,
	                             data->channels,
	                             XMMS_STREAM_TYPE_FMT_SAMPLERATE,
	                             data->samplerate,
	                             XMMS_STREAM_TYPE_END);

	XMMS_DBG ("AAC decoder inited successfully!");

	return TRUE;

err:
	g_string_free (data->outbuf, TRUE);
	g_free (data);

	return FALSE;
}

static gint
xmms_faad_read_some (xmms_xform_t *xform, xmms_error_t *err)
{
	xmms_faad_data_t *data;
	NeAACDecFrameInfo frameInfo;
	gpointer sample_buffer;
	guint bytes_read = 0;

	g_return_val_if_fail (xform, -1);
	data = xmms_xform_private_data_get (xform);
	g_return_val_if_fail (data, -1);

	while (data->outbuf->len == 0) {
		gboolean need_read;

		/* MP4 demuxer always gives full packets so we need different handling */
		if (data->filetype == FAAD_TYPE_MP4)
			need_read = (data->buffer_length == 0);
		else
			need_read = (data->buffer_length < data->buffer_size);

		if (need_read) {
			bytes_read = xmms_xform_read (xform,
			                              (gchar *) data->buffer + data->buffer_length,
			                              data->buffer_size - data->buffer_length,
			                              err);

			if (bytes_read <= 0 && data->buffer_length == 0) {
				XMMS_DBG ("EOF");
				return 0;
			}

			data->buffer_length += bytes_read;
		}

		sample_buffer = NeAACDecDecode (data->decoder, &frameInfo, data->buffer,
		                               data->buffer_length);

		g_memmove (data->buffer, data->buffer + frameInfo.bytesconsumed,
		           data->buffer_length - frameInfo.bytesconsumed);
		data->buffer_length -= frameInfo.bytesconsumed;
		bytes_read = frameInfo.samples * xmms_sample_size_get (data->sampleformat);

		if (bytes_read > 0 && frameInfo.error == 0) {
			if (data->samplerate != frameInfo.samplerate ||
			    data->channels != frameInfo.channels) {
				/* We should inform output to change parameters somehow */
				xmms_log_error ("Output format changed in the middle of a read!");
				data->samplerate = frameInfo.samplerate;
				data->channels = frameInfo.channels;
			}
			g_string_append_len (data->outbuf, sample_buffer, bytes_read);
		} else if (frameInfo.error > 0) {
			xmms_log_error ("ERROR %d in faad decoding: %s", frameInfo.error,
			                NeAACDecGetErrorMessage (frameInfo.error));
			return -1;
		}
	}
	return data->outbuf->len;
}

static gint
xmms_faad_read (xmms_xform_t *xform, xmms_sample_t *buf, gint len, xmms_error_t *err)
{
	xmms_faad_data_t *data;
	gint ret;
	guint size;

	g_return_val_if_fail (xform, -1);
	data = xmms_xform_private_data_get (xform);
	g_return_val_if_fail (data, -1);

	while (data->outbuf->len == 0) {
		ret = xmms_faad_read_some (xform, err);
		if (ret < 0) { return ret; }
		if (ret == 0) { break; /* EOF */ }
	}

	size = MIN (data->outbuf->len, len);
	memcpy (buf, data->outbuf->str, size);
	g_string_erase (data->outbuf, 0, size);
	return size;
}

static gint
xmms_faad_get_framesize (xmms_xform_t *xform) {
	xmms_faad_data_t *data;
	const guchar *tmpbuf;
	gsize tmpbuflen;
	guchar *copy;
	mp4AudioSpecificConfig mp4ASC;

	g_return_val_if_fail (xform, 0);
	data = xmms_xform_private_data_get (xform);
	g_return_val_if_fail (data, 0);

	if (data->filetype != FAAD_TYPE_MP4) {
		return 0;
	}

	if (!xmms_xform_auxdata_get_bin (xform, "decoder_config", &tmpbuf,
	                                 &tmpbuflen)) {
		xmms_log_error ("ERROR: Cannot get AAC decoder config, but filetype is FAAD_TYPE_MP4!");
		return 0;
	}
	copy = g_memdup (tmpbuf, tmpbuflen);
	if ((signed char)NeAACDecAudioSpecificConfig (copy, tmpbuflen, &mp4ASC) < 0) {
		/* FIXME: That function ^^^ returns char. How can it signal errors when
		 * char is unsigned?!
		 */
		g_free (copy);
		XMMS_DBG ("ERROR: Could not get mp4ASC!");
		return 0;
	}
	g_free (copy);

	return ((mp4ASC.frameLengthFlag == 1) ? 960 : 1024)
	       * ((mp4ASC.sbr_present_flag == 1) ? 2 : 1);
}

static gint
xmms_faad_get_framesize_upper_bound (xmms_xform_t *xform)
{
	gint ret = xmms_faad_get_framesize (xform);
	if (ret == 0) {
		return 2048; /* FIXME: This is a guess. Find real upper bound. */
	}
	return ret;
}

static gint64
xmms_faad_seek (xmms_xform_t *xform, gint64 samples, xmms_xform_seek_mode_t whence, xmms_error_t *err)
{
	xmms_faad_data_t *data;
	gint64 ret = -1;

	g_return_val_if_fail (whence == XMMS_XFORM_SEEK_SET, -1);
	g_return_val_if_fail (xform, -1);

	data = xmms_xform_private_data_get (xform);
	g_return_val_if_fail (data, -1);

	/* Seeking only supported on MP4 AAC right now */
	if (data->filetype == FAAD_TYPE_MP4) {
		gint64 location;

		/* Seek to some time before samples, to take care of decoder delay */
		location = samples - xmms_faad_get_framesize_upper_bound (xform);
		if (location < 0) { location = 0; }

		location = xmms_xform_seek (xform, location, whence, err);
		if (location >= 0) {
			data->buffer_length = 0;
			g_string_erase (data->outbuf, 0, -1);

			NeAACDecPostSeekReset (data->decoder, -1);
			ret = location;
		}
	}

	return ret;
}

static gboolean
xmms_faad_gapless_try (xmms_xform_t *xform)
{
	xmms_faad_data_t *data;
	gint64 start = 0, stop = 0;

	g_return_val_if_fail (xform, FALSE);
	data = xmms_xform_private_data_get (xform);
	g_return_val_if_fail (data, FALSE);

	xmms_xform_auxdata_get_int64 (xform, "startsamples", &start);
	if (start == 0) {
		XMMS_DBG ("First frame of AAC should be ignored, but is not. Trying to fix.");
		start = xmms_faad_get_framesize (xform);
		if (start == 0) {
			XMMS_DBG ("No luck. Couldn't get the framesize.");
		}
	}
	if (start != 0) {
		xmms_xform_auxdata_set_int (xform, "startsamples", start);
	}

	xmms_xform_auxdata_get_int64 (xform, "stopsamples", &stop);
	if (stop != 0) {
		xmms_xform_auxdata_set_int (xform, "stopsamples", stop);
	}

	return (start != 0) || (stop != 0);
}

static void
xmms_faad_get_mediainfo (xmms_xform_t *xform)
{
	xmms_faad_data_t *data;
	const gchar *metakey;

	g_return_if_fail (xform);

	data = xmms_xform_private_data_get (xform);
	g_return_if_fail (data);

	if (data->filetype == FAAD_TYPE_ADIF) {
		guint skip_size, bitrate;
		gint32 duration;

		skip_size = (data->buffer[4] & 0x80) ? 9 : 0;
		bitrate = ((guint) (data->buffer[4 + skip_size] & 0x0F) << 19) |
		          ((guint) data->buffer[5 + skip_size] << 11) |
		          ((guint) data->buffer[6 + skip_size] << 3) |
		          ((guint) data->buffer[7 + skip_size] & 0xE0);

		metakey = XMMS_MEDIALIB_ENTRY_PROPERTY_BITRATE;
		xmms_xform_metadata_set_int (xform, metakey, bitrate);

		metakey = XMMS_MEDIALIB_ENTRY_PROPERTY_SIZE;
		if (xmms_xform_metadata_get_int (xform, metakey, &duration)) {
			duration = ((float) duration * 8000.f) / ((float) bitrate) + 0.5f;

			metakey = XMMS_MEDIALIB_ENTRY_PROPERTY_DURATION;
			xmms_xform_metadata_set_int (xform, metakey, duration);
		}
	} else if (data->filetype == FAAD_TYPE_ADTS) {
		gint32 val = faad_mpeg_samplerates[(data->buffer[2] & 0x3c) >> 2];
		metakey = XMMS_MEDIALIB_ENTRY_PROPERTY_SAMPLERATE;
		xmms_xform_metadata_set_int (xform, metakey, val);
	}
}
