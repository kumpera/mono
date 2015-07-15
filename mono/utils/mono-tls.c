/*
 * mono-tls.c: Low-level TLS support
 *
 * Copyright 2013 Xamarin, Inc (http://www.xamarin.com)
 */

#include <config.h>

#include <mono/utils/atomic.h>
#include <mono/utils/mono-tls.h>

static int tls_offsets [TLS_KEY_NUM];
static gboolean tls_offset_set [TLS_KEY_NUM];

/*
 * mono_tls_key_get_offset:
 *
 *   Return the TLS offset used by the TLS var identified by KEY, previously initialized by a call to
 * mono_tls_key_set_offset (). Return -1 if the offset is not known.
 */
int
mono_tls_key_get_offset (MonoTlsKey key)
{
	g_assert (tls_offset_set [key]);
	return tls_offsets [key];
}

/*
 * mono_tls_key_set_offset:
 *
 *   Set the TLS offset used by the TLS var identified by KEY.
 */
void
mono_tls_key_set_offset (MonoTlsKey key, int offset)
{
	tls_offsets [key] = offset;
	tls_offset_set [key] = TRUE;
}

/* 0 means not initialized, 1 is initialized, -1 means in progress */
static int tls_initialized = 0;

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
