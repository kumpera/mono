/*
 * mono-tls-darwin.c: Low-level thread local storage for darwin systems.
 *
 * Author:
 *	Rodrigo Kumpera (kumpera@gmail.com)
 *
 * Copyright 2013 Xamarin, Inc (http://www.xamarin.com)
 */

#include <mono/utils/mono-fast-tls.h>

#if defined(FAST_TLS_MODEL_DARWIN)

#include <glib.h>
#include <mono/utils/mono-compiler.h>
#include <mono/utils/mono-tls.h>
#include <mono/utils/mono-threads.h>
#include <mono/utils/atomic.h>

static gint32 tls_model;
static MonoNativeTlsKey tls_keys_start;

void*
mono_tls_get (MonoFastTlsKey key)
{
	switch (tls_model) {
	case MONO_FAST_TLS_MODEL_NONE:
	case MONO_FAST_TLS_MODEL_EMULATED_INDIRECT:
		return mono_thread_info_current () ? mono_thread_info_current ()->tls_block [key] : NULL;

	case MONO_FAST_TLS_MODEL_EMULATED:
		return mono_native_tls_get_value (tls_keys_start + key);
	default:
		g_assert (0);
		return NULL;
	}
}

void
mono_tls_set (MonoFastTlsKey key, void *value)
{
	switch (tls_model) {
	case MONO_FAST_TLS_MODEL_NONE:
	case MONO_FAST_TLS_MODEL_EMULATED_INDIRECT:
		if (mono_thread_info_current ()) mono_thread_info_current ()->tls_block [key] = value;
		break;
	case MONO_FAST_TLS_MODEL_EMULATED:
		mono_native_tls_set_value (tls_keys_start + key, value);
		break;
	default:
		g_assert (0);
	}
}

void*
mono_tls_get_address (MonoFastTlsKey key)
{
	switch (tls_model) {
	case MONO_FAST_TLS_MODEL_NONE:
	case MONO_FAST_TLS_MODEL_EMULATED_INDIRECT:
		return mono_thread_info_current () ? &mono_thread_info_current ()->tls_block [key] : NULL;

	case MONO_FAST_TLS_MODEL_EMULATED:
		return mono_mach_get_tls_address_from_thread (pthread_self (), tls_keys_start + key);
	default:
		g_assert (0);
		return NULL;
	}
}

static gint32
local_tls_offset (gint32 key)
{
	return mono_mach_get_local_tls_offset () + (key * sizeof (gpointer));
}

gint32
mono_tls_get_fast_tls_offset (MonoFastTlsKey key)
{
	g_assert (key >= 0 && key < MONO_TLS_KEY_COUNT);
	
	switch (tls_model) {
	case MONO_FAST_TLS_MODEL_NONE:
		return -1;
	case MONO_FAST_TLS_MODEL_EMULATED:
		return local_tls_offset (tls_keys_start + key);
	case MONO_FAST_TLS_MODEL_EMULATED_INDIRECT:
		return key;
	default:
		g_assert (0);
	}
}

gint32
mono_tls_get_fast_tls_model (void)
{
	return tls_model;
}

gint32
mono_tls_get_fast_tls_block_offset (void)
{
	switch (tls_model) {
	case MONO_FAST_TLS_MODEL_NONE:
		return -1;
	case MONO_FAST_TLS_MODEL_EMULATED:
		return tls_keys_start;
	case MONO_FAST_TLS_MODEL_EMULATED_INDIRECT:
		return local_tls_offset (mono_thread_get_current_thread_info_tls_key ());
	default:
		g_assert (0);
	}
}

void**
mono_tls_get_block_address (void)
{
	switch (tls_model) {
	case MONO_FAST_TLS_MODEL_NONE:
	case MONO_FAST_TLS_MODEL_EMULATED_INDIRECT:
		return mono_thread_info_current () ? &mono_thread_info_current ()->tls_block [0] : NULL;
	case MONO_FAST_TLS_MODEL_EMULATED:
		return mono_mach_get_tls_address_from_thread (pthread_self (), tls_keys_start);
	default:
		g_assert (0);
	}
}

MonoFastTlsKey
mono_tls_translate_native_offset (gint32 native_offset)
{
	g_error ("current tls model cannot translate offsets");
	return -1;
}

void
mono_tls_real_init (void)
{
	gboolean has_pt_off, has_tls_off;

	mono_mach_init ();
	
	/* TODO detect __thread */
	has_pt_off = mono_mach_get_pthread_offset () != -1;
	has_tls_off = mono_mach_get_local_tls_offset () != -1;

	if (!has_tls_off) {
		tls_model = MONO_FAST_TLS_MODEL_NONE;
	} else if (!has_pt_off || TRUE) {
		tls_model = MONO_FAST_TLS_MODEL_EMULATED_INDIRECT;
		printf ("using indirect model\n");
	} else {
		MonoNativeTlsKey last_key;
		int i;

		tls_model = MONO_FAST_TLS_MODEL_EMULATED;
		if (!mono_native_tls_alloc (&last_key, NULL))
			goto fail;
		tls_keys_start = last_key;
		for (i = 1 ; i < MONO_TLS_KEY_COUNT; ++i) {
			MonoNativeTlsKey key;
			if (!mono_native_tls_alloc (&key, NULL))
				goto fail;

			if (key != last_key + 1)
				goto fail;

			last_key = key;
		}
		return;

fail:
		g_warning ("Failed to create fast tls block");
		tls_model = MONO_FAST_TLS_MODEL_EMULATED_INDIRECT;
	}
}

#endif