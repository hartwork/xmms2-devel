/*  XMMS2 - X Music Multiplexer System
 *  Copyright (C) 2003-2006 Peter Alm, Tobias Rundstr�m, Anders Gustafsson
 * 
 *  PLUGINS ARE NOT CONSIDERED TO BE DERIVED WORK !!!
 * 
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *                   
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 */

#include <xmmsclient/xmmsclient.h>
#include <xmmsclient/xmmsclient-glib.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/utsname.h>

#include <glib.h>

#include <dns_sd.h>

static DNSServiceRef g_sdref;
static GPollFD *pollfd;
static GMainLoop *ml;

void
dns_callback (DNSServiceRef sdref, 
			  DNSServiceFlags flags, 
			  DNSServiceErrorType errorCode, 
			  const char *name, 
			  const char *regtype, 
			  const char *domain, void *context)
{
	if (errorCode == kDNSServiceErr_NoError) {
		printf ("Registered: %s %s %s\n", name, regtype, domain);
	} else {
		printf ("error! we did NOT register!\n");
	}
}

static void
disconnected (void *data)
{
	DNSServiceRefDeallocate (g_sdref);
	exit (0);
}

static void
handle_quit (xmmsc_result_t *res, void *data)
{
	g_main_loop_quit ((GMainLoop *) data);
}

static gboolean
dns_ipc_source_prepare (GSource *source, gint *timeout_)
{
	/* No timeout here */
	*timeout_ = -1;

	return FALSE;
}

static gboolean
dns_ipc_source_check (GSource *source)
{
	/* Maybe check for errors here? */
	return TRUE;
}

static gboolean
dns_ipc_source_dispatch (GSource *source, GSourceFunc callback, 
						 gpointer user_data)
{
	if (pollfd->revents & G_IO_IN) {
		if (DNSServiceProcessResult (g_sdref) != kDNSServiceErr_NoError) {
			printf ("Error in data callback!\n");
			g_main_loop_quit (ml);
		}
	} else if (pollfd->revents & (G_IO_HUP | G_IO_ERR)) {
		return FALSE;
	}

	return TRUE;
}

static GSourceFuncs dns_ipc_func = {
	dns_ipc_source_prepare,
	dns_ipc_source_check,
	dns_ipc_source_dispatch,
	NULL
};

void
register_service (int port)
{
	int dnsfd;
	GSource *source;

	if (DNSServiceRegister (&g_sdref, 0, 0, NULL, "_xmms2._tcp", NULL, NULL,
							port, 0, NULL, dns_callback, NULL) 
		!= kDNSServiceErr_NoError) {

		printf ("failed to register!\n");
		exit (1);
	}

	dnsfd = DNSServiceRefSockFD (g_sdref);
	if (dnsfd == -1) {
		printf ("no fd!?\n");
		exit (1);
	}

	pollfd = g_new0 (GPollFD, 1);
	pollfd->fd = dnsfd;
	pollfd->events = G_IO_IN | G_IO_HUP | G_IO_ERR;

	source = g_source_new (&dns_ipc_func, sizeof (GSource));

	g_source_add_poll (source, pollfd);
	g_source_attach (source, NULL);
}

int
main (int argc, char **argv)
{
	xmmsc_connection_t *conn;
	gchar *path;
	gchar **s;
	guint port;

	printf ("Starting XMMS2 mDNS agent...\n");

	path = getenv ("XMMS_PATH");
	if (!path) {
		printf ("Sorry you need XMMS_PATH set\n");
		exit (1);
	}

	if (g_ascii_strncasecmp (path, "tcp://", 6)) {
		printf ("Since we don't listen to TCP, I won't register anything!\n");
		exit (1);
	}

	s = g_strsplit (path, ":", 0);
	if (s && s[2]) {
		port = strtol (s[2], NULL, 10);
	} else {
		port = 5555;
	}

	conn = xmmsc_init ("xmms2-mdns");

	if (!conn) {
		printf ("Could not init xmmsc_connection!\n");
		exit (1);
	}

	if (!xmmsc_connect (conn, path)) {
		printf ("Could not connect to xmms2d: %s\n", xmmsc_get_last_error (conn));
		exit (1);
	}

	ml = g_main_loop_new (NULL, FALSE);

	XMMS_CALLBACK_SET (conn, xmmsc_broadcast_quit, handle_quit, ml);
	xmmsc_disconnect_callback_set (conn, disconnected, NULL);
	
	register_service (port);
	
	xmmsc_mainloop_gmain_init (conn);

	g_main_loop_run (ml);

	DNSServiceRefDeallocate (g_sdref);

	xmmsc_unref (conn);
	printf ("XMMS2-mDNS shutting down...\n");

	return 0;
}

