/*
 * mono-tls.c: Low-level thread local storage
 *
 * Author:
 *	Rodrigo Kumpera (kumpera@gmail.com)
 *
 * Copyright 2013 Xamarin, Inc (http://www.xamarin.com)
 */

#include <glib.h>
#include <mono/utils/mono-compiler.h>
#include <mono/utils/mono-tls.h>
#include <mono/utils/mono-threads.h>
#include <mono/utils/atomic.h>

/* 0 means not initialized, 1 is initialized, -1 means in progress */
static int tls_initialized = 0;

void mono_tls_real_init (void) MONO_INTERNAL;

void
mono_tls_init (void)
{
	int result;

	do {
		result = InterlockedCompareExchange (&tls_initialized, -1, 0);
		switch (result) {
		case 1:
			/* already inited */
			return;
		case -1:
			/* being inited by another thread */
			g_usleep (1000);
			break;
		case 0:
			/* we will init it */
			break;
		default:
			g_assert_not_reached ();
		}
	} while (result != 0);
	
	mono_tls_real_init ();
	tls_initialized = 1;
}

gboolean
mono_tls_is_fast_tls_available (MonoFastTlsKey key)
{
	return mono_tls_get_fast_tls_offset (key) != -1;
}
