/*  XMMS2 - X Music Multiplexer System
 *  Copyright (C) 2003	Peter Alm, Tobias Rundstr�m, Anders Gustafsson
 * 
 *  PLUGINS ARE NOT CONSIDERED TO BE DERIVED WORK !!!
 */





#include "xmms/xmms_defs.h"
#include "xmms/xmms_decoderplugin.h"
#include "xmms/xmms_log.h"
#include <modplug.h>

#include <glib.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <ctype.h>

/*
 * Type definitions
 */

typedef struct xmms_modplug_data_St {
	ModPlug_Settings settings;
	ModPlugFile *mod;
	gchar *buffer;
	gint buffer_length;
} xmms_modplug_data_t;

/*
 * Function prototypes
 */

static gboolean xmms_modplug_new (xmms_decoder_t *decoder);
static gboolean xmms_modplug_decode_block (xmms_decoder_t *decoder);
static void xmms_modplug_get_media_info (xmms_decoder_t *decoder);
static void xmms_modplug_destroy (xmms_decoder_t *decoder);
static gboolean xmms_modplug_init (xmms_decoder_t *decoder, gint mode);
static gboolean xmms_modplug_seek (xmms_decoder_t *decoder, guint samples);

/*
 * Plugin header
 */

xmms_plugin_t *
xmms_plugin_get (void)
{
	xmms_plugin_t *plugin;

	plugin = xmms_plugin_new (XMMS_PLUGIN_TYPE_DECODER, 
				  XMMS_DECODER_PLUGIN_API_VERSION,
				  "modplug",
				  "MODPLUG decoder " XMMS_VERSION,
				  "modplug");

	if (!plugin) {
		return NULL;
	}

	xmms_plugin_info_add (plugin, "URL", "http://www.xmms.org/");
	xmms_plugin_info_add (plugin, "Author", "XMMS Team");
	xmms_plugin_info_add (plugin, "License", "GPL");

	xmms_plugin_method_add (plugin, XMMS_PLUGIN_METHOD_NEW, xmms_modplug_new);
	xmms_plugin_method_add (plugin, XMMS_PLUGIN_METHOD_DECODE_BLOCK, xmms_modplug_decode_block);
	xmms_plugin_method_add (plugin, XMMS_PLUGIN_METHOD_DESTROY, xmms_modplug_destroy);
	xmms_plugin_method_add (plugin, XMMS_PLUGIN_METHOD_GET_MEDIAINFO, xmms_modplug_get_media_info);
	xmms_plugin_method_add (plugin, XMMS_PLUGIN_METHOD_INIT, xmms_modplug_init);
	xmms_plugin_method_add (plugin, XMMS_PLUGIN_METHOD_SEEK, xmms_modplug_seek);

	xmms_plugin_properties_add (plugin, XMMS_PLUGIN_PROPERTY_FAST_FWD);
	xmms_plugin_properties_add (plugin, XMMS_PLUGIN_PROPERTY_REWIND);
	
	
	xmms_plugin_magic_add (plugin, "Fasttracker II module", "audio/xm",
						   "0 string Extended Module:", NULL);

	xmms_plugin_magic_add (plugin, "ScreamTracker III module", "audio/s3m",
						   "44 string SCRM", NULL);

	xmms_plugin_magic_add (plugin, "Impulse Tracker module", "audio/it",
						   "0 string IMPM", NULL);

	xmms_plugin_magic_add (plugin, "MED module", "audio/med",
						   "0 string MMD", NULL);

	/* these are for all (not all but should be most) various types of .mod files */
	xmms_plugin_magic_add (plugin, "4-channel Protracker module", "audio/mod",
						   "1080 string M.K.", NULL);
	xmms_plugin_magic_add (plugin, "4-channel Protracker module", "audio/mod",
						   "1080 string M!K!", NULL);
	xmms_plugin_magic_add (plugin, "4-channel Startracker module", "audio/mod",
						   "1080 string FLT4", NULL);
	xmms_plugin_magic_add (plugin, "8-channel Startracker module", "audio/mod",
						   "1080 string FLT8", NULL);
	xmms_plugin_magic_add (plugin, "4-channel Fasttracker module", "audio/mod",
						   "1080 string 4CHN", NULL);
	xmms_plugin_magic_add (plugin, "6-channel Fasttracker module", "audio/mod",
						   "1080 string 6CHN", NULL);
	xmms_plugin_magic_add (plugin, "8-channel Fasttracker module", "audio/mod",
						   "1080 string 8CHN", NULL);
	xmms_plugin_magic_add (plugin, "8-channel Octalyzer module", "audio/mod",
						   "1080 string CD81", NULL);
	xmms_plugin_magic_add (plugin, "8-channel Octalyzer module", "audio/mod",
						   "1080 string OKTA", NULL);
	xmms_plugin_magic_add (plugin, "16-channel Taketracker module", "audio/mod",
						   "1080 string 16CN", NULL);
	xmms_plugin_magic_add (plugin, "32-channel Taketracker module", "audio/mod",
						   "1080 string 32CN", NULL);
	
	return plugin;
}

static void
xmms_modplug_destroy (xmms_decoder_t *decoder)
{
	xmms_modplug_data_t *data;

	g_return_if_fail (decoder);

	data = xmms_decoder_private_data_get (decoder);
	g_return_if_fail (data);

	
	if (data->buffer)
		g_free (data->buffer);

	if (data->mod)
		ModPlug_Unload (data->mod);

	g_free (data);

}

static void
xmms_modplug_get_media_info (xmms_decoder_t *decoder)
{
	xmms_medialib_entry_t entry;
	xmms_modplug_data_t *data;
	xmms_medialib_session_t *session;
	gchar tmp[25];

	g_return_if_fail (decoder);

	data = xmms_decoder_private_data_get (decoder);

	entry = xmms_decoder_medialib_entry_get (decoder);

	/* */
	session = xmms_medialib_begin ();
	xmms_medialib_entry_property_set_int (session, entry,
					      XMMS_MEDIALIB_ENTRY_PROPERTY_DURATION,
					      ModPlug_GetLength (data->mod));

	g_snprintf (tmp, sizeof (tmp), "%s", ModPlug_GetName (data->mod));
	xmms_medialib_entry_property_set_str (session, entry,
					      XMMS_MEDIALIB_ENTRY_PROPERTY_TITLE,
					      tmp);
	
	xmms_medialib_entry_property_set_int (session, entry,
					      XMMS_MEDIALIB_ENTRY_PROPERTY_SAMPLERATE,
					      44100);

	xmms_medialib_end (session);
	return;
}

static gboolean
xmms_modplug_seek (xmms_decoder_t *decoder, guint samples)
{
	xmms_modplug_data_t *data;
	
	g_return_val_if_fail (decoder, FALSE);

	data = xmms_decoder_private_data_get (decoder);

	ModPlug_Seek (data->mod, (int) ((gdouble)1000 * samples / 44100));

	return TRUE;
}

static gboolean
xmms_modplug_new (xmms_decoder_t *decoder)
{
	xmms_modplug_data_t *data;

	g_return_val_if_fail (decoder, FALSE);

	data = g_new0 (xmms_modplug_data_t, 1);

	xmms_decoder_private_data_set (decoder, data);
	
	return TRUE;
}

static gboolean
xmms_modplug_init (xmms_decoder_t *decoder, gint mode)
{
	xmms_transport_t *transport;
	xmms_modplug_data_t *data;
	xmms_error_t error;
	gint len = 0;

	g_return_val_if_fail (decoder, FALSE);
	
	transport = xmms_decoder_transport_get (decoder);
	g_return_val_if_fail (transport, FALSE);
	
	data = xmms_decoder_private_data_get (decoder);
	g_return_val_if_fail (decoder, FALSE);

	if (mode & XMMS_DECODER_INIT_DECODING) {
		/* 
		   ModPlug always decodes sound at 44100kHz, 32 bit, stereo
		   and then down-mixes to the selected settings.  So there is
		   no need exporting any other formats, it's better to let
		   xmms2 do the conversion
		*/
		
		xmms_decoder_format_add (decoder, XMMS_SAMPLE_FORMAT_S16, 2, 44100);
		if (xmms_decoder_format_finish (decoder) == NULL) {
			return FALSE;
		}
		
		data->settings.mResamplingMode = MODPLUG_RESAMPLE_FIR;
		data->settings.mChannels = 2;
		data->settings.mBits = 16;
		data->settings.mFrequency = 44100;
		/* more? */
		ModPlug_SetSettings(&data->settings);
	}

	data->buffer_length = xmms_transport_size (transport);
	data->buffer = g_malloc (data->buffer_length);
		
	while (len < data->buffer_length) {
		gint ret = xmms_transport_read (transport, data->buffer + len,
		                                data->buffer_length, &error);
		
		if ( ret <= 0 ) {
			g_free (data->buffer);
			data->buffer = NULL;
			return FALSE;
		}
		len += ret;
		g_assert (len >= 0);
	}
	
	data->mod = ModPlug_Load(data->buffer, data->buffer_length);
	if (!data->mod) {
		XMMS_DBG ("Error loading mod");
		return FALSE;
	}

	return TRUE;
}


static gboolean
xmms_modplug_decode_block (xmms_decoder_t *decoder)
{
	xmms_modplug_data_t *data;
	gint16 out[4096];
	gint ret;

	data = xmms_decoder_private_data_get (decoder);

	ret = ModPlug_Read(data->mod, out, 4096);

	if (!ret)
		return FALSE; /* eos */

	xmms_decoder_write (decoder, (gchar *)out, ret);

	return TRUE;
}

